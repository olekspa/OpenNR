# OpenNR 2.21.0 — improvement backlog

Written 2026-10-08. Ranked by value to the visual problems first, then by performance and risk. Items reference
`01-architecture.md` and `02-seam-and-colour-analysis.md`.

Performance numbers marked INFERENCE are estimates from pixel counts. Only in-game measurements count
(`AGENTS.md` §4.3). Items that need a live test need your approval first (`CLAUDE.md`).

---

## Priority summary

| ID | Item | Fixes | Perf cost | Risk | Needs live test |
|---|---|---|---|---|---|
| **P0-1** | Stop the periphery gate silently refusing on height % 8 | edge colour, circles (indirectly) | +41 % NR/eye with stagger, +82 % without (INFERENCE) | medium | yes |
| **P0-2** | Log which route runs and why the hybrid was refused | diagnosis | none | none | no (logs only) |
| **P1-1** | Stop the per-frame dither flicker in the NR-only band | circles | none | low | yes (A/B) |
| **P1-2** | Replace the NR-only oval with a superellipse, or retire it once P0-1 works | circles | none | low | yes |
| **P1-3** | Allow feather width above 128 px in the NR-only band | circles | none | low | yes |
| **P1-4** | Rounded-rectangle distance in the hybrid composite | seam corners | negligible | low | yes |
| **P1-5** | Apply the chroma part of result shaping to the periphery field | colour sliders | negligible | low–med | yes |
| **P1-6** | Retire the guided-filter key (no UI, compiled out) | confusion | none | none | no |
| P2-1 | Lighting-only periphery at 33 % or 25 % model resolution | cost | saves ~50–75 % of periphery (INFERENCE) | medium | yes |
| P2-2 | 4 px coarse grid for the centre high-pass | faint 8 px pattern | small | low | yes |
| P2-3 | Separate luma and chroma lighting gains | saturation control | none | quality call | yes |
| P2-4 | Flicker metric for periphery stagger (cadence 2) | temporal colour lag | measure | low | yes |
| P2-5 | Saturation-ratio extension for NR-only, if the hybrid is rejected | edge colour | small | high (unproven) | yes |
| P3-1 | Documentation refresh (stale architecture doc, adaptive doc, shader table, naming) | maintenance | none | none | no |
| P3-2 | Commit a clean 2.21.0 (branch tip still says 2.20.10) | release hygiene | none | none | no |
| P3-3 | Expose the hybrid cost in the overlay (detailed rows are off by default) | measurement | none | none | no |
| P3-4 | Source-contract test for the gate and for odd heights | regression | none | none | no |

---

## P0-1. Stop the periphery gate refusing on height % 8

**Problem.** `Integration.cpp:1199` requires `totalDesc.Height % 8 == 0`. Your display height is 2428 (`2428 % 8 = 4`),
so the whole-eye periphery route never runs. The edge colour and the oval seam both follow from that
(`02-seam-and-colour-analysis.md` §2).

**Evidence that the height term is not structurally required.**

- The dispatch sizes height with ceiling division: `coarseHeight = (height + 7u) / 8u` and `halfHeight = (height + downsample − 1) / downsample` (`Integration.cpp`, the dispatch block around `:456-520`).
- `PeripheryHybridCS` uses `(Height + 7u) / 8u` and clamps the last block with `min(..., Height)`.
- Width does need the multiple of 8, because the coarse grid maps each eye with `EyeWidth / 8`.
- No test asserts the height rule.

**Options.**

1. **(recommended)** Drop the height term from `hybridRequested`. Keep the width term. Audit every periphery-height use (see the
   brief, step 3) and fix any that assume a multiple of 8.
2. Round the display eye height to a multiple of 8 when PerfMode latches it (`PerfMode/ResolutionHook.cpp`). This
   changes the output size by up to 7 px and touches the compositor contract. Riskier. Only if option 1 fails the audit.
3. Ask the user to set SteamVR supersampling so the recommended height is a multiple of 8 (for example 2432 or 2688).
   Zero code, but it changes the whole render and needs a test. Useful as a control experiment, not a fix.

**Expected visual result.** The oval seam and the edge colour step disappear in the default configuration, because
the whole-eye lighting field (`g·coarseP`) starts to apply.

**Cost.** INFERENCE: +41 % of the NR centre cost per eye with stagger, +82 % without (pixel-count estimate, see
`02-seam-and-colour-analysis.md` §4). Measure in game with the detailed overlay rows on (P3-3).

**Risk.** Medium. The hybrid route has never run at this height. It is fail-closed, so a failure falls back to
centre-only (`Integration.cpp:1462`), but the fallback has to be checked too.

**Verification.**
- Source test (P3-4) and shader compile at 2428 and 2432.
- A bit-exact or ≤1 LSB regression check: at heights that already pass (for example 2432), the output must not change.
- Live: hybrid on versus off, same scene, per-band metrics, colour against the vanilla frame, NR cost.

