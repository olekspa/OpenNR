# OpenNR Periphery NR (hybrid) deep audit - 2.21.8 code

Read-only audit. No repo file was edited. Nothing was compiled or run in game.
Repo root used: `<REPO>`. `OS` = `runtime\open-shaders`.
Evidence tags: SOURCE-VERIFIED (read in code), MEASURED (read from a file on disk), INFERENCE, HYPOTHESIS.
Line numbers are for this tree. The numbers in the task text and in `03-improvement-backlog.md` are stale (see section 4.0).

Abbreviations: `HCS` = `OS\features\Upscaling\Shaders\Upscaling\NeuralRendering\PeripheryHybridCS.hlsl`,
`INT` = `OS\src\Features\Upscaling\NeuralRendering\Integration.cpp`, `REN` = `...\Renderer.cpp`,
`TRU` = `...\NeuralRendering\TemporalReuseCS.hlsl`, `MRC` = `...\NeuralRendering\ModelResolutionCS.hlsl`.

---------------------------------------------------------------------------------------------------

## 0. Executive summary (prioritised)

| # | Sev | Finding | Tag |
|---|---|---|---|
| A | **HIGH** | The three **signed** residual textures (`PeripheryCoarse`, `CentreCoarse`, `PeripheryLowResResidual`) are created in the **kTOTAL format**, and kTOTAL is almost certainly `R8G8B8A8_UNORM`. A UNORM UAV store clamps every negative channel to 0 and quantises to 1/255. The composite then only shares the **positive** part of the low-frequency field; negative parts (e.g. blue-down in the sky) stay per-region, so a centre-vs-periphery step in exactly the "greyer sky" component can survive even when the hybrid runs. | INT:317-319 SOURCE-VERIFIED; format MEASURED from older capture, current build INFERENCE |
| B | **HIGH** | A persistent hybrid failure makes `ResetHistory()` run **every frame** (also resets the centre renderer) and logs a `warn` every frame. Two other failure paths (`EnsurePeripheryResources` false, shader-init failure) leave **no status and no transition log**. | INT:1462-1467, 1577-1595 SOURCE-VERIFIED |
| C | MED | `hybridRequested` has no `coverage < 100` term. A JSON-loaded `coverage=100` + periphery runs the whole hybrid with the crop = the entire eye: costs more than plain full-eye NR and shows nothing extra. | INT:1250 SOURCE-VERIFIED |
| D | MED (cost) | Four redundant full-SBS copies per hybrid frame (3 snapshot copies + the periphery writeback) plus a useless staged copy-in. About 0.4-1.0 ms/frame on an 11 Mpx SBS target (INFERENCE, byte counts only). | INT:1492-1495, 1375-1379 |
| E | LOW (correctness) | Coarse-grid UV uses `size/ceil(size/8)` instead of `1/8`. With H % 8 != 0 the coarse field is mis-registered by up to (8 - H%8)/8 texel at the bottom (3.7 px at H=2428). Magnitude of the visible error is tiny (< 0.5 LSB typical, derived below) but the fix is free. The 2.21.3 audit missed it. The `eyeWidth % 8` gate is the same mapping assumption and can be removed with the same patch. | HCS:35,42,104-108 SOURCE-VERIFIED |
| F | MED (quality) | Hybrid distance field is a bare `min()` of four edge distances (rectangular contours with corner creases). Rounded-rectangle SDF + "do not feather sides that touch the eye border" fixes both. | HCS:113-115 |
| G | LOW | Backlog P2-2 as written (4 px grid for the centre only) is **structurally wrong**: both coarse operators must be identical or the mid band is double counted. | section 4 |
| H | INFO | `ResultShaping` sliders only reach the centre's detail band, never the shared low-frequency field (confirms P1-5); the guided-filter path is dead code with the same UV bug. | INT:1519, INT:98 |

The composite algebra itself (section 1) is correct, continuous at the crop edge, and has no cross-eye bleed.

---------------------------------------------------------------------------------------------------

## 1. Composite math re-derivation and edge cases

### 1.1 Algebra (SOURCE-VERIFIED, `PeripheryHybridMath.hlsli:17-25`, HCS:118-125)

```
effP = Pout - raw          (Pout = raw + bilinear(Pm_out - Pm_in), MRC:143-149)
effC = Cout - raw
cP   = coarse8(effP),  cC = coarse8(effC)        (HCS mode 0)
out  = raw + g*cP + w*(effC - cC) + (1-w)*(effP - cP)
```
Group it by band, with `H` = "8x8 block mean then bilinear up" and `I-H` its complement:

```
out = raw + g*H(effP) + (I-H)[ w*effC + (1-w)*effP ]
```
Consequences:
1. The low-frequency term `g*H(effP)` is the **same function of x everywhere**, inside and outside the crop. There is no w in it, so no low-frequency step at the seam for any w, g. This is why the hybrid fixes the saturation step. SOURCE-VERIFIED (algebra).
2. Only the high band `(I-H)` is cross-faded. A step can only come from `(I-H)(effC - effP)` times a C1 ramp. The raised cosine `0.5-0.5cos(pi t)` has zero slope at both ends (`PeripheryHybridMath.hlsli:7-10`), so there is no gradient kink at the crop edge or at the end of the feather.
3. `w = 0` at the outermost crop pixel (`edge > 0 ? ... : 0`, HCS:115). Outside the crop `out = raw + effP - (1-g)*cP`, i.e. the periphery's own effect with the low-frequency scaled by g. `w = 1` deep inside gives `raw + g*cP + effC - cC`: the centre's own low-frequency is **discarded** and replaced by `g*cP` (design choice, see 5.4).
4. `H` is a weak low-pass. Box(8) followed by bilinear has a response of roughly 0.6 at a 16 px period, ~0.8 at 32 px, ~0.95 at 64 px (INFERENCE, closed form). So 16-48 px scale structure is only partly moved into the shared band; the rest still cross-fades. That is the real source of any residual faint boundary once A is fixed.

### 1.2 Edge-case table

