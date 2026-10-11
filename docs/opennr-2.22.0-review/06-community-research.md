# DLSS-NR / open-shaders community research (as of 2026-10-09)

Method: `gh api` (read-only) against GitHub plus WebSearch. Nexus Mods and schaken-mods pages returned HTTP 403, so Nexus-only content is **unverified** (search-snippet level only). Everything marked "READ" was read in full or in meaningful part (commit message, PR body, diff file list, docs). Dates are UTC from GitHub.

## 0. Repo identification (corrections to the brief)

| Item | Finding |
|---|---|
| "open-shaders" | `github.com/alandtse/open-shaders`, fork of `community-shaders/skyrim-community-shaders` (renamed org; `doodlum/skyrim-community-shaders` is the older name, last push 2026-08-28). Default branch `dev`. Latest release **v2.18.0 (2026-10-08)**; pre-release builds per PR (`v2.18.0-pr890`, `v2.17.0-pr889`...). License: **GPL-3.0-or-later with Modding + Linking exception** (README "License"; GitHub API mislabels as LGPL because of COPYING.LESSER). Compatible with OpenNR GPL-3.0-or-later. |
| Native NR in open-shaders | Merged 2026-09-28: PR #723 (kevdevrev) `386bb211`. Doc: `docs/development/neural-rendering.md` on `dev` (649 lines, READ in full). |
| `DoNotHeat/openshaders-nr` | Exists, created 2026-10-07, branch `feature/dlssnr-vr`, GPL-3.0. A downstream fork with its own DLSSNR VR implementation (character-mask erosion, binocular alignment offsets, eye tracking port plan). Last push 2026-10-07. Merged v2.17.0 and **removed upstream NR**. |
| `maanHimself/OpenDLSS-NR` | Real, MIT, 861 stars. Last code push 2026-09-21 (4 commits); issues 3-5 closed 2026-10-04. Vulkan re-implementation of the 71-block NR network. |
| `andrewmd5/bgfx-dlss5-nr` | Real, **no license file**. Borderless-Gaming effect port (WinML/TensorRT-RTX EP). Last push 2026-10-08. |
| "CSX" | `ParticleTroned/skyrim-community-shaders` (Community Shaders Expanded, branch `main-VR`, COPYING + COPYING.LESSER). Has its own NR since 2026-10-06 (commit `0564caff` "feat(nr): add neural rendering (#110)"). |
| OptiScaler NR forks | `Dagherbou/OptiScaler_DLSSNR` (GPL-3.0, D3D12/Vulkan, last push 2026-09-04, many open PRs), `tig3rmast3r/OptiScaler_DLSSNR_VR` (GPL-3.0, issues disabled, last push 2026-09-26), `GrimsVerk/OptiScaler_NR_then_SR` (GPL-3.0). `edgarbatjr/...THERMOTRON-multipass` no longer resolves (deleted/renamed). |

Ground truth for recency: alandtse/open-shaders has 339 commits on `dev` since 2026-09-01.

---

## 1. Findings: open-shaders (alandtse) NR work (most valuable)

Every entry: source / date / what / relevance to a VR NR port / portability (license) / confidence. All alandtse/open-shaders items are GPL-3.0-or-later (+exception) => **portable**. "OS" = open-shaders.

### 1.1 Cost reductions that are quality-neutral or near-neutral (OPEN PRs by alandtse, Claude-authored, measured with Tracy on SkyrimVR 1.4.15, null driver, Dragonsreach)