---

## P0-2. Make the route visible

**Problem.** When the hybrid is refused, nothing is logged. The coverage log (`Integration.cpp:1243`) prints either way.
That is how this regression went unnoticed.

**Change.** Log on transitions only (not per frame):
- `[DLSSNR] Periphery NR active: display=WxH coverage=C% res=R% gain=G feather=F stagger=S`
- `[DLSSNR] Periphery NR inactive: <first failing term>` (name the term, for example `height%8`).

**Cost.** None. **Risk.** None.

---

## P1-1. Stop the per-frame dither flicker

**Problem.** The NR-only blend in `SubrectBlendCS.hlsl` uses noise seeded with `frameIndex`, and your strength is 1.45.
The band changes pattern every frame (`02` §3). HYPOTHESIS: this is the main visible part of the "circles".

**Options.**
- Set `subrectBlendMode = 1` (feather). Settings change only, no code. Ask first.
- Remove the `frameIndex` term from the noise, so the pattern is fixed in screen space. Code, small.
- Keep dither but lower the strength (test values 0.5 to 1.0).

**Verification.** Static scene, same camera. Measure the temporal standard deviation of band pixels over 60 frames for each option. Save a 4-panel PNG (raw | feather | dither | amplified diff) per `AGENTS.md` §4.

**Cost.** None. **Risk.** Low. A fixed pattern can look like a fixed texture. Check that.

---

## P1-2. Replace the NR-only oval

**Problem.** The oval mask is an ellipse inscribed in the crop. It produces the closed contour that reads as
"circles".

**Options.**
- Use a superellipse distance, like `package/Shaders/Common/FoveatedMask.hlsli` (shape power 4). That is a
  squarer shape with less obvious curvature.
- Once P0-1 works, retire the oval for NR coverage (the hybrid does not use it).

**Cost.** None. **Risk.** Low. The oval is also used by foveated render, so change it only for the NR path.

---

## P1-3. Allow a feather width above 128 px in the NR-only band

**Problem.** `FoveatedRender.cpp:346` clamps `subrectFeatherWidth` to 128. A wider band lowers the slope of the colour
step. The hybrid's clamp is already 256 (`FoveatedRender.cpp:374`).

**Change.** Raise the clamp to 256 for the NR-only path, and add a UI tooltip that says the default falloff curve
decides where the band sits.

**Cost.** None. **Risk.** Low. A wider band mixes more DLSS detail into the NR area. Check detail ROIs.

---

## P1-4. Rounded-rectangle distance in the hybrid composite

**Problem.** `PeripheryHybridCS.hlsl` uses `edge = min(...)` over the four crop edges. Its contours are rectangles,
with diagonal creases at the corners.

**Change.** Use a rounded-rectangle or superellipse distance, the same idea as `FoveatedMask.hlsli`.

**Cost.** Negligible (a few ALU per pixel). **Risk.** Low. The crop and the feather must keep the same contract
(`w = 0` at the crop edge), which the current code already satisfies.

---

## P1-5. Make colour sliders apply across the whole eye

**Problem.** Result shaping is off for the periphery (`Integration.cpp:1416`). The centre gets the sliders, the
periphery does not. With the hybrid on, the low-frequency colour field comes from the periphery, so the sliders
mostly fail to reach the colour you see at the edges. They can also create a centre-versus-edge step.

**Change.** Apply the chroma part of result shaping to `coarseP` in the coarse pass (1/8 resolution, so the cost is
negligible). Keep the luma part on the centre only, if that is what you want. Document which part applies where.

**Risk.** Low to medium. This touches the shared low-frequency field, so verify on a sky-against-ground boundary.

---

## P1-6. Retire the guided-filter key

**Problem.** `neuralRenderingPeripheryGuidedFilter` is `true` in your settings file. It has no UI control and is
compiled out. The setting does nothing.

**Change.** Drop the key from the user defaults and the benchmark JSON, or log a one-line notice when it is set.

**Cost.** None. **Risk.** None.

---

## P2-1. Lighting-only periphery at lower resolution

**Idea.** The periphery's only job in the composite is the low-frequency field `g·coarseP`. It does not need 50 %
model resolution. 33 % or 25 % may give the same field at a fraction of the cost.

**Cost saving.** INFERENCE: periphery cost scales with pixels. 33 % is about 44 % of the 50 % cost; 25 % is 25 %.

**Risk.** Medium. The field also lights the centre, so a change here changes the centre's lighting. Under
`AGENTS.md` §4 this is only allowed for low-detail regions, and it has to report per-region metrics (detail ROI versus
background) and flicker. The periphery lighting is not a detail region, but the centre's lighting is.

**Verification.** Per-band error of the low-frequency field against the 50 % reference, on the cached samples. Then a
live check.

---

## P2-2. 4 px coarse grid for the centre high-pass