| Case | Verdict | Evidence |
|---|---|---|
| Eye width 2240 / 2496 / 2432 (all % 8 == 0) | Correct. `coarseEyeWidth = coarseSize.x*0.5 = EyeWidth/8` exactly, `eyeCoarseWidth = EyeWidth/8`. | HCS:42,104 |
| Eye width not a multiple of 8 | Not reachable (gate INT:1249 `eyeWidthAligned`, reason `"eyeWidth%8"` INT:1255). If the gate were lifted, mode 0 would mis-assign `eye = id.x/eyeCoarseWidth` for the last column and `id.xy*8` (HCS:85) would address the wrong pixels. | HCS:42-43,85 |
| Height 2428 (H%8 = 4), 2432, 2688 | 2432/2688: exact. 2428: coarse rows 304, last block 4 rows. Mode 0 area weights are correct (`displayY1 = min(+8, Height)` HCS:51; `lowY1` scaled by real `Height` HCS:55). Dispatch is ceil-based (INT:488-489, 514, 434). Texture sizes use ceil (INT:310-311, 317-318). Sampler is LINEAR+CLAMP (INT:345-347). | verified |
| Height 2428 UV in mode 1 | **Mis-scaled**: `coarseY = (y+0.5)*coarseSize.y/Height` (HCS:107) = `(y+0.5)*304/2428 = (y+0.5)*0.125206`, but block i covers pixels `[8i,8i+8)`, so the correct coordinate is `(y+0.5)/8`. Error at row y is `y*(ceil(H/8)/H - 1/8)` texels; at the bottom `(8 - H%8)/8` texel (0.5 texel = 4 px at H=2428; worst case H%8=1 gives 0.875 texel = 7 px). Zero at the top. | HCS:107 |
| Why E is cosmetic | The shifted field leaks into the detail path as `shift * grad(cP)`. For a lighting field with amplitude 0.05 and 200 px scale: `4 px * 0.05/200 = 1e-3` (0.25 LSB). Scaled by (1-g) = 0.17 outside and by `g*grad(cP) - grad(cC)` inside. INFERENCE (estimate), negligible unless the field has sharp features. | - |
| Partial last block, centre sum | `c` is the sum over 64 clamped taps / 64 (HCS:82-92), so duplicated last row/col over-weights the border pixels. Only matters if the crop touches the bottom edge. | HCS:85,92 |
| Per-eye split | Mode 0: `eye = id.x / eyeCoarseWidth` HCS:43; low-res residual is read at `eye*uint(lowEyeWidth) + lx` HCS:68 where `lowEyeWidth = lowWidth*0.5`, and the texture is `modelEyeWidth*2` wide (INT:319), written at `targetX = eyeIndex*modelWidth` (REN:2594). Mode 1 clamps the coarse X to the eye's own half-texel range (HCS:105-106). No cross-eye bleed. | verified |
| Per-eye crop offsets (2.21.4) | `leftCoverageRect/rightCoverageRect` are passed (INT:1545-1546); constants carry per-eye X/Y but a **single** `CropWidth/Height` from the left crop (INT:495-499). Safe only because `matchingCoverageRects` is enforced (INT:1203-1218, 1247). Rects are clamped inside the eye (`RuntimePolicy.h` `snappedStart` clamps to `[0,maxStart]`). | verified |
| Crop at / near eye border | `edge` counts the distance to the border-coincident crop side too, so a feather ring is drawn at the eye border where there is no seam to hide (periphery detail replaces centre detail in the outer `FeatherWidth` px of the eye). Harmless but wasteful quality loss at coverage 90/95 or large offsets. See F. | HCS:113-114 |
| `w = 0` at crop edge, feather > half crop | `t = saturate(edge/max(F,1))`; if `F` (<=256, `FoveatedRender.cpp:682`) exceeds half the smaller crop side, `w` never reaches 1 in the middle. Benign, still continuous. | PeripheryHybridMath.hlsli:7 |
| Centre coarse near the crop edge | `CentreCoarse` is only computed in blocks overlapping the crop +-1 block (HCS:73-79). Blocks straddling the edge average in the raw-copy (effC=0) outside the crop, biasing cC low there. The bias lives within 8 px of the edge where `w <= 0.006` for a 160 px feather. Negligible. | HCS:73-90 |
| Clamps | Gain clamped [0,1.5] in both paths (HCS:124, hlsli:15). `FeatherWidth` floor 1. `w` saturated. `result` is **not** clamped or NaN-guarded (HCS:121-126). | verified |
| NaN / Inf | No guard in the hybrid. With UNORM kTOTAL sources NaN cannot occur (UNORM load/store maps NaN to 0), so this is latent. It becomes live the moment the residual textures are R16F (fix A): add `isfinite` guard (patch A3). | INFERENCE |
| fp16 | Hybrid math is fp32. Current storage is kTOTAL format (A). After fix A, R16F gives 2^-11 relative precision (about 0.0005 near 1.0), below 1/255. The stagger coarse history is already R16F (REN:2168). | verified |
| Alpha | `Output = float4(result, rawSample.a)` HCS:126 - preserves kTOTAL alpha. | verified |
| Output clamp | `max(result,0)` is absent; for an UNORM kTOTAL the UAV store clamps anyway. If kTOTAL were float, negatives would pass downstream. | HCS:126 |
| Hazards | Mode 0 binds UAV u0,u1 + SRV t0,t1,t2,t5; mode 1 binds UAV u2 + SRV t0,t1,t2,t3,t4; SRVs/UAVs are nulled after every dispatch (INT:515-519). Output UAV = kTOTAL, Raw = private copy, so no SRV/UAV alias. | INT:505-519 |

### 1.3 The UNORM problem in detail (Finding A)

* `EnsurePeripheryResources` creates `peripheryCoarse`, `centreCoarse`, `peripheryLowResResidual` with `Upscaling::CreateTextureFromSource(source, ...)` (INT:317-319), and `CreateTextureFromSource` copies `srcDesc.Format` for the texture, the SRV and the UAV (`FoveatedRender\Core.cpp:176-224`, e.g. `desc.Format = srcDesc.Format` line ~199). `source` is `total.texture` (INT:1491).
* The NR input/teacher format in the shipped capture (`<DATA_ROOT>\OpenNR_Captures\seq-1790292185651-1\frames.jsonl`, stages `input` and `teacher`) is `R8G8B8A8_UNORM` (MEASURED, older capture build; `eye.color` is created from the same kTOTAL source). kTOTAL format on the current build is therefore INFERENCE, to be confirmed with the one-line log in patch A1.
* Those three textures hold **signed** quantities: MRC mode 5 writes `float4(effect, 0)` (MRC:134-135); HCS mode 0 writes `float4(p,0)` / `float4(c/64,0)` (HCS:91-92). On an UNORM UAV the negative channels saturate to 0 and the rest is quantised to 1/255.
* Contrast: the stagger fallback `PeripheryCoarseEffect` is explicitly `R16G16B16A16_FLOAT` (REN:2168), and the temporal base/residual are R16F (REN:1803-1805). The author knew; the Integration textures were missed.
* Resulting behaviour (INFERENCE, algebra): with `c = cP`, `c' = cC` (signed) and `x+ = max(x,0)`:
  inside crop low-freq = `g*c+ + c'-` ; outside = `g*c+ + c-`... i.e. positive parts are shared (and gain-scaled), **negative parts are taken from each region's own model** (centre vs 50% periphery), so a centre-vs-periphery difference in the negative components (e.g. blue reduction in sky) is not removed. This matches the symptom the user attributed to the gate. It also explains why no one saw a hard failure: the degradation is graceful.
* Memory cost of the fix: coarse textures are 1/64 area (negligible); `PeripheryLowResResidual` at 50% is 2*1120 x 1214 x 8 B = 21.8 MB vs 10.9 MB now (+11 MB, INFERENCE from INT:312-319).

---------------------------------------------------------------------------------------------------

## 2. Temporal stagger review (eye-alternating cadence)

### 2.1 How it works (SOURCE-VERIFIED)

* Periphery tuning: `temporalReuseCadence = stagger ? 2 : 0`, `temporalReuseStaggerEyes`, `temporalReuseResetAfterSkip = false`, mode 4, no result shaping, no stabilisation (INT:1507-1520).
* `REN:1008-1040`: `staggerConfigured` requires cadence==2, tier supported, stable layout, no adaptive. `nativeEye = temporalFrameIndex & 1` (`RuntimePolicy.h:359-362`). If `reuseReady` (history valid, dims/guide/region match, no `resetPending` on either eye) it calls `ApplyStaggeredStereo` (REN:1421), which warps the reuse eye (`TryTemporalReuse`, REN:1435) **before** running NGX for the native eye, then writes both eyes' full region to `peripheryOutput` (REN:1488-1505), records history for the native eye only (REN:1508-1515), flips `staggerSkippedLastFrame`.
* The low-res residual for each eye is regenerated every frame: native eye from model-res `output - modelInput`; reuse eye from `resolved - eye.color` sampled at model-res (REN:2576-2610, flag `staggerSkippedLastFrame`). The native eye also snapshots a 1/8 coarse effect (REN:2150-2197).
* Cadence math: the reuse eye is always exactly **1 frame** old (native at t-1, reuse at t), accumulated MV covers exactly that frame (`RecordTemporalHistory` zeroes accumulated motion, REN:2294-2296). The comment at INT:1511-1516 says "two host frames old"; for the eye-staggered path that is not the case (it is true for the both-eyes-alternate N2 path). INFERENCE, derived from REN:1011-1012, 1518-1519.