**F1. Parallel VR eye queues (PR #890, open, 2026-10-08)** https://github.com/alandtse/open-shaders/pull/890 (pre-release build `v2.18.0-pr890`)
- Two NGX instances (one per eye) were recorded into one command list on one queue. Model's deep levels launch few thread blocks so each eye leaves the GPU idle. Fix: `D3D12Interop::ParallelList()` records eye 1 on a second **compute** queue (a second *direct* queue did NOT help: 7.05 ms vs 6.10 ms single), waits on the same D3D11 input fence, main queue waits on the second before the completion signal. Falls back to main list if queue creation fails.
- Measured `NREvaluate` p50 (both eyes): full frame 6.10 -> 4.89 ms; fovea-clip 5.38 -> 3.41 ms (-2.0 ms = 18% of 11.1 ms). SSIM vs single queue 0.973/0.945 against 0.973-0.985 noise floor (i.e. same image). Frame time unchanged in their setup (not GPU-bound). Not tested on a real HMD / 90 fps pacing / frame gen.
- Relevance: **High**. Quality-neutral. Directly applicable to OpenNR's D3D11/D3D12 interop (OpenNR already has the shared-fence architecture). Port effort Low-Med. Confidence High on mechanism (READ), Medium on gain on your GPU (RTX 50 SM120; their GPU not stated in the PR).

**F2. Side-by-side (SBS) single-instance VR NR (PR #889, open, 2026-10-08)** https://github.com/alandtse/open-shaders/pull/889 (stacked on #880)
- `sbsEvaluate` copies both eyes' proxy/depth/motion (each eye's crop when a fovea/actor crop is active, e.g. 1432x832 total for shipped foveated config) into one wide image, runs ONE Feature 18 instance (third runtime slot), splits output back. Pays the model's large fixed cost once.
- Measured p50 `NREvaluate`: separate eyes fovea-clip 5.37; SBS whole eyes 4.00 (-1.37); SBS crops 3.07 (-2.30 ms = 21% of budget). Full frame no-clip: separate 6.13, SBS 4.02, SBS at 0.5 model scale 2.81.
- Quality: mean abs log-gain error vs full-size per-eye model 0.08-0.11 stop (model's own mean effect 0.27-0.29 stop); held-pose SSIM 0.982-0.985 vs 0.982 noise floor. They note the model is position dependent: identical inputs in both halves differ by 0.137 stop. **Not exactly quality-neutral** (small, smooth position-dependent gain error), and shared temporal history between eyes is a risk (OpenNR invariant: "eyes processed separately to avoid shared temporal state"; here history is per-SBS-instance, so cross-eye history is mixed - verify).
- Also adds `modelScale` (0.25-1.0): run model at fraction of eye size and carry only luminance gain back via guided upsample; on separate eyes it saves little (-8%/-16%) because per-instance cost does not shrink with image size. Dev-only, default off.
- Relevance: **High** (biggest single reduction), but conflicts with the OpenNR per-eye isolation invariant; treat as an A/B experiment gated behind a flag. Confidence High on the numbers as reported (READ), Medium on transfer to your teacher pipeline.

**F3. VR eye-stagger gain reuse (PR #880, open, 2026-10-06)** https://github.com/alandtse/open-shaders/pull/880
- Run model on one eye per frame (even/odd). Other eye reuses its last per-pixel **log gain** reprojected by motion vectors (composite only applies the model's luminance ratio to the untouched HDR frame). Reject reused sample if off-screen, linear depth differs >5%, or input luma differs >0.5 stop. `skipFrameGapMode` 0 = tell model about 2-frame gap, 1 = reset history. Stagger disabled when frame gen active; both eyes evaluate on any history reset/crop change/material change.
- Measured: `NeuralRendering` 5.32 -> 3.16 ms, NREvaluate 5.18 -> 2.98 (-2.16 ms, 19% budget). Reuse error 0.09-0.10 stop static/moving (model delta 0.27-0.29), ~19-25% of frames exceed 0.1 stop. Later note in #889: at **walking** speed reuse error is 0.26 stop, i.e. removes little. Notify-gap about 10% better than reset.
- Relevance: Medium. Not quality neutral (cadence/temporal reuse); only suits near-static viewing. Confidence High (READ).

**F4. NR clipped to foveated region (PR #849 merged 2026-10-04; shared helpers #848/#850)** https://github.com/alandtse/open-shaders/pull/849 , commits `a93969a3`, `5fbbe5c9`
- On VR with foveation active, NR evaluates only the foveated region (intersected with tracked-actor crop if set); periphery keeps pre-NR content (which the foveated route replaces with a stretched view anyway). `NR::FoveaClip::BuildClip` pads by 32 px feather and aligns to a **64 px grid**; Color/Output subrects set, composite fades to pre-NR content with smoothstep. Skipped in main/loading menus and while calibrating.
- Relevance: **High**. OpenNR already has a periphery/hybrid gate (and a known silent-off bug: height%8 on 2428 per your memory), so compare 64 px alignment, stabiliser and feather. Quality-neutral inside the fovea, periphery unchanged by design. Confidence High (READ doc + PR).

**F5. Crop stabiliser + reset policy (PR #831 merged 2026-10-02, `0d169a2a`)** https://github.com/alandtse/open-shaders/pull/831
- `Util::Region::RegionStabilizer`: hold crop for N frames after candidate inactive, keep unchanged while candidate inside, grow only by union, shrink only after a whole window's envelope <= fraction of held area. A crop change larger than one 64 px step adds `RegionChanged` reset reason. Prevents a crop that jitters one grid step every frame from resetting NR temporal history every frame. Reset reasons are counted (`request, first, gap, position, direction, projection, creation, region`) plus `neuralResetDrainMs` (CPU time blocked draining the GPU for resets).
- Also: tracked-actor crop (actor scoring by on-screen coverage with Gaussian centrality, incumbent bonus, skeleton-joint bound box), `Calibrate Crop Cost` (cost sweep; "NR's timing flips between a fast and a slow state, so use low-percentile not median").
- Relevance: High for the stabiliser/reset-drain diagnostics (Med for actor crop; in VR the player is not a candidate and crop only follows actors). Confidence High (READ).

**F6. Reverse-Z depth correctness for NR (PR #875 merged 2026-10-06 `fc63efde`; PR #818 ReverseZ merged 2026-10-03)** https://github.com/alandtse/open-shaders/pull/875
- `DLSSNR.DepthInverted` follows the Reverse Z feature (restart-gated). Depth guide is the raw device depth in R32F, not linearised (inverted, 1 = near, when ReverseZ active). Relevance: Medium (only matters if you ever adopt ReverseZ; but confirms DepthInverted semantics). Confidence High.

**F7. Streamline camera reprojection fix (PR #756 `7f4672b9`, 2026-09-24)** ALREADY PORTED per OpenNR 2.17 review. Skip.

**F8. Right-eye / foveated depth bug fixes (merged 2026-09-26)** PR #779 `72518c26` + PR #769 `aeba8461` https://github.com/alandtse/open-shaders/pull/779
- Root cause: `CopySubresourceRegion` with a box out of a depth-stencil source is invalid in D3D11 (pSrcBox must be NULL), so eye-1 (and foveated per-eye) depth was **uninitialised** for DLSS. Fix: use the existing `EncodeTexturesCS` `DEPTH_OUTPUT` typed R32F output / a new `DepthToPerEyeCS` compute conversion. 
- Relevance: **High as an audit item** - check that OpenNR's right-eye depth guide never uses a boxed `CopySubresourceRegion` from kMAIN depth-stencil (README line "right-eye depth" was in your brief). OpenNR uses EncodeTexturesCS already per the doc, but verify the hybrid/periphery path. Confidence High (READ).

**F9. DLSS VRAM-budget warning treated as success (PR #770 `3a1976b3`, 2026-09-26)** https://github.com/alandtse/open-shaders/pull/770
- `slEvaluateFeature` returns `eWarnOutOfVRAM` after a SUCCESSFUL evaluate; treating it as failure made the fallback evaluate DLSS a second time on the same viewports (double cost exactly when memory is tight, common in VR at high render scale). Relevance: Med (OpenNR already has "Streamline VRAM-warning handling" in PR #606 per its body). Confirm no double evaluate. Confidence High.

**F10. Foveated route fixes (merged 2026-09-26)**: periphery history reseed on route resume (#791 `061cda2d`), run route in pause menus (#790), frozen VR menu with foveated DLSS (#783 - sharpening path selected with `IsRouteActive()` instead of "route handled this frame"), oval edge mask (#778), PerfMode sharpen (#784), RCAS exposure normalisation (#780). Relevance: Med (hybrid seam behaviour; #791 and the oval mask are directly relevant to your "NR-only oval seam + edge colour" finding in the periphery-gate review). Confidence High.

**F11. HMD size drift flag (#710, 2026-09-18), foveated SSR per-eye consistency (#859, 2026-10-05)**. Low-Med.

**F12. NR dialogue profile (PR #870 merged 2026-10-06)** + **NR by material strength (PR #866 merged 2026-10-06)** + category/material fixes (PRs #881, #885, #886 open) https://github.com/alandtse/open-shaders/pull/866
- Per-material application via Feature 18 `DLSSNR.UIAlpha` (graded protection; alpha 0 = full NR, 1 = restore from `DLSSNR.Backbuffer`). Important details: `Backbuffer` must be the NR input texture itself (the bounded proxy domain), a raw scene copy "darkens the frame"; do NOT bind ControlMask and UIAlpha together ("can collapse the network contribution"); UIAlpha binding is **latched at feature creation** so toggling rebuilds features. Cost 0.015-0.042 ms/eye at softness 0-2, 0.125 at 4. Explicitly "a quality feature, not a performance one".
- "Only in dialogue" mode suspends NR and keeps NGX features alive (no rebuild), with one history reset on resume: a cheap big saving if the user wants NR just in conversations.
- Relevance: Med as a feature; **High as a gotcha list** (UIAlpha/Backbuffer/ControlMask contract). Confidence High.

**F13. Color / HDR contract and runtime hashing (PR #723 doc)**: bounded display-referred proxy (exposure, soft knee, peak bound, sRGB) into Feature 18; NR output decoded in the same proxy space; a **luminance ratio** with near-black floor and two-sided highlight bounds applied to untouched scene-linear RGB (never inverse tonemapped); scene white point from GPU adaptation value, no CPU readback. Channel order test: swapping `.rgb`->`.bgr` produced a full-frame blue cast, so output is RGBA for 310.8 builds; accept only validated SHA-256s (`8270B350...CC206` DVS Production, `E16BCF15...C8E` 310.8.0.0 test build). Parameter-contract details (creation flags 0x61, `DLSSNR.Upscaling=0`, `Scale/ScalingRatio=1`, `Preset=0`, UICorrection=1, AutoExposure=1, Hdr=1, Sdr=0, `MVecScaleX/Y` = NR input dims). Frame time capped 250 ms; jitter beyond 1 px read as 0. Relevance: Med (verification checklist for OpenNR's teacher/color path). Confidence High.

**F14. Flicker diagnostic suite (PR #723 doc)**: 8 tests x 600 frames (baseline, inferred cuts, ignore position, reset every frame, zero MVs, zero jitter, serialized GPU, bypass writeback); reset thresholds distance >256, direction dot <0.5, projection change >0.1 are **diagnostic only**; production history is only reset on first frame, gap, loading, resource change, crop change. Relevance: Med (compare OpenNR's reset thresholds; note OS deliberately does *not* reset on inferred cuts in production). Confidence High.

### 1.2 Skylighting / dark interiors (relevant to the confirmed "dungeon black = Skylighting" finding)

I found **no upstream or OS GitHub issue titled about black dungeons/interiors** (searched "interior dark", "dungeon", "skylighting", "black interior" in both repos; only unrelated hits). So the link to your bug is inferred from the fix content below, not stated by any reporter - **unverified as the same bug**.

**S1. "Seed lit history" + reset fixes** - OS PR #694 (ParticleTroned, merged 2026-09-30 `9a810a9c`) https://github.com/alandtse/open-shaders/pull/694 ; upstream port PR #2806 (merged 2026-10-01) https://github.com/community-shaders/skyrim-community-shaders/pull/2806 (also ports OS #355).
- Root cause stated: `ResetSkylighting` (startup + every loading screen) seeded each probe's 32-bit shadow-history bitmask with **0** while visibility was cleared to 1.0, so first update dropped visibility to <= 1/32 and "skylighting faded in from black over the next 32 frames"; `texProbeArray` was not reset (stale SH after fast travel); `ClearUnorderedAccessViewUint` was passed a 1-element array but reads 4 values (OOB read). Fix: `UpdateProbesCS.hlsl` seeds all-lit (`0xFFFFFFFF`) for invalid probes; reset clears probe SH to unit SH `(sqrt(4pi),0,0,0)` and bitmask to all-lit. Skylighting ini 1-5-2 -> 1-5-3. 
- Relevance: **High** (black-after-load/teleport signature matches). Confidence: fix content High (READ), match to your dungeon bug Medium.

**S2. Interior occlusion probes (OS PR #596, kevdevrev, merged 2026-09-06)** https://github.com/alandtse/open-shaders/pull/596 - added interior-only precipitation occlusion mask, `OCCLUSION_ONLY` probe permutation, rebuild on interior/exterior transitions; "Runtime testing remains for VR interiors". This is the most plausible *introducer* of interior skylighting state problems in OS builds >= 2.12ish. (Inference.)

**S3. Probe readiness gating + history tracking + projection validation + shadow-input guard** - OS #699 (gate readiness, merged 2026-10-05), #697 (R16_UINT temporal state, shift-register history, merged 2026-10-05), #695 (reject invalid cascade/near-plane projections; upstream port #2842 merged 2026-10-09), #696 (guard missing shadow inputs: "cannot leave dark history behind"; clears all history on shadow-input recovery), #698 (include roof editor-marker meshes; upstream #2807), #700 (clamp zenith; upstream #2805), upstream #2844 (max-zenith radius `sin(Z)*sqrt(u)`, fade-out grid offset `PosOffset`, merged 2026-10-06). All GPL, portable. **High** relevance to Skylighting black/dark issues; 2.18.0 (2026-10-08) contains #694-#705, #826.
- Caveat: OS #696 explicitly says missing directional-shadow inputs "must not create persistent dark probe histories" - i.e. dark persistent histories are a known failure class.

**S4. Skylighting VR cost knobs (OS #701-#705, #826, merged 2026-10-05)** https://github.com/alandtse/open-shaders/pull/704 : runtime toggle, grid size (128x128x64 / 192x192x96 / 256x256x128), Z-slice incremental updates, capture/probe update cadence 1..32 frames, field width. Measured Riverwood VR GPU: probe-update-only every 2 frames -0.164 ms (-49.9%), every 4 -0.243 ms (-74%), every 8 -0.289 ms; capture+probe every 4 frames: probes -0.236 ms, capture -0.787 ms (-72%), every 8 capture -0.964 ms. Movement and reset warmup bypass the throttle. Defaults unchanged. Relevance: Med-High (not NR itself, but frees ~1 ms of frame budget in VR; quality impact: slower light updates). Confidence High on numbers (READ, n=2 baselines).
- Upstream perf: skip occluders below probe grid, `perf(skylighting)` PR #2767 (merged 2026-09-27) from InTheBottle's fork.

### 1.3 Other VR / perf fixes in OS or upstream worth auditing (not NR)

| # | Item | Source / date | Relevance | Notes |
|---|---|---|---|---|
| P1 | **Driver-profile DWORD `0x00313536 = 0x00001000` stops NVIDIA D3D11 driver shader re-specialisation** (city-entry CPU bursts 140-220 CPU-s -> ~4 CPU-s; 2,300-2,600 new driver cache records/session -> 0; no GPU cost). Alternative `0x00A4F311=0xA2B53761` costs ~+3 ms GPU. | OS issue #877, 2026-10-06 https://github.com/alandtse/open-shaders/issues/877 | **High** for VR stutter (RTX 5090 + driver 617.14 + Skyrim VR 1.4.15, Open Shaders 2.12/2.17). Applied via NvAPI DRS to the SkyrimVR.exe profile. **Undocumented ID, one reporter, single GPU/driver** -> test on your machine, treat as unverified. Reporter's shader-side alternative (opaque FeatureData index) untested in game. |
| P2 | Hard-lock regression with Shadow Caster Manager (SCM) hooks | OS issue #837, 2026-10-01 (open) - thread dump in `ShadowCasterManager::s_lightsPoolMutex` under TerrainBlending + EngineFixes hooks; 2.16/2.17; not on 2.15 | Med (stability); workaround: disable Shadow Casters in Light Limit Fix. Single reporter, hypothesis. |
| P3 | `#838` SCM/ISL shadow blink: anchor light radius for shadow decisions | OS PR #838 (alandtse), open since 2026-10-02 | Med. Root cause: SKSE-ISL rewrites LIGH radius from live fade each frame; SCM consumed raw radius so `PlayerWithinLightRadius`, sleep-skip eligibility and score geometry flipped each frame (player body flicker, shadow map blink). Fix: read the EMA-anchored radius (`AdvanceRadiusAnchor` once/frame + read-only `AnchoredRadius`). Known limitation: split-cache retention for ISL-flickering lights not recovered. Confidence High (READ). Companion SCM change: default redraw budget 7 ms (#843, merged 2026-10-03) and skip back-side caster (#839). |
| P4 | Hang fixes: `NormalizeDegrees` fmod (infinite loop for huge floats) and `FrameLimiter` swap-chain latency wait bounded to 1 s instead of INFINITE | OS #845 `0683d86e` (2026-10-04) / upstream #2826 (2026-10-04) | Med (startup/hang class). Port trivially. |
| P5 | `fix(hooks)`: abort/report failed detour attaches, vtable fallback when Detours fails | upstream #2846 (2026-10-09), #2648 (2026-09-04) | Low-Med. |
| P6 | Shader compile caching: persistent shader store (OS #835 / upstream #2840), share identical compiles (#847/#2836), disk-cache utility shader compiles (upstream #2848, 2026-10-09), merge lighting permutations (#846, perf), stop retrying failed compiles (#790) | Med for startup time. License GPL. |
| P7 | VR grass: Grass Optimizations VR support (OS #630 2026-09-11, VR placement #817, instance culling #648); "detect Dll version causing grass stutter" (#2716, 2026-09-14); anisotropic mismatch causing pixel discards in 1.7.99+ (upstream #2860, 2026-10-08) | Med-High if OpenNR ships grass opt; not NR. |
| P8 | Map / world map: `fix(linear lighting): skip the flat world map` (upstream #2763, 2026-09-27), `fix(height-fog): keep pre-map scene on the map` (#2788), `disable height fog in the world map` (OS #788), `keep vanilla fog on the map` (#2765), `advance rain clock...` | Low-Med; relevant to "map rendering in VR" (fog/lighting bleed). |
| P9 | `fix(vr)`: classify stereo modes from depth history (#612), reclassify from final depth (#609), blend stereo reprojection samples (#599), restore prepass-skipped geometry in Eye 1 (#608), size Eye 1 repair viewports (#595), stop DepthFill wiping culled Eye 1 depth (#594), isolate stereo fog/SSR samples (#593), obsolete depth culling cache clear (#830), dynamic near clip (#615, disabled by default #797) | Med: right-eye depth/VR stereo-share correctness. 2026-09-05..09-30. |
| P10 | Open issues in OS: #99 SSGI over-brightening on fast VR head movement (temporal history over-retention); #143 foveated stereo reprojection; #187 hardware VRS (NVAPI AAVRSController) port from ParticleTroned fork; #496 DLSS-FG flicker at end of load; #573 menu break in VR; #317 HUD menus cropped under DLSS/FSR | Low-Med, tracking only. |

---

## 2. Other community NR implementations: findings

**C1. CSX NR (ParticleTroned) - `0564caff` 2026-10-06, `4dcdff11` / `552eaed3` / `45a59395` 2026-10-07** https://github.com/ParticleTroned/skyrim-community-shaders
- Features seen in commit messages (READ): single enclosing actor ROI per eye across "all three rendering routes", actor categories, material strengths (armour/weapon), FOV-aligned focus, "full-resolution NR gets dedicated depth and motion guides" (**FSR foveation and full-resolution NR reused guide allocations of different sizes, forcing backend resets** - fixed), transparent FOV coverage preview, NR activation rejected against incompatible pending profiles. 
- Relevance: Med. Reveals a real bug class: sharing guide allocations of different sizes between routes forces NGX/backend resets. License COPYING+COPYING.LESSER on a GPL-3 base: treat as GPL-3; confirm per file. No perf numbers. No tests on HMD ("no new in-game or physical-HMD validation").

**C2. DoNotHeat/openshaders-nr (2026-10-03..07)** https://github.com/DoNotHeat/openshaders-nr
- `f0aa1e67` (2026-10-04): neural write-back mask extended from FaceGen skin to **all character geometry** via `IsCharacter` flag, plus **mask erosion** "to kill the shimmering silhouette under camera motion"; `f79b9eaf`: erosion slider [-8,8], negative = dilation (max over cross neighbourhood, 5 taps) "so an under-covering mask never leaves edge pixels flickering"; fixes `IsHeldWeapon` bit collision with `IsCharacter` in Lighting.hlsl. `cfb3b977` (2026-10-03): **binocular alignment offsets lost on every boot** because `LoadSettings` ran before `SetStereoEnabled(true)` and the read was gated on `stereoEnabled`; now load unconditionally and re-derive offset UVs after `SyncRightUV`. `633820fd`: eye-tracking port plan `docs/EyeTrackingFoveation_PORT_PLAN.md` (+76 lines).
- Relevance: **High for the binocular-alignment persistence bug** (your stereo offsets thread: check settings load order vs stereo-enable); Med for mask erosion (an edge-shimmer mitigation for masked compositing: relevant to your oval seam / edge colour finding). GPL-3.0. Confidence High (READ commits).

**C3. xenmods/DLSSNR-Cost-Scaler (MIT; last push 2026-09-13)** https://github.com/xenmods/DLSSNR-Cost-Scaler
- Proxy on `nvngx_dlssnr.dll`: run model at 0.25-2.0x scale and composite via **matched residual** onto native frame. Claims (README, not independently verified): 0.85 = ~28% faster, 0.75 = ~40% faster, anamorphic 0.65x X / 0.85x Y ~45% workload cut, hardware bilinear downsample, LDS tile caching in RCAS (-69% global transactions), in-place resolve (saves 66-132 MB VRAM), dynamic FPS governor with discrete 5% tiers and hysteresis (1 s fast-drop, 3 s climb), "alternating frame neural execution / VRNR" (experimental). Issues: very dark in Watch Dogs (#10), ESO crashes with scale < 1.0 (#4/#2), PR #8 fixes uniform 150/200% bypassing scaling path.
- Relevance: Med - OpenNR already has adaptive 100%-70% tiers (PR #606) and 50/33% manual. OS #889 measured that reduced scale on separate eyes saves only 8-16% in VR (fixed per-instance cost), which tempers these flat-screen numbers. Quality: NOT neutral (resolution reduction). Matched residual = quality-preservation trick for reduced-scale. License MIT.

**C4. Dagherbou/OptiScaler_DLSSNR (GPL-3.0; most PRs still open)**
- PR #42 (2026-09-08) MVLowRes/subrect metadata fixes shadow flicker with SR (already reviewed in OpenNR 2024-09-24 review; OS doc cites it). Issue #29 (2026-09-05): alternating feature sizes -> model rebuilt every dispatch, VRAM runaway. Issue #47 (2026-09-14): RTX 4060 8 GB + NR + high textures hits VRAM ceiling -> hard GPU hang (nvlddmkm Event 153). Issue #5: 40-series works only with community-patched `nvngx_dlssnr.dll`; stock 310.8.0.0 (sha256 e16bcf15...) gives FAIL_PlatformError on Ada. PRs #14/#12/#1 matched-residual + GPU paper-white from exposure; #36 pre-upscale NR (flat game); #23/#26 multipass (up to 5 chained passes). Issue #50: Blackwell RTX 5060 Ti on Linux/Proton Xid 69 black screen on first composition. All Med/Low for VR.

**C5. NIGos/dlss5-bridge (MIT; ReShade add-on)** - issue #8 (2026-08-30): Unity D3D11 games pass NGX scalars inverted + **DRS-padded textures** -> `0xBAD00005 InvalidParameter` on rebuild; PR #40/#43 (merged 2026-09-10/11): keep **separate temporal histories per view**, avoid rebuilding shared textures for unequal split-screen views (keeping render/output dimensions with each private history). Issue #30 (2026-09-09) "Possible fake website with malware" - **security warning: only use GitHub sources**. Relevance: Low-Med (confirms per-view history and rebuild-avoidance). Issue #48: NR in NMS (Vulkan) flickering constant.

**C6. bmitch87/DLSS5VKLayer (AGPL-3.0, Linux Vulkan layer)** - PR #11 (merged 2026-09-11): refuse frames below 64x64 floor (1x1 probe swapchain hangs the GPU); issue #13: IPC spin-wait costs ~6.7 ms/frame, GPU utilisation ceiling; issue #21: Xid 79 on Ada when NR evaluates real frames. `extracted_pipeline_notes.md` used by OS as contract evidence. AGPL: **do not copy code**; ideas only. Relevance Low (OpenNR is Windows D3D).

**C7. kibblerz/DLSS5-Reshade-AIO (Apache-2.0)** `lab/PRIVATE-CONTRACT-FINDINGS.md` (READ): Style values 0/1/2 give three distinct outputs (3 aliases 2); `Hint.Render.Preset` 1-3 changes nothing; **NR is input-resolution only, never upscales** (runtime ScalingRatio callback accepted but still writes input-sized quadrant); `ControlMask` R channel is effective NR mask (1 = NR, 0 = bypass, overrides auto mask); `UIAlpha`/`UI` alpha = protection (0 NR active, 1 bypass), RGB of `Backbuffer` supply restored pixels, `UICorrection=0` makes all UI resources inert; `BidirectionalDistortionField` has **no observable effect** (don't integrate). Idea: inverted unreliable-motion mask as ControlMask for anti-smear. Relevance: Med (contract facts match what OS documents; ControlMask-inversion for ghosting is a candidate quality fix). Issue #8 (2026-10-02): persistent NR pass count request.

**C8. maanHimself/OpenDLSS-NR (MIT)** - bit-exact Vulkan reimplementation (needs user-supplied weights extracted from the NVIDIA runtime; 71-block Swin/ViT U-net, FP8 E4M3 w/ FP16 accumulate, 141 MiB weights). Bench RTX 4070 SUPER: 768^2 2.8 ms; 1080p 7.8 ms; 1440p 12.6 ms; 4K 29.3 ms. Switches: PTX `mma.sync` E4M3, cp.async rings, **barrier-free chaining via device counters**, split-K, streamed global attention, fusions. Issues #3-#5 (closed 2026-10-04): fused block-70 head red-biased; NaN/Inf in `*.prior` tensors. Nothing newer than 2026-09-21. Relevance: Low as a port source (obtaining weights = extraction of NVIDIA's model; different runtime; no temporal integration into Skyrim); Med as an independent timing reference: the OS doc says `NREvaluate` ~3.1 ms per eye at 1706x960, consistent with this network's cost. Forks: `bhouston/three-dlss-nr` (WebGPU), `SamG-Coder/OpenDLSS-NR-WebCuda`, `romangalaxys10-spec/OpenDLSS-NR-MetalFX` - not examined beyond metadata.

**C9. andrewmd5/bgfx-dlss5-nr** - WinML/TensorRT-RTX EP effect for Borderless Gaming. Issues #2/#3 (2026-10-08): install fails on RTX 5070 / 5070 Ti, `NvTensorRTRTXExecutionProvider` error 0x80073D3B, no DirectML fallback. **No license**, so non-portable; and the failure shows the TensorRT-RTX EP route is fragile. Low.

**C10. Nexus (unverified; fetch blocked, search snippets only)**: Open Shaders With DLSS5 Neural Rendering (VR+flat, mod 190062, experimental, "tested with driver 591.86", not for reporting to OS/CS maintainers, DLSS FG unsupported), SkyrimVRPerfKIT Custom-Region DLSS5 NR (mod 182089, v0.1.1, adds NR for RTX 40/50, patched DLL needed on 40), DynamicShaderFrameGen (190154, FSR3.1 FG + DLSS5, "ported from CS/OS GPL-3.0"), Combat DLSS5 Switcher (190991, auto-disable NR on combat, claims no hitch on 5800X3D+4090), skyrimupscaling (191636, "do not run with Community Shaders"), SkyrimSE MFG (194020). Combat-toggle is an idea already covered by OS "dialogue-only" profile (inverse) - potential cheap win: context profiles (combat suspend).

---

## 3. Bugs and fixes, mapped to the brief's categories

| Category | Finding | Source |
|---|---|---|
| Ghosting / flicker | Check MV scale: `MVecScaleX/Y` = NR input dims (the dev switch allows identity). OS tests 8 diagnostic modes; production does NOT reset on inferred camera cuts. Edge shimmer under masks: erode/dilate mask (DoNotHeat). Unreliable-motion mask could be passed as inverted ControlMask (kibblerz, untested). | OS doc, DoNotHeat f0aa1e67, kibblerz lab |
| Black frames | Skylighting history seeded 0 (see S1); `ClearUnorderedAccessViewUint` OOB array; DLSS evaluate with uninitialised right-eye depth (F8); missing shadow inputs leaving dark history (OS #696). Persistent-NaN `*.prior` tensors in OpenDLSS-NR (unrelated runtime). Cost Scaler "very dark in Watch Dogs" (#10, flat). | links above |
| MVec issues | OptiScaler #42 (MVLowRes / subrect), Cost-Scaler #8 (scale path bypass at 150/200%), NIGos #8 (inverted scalars + DRS padding). OS: motion encoded undilated RG16F to preserve correspondence with centre-pixel depth; reset frames submit zero motion. | |
| Reset thresholds | Production resets: first frame, gap, loading, resource, crop-change (`RegionChanged` > 1 alignment step), material-strength creation change; diagnostic thresholds dist >256, dot <0.5, proj delta >0.1. Stabiliser prevents per-frame crop-triggered resets. `neuralResetDrainMs` = CPU stall from GPU drain on reset: track it. | OS doc, #831 |
| Right-eye depth | F8 (invalid boxed depth-stencil copy; typed R32F via `DEPTH_OUTPUT`). | #779/#769 |
| HDR / LDR | Bounded proxy + luminance ratio on untouched linear RGB; RCAS must be exposure-normalised (#780); `Backbuffer`=NR input proxy domain, not raw scene (#866 doc); upstream #2862 linearise ShadowSampling ambient w/ Linear Lighting, #2723 gate HDR compositing on HDR setting. | |
| UI exclusion | `UICorrection=1` + `UIAlpha`/`Backbuffer` path; OS keeps NR before UI composition so UI is never passed through Feature 18; foveated-DLSS frozen-menu bug (#783). | |
| Quest / Pimax alignment | Nothing specific found for Quest or Pimax. Related: DoNotHeat binocular alignment offsets persistence bug; OS "flag live HMD size drift" (#710); Nasal Convergence 60/70 presets (#775). | |
| Startup hangs | Bound `FrameLimiter` wait (1 s), fmod angle wrap (OS #845 / upstream #2826); stop retrying failed shader compiles (#790, #781); detour-attach abort (#2846). | |
| Memory / VRAM | OptiScaler #29/#47 (rebuild on alternating sizes; 8 GB hang); Cost-Scaler in-place resolve saves 66-132 MB; OS #770 VRAM warning semantics; Skylighting grid sizes 128/192/256. | |

## 4. SDK / driver / runtime changes

- NVIDIA DLSS SDK **310.9.1** (2026-09-08): "Added DLSS Ray Reconstruction Transformer Mode (Preset F); bug fixes & stability" (GitHub release body, READ). No mention of NR / Feature 18 (NR runtime is not in the public SDK).
- Streamline SDK **2.14.1** (2026-09-08): V-Sync + frame-rate-limiter support in Dynamic mode for DLSS MFG; `VK_NV_low_latency2`. OS upgraded to Streamline 2.14.1 on 2026-09-13 (`d87f7863`). No NR changes.
- `nvngx_dlssnr.dll` 310.8.x: not an official public redistribution (per search results: leaked from NBA 2K27 early-access build; community mirrors; one mirror has an Authenticode signature that does not match file contents - NODIX-TECH/DLSS-5-MANAGER#58). **Unverified** beyond search snippet. OS validates exactly two SHA-256s (listed in F13). Stock build contains only Blackwell (sm_120) binaries; Ada/Ampere need community-patched builds (OptiScaler#5).
- Driver notes (community, unverified): one project says drivers 32.0.16.1664 and 1686 route NR into `nvngx_dlssnr.dll` itself and a tested 310.8.0.0 faulted inside D3D12 with them (search snippet only; I could not locate the primary source). OS #877 reporter: driver 617.14 (RTX 5090). OptiScaler#47 reporter: ~610.88.
- Official NVIDIA release notes for any October 2026 NR driver: **not found**.

---

## 5. Port candidates (ranked, top 15)

Scoring = expected benefit to OpenNR VR x confidence x ease. License for all OS items is GPL-3.0-or-later (compatible).

| # | Candidate | Why | Quality-neutral? | Effort | Source |
|---|---|---|---|---|---|
| 1 | **Parallel eye queue (compute queue for eye 1)** | -1.2 to -2.0 ms GPU for NR, same image; mechanism simple (fence wait, second compute queue; direct queue does not help) | Yes (SSIM within noise) | Low-Med | OS #890 |
| 2 | **Skylighting reset/seed fixes** (seed lit bitmask, reset probe SH to unit SH, 4-element UAV clears, validate projections, gate readiness, guard missing shadow inputs, roof editor markers) | Directly targets the confirmed dungeon-black cause; 2.18.0 already contains it | Yes (fix) | Low (cherry-pick `9a810a9c`, `127b7ebd`, `4d17ce0c`, `bc25f2af`, `f8b8b86b`, `d6bfd6b7`) | OS #694-#699; upstream #2806/#2807/#2842 |
| 3 | **Binocular-offset load-order fix** (load StereoOffset unconditionally; re-derive after SyncRightUV) | Same bug class as your stereo-offset persistence; 15-line fix | Yes | Very low | DoNotHeat `cfb3b977` |
| 4 | **Right-eye depth audit/fix** (no boxed CopySubresourceRegion from depth-stencil; typed R32F) | Possible uninitialised eye-1 depth in any path that still copies | Yes | Low | OS #779/#769 |
| 5 | **SBS single-instance eval (experiment flag)** | -1.4 to -2.3 ms; but breaks per-eye isolation; start behind a dev flag and A/B for cross-eye history bleed | Near (0.08-0.11 stop mean error) | Med-High | OS #889 |
| 6 | **Crop stabiliser + reset-reason counters + `neuralResetDrainMs`** | Prevents history resets from crop jitter; adds diagnosability for periphery gate | Yes | Med | OS #831 `Region.h` |
| 7 | **Foveated clip alignment/feather details** (64 px grid, 32 px feather, skip in menus, periphery history reseed on resume #791, oval edge mask #778) | Compare with OpenNR hybrid gate; resolves "oval seam" issues | Yes | Low-Med | OS #849, #791, #778 |
| 8 | **Skylighting cadence/grid options (probe/capture interval 2-4, Z slices)** | -0.24 ms probes, -0.79 ms capture in VR at interval 4; settings only | Slight (slower light updates) | Low (config/port #701-#705) | OS #704 |
| 9 | **Driver-profile DWORD 0x00313536=0x1000 for SkyrimVR.exe** (test only) | Eliminates city-entry multi-second CPU bursts; no measured GPU cost | Yes | Very low (NvAPI DRS) but unverified/undocumented | OS issue #877 |
| 10 | **Context profiles: dialogue-only / suspend-without-rebuild** | Large average saving if user accepts NR only in dialogue; features kept alive; one reset on resume | No (by design) | Low-Med | OS #870 |
| 11 | **VRAM-warning eWarnOutOfVRAM = success** audit; avoid double evaluate | Prevents silent 2x cost under VRAM pressure | Yes | Very low | OS #770 |
| 12 | **Hang fixes**: bounded FrameLimiter wait (1 s) + fmod angle normalisation | Startup/loading hangs | Yes | Very low | OS #845 / upstream #2826 |
| 13 | **Mask erosion/dilation for masked composite edges** | Kills silhouette shimmer in masked NR; 5 taps | Yes (edge only) | Low | DoNotHeat `f79b9eaf` |
| 14 | **By-material `UIAlpha` protection (with its contract rules)** | Quality control; protects non-character areas; 0.04 ms | No (feature) | Med | OS #866 |
| 15 | **Shader compile caching (dedupe, persistent store, utility-shader disk cache)** | Startup time, not frame time | Yes | Med | OS #835/#847, upstream #2840/#2848 |

Near-misses: eye-stagger gain reuse (#880) - only valid when nearly static; camera-reprojection fix #756 - already ported; scale-down model tricks (modelScale) - small gain on VR.

## 6. Things to avoid / known not to work

1. **A second direct queue for parallel eyes** - slower (7.05 ms vs 6.10). Use a compute queue (OS #890).
2. **Reduced model scale on separate eyes in VR** - only -8%/-16% because per-instance fixed cost dominates (OS #889). Scaling helps flat; not VR unless combined with SBS.
3. **Eye-stagger reuse during walking** - 0.26 stop error vs 0.27-0.29 stop model effect, i.e. it removes the benefit (OS #889 note); also disabled with frame gen.
4. **Binding ControlMask together with UIAlpha** - "can collapse the network contribution" (OS #866 doc). `Backbuffer` must be the NR proxy input, not a raw scene copy.
5. **Swapping output to BGR** for 310.8 - disproved (full-frame blue cast). Keep RGBA.
6. **`DLSSNR.BidirectionalDistortionField`**, **`Hint.Render.Preset`**, runtime ScalingRatio callback - no observable effect on output (kibblerz lab); NR never upscales. Don't spend time on them.
7. **Forced Hard-lock/crash hotspots**: SCM + EngineFixes + TerrainBlending shared-mutex lock (OS #837, unconfirmed); disable SCM if seen.
8. **Using inferred camera-cut resets in production** - OS deliberately keeps them diagnostic only (thresholds dist>256, dot<0.5, proj>0.1).
9. **Pre-upscale NR copies from OptiScaler (flat games)** - jitter / dynamic resolution / colour-transfer issues unresolved (OptiScaler #36); OS places NR at render-res before SR in VR with its own guides.
10. **Anamorphic/Cost-Scaler tricks** - README claims unverified, MIT but flat-game specific; crash reports at scale < 1.0 in ESO.
11. **bgfx-dlss5-nr / TensorRT-RTX EP** - fragile install on RTX 50-series (0x80073D3B), no license.
12. **DLSS5VKLayer code** - AGPL-3.0, Linux Vulkan only; ideas only.
13. **Untrusted binary mirrors of `nvngx_dlssnr.dll` and "DLSS 5 Manager"-style sites** - NIGos issue #30 flags a fake site/malware, DLSS-5-MANAGER#58 reports signature mismatch; only validate by the two SHA-256s in the OS doc.
14. **Reading `eWarnOutOfVRAM` as failure** (double-evaluate).
15. **Shadow-input-missing paths that leave dark probe history** (OS #696) - what to avoid in any Skylighting backport.

## 7. Caveats

- All OS measurements are from one author's null-driver SteamVR setup, GPU not stated, frame-gen off; "frame time did not change" in their scenes (not GPU bound). Treat ms numbers as GPU-zone deltas, not fps.
- #880/#889/#890 and #886/#885/#881 are **open, unmerged** PRs (as of 2026-10-09 17:xx UTC) and by design dev-only/default-off; code may still change. Pre-release builds exist per PR.
- Link between S1 and your dungeon-black bug is inferred; no issue text mentions dungeons.
- Nexus pages and several search results were not readable (403); Nexus-derived statements are labelled unverified.
- I did not read OptiScaler PR #55/#57, OpenNR-adjacent forks (three-dlss-nr, WebCuda, MetalFX), or `tig3rmast3r` source (issues disabled).