**Problem.** `CentreCoarse` uses 8×8 block means, and the high-pass subtracts their bilinear upsample. That can leave
energy at the 8 px scale (HYPOTHESIS).

**Change.** Use 4×4 blocks for `CentreCoarse` only. The centre's coarse pass runs only on the crop tiles, so the cost
rises only a little.

**Verification.** High-pass energy at 8 px in flat areas, before and after.

---

## P2-3. Separate luma and chroma lighting gains

**Idea.** `g` scales both luma and chroma of the low-frequency field. Splitting it into `g_L` and `g_C` would let you
keep the NR lighting and still restore saturation in the sky.

**Cost.** None. **Risk.** This is a quality decision, not a bug fix. Show the sky against the vanilla frame at a few
values before choosing a default.

---

## P2-4. Flicker metric for periphery stagger

**Problem.** The periphery is refreshed every other frame per eye (`temporalReuseCadence = 2`,
`Integration.cpp:~1400`). Colour may lag by two frames.

**Verification.** Static scene, 60 frames. Temporal standard deviation of the periphery-only region for cadence 2 and
cadence 1. Cadence 1 costs roughly twice as much, so this is only worth doing if P2-4 shows visible lag.

---

## P2-5. Saturation-ratio extension for NR-only (only if the hybrid is rejected)

**Idea.** If the hybrid is not wanted, estimate the low-frequency saturation ratio (chroma of NR over chroma of raw)
in a ring just inside the crop, and extend it outward with a push-pull fill on a 1/16 grid. Apply it as a
chroma-only correction outside the crop.

**Risk.** High. Content outside the crop (ground, buildings) differs from the crop's ring, so the ratio may not
transfer. Unproven. Only consider it if the hybrid cannot be made to run.

---

## P3-1. Documentation refresh

- `docs/development/architecture.md` links to `file:///E:/Documents/source/repos/open-shaders/...` and describes a
  generic template. Replace it, or mark it as legacy.
- `docs/development/neural-rendering-adaptive.md` covers only adaptive NR and crop. Add the hybrid periphery, the
  PerfMode latch, projection-centred crop, the resolve modes, and the NR-only oval.
- Add a shader table for `features/Upscaling/Shaders/Upscaling/NeuralRendering/` and `FoveatedRender/`.
- Add a naming guide for the two "periphery" systems.

**Cost.** None. **Risk.** None.

---

## P3-2. Commit a clean 2.21.0

**Problem.** The worktree `opennr-2-20-10-map-resolution` has 31 uncommitted entries (25 modified, 1 deleted, 5 untracked)
relative to `b9f27136`, with `VERSION` = `2.21.0`. The branch tip still says `2.20.10`. The worktree also deletes
`runtime/open-shaders/package/OPENNR-2.20.10-CHANGELOG.md`. (Measured by `git status --short` in the worktree.)

**Change.** Commit 2.21.0 as its own commit (or a clean branch) before any test build. Confirm whether the deleted
2.20.10 changelog is intentional. Do not do this without your approval (it is a git commit; `CLAUDE.md`).

---

## P3-3. Expose the hybrid cost in the overlay

**Problem.** The detailed overlay rows are off by default in 2.21.0. The periphery pass, the centre pass and the
hybrid composite each need a row so the cost can be read in game. `AGENTS.md` §4 requires separate timing scopes.

**Change.** Add GPU pass rows for the periphery instance, the centre instance and the hybrid composite, under the
existing `CS_GPU_PASS` names.

---

## P3-4. Source-contract test for the gate

**Change.** Add to `tests/test_periphery_nr_source_contract.py`: assert the gate has no `% 8` term on height (P0-1),
and that the odd-height cases compile in the shader tests. Add a runtime-include test at 2428 and 2432.

---

## Checked and fine (no action)

- **Hybrid crop boundary.** `w = 0` at the crop edge, so the composite has no step where the crop starts.
  (`PeripheryHybridMath.hlsli`.)
- **Coarse mapping per eye.** `coarseX` clamps to each eye's half and uses `EyeWidth / 8`, so eyes do not bleed into
  each other (`PeripheryHybridCS.hlsl`).
- **Classic resolve bounds.** The luma ratio is clamped to `[1/maxRatio, maxRatio]` and the candidate is bounded
  before blending (`ModelResolutionCS.hlsl`, mode 1).
- **Result shaping NaN guards.** Non-finite results fall back to the NR colour (`ResultShapingCS.hlsl`).

---

## Not verified in this pass

- Anything in game: no live test was run. No GPU job was run. No SkyrimVR launch.
- The cost numbers (all INFERENCE).
- The dither flicker as the visible cause (HYPOTHESIS, P1-1).
- The periphery stagger lag (HYPOTHESIS, P2-4).
- The 8 px coarse artefact (HYPOTHESIS, P2-2).
- Files outside the NR, periphery and blend path (see `01-architecture.md` §0).