### 2.2 Validity scenarios

| Scenario | Behaviour | Verdict |
|---|---|---|
| First frame / uninitialised history | `ResetHistory` -> `InvalidateTemporalHistory` sets `temporal.valid=false`, `temporalFrameIndex=0` (REN:2352-2366); `resetPending[*]=true` (REN:1587-1594). `reuseReady` is false so the first frame runs the full both-eye path and records both histories (REN:1041-1047, 1384-1393). Frame 2 onwards staggers. | OK |
| Mode toggle (hybrid on/off, stagger on/off, resolution) | INT:1462-1467 resets on `hybridRequested` change, resolution change, stagger change, or a >2-frame gap. `SyncTemporalReuseConfig` also invalidates on model/cadence change (REN:1683-1693). | OK |
| Resize / display relatch | `CanAttemptTemporalReuse` compares width/height/guide/region (REN:1695-1708); `EnsurePeripheryResources` rebuilds on size/format/resolution change (INT:302-327). `peripheryCoarseEffect` also re-created on dims change (REN:2160-2171). | OK (INFERENCE for NGX feature re-creation on resize) |
| Crop move / stereo offset slider | The periphery instance's inputs are fixed `sourceX = eye*eyeWidth` (INT:1498-1502) so its history does not depend on the crop. But INT:1438-1442 calls `ResetHistory()` which also resets **both** renderers whenever the crop geometry key changes. While a crop-offset slider is dragged this forces a full both-eye periphery evaluation plus NGX reset every frame. Quality-neutral fix: reset only `Renderer::Instance()` there (patch B3). | MED/LOW |
| Cell change / loading / teleport | Loading overlay and console set `temporalSuppressed` -> `ResetHistory` (INT:544-553, 857-868). Menu open/close only calls `InvalidateTemporalState` (INT:861-865), not `ResetHistory`; the >2-frame gap check (INT:1465-1466) then covers the periphery. Door/teleport without a loading menu relies on the warp rejection below. | OK |
| Warp rejection | `Reproject` rejects on bounds, depth (`gDepthThreshold`), and luma (`gColorTolerance`) (TRU:160-180). On rejection with periphery context it adds the **unwarped** previous coarse effect sampled at the current pixel (TRU:185-191). During head rotation this stale-but-smooth field is mis-registered by the full motion; fine for lighting, visible only at disocclusions. | OK (HYPOTHESIS on visibility) |
| One-eye-stale artifacts | The reuse eye shows `raw_now + warp(residual_{t-1})`; the centre detail is always current. Risk is **45 Hz binocular alternation** if native and reused periphery eyes differ in brightness (e.g. when the model output is itself temporally unstable, or when the coarse fallback kicks in only in one eye). | HYPOTHESIS; measure with P2-4 |
| NGX history age | The native eye's NGX internal history is 2 host frames old but is fed 1-frame MVs (INT:1511-1516, `temporalReuseResetAfterSkip=false`). Acceptable for a 50% low-frequency pass, a slight motion lag/ghost is possible. | HYPOTHESIS |
| Missed host frames | If `ApplyFoveatedLdr` returns early (guides not current INT:1095-1096, menu, etc.) the accumulated MV lacks those frames. The `>2` gap reset limits the damage to a 1-frame hole. | OK |
| Gaze-dynamic / adaptive crop | Hybrid is inactive (INT:1250-1255), stagger not involved. | OK |
| Failure path | A failed hybrid frame sets `peripheryRouteWasActive=false` (INT:1582) so the next frame calls `ResetHistory()` again (INT:1462). See Finding B. | BUG |

### 2.3 Finding B in detail

```
INT:1462  if (hybridRequested != peripheryRouteWasActive || ...) ResetHistory();
INT:1481  peripheryRouteWasActive = hybridRequested;
INT:1582  peripheryRouteWasActive = false;   // inside the "else if (rawSnapshotReady)" failure branch
INT:1584  logger::warn("[DLSSNR] Periphery NR hybrid pass failed; using centre-only coverage this frame");
```
If the periphery or centre `ApplyStereo`, or `DispatchPeripheryHybrid`, fails persistently (for example NGX latched, VRAM exhaustion in `EnsureResources`), every frame: warn line, status mutex publish, full-SBS raw restore (`CopyResource`, INT:1581), **and** `ResetHistory()` (resets the centre renderer too, INT:799-806), so the "centre-only fallback" runs with permanently reset temporal history. SOURCE-VERIFIED by reading; the per-frame repeat is INFERENCE from the control flow.

Silent paths (no log, no status):
* `EnsurePeripheryResources` returns false (INT:320-321 returns false with no log) -> `rawSnapshotReady` stays false (INT:1490-1491) -> no warn, no status, hybrid just does not run, fallback centre route runs (INT:1597).
* `hybridShaderReady` false: one `logger::error` at init (INT:352) but the route status is not updated and no "inactive: shader" transition is logged (the inactive block INT:1256 only runs when `!hybridRequested`).

---------------------------------------------------------------------------------------------------

## 3. Cost analysis

All GPU numbers are INFERENCE from byte/pixel counts unless stated. SBS target 4480x2428 = 10.88 Mpx; if kTOTAL is RGBA8 that is 43.5 MB per full copy (read+write 87 MB); if RGBA16F twice that.

### 3.1 Current per-frame pass inventory (hybrid, stagger on)
1. `HybridInputSnapshotCopies`: 3 SBS `CopyResource` (INT:1493-1495): total->raw, raw->peripheryOutput, raw->centreOutput.
2. Periphery instance (REN:947-979): for **both** eyes every frame: `ColorUpload` (full-eye copy from `peripheryRaw`), `ModelDownsample` (exact-area, full-res read), `GuideCopies` (depth+MV). NGX on 1 eye (50% res). `ModelResolve` mode 4 on that eye (full-res write). Reuse eye: accumulate MV + reproject (full-res). Per eye `DispatchPeripheryResidual` (model res). Writeback of both eyes into `peripheryOutput` (2 full-eye copies, REN:1488-1505). Native eye coarse snapshot.
3. Centre instance: both eyes every frame at 100% on the crop only (INT:1533-1541).
4. Hybrid mode 0 (coarse, 560x304 threads, 64-tap loops only on crop blocks) + mode 1 (full SBS, 3 loads + bilinear + 1 store).
5. If `total.UAV` is null: `FoveatedStagedTargetCopyIn` (SBS) + `PeripheryStagedWriteback` (SBS) (INT:1375-1379, 1573-1576). Whether kTOTAL has a UAV is unknown; the only log line available says `path=direct-copy` for the **full-eye** (non-blend) route, which does not test it (INT:1643-1644; CommunityShaders.log line 656).

### 3.2 Quality-neutral savings, ranked by value/risk

| ID | Change | Saving (INFERENCE) | Risk | Where |
|---|---|---|---|---|
| C1 | Drop `CopyResource(peripheryOutput, peripheryRaw)`. Every path writes the whole eye of `peripheryOutput` before the composite reads it (stagger REN:1488-1505; full path REN:1366-1377), and a failed frame restores from `peripheryRaw` anyway (INT:1581). | 1 SBS copy (~0.1-0.25 ms) | none | INT:1494 |
| C2 | Drop `CopyResource(centreOutput, peripheryRaw)` by masking in mode 0: treat `effC = 0` outside the crop rectangle in the shader (`inCrop` test per tap) and never read outside it in mode 1 (already true: `w > 0` implies inside). | 1 SBS copy | low (add mask in HCS:87) | INT:1495, HCS:80-90 |
| C3 | Skip `FoveatedStagedTargetCopyIn` when the hybrid will run: the composite overwrites every pixel (`Output[q]` for all q) and failure restores from `peripheryRaw`. | 1 SBS copy, only if kTOTAL has no UAV | low | INT:1370-1385 |
| C4 | Do not load `Periphery` where `w == 1` and do not load `Centre` where `w == 0` (the latter already holds). Currently `Periphery.Load` is unconditional (HCS:116). | ~30% of the periphery read bandwidth of mode 1 | none | HCS:116-125 |
| C5 | Stop duplicating the raw snapshot: pass `total.texture` as the colour source to both instances and read `Raw` in the composite from the periphery instance's per-eye `eye.color` SRVs; composite reads `Periphery` from `tier.resolved` SRVs and `Centre` from the centre instance's per-eye output SRVs. Removes `peripheryRaw`, `peripheryOutput`, `centreOutput` and the 4 SBS copies. | ~0.4-1.0 ms total with C1-C3 | medium (API plumbing: new accessors on `Renderer`, composite takes 2x3 SRVs) | INT:1492-1551, REN:1488-1505 |
| C6 | Skip `ModelDownsample` for the reuse eye in the stagger path. It is only needed for the native eye and for the fallback full path. Needs the stagger decision before the per-eye loop (decide `nativeEye` first, run the reuse-eye downsample lazily on `FallBack`). | 1 exact-area downsample / frame | medium | REN:968-973, 1010-1040 |
| C7 | Lighting-only periphery tier at 33% (renderer already supports a 33 tier: `NormalizeModelResolution` REN:240-254). F18 pixels fall to 44% of the 50% tier. | ~55% of the periphery NGX cost | medium: gain calibration + quality (P2-1) | `RuntimePolicy.h:18`, `FoveatedRender.cpp:678-680, 1500-1508` |
| C8 | Move the periphery wholly to model-res space (history, warp, composite sample the low-res residual bilinearly). Removes mode-4 resolve, full-res reproject, full-res snapshot, 2 writeback copies and the full-res `Periphery` read. | the largest non-NGX saving, maybe 0.3-0.6 ms | high (rewrite) | REN, HCS |
| C9 | Adaptive stagger cadence (3-4 when accumulated MV is below a threshold). Stagger is hard-wired to cadence 2 (REN:1008), so this is new work. | up to ~50% of periphery NGX when still | medium; HYPOTHESIS on visibility (animated NPCs in the periphery) | REN:1008-1040 |
| C10 | Mode 0 centre sum via group-shared tile reduction (one group per 8x8 block, 64 lanes, wave reduce) instead of 64 serial loads per thread. | ~0.03 ms | low | HCS:80-90 |
| C11 | Avoid ResetHistory of the periphery on crop moves (see 2.2). | removes a spike while dragging sliders | low | INT:1438-1443 |
| C12 | Gate coverage 100 (Finding C). At 100% the crop is the whole eye: centre NR 100% + periphery 50% = about 125% of plain NR for no visible gain. | up to ~20% of NR cost in that misconfiguration | none | INT:1250 |

Not worth it / not applicable:
* 4 px coarse grid: raises the coarse pass thread count 4x and changes the filter (see 4, P2-2). Not a cost saving.
* Fusing mode 0 and mode 1: not possible, mode 1 needs neighbour blocks.
* Async compute: D3D11 immediate context only; NGX already runs on the D3D12 queue via interop. HYPOTHESIS that overlap would help; not verified.
* Packed formats: kTOTAL is already RGBA8; R11G11B10 cannot hold signed residuals. Use R16G16B16A16_FLOAT for the three residual textures (cost +11 MB, see 1.3).

---------------------------------------------------------------------------------------------------

## 4. Backlog walk-through (P1-1..P1-6, P2-1..P2-5)

### 4.0 Stale references to fix in the docs
| Doc says | Current location |
|---|---|
| gate `Integration.cpp:1196-1200` | INT:1249-1255 (height term already removed in 2.21.3) |
| `FoveatedRender.cpp:346` (NR-only feather clamp 128) | `FoveatedRender.cpp:650` (clamp), sliders at `:1530` and `:1534` |
| `FoveatedRender.cpp:374` (hybrid clamp 256) | `FoveatedRender.cpp:682` (`2.0f..256.0f`) |
| result shaping off `Integration.cpp:1416` | INT:1519 (`peripheryTuning.resultShapingEnabled = false`) |
| cadence `Integration.cpp:~1400` | INT:1509-1510 |
| UI block lines 1060-1330 | periphery UI is now `FoveatedRender.cpp:1481-1517` |
| hybrid fail-closed `Integration.cpp:1462` | INT:1577-1595 |

### 4.1 P1 items

**P1-1 Dither flicker (NR-only band).** Still valid, but only for the NR-only and hybrid-fallback routes (the hybrid does not use `SubrectBlendCS`). Cause SOURCE-VERIFIED: `BlueNoise(pos, frame)` adds `5.588238*frame` to both coordinates (`FoveatedRender\SubrectBlendCS.hlsl:36-37`), `FrameIndex = globals::state->frameCount` (`FoveatedRender\Core.cpp:1285`), `alpha = saturate(FalloffAlpha + (noise-0.5)*DitherStrength)` (`SubrectBlendCS.hlsl:125`) with strength up to 2 (clamp `FoveatedRender.cpp:652`). At 1.45 the alpha is mostly 0 or 1. Plan: remove the frame term (screen-fixed pattern), or default `subrectBlendMode=1` (Feather). Diff in 7.5. Also note the shader's `BlendMode == 1` is "dither" while the UI enum 2 is dither; `Core.cpp:1280` maps it (`kDither ? 1 : 0`), so no mismatch.

**P1-2 Oval -> superellipse.** Valid for the NR-only path only. The oval is `EllipseEdgeDistance` (`SubrectBlendCS.hlsl:46-58`) fed by `MaskCenter/Radius` from `Core.cpp:1296-1302`. Foveated stretch shares `MaskMode`, so gate the change behind a new mask mode (value 2) or apply only when `forceFeatherBlend`. A squircle makes the NR region reach the crop corners (more NR area for the same crop). Diff 7.5. With the hybrid running, retire it for the default config, as the backlog says.

**P1-3 Feather > 128 px in NR-only.** Valid. Change `FoveatedRender.cpp:650` upper bound 128->256 and sliders `:1530,1534` to 256. Side effect: the same setting feeds the foveated stretch path (`Core.cpp:1292`) and adaptive crop forces >= 32 (`Core.cpp:1306-1307`); both tolerate larger values (band is clamped by the mask extent). Note the shader treats `edgeDist >= featherDistance` as fully NR, so for the oval the max usable band is the minor radius.

**P1-4 Rounded-rectangle distance in the hybrid.** Valid and now more valuable than P1-1..3 since the hybrid is the default. Exact locations: HCS:113-115, `PeripheryHybridMath.hlsli:7-10`, mirror `RuntimePolicy.h:25-29` (`CentreFeatherWeight`) and the CPU fixture test (`tests\test_periphery_nr_source_contract.py:207,279`). The dead guided filter has its own copy (`PeripheryGuidedFilterCS.hlsl:58-65`); do not touch. Keep the contract `w = 0` on the outermost crop pixel (subtract 0.5). Include the eye-border exemption (F). Diff in 7.4. Cost: ~10 ALU per pixel in mode 1 (negligible).

**P1-5 Colour sliders reach the periphery.** Valid but the proposed fix needs one correction: the shaping delta must be applied to the **coarse field** `cP` with the **matching block-mean raw** (the tonal weights are luma-of-base dependent, `ResultShapingCS.hlsl:392-466`). Only `gEditStrength`, `gLargeScaleTone` (the coarse field IS the low band), brightening/darkening, shadows/mids/highlights, colour strength, hue shift and the stop caps make sense at 1/8 resolution; halo suppression (needs 3x3 neighbours) and fine detail do not. Plumbing: add the shaping constants to `PeripheryHybridConstants` (INT:131-138, 48 B -> pad to 64 B and update the static_assert) and run `ShapeDelta` on `p` in mode 0 (HCS:72-91) using a block-mean raw (64 extra loads for every block, ~11 M extra loads, still small). Currently the centre's `eC` is shaped (centre instance keeps `resultShapingEnabled`, INT:1533) while the shared low band `g*cP` is unshaped (INT:1519), which is why the sliders mostly do nothing. Low priority unless the user uses the sliders.

**P1-6 Retire guided-filter key.** Partially done: UI control removed in 2.21.5 (task list), but the key is still in `OPENNR_FOVEATED_SETTINGS_FIELDS` (`FoveatedRender.cpp:407`), `kPeripheryGuidedFilterQualified = false` (INT:98), the dispatch (INT:364-478), the 262-line shader, and 7 shader-compile tests (`tests\cpp\test_neural_rendering_shader_compile.cpp:33-39`). It carries the same coarse-UV bug (`PeripheryGuidedFilterCS.hlsl:47-56, 252-256`). Plan: either delete, or when re-qualifying, port patch E first.

### 4.2 P2 items

**P2-1 Lighting-only periphery at 33/25%.** Valid; best cost lever. Required edits: `kPeripheryResolutionPresets` (`RuntimePolicy.h:18`), normaliser (`FoveatedRender.cpp:678-680`), UI list/gains (`FoveatedRender.cpp:1500-1508`: add 33 and a calibrated gain), benchmark JSON. The renderer already has a 33 tier (REN:240-254) and mode 4 temporal reuse is tier-agnostic (REN:1679-1680). Quality caveat: at 33% the mid band `effP - cP` loses scales below ~24 px, so measure the centre-vs-periphery step in the feather band, not just the field RMS. Also the backlog says "25%"; no 25 tier exists (floor is 33, REN:248-249), so drop that option. Gain calibration (0.83 at 50%) must be re-measured; see 5.4 for an auto-calibration that removes the lookup table.

**P2-2 4 px coarse grid for the centre.** **Wrong as written.** The detail band is `(I-H)` where `H` must be the same operator for the periphery and the centre. If only `cC` moves to 4 px, `out = raw + g*H8(effP) + (I-H4)(...)` double counts the 4-8 px band (centre keeps it and the shared low band also contains it from the periphery). The correct options: (a) move both grids to 4 px (coarse pass threads x4: 1120x608 per SBS, still cheap; mode 0 p-loop is ~3x3 taps at 50%), or (b) keep 8 px and replace bilinear with a smoother kernel (Catmull-Rom/B-spline up-sample of the 8 px grid, 4 bilinear fetches per field), which is a better low-pass and removes the grid-aligned energy. The seam is helped by a **stronger** low-pass (larger shared band), not a weaker one (see 1.1.4, 5.3). The 8 px energy hypothesis itself is plausible (HYPOTHESIS): box8+bilinear leaves 16 px period at ~0.6 gain. Recommend (b) first.

**P2-3 Separate luma/chroma lighting gains.** Valid, cheap, no new passes. Needs 1 extra float in `PeripheryHybridConstants` (pad 48 -> 64 B, update `static_assert` INT:138) and a UI slider. Diff 7.6. Default gC = gL = current gain so default output is unchanged.

**P2-4 Flicker metric for the stagger.** Valid. Correct the premise: reuse age is 1 frame, not 2 (2.1). The relevant artefact is 45 Hz binocular alternation, so the metric should compare L/R periphery band means per frame (sign-alternating component at Nyquist), not only temporal std. Cadence 1 costs ~2x periphery NGX.

**P2-5 Saturation-ratio extension for NR-only.** Valid but superseded. With the hybrid working (after fix A) there is no need; the data it would need (`cC`/`cP` per block) is exactly what mode 0 already produces. Keep as "do not build".

---------------------------------------------------------------------------------------------------

## 5. Seam and colour: SOTA comparison and ideas

What the hybrid already is: a two-band Laplacian/pyramid-style blend (Burt-Adelson 1983) where the coarse band is taken from one source everywhere and only the detail band is cross-faded. That is the right structure. Differences from published practice:

5.1 **Band-dependent transition width (Burt-Adelson rule: transition width proportional to wavelength).** Today the low band has transition width 0 (a hard switch to `g*cP`) and the detail band uses one width. A 3-band version: low band shared (as now), a mid band (16-64 px) cross-faded over ~2-3x the feather, a high band (<8 px) over the current feather. Needs a second coarse grid (1/32) plus a tiny extra pass; shader cost ~1 more bilinear tap. HYPOTHESIS that it removes the residual 16-48 px mismatch noted in 1.1.4; cheap to A/B in a screenshot study.

5.2 **Weight shape.** Cosine ramp is C1 (OK). The visible defects are geometric: rectangular contours with 45 deg creases. Use a rounded-rectangle or superellipse SDF so the iso-contours are C1 everywhere (7.4). Guard: do not feather sides that coincide with the eye border (no seam to hide there, and the outer 80-160 px band is then wasted periphery).

5.3 **Stronger low-pass for the shared band.** Equivalent to making the "pure periphery" band wider. Use an 8 px grid with a cubic (B-spline) upsample, or a 16 px grid with bicubic. Effect: more mid-scale structure moves into the shared band where no seam is possible. Cost: 4 fetches per field per pixel in mode 1 (+~0.1 ms, INFERENCE).

5.4 **Low-frequency colour matching (gain auto-calibration).** The fixed `g` table (0.83/0.86/0.89/0.93, `FoveatedRender.cpp:1507`) was measured at one scene. The inside of the crop currently shows `g*cP` instead of the full-res model's own `cC`, so a wrong `g` changes the look of the *centre*, not just the border. Per frame, mode 0 already has `cP` and `cC` for all crop blocks; a least-squares gain per channel group `g_L = <cC . cP>/<cP . cP>` (luma) and `g_C` (chroma) over the crop-interior blocks, smoothed with an EMA over ~30 frames and clamped to [0.6,1.2], makes the interior match the true centre NR on average and removes the dependence on the per-resolution table. Implementation: 3 reduction sums in a second tiny dispatch + a 16 B structured buffer read by mode 1. HYPOTHESIS that it improves scenes far from the calibration scene; low risk because it only changes a scalar. This subsumes P2-3 (separate luma/chroma gains).

5.5 **Wider low-band transition using the centre's own `cC`.** Instead of `g*cP` everywhere, `low = lerp(g*cP, cC, w_low)` where `w_low` is a much wider ramp (2-3x feather, still inside the crop). This is true pyramid blending and keeps the 100%-resolution model's low band in the interior. It is only safe if `g` is calibrated (5.4), otherwise it re-creates the step it removed. Needs `cC` available for the wider ring (extend `centreCoarseNeeded` margin, HCS:73-79).

5.6 **Gradient-domain / Poisson blending.** Because the low band is already shared by construction, a Poisson solve adds little over (5.1-5.5) and needs an iterative solver; not worth it. A cheap approximation is exactly 5.3 (smoother low-pass).

5.7 **Dither / temporal noise.** The hybrid has no noise. Keep it that way. For the NR-only fallback use a screen-fixed pattern (7.5).

5.8 **Eccentricity-tied width.** Foveated-rendering literature ties the blend width to visual angle, not pixels (Guenter 2012, Patney 2016, Kaplanyan 2019). Feather is in px (`FoveatedRender.cpp:682`); a degree-based setting would make it scale with display FOV/size (the user's 90% FOV tangent changes px per degree by ~10%). Minor.

---------------------------------------------------------------------------------------------------

## 6. Silent-off gates and unlogged degradations

Logged on transition (OK): the `hybridRequested` terms (INT:1250-1255, log INT:1256-1274): `disabled`, `fullEye`, `gaze`, `adaptiveCrop`, `coverageRects`, `depthFormat`, `eyeWidth%8`. Priority is first-failing-term only; fine.

Still silent or misleading (SOURCE-VERIFIED unless noted):

| # | Condition | Effect | Where | Proposed reason string |
|---|---|---|---|---|
| S1 | NR enabled but the early route checks fail after the status block (`temporalSuppressed`, `preUpscaleAppliedFrame==frame`, `lastAppliedFrame==frame`, guide frame stale) | Whole frame has no NR (including the periphery); no log | INT:1063-1065, 1092-1096 | counter + rate-limited log `guideStale` |
| S2 | `!depth/motion` guides null, `leftUV.w != rightUV.w`, `fullGuide{Width,Height}==0` | `return false`, silent, periphery not attempted | INT:1138-1141, 1150-1151 | `guidesMissing` / `uvMismatch` |
| S3 | `EnsurePeripheryResources` false (VRAM, null source) | hybrid silently not run, centre-only every frame, no status | INT:1490-1491, 320-321 | `resources` |
| S4 | `hybridShaderReady` false | one `error` at init, no transition log, status not updated | INT:1303, 352 | `shader` |
| S5 | `coverage == 100` with periphery enabled | hybrid runs with crop = whole eye (cost, no benefit); UI forces 55 but JSON load/benchmarks do not | INT:1250; UI only at `FoveatedRender.cpp:1488-1490` | `coverage100` (gate) |
| S6 | Hybrid runs but any sub-pass fails | `warn` every frame + `ResetHistory` every frame (Finding B) | INT:1577-1595, 1462 | add backoff (patch B) |
| S7 | `coverage` value not in presets | `NormalizeNeuralCoverage` silently maps to 100 (warn once) | `FoveatedRender.cpp:663-666` | already warns once; combine with S5 |
| S8 | `projectionCropResolved` fails -> centred crop | logged once (`projectionCropUnavailableLogged`), but the hybrid then runs with a centred crop silently | INT:1168-1170, 1209-1218 | include `crop=centred` in the "active" log |
| S9 | Per-frame hybrid refusal transitions are logged, but the **first** log after startup is always `inactive: disabled` (user's log line 649) because `lastPeripheryInactiveReason` starts `nullptr` | noise only | INT:1257 | none |
| S10 | `Periphery NR active` does not print the kTOTAL format, staged-vs-direct path, or whether crop is projection-centred | cannot tell A from a log | INT:1556 | add `format`, `path`, `crop` |

---------------------------------------------------------------------------------------------------

## 7. Proposed changes (diff-style)

### 7.1 Patch A: signed residual textures and format log (Finding A) - INT

A1. Log once on first activation (zero risk, do this even before fixing):
```diff
--- INT: inside the "Periphery NR active" log (INT:1556)
-logger::info("[DLSSNR] Periphery NR active: display={}x{} coverage={}% res={}% gain={} feather={} stagger={}",
-    totalDesc.Width, totalDesc.Height, coverage, ...
+logger::info("[DLSSNR] Periphery NR active: display={}x{} fmt={} path={} coverage={}% res={}% gain={} feather={} stagger={}",
+    totalDesc.Width, totalDesc.Height, static_cast<int>(totalDesc.Format),   // 28 = R8G8B8A8_UNORM
+    stagedBlendTarget ? "staged" : (destinationUAV ? "direct-uav" : "no-uav"), coverage, ...
```
A2. Create the three signed textures as R16G16B16A16_FLOAT:
```diff
 // INT, next to CreatePeripheryGuidedArray (line ~199)
+eastl::unique_ptr<Texture2D> CreateSignedHalfTexture(std::uint32_t width, std::uint32_t height, const char* name)
+{
+    if (!width || !height) return nullptr;
+    D3D11_TEXTURE2D_DESC desc{};
+    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
+    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
+    desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
+    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
+    try {
+        auto t = eastl::make_unique<Texture2D>(desc, name);
+        D3D11_SHADER_RESOURCE_VIEW_DESC s{}; s.Format = desc.Format;
+        s.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; s.Texture2D.MipLevels = 1;
+        t->CreateSRV(s);
+        D3D11_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format = desc.Format;
+        u.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
+        t->CreateUAV(u);
+        return t;
+    } catch (...) { return nullptr; }
+}
...
-peripheryCoarse = Upscaling::CreateTextureFromSource(source, coarseWidth, coarseHeight, false, true, true, "NeuralRendering::PeripheryCoarse8");
-centreCoarse = Upscaling::CreateTextureFromSource(source, coarseWidth, coarseHeight, false, true, true, "NeuralRendering::CentreCoarse8");
-peripheryLowResResidual = Upscaling::CreateTextureFromSource(source, modelEyeWidth * 2u, modelHeight, false, true, true, "NeuralRendering::PeripheryLowResResidual");
+peripheryCoarse = CreateSignedHalfTexture(coarseWidth, coarseHeight, "NeuralRendering::PeripheryCoarse8");
+centreCoarse = CreateSignedHalfTexture(coarseWidth, coarseHeight, "NeuralRendering::CentreCoarse8");
+peripheryLowResResidual = CreateSignedHalfTexture(modelEyeWidth * 2u, modelHeight, "NeuralRendering::PeripheryLowResResidual");
```
Texture2D constructor/`CreateSRV/CreateUAV` usage mirrors `CreatePeripheryGuidedArray` (INT:199-235). `peripheryRaw/Output/centreOutput` stay in the kTOTAL format (they are display-referred colours).

A3. NaN/overflow guard in the composite (needed once the textures are float):
```diff
 // HCS end of main
-Output[q] = float4(result, rawSample.a);
+if (!all(isfinite(result))) result = raw;
+Output[q] = float4(result, rawSample.a);
```
and in mode 0 `p = all(isfinite(p)) ? p : 0;` before the store (HCS:91).

Test: extend `test_periphery_nr_source_contract.py` with an assertion that none of `peripheryCoarse|centreCoarse|peripheryLowResResidual` is created via `CreateTextureFromSource`; add a runtime check that `desc.Format` of those three is `R16G16B16A16_FLOAT`.

### 7.2 Patch B: failure backoff and visible refusal (Finding B, S3, S4, S6) - INT

B1. Stop the per-frame reset loop and the warn spam:
```diff
 // file-scope state
+std::uint32_t peripheryBackoffUntilFrame = 0;
+bool peripheryBackoffLogged = false;
...
 const bool eyeWidthAligned = ...;
-const bool hybridRequested = peripheryEnabled && fullEye && !gaze.dynamic && !adaptiveCropActive &&
-    coverageRectsValid && coverageDepthSupported && eyeWidthAligned;
+const bool inBackoff = frame < peripheryBackoffUntilFrame;
+const bool coverageUseful = coverage < 100;
+const bool hybridRequested = peripheryEnabled && fullEye && !gaze.dynamic && !adaptiveCropActive &&
+    coverageRectsValid && coverageDepthSupported && coverageUseful && !inBackoff;   // + eyeWidthAligned unless patch E
 const char* peripheryInactiveReason = !peripheryEnabled ? "disabled" :
     !fullEye ? "fullEye" : gaze.dynamic ? "gaze" : adaptiveCropActive ? "adaptiveCrop" :
-    !coverageRectsValid ? "coverageRects" : !coverageDepthSupported ? "depthFormat" :
-    !eyeWidthAligned ? "eyeWidth%8" : nullptr;
+    !coverageRectsValid ? "coverageRects" : !coverageDepthSupported ? "depthFormat" :
+    !coverageUseful ? "coverage100" : inBackoff ? "hybridBackoff" : nullptr;
```
```diff
 // INT:1577 failure branch
-peripheryRouteWasActive = false;
-peripheryStatusWasActive = false;
-logger::warn("[DLSSNR] Periphery NR hybrid pass failed; using centre-only coverage this frame");
+peripheryStatusWasActive = false;
+peripheryBackoffUntilFrame = frame + 600;          // ~6.7 s at 90 Hz, then retry
+logger::warn("[DLSSNR] Periphery NR hybrid pass failed (frame {}); centre-only for 600 frames, then retry", frame);
```
`peripheryRouteWasActive` stays `true` until the next `hybridRequested==false` frame updates it at INT:1481, so the next frame sees `false != true` once, calls `ResetHistory()` once, and then not again. Add the inactive reason `hybridBackoff` to the status UI string table (`FoveatedRender.cpp:154-165`).

B2. Make S3/S4 visible:
```diff
 // INT:1490
-if (hybridRequested && hybridShaderReady &&
-    EnsurePeripheryResources(Util::AsReal(total.texture), totalDesc, requestedPeripheryResolution)) {
+const bool hybridResources = hybridRequested && hybridShaderReady &&
+    EnsurePeripheryResources(Util::AsReal(total.texture), totalDesc, requestedPeripheryResolution);
+if (hybridRequested && !hybridResources && !hybridResourceFailureLogged) {
+    hybridResourceFailureLogged = true;
+    logger::error("[DLSSNR] Periphery NR refused: {}", hybridShaderReady ? "resources" : "shader");
+    /* publish RouteStatus with refusalTerm = hybridShaderReady ? "resources" : "shader" */
+}
+if (hybridResources) {
```
(`hybridResourceFailureLogged` reset in `Reset()` and on settings change.)

B3. Do not reset the periphery on crop moves:
```diff
 // INT:1438-1443
-logger::info("[DLSSNR] Stable NR crop geometry changed; resetting both Feature 18 histories");
-ResetHistory();
+logger::info("[DLSSNR] Stable NR crop geometry changed; resetting centre Feature 18 history");
+Renderer::Instance().ResetHistory();
+coverageHistoryGeometryObserved = false;
```
(the periphery instance consumes fixed full-eye inputs, INT:1498-1502).

### 7.3 Patch C: copies and loads (Finding D, C1-C4)

```diff
 // INT:1492-1496
 context->CopyResource(peripheryRaw->resource.get(), Util::AsReal(total.texture));
-context->CopyResource(peripheryOutput->resource.get(), peripheryRaw->resource.get());   // C1: whole eye is rewritten by the periphery writeback
-context->CopyResource(centreOutput->resource.get(), peripheryRaw->resource.get());      // C2: replaced by an in-crop mask in mode 0
 rawSnapshotReady = true;
```
```diff
 // HCS mode 0 centre sum (HCS:80-90): mask by crop rectangle instead of relying on the pre-copy
-c += Centre.Load(int3(q, 0)).rgb - raw;
+const int2 lq = int2(q.x - eye * EyeWidth, q.y);
+const bool inCrop = lq.x >= int(cropX) && lq.x < int(cropX + CropWidth) &&
+                    lq.y >= int(cropY) && lq.y < int(cropY + CropHeight);
+c += inCrop ? (Centre.Load(int3(q, 0)).rgb - raw) : 0.0;
```
```diff
 // HCS mode 1 (HCS:116-125): load the periphery only when it contributes
-const float3 peripheryOutput = Periphery.Load(int3(q, 0)).rgb;
 float3 result;
 if (w > 0.0) {
     const float3 coarseC = CentreCoarse.SampleLevel(LinearClamp, coarseUV, 0).rgb;
     const float3 centreOutput = Centre.Load(int3(q, 0)).rgb;
-    result = PeripheryCompose(raw, peripheryOutput, centreOutput, coarseP, coarseC, w, PeripheryGain);
+    const float3 peripheryOutput = (w < 1.0) ? Periphery.Load(int3(q, 0)).rgb : raw + coarseP;
+    result = PeripheryCompose(raw, peripheryOutput, centreOutput, coarseP, coarseC, w, PeripheryGain);
 } else {
+    const float3 peripheryOutput = Periphery.Load(int3(q, 0)).rgb;
     ...
```
(for `w==1` the `(1-w)` term vanishes, so any finite stand-in is exact.)

```diff
 // C3, INT:1370
-if (wantsEdgeBlend && !destinationUAV) {
+if (wantsEdgeBlend && !destinationUAV) {
     if (EnsureStereoBlendTarget(...)) {
-        context->CopyResource(stereoBlendTarget->resource.get(), Util::AsReal(total.texture));
+        if (!hybridRequested)   // the hybrid overwrites every pixel and restores from peripheryRaw on failure
+            context->CopyResource(stereoBlendTarget->resource.get(), Util::AsReal(total.texture));
```
Caveat: if the hybrid fails before the composite, INT:1581 restores `destination` from `peripheryRaw`, so the staged copy is still correct on the failure path. If hybrid is requested but resources fail (no snapshot), the fallback needs the background, so keep the copy when `hybridResources` is false: use `hybridRequested && hybridResources` (needs B2 reorder).

### 7.4 Patch D: rounded-rectangle weight + eye-border exemption (P1-4, F) - HCS / hlsli

```hlsl
// PeripheryHybridMath.hlsli
float PeripheryInsideDistance(float2 pEye /*eye-local pixel centre*/, float2 cropMin, float2 cropMax,
                              float radius)
{
    const float2 c = 0.5 * (cropMin + cropMax);
    const float2 h = 0.5 * (cropMax - cropMin);
    const float  r = min(radius, min(h.x, h.y));
    const float2 q = abs(pEye - c) - (h - r);
    const float  sdf = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;   // < 0 inside
    return -sdf - 0.5;   // keeps the contract: outermost crop pixel -> distance 0 -> w = 0
}
```
```diff
 // HCS:111-115
 const uint cropX = ...; const uint cropY = ...;
-const int edge = min(min(int(localX) - int(cropX), int(cropX + CropWidth - 1u) - int(localX)),
-    min(int(q.y) - int(cropY), int(cropY + CropHeight - 1u) - int(q.y)));
-const float w = edge > 0 ? PeripheryCentreWeight(float(edge), FeatherWidth) : 0.0;
+// sides that touch the eye border extend outward so they are never feathered
+const float big = 4.0 * FeatherWidth + 64.0;
+const float2 cMin = float2(cropX == 0u ? -big : float(cropX), cropY == 0u ? -big : float(cropY));
+const float2 cMax = float2((cropX + CropWidth)  >= EyeWidth ? float(EyeWidth) + big : float(cropX + CropWidth),
+                           (cropY + CropHeight) >= Height   ? float(Height)   + big : float(cropY + CropHeight));
+const float d = PeripheryInsideDistance(float2(localX, q.y) + 0.5, cMin, cMax, FeatherWidth);
+const float w = d > 0.0 ? PeripheryCentreWeight(d, FeatherWidth) : 0.0;
```
Mirror in `RuntimePolicy.h:25-29` + the CPU fixture; keep the existing `centreCoarseNeeded` bounding box (rounding only shrinks `w>0`). Optional config: expose the corner radius as `min(FeatherWidth, 0.25*min(CropWidth,CropHeight))`. Note rounded corners shrink the used centre area by about `0.86 R^2` per corner (R=160: 22 kpx per corner, 1.3% of a 55% crop); acceptable, or use a smaller radius (R = 0.5*Feather).

### 7.5 NR-only seam fixes (P1-1, P1-2)

```diff
 // SubrectBlendCS.hlsl:33-39  (P1-1, screen-fixed pattern)
-float x = float(pos.x) + 5.588238 * float(frame);
-float y = float(pos.y) + 5.588238 * float(frame);
+float x = float(pos.x);
+float y = float(pos.y);        // frame kept in the signature for ABI, unused
```
```hlsl
// P1-2: superellipse (p = 4) first-order distance, replace EllipseEdgeDistance body behind MaskMode==2
float SuperEllipseEdgeDistance(float2 offset, float2 radii)
{
    const float2 r  = max(radii, 0.5);
    const float2 n  = abs(offset) / r;
    const float2 n2 = n * n;
    const float  f  = 1.0 - dot(n2, n2);            // 1 - (x^4 + y^4)
    const float2 g2 = 4.0 * (n2 * n) / r;           // |grad f|
    const float  gl = length(g2);
    if (gl < 1e-5) return min(r.x, r.y);
    return clamp(f / gl, -max(r.x, r.y), max(r.x, r.y));
}
```
Add `SubrectMaskMode::kSuperEllipse` (settings clamp `FoveatedRender.cpp:647`, `std::min(settings.subrectMaskMode, 1u)` -> 2u; `Core.cpp:1281`).
P1-3: `FoveatedRender.cpp:650, 1530, 1534`: `128.0f -> 256.0f`.

### 7.6 Patch E: general coarse mapping, remove width gate (E, S-gate) - HCS + INT

Per-eye coarse width `cw = ceil(EyeWidth/8)`, coarse texture width `2*cw`, eye-local blocks, `(p+0.5)/8` mapping.
```diff
 // HCS mode 0
-const uint2 coarseSize = uint2((Width + 7u) / 8u, (Height + 7u) / 8u);
+const uint eyeCoarseWidth = (EyeWidth + 7u) / 8u;
+const uint2 coarseSize = uint2(2u * eyeCoarseWidth, (Height + 7u) / 8u);
 ...
-const uint eyeCoarseWidth = EyeWidth / 8u;
 const uint eye = id.x / eyeCoarseWidth;
 const uint coarseLocalX = id.x % eyeCoarseWidth;
 ...
-const uint2 q = min(id.xy * 8u + uint2(x, y), uint2(Width - 1u, Height - 1u));
+const uint2 q = uint2(eye * EyeWidth + min(coarseLocalX * 8u + x, EyeWidth - 1u),
+                      min(id.y * 8u + y, Height - 1u));
```
```diff
 // HCS mode 1 (HCS:102-108)
-const uint2 coarseSize = uint2((Width + 7u) / 8u, (Height + 7u) / 8u);
-const uint eye = q.x / EyeWidth;
-const float coarseEyeWidth = float(coarseSize.x) * 0.5;
-const float coarseX = clamp(float(eye) * coarseEyeWidth + (float(q.x % EyeWidth) + 0.5) * coarseEyeWidth / float(EyeWidth),
-    float(eye) * coarseEyeWidth + 0.5, float(eye + 1u) * coarseEyeWidth - 0.5);
-const float coarseY = clamp((float(q.y) + 0.5) * float(coarseSize.y) / float(Height), 0.5, float(coarseSize.y) - 0.5);
-const float2 coarseUV = float2(coarseX / float(coarseSize.x), coarseY / float(coarseSize.y));
+const uint eyeCoarseWidth = (EyeWidth + 7u) / 8u;
+const uint2 coarseSize = uint2(2u * eyeCoarseWidth, (Height + 7u) / 8u);
+const uint eye = q.x / EyeWidth;
+const float coarseX = float(eye * eyeCoarseWidth) +
+    clamp((float(q.x % EyeWidth) + 0.5) * 0.125, 0.5, float(eyeCoarseWidth) - 0.5);
+const float coarseY = clamp((float(q.y) + 0.5) * 0.125, 0.5, float(coarseSize.y) - 0.5);
+const float2 coarseUV = float2(coarseX, coarseY) / float2(coarseSize);
```
```diff
 // INT:310 and INT:488 (both)
-const auto coarseWidth = std::max(1u, (desc.Width + 7u) / 8u);
+const auto coarseWidth = std::max(1u, 2u * (((desc.Width / 2u) + 7u) / 8u));
 // INT:488 uses `width`: same expression with width
 // INT:1249 and the "eyeWidth%8" reason: delete eyeWidthAligned
```
With `EyeWidth % 8 == 0` this is bit-identical for the horizontal axis, and exact `1/8` for vertical, so aligned heights (2432, 2688) are unchanged; for 2428 only the sub-texel registration changes. `TemporalReuseCS.hlsl:89-115` and `Reproject` use a self-consistent stretch mapping (snapshot and sample both stretch), so no change is needed there. Update `PeripheryGuidedFilterCS.hlsl:47-56,252-256` only if it is ever re-enabled.

### 7.7 Patch F: luma/chroma gains (P2-3)

```diff
 // PeripheryHybridMath.hlsli
+float3 PeripheryLowBand(float3 c, float gL, float gC)
+{
+    const float L = dot(c, float3(0.2126, 0.7152, 0.0722));
+    return L * clamp(gL, 0.0, 1.5) + (c - L) * clamp(gC, 0.0, 1.5);
+}
 return raw + PeripheryLowBand(coarsePeriphery, gainL, gainC) +
        w * (effC - coarseCentre) + (1.0 - w) * (effP - coarsePeriphery);
```
Outside the crop `out = raw + effP - cP + G(cP)`, so desaturation can be dialled with gC without touching luma. C++: add `gainChroma` to `PeripheryHybridConstants` (INT:131-138, 48 -> 64 B), settings key `neuralRenderingPeripheryChromaGain` (default = luma gain), UI slider beside `nr_periphery_gain` (`FoveatedRender.cpp:1511`).

---------------------------------------------------------------------------------------------------

## 8. Verification plan

Without the game:
1. Add `fmt=` to the activation log (7.1 A1) and read it from the next session's log. If it prints 28, Finding A is confirmed.
2. CPU fixture: extend `test_periphery_nr_source_contract.py:207,279` with H = 2428 and a ramp field; assert the new mapping gives zero low-frequency leak (< 1e-6) and the old mapping gives the predicted drift (0.46 texel at the bottom).
3. Shader compile of `PeripheryHybridCS.hlsl` at all entry points (existing runtime-include check at 2428/2432 plus 2243-wide to prove patch E).
4. Offline simulation of the UNORM clamp: apply `max(.,0)` and 1/255 quantisation to `cP, cC` on captured frames from `<DATA_ROOT>\OpenNR_Captures` (input/teacher pairs exist) and measure the centre-vs-periphery low-band step on blue-sky ROI with and without the clamp.

In game (requires an in-headset session):
5. Hybrid on/off at 2240x2428, sky ROI chroma against the vanilla frame, with and without patch A.
6. Stagger flicker: per-frame L/R periphery band mean, alternating-sign (Nyquist) component.
7. Cost: detailed overlay rows for `PeripheryInstance::*`, `CentreInstance::*`, `HybridInputSnapshotCopies`, `PeripheryHybridDispatchChain` before and after C1-C4.

Not verified in this audit: any runtime behaviour, any GPU time, the kTOTAL format on the current build (only the older capture), whether kTOTAL has a UAV, and NGX-side behaviour on resize.

---------------------------------------------------------------------------------------------------

## 9. Suggested order of work

1. A1 (format log) + A2 (R16F) + A3 (guards) + test. Highest value, small, and it changes what the hybrid actually shows.
2. B1-B3 (backoff, visible refusals, no periphery reset on crop move) + gate `coverage100`.
3. E (mapping + width gate removal) together with D (rounded-rect + border exemption) - both touch the same shader lines.
4. C1-C4 (copy and load savings), measure, then decide on C5/C6.
5. P2-3/5.4 (luma/chroma gain, auto-calibration), P2-1 (33% tier) after a screenshot A/B of the feather band.
6. Docs: refresh the stale line references in section 4.0; remove dead guided-filter path or fix its UV when re-qualifying.
