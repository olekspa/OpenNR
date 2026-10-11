# NR seam and edge-colour mismatch — investigation

Written 2026-10-08. Read with `01-architecture.md` (section numbers below refer to it).

Your two reports:

1. **Seam.** The boundary where NR begins is still faintly visible, shaped like circles.
2. **Edge colour.** Outside the NR area the image keeps the DLSS colour, and the sky there is more saturated blue
   than inside. Inside, NR makes the sky greyer and less saturated. You asked whether NR colour can be extended to
   the edges without costing performance.

Short answers:

- **Colour:** the edge mismatch is caused by a silent gate. The whole-eye periphery route that carries NR colour to
  the edges was disabled on your PC for the whole session. The fix is to make that route run. It is the path the
  architecture was built for, and it does extend NR colour to the edges.
- **Circles:** the circles are the oval edge of the NR-only blend. Its band is 128 px wide and uses a dither that
  re-randomises every frame. Both factors make the transition visible.

Evidence tags follow `AGENTS.md` §4.2.

---

## 1. Facts from your setup (LOCAL-MEASURED)

| Fact | Value | Source |
|---|---|---|
| Display eye (output) | 2240 × 2428 | `CommunityShaders.log` line 313 (`[PerfMode] Latched display 2240x2428 -> render 1316x1428`, 11:57:48) |
| Render eye (engine) | 1316 × 1428 (quality mode 2, preset-derived) | same line; `SettingsUser.json` `qualityMode: 2`, `vrRenderScale: 0` |
| Projection centre | from OpenVR, `forward_px` (1391.61, 1448.53) left, (848.39, 1448.53) right | log lines 648-649 |
| NR coverage | 55 % (linear per axis), crop 1233 × 1337 px = 30.3 % of the eye | log line 650; `neuralRenderingCoverage: 55` |
| Projection-centred crop | on | `neuralRenderingProjectionCenteredCrop: true` |
| Periphery settings | enabled, 50 %, gain 0.83, feather 160 px, stagger on | `SettingsUser.json` |
| Guided filter setting | `true` in the file, no UI control, no effect (see §5) | `SettingsUser.json`; `Integration.cpp:78` |
| Edge blend | oval mask (`subrectMaskMode: 1`), **dither** (`subrectBlendMode: 2`), dither strength 1.45, falloff 1.23, feather 128 px | `SettingsUser.json` |
| Resolve mode | 1 = matched residual (not the default 0 = classic) | `SettingsUser.json`; log line 653 shows `resolve=transfer:1.00,colour:1.00,maxRatio:4.00,residual:1.00` |
| Periphery log lines | none in the whole session | `grep -i periphery` on the log returns nothing |

---

## 2. Finding A — the periphery route never ran (root cause of the edge colour)

**Gate.** `hybridRequested` requires `(totalDesc.Height % 8u) == 0` (`Integration.cpp:1199`). Your display eye
height is 2428, and `2428 mod 8 = 4`. So `hybridRequested` is false on every frame, and the gate prints nothing.

**Proof it is the only failing term.** `coverageCrop` (the NR-only route, which the log shows as active) requires
`fullEye && coverage < 100 && !gaze.dynamic && !adaptiveCropActive && coverage rects valid && matching sizes &&
R32 depth`. Every one of those terms is also in `hybridRequested`. The remaining hybrid terms are
`neuralRenderingPeripheryEnabled` (true in your settings) and `(eyeWidth % 8) == 0` (2240 % 8 = 0, passes). So
with coverage active and periphery enabled, the height term is the only term that can be false. **SOURCE-VERIFIED**
from the boolean expressions at `Integration.cpp:1118-1200`.

**Consequence.** Every frame runs the NR-only route:

- F18 runs at 100 % on the 55 % oval-crop only.
- Everything outside the crop is the raw DLSS frame. Nothing is added there.
- The NR colour (greyer sky) exists only inside the crop. The DLSS colour (bluer sky) exists outside it. The step
  between them is your edge mismatch.

The edge mismatch is therefore not a colour bug in the model. It is a coverage gap. The architecture was designed
to remove that gap (see §4).

**What changed the display height (LOCAL-MEASURED; the link to the streamer is inference).** The display height is
the recommended target OpenVR reports (`ResolutionHook.cpp`), which follows the headset projection. Virtual Desktop's
*Horizontal and Vertical FOV Tangent* are both set to 90% on this PC.

- 1 Oct Quest 3 measurement (`<RESEARCH_DIR>\QUEST3_PROJECTION_MEASURED_20261001.md`): L −1.3764, R 0.8391,
  T −1.4281, B 0.9657. That file does not record the streamer setting, so 100% is an assumption.
- Today's log (lines 648-649): L −1.2367, R 0.7539, T −1.2917, B 0.8734.
- Ratios 0.899, 0.899, 0.904, 0.904, which match the 90% tangent setting.
- 7 Oct session (`Downloads\CommunityShaders (7).log`, 21:54) latched the display at **2496×2688**. 2688 mod 8 = 0, so
  the gate passes. Today's 2240×2428 is a shrink of 0.897 wide and 0.903 tall, the same as the tangents.

So the streamer setting is the most likely trigger for the height change. Lowering the FOV is a legitimate performance
choice, so the code should accept it. The gate is what made it a silent failure.

**Whether the periphery ever ran here.** Unknown. The success path logs nothing, and the 7 Oct session was full-eye
NR at 100% with no crop, so it does not show the periphery either. Eligibility at native size is confirmed; actual
activity is not.

**How it was confirmed.** Static reading of the gate, plus the live log (no periphery output at all; coverage
active). The one gap is that there is no log line for a refused hybrid. That gap is itself a finding (P0-2).

---

## 3. Finding B — the circles are the oval dither band (cause of the visible seam)

On the NR-only route, the blend is `SubrectBlendCS.hlsl` with the settings above:

- **Mask (oval).** The ellipse is inscribed in the 1233 × 1337 crop. The seam is a closed elliptical contour, which
  is what reads as "circles". `MaskMode = 1` (`Core.cpp:1284`, `FoveatedRender.h:101`).
- **Band.** The feather runs 128 px inside the ellipse (`FoveatedRender.cpp:346` caps it at 128). Inside the band,
  alpha ramps from 0 at the edge to 1 at the inner boundary.
- **Dither (your mode, blend = 2).** `alpha = FalloffAlpha(t, curve) + (noise − 0.5) × strength`, with
  `strength = 1.45`. The noise is interleaved gradient noise seeded with `frameIndex × 5.588`
  (`SubrectBlendCS.hlsl`, `BlueNoise()`). The `frameIndex` term makes the pattern change every frame.
  At strength 1.45 the alpha is pushed well past 0 and 1, so most pixels in the band are either fully DLSS or fully
  NR. The enum comment calls this a "binary threshold". The result is a grainy band whose pattern moves every frame.

**Why that reads as a seam.** In flat areas such as sky, the DLSS and NR colours differ. A random per-frame mix of
the two shows up as shimmering grain along the oval. It also explains why the seam is "subtle but still there": the
colour step is small, but the shimmer is what the eye picks up.

**Status.** The mechanism is **SOURCE-VERIFIED**. That it is the visible cause is **HYPOTHESIS** until an A/B
test is run in game (§6).

---

## 4. Answer to your colour question: extend NR colour to the edges

NR colour lives in one place in the periphery composition:

```
result = raw + g·coarseP + w·(effC − coarseC) + (1 − w)·(effP − coarseP)     (PeripheryHybridCS / PeripheryCompose)
```

- `coarseP` is the periphery's low-frequency effect, averaged on 8×8 blocks over the **whole eye**. The term
  `g·coarseP` therefore applies NR's low-frequency colour and lighting to **every pixel**, including the edges.
- The periphery is already computed for this (`Integration.cpp` periphery pass, `modelResolveMode = 4`). The
  composite only adds a bilinear read of a 1/8-resolution texture. That is effectively free per pixel.
- The uniform gain `g = 0.83` is applied to the whole eye, so it does not create a seam. Edge and crop get the same
  colour treatment.

So the answer is: **yes, and the code already does it.** Enabling the hybrid route (P0-1) extends NR colour to the
edges at the cost of one periphery pass, not a new mechanism.

**Cost estimate (INFERENCE, measure in game).** Pixel counts per eye: centre crop 1233 × 1337 = 1.65 Mpx; periphery
at 50 % = 1120 × 1214 = 1.36 Mpx. F18 cost is roughly proportional to pixels, so the hybrid adds about 82 % of the
NR centre cost per eye without stagger, or about 41 % with stagger (which halves the periphery's refresh rate). The
real number has to come from an in-game measurement with the detailed overlay rows turned on (they default off in
2.21.0).

**Limits of the hybrid colour model.**

- The gain `g` is uniform. It scales NR's colour everywhere by 0.83. If you want less desaturation, change `g` or
  split it into separate luma and chroma gains (backlog P2-3). Both are quality choices, not bugs.
- Result shaping (the colour and tone sliders) is **not applied to the periphery** (`Integration.cpp:1416`). With the
  hybrid active, the sliders change the centre but not the low-frequency colour field that the edges use. Do not use
  the colour sliders in hybrid mode until backlog P1-5 is done. They would create a centre-versus-edge mismatch.

---

## 5. Finding C — smaller issues that affect the same area

- **Guided filter is compiled out.** `kPeripheryGuidedFilterQualified = false` (`Integration.cpp:78`), so the
  guided path never runs. Your settings file still says `neuralRenderingPeripheryGuidedFilter: true`, probably left
  over from an earlier build. 2.21.0 has no UI control for this flag (grep of `FoveatedRender.cpp` finds only the
  settings key), so the value is invisible in-game and harmless. Remove it from the file or ignore it.
- **Feather limit.** The NR-only band cannot exceed 128 px (`FoveatedRender.cpp:346`). The hybrid band can (256 px
  clamp at `FoveatedRender.cpp:374`). If the oval stays in use, a wider band is one of the cheapest seam fixes.
- **Rectangle with corner creases (hybrid, once active).** The hybrid composite measures distance to the nearest
  crop edge, so the feather contours are rectangles with diagonal creases at the corners
  (`PeripheryHybridCS.hlsl`, `edge = min(...)`). `FoveatedMask.hlsli` already has a superellipse distance that would
  remove the creases at negligible cost (backlog P1-4).
- **8 px coarse blocks.** The centre's high-pass subtracts a bilinear upsample of 8×8 block means
  (`CentreCoarseOut`). That can leave faint energy at the 8 px scale around the feather. **HYPOTHESIS**, needs a
  per-band check (backlog P2-2).
- **Periphery refreshed every other frame** (`temporalReuseCadence = 2`, eye stagger). Colour may lag by two frames
  in the periphery. **HYPOTHESIS**, needs a flicker metric (backlog P2-4).
- **Two periphery systems.** The foveated `peripheryAAMode` family and the NR `neuralRenderingPeriphery*` family are
  unrelated. When reading settings or logs, check which one a key belongs to.

---

## 6. How to verify (needs your approval before any live test)

Live in-headset tests are required to confirm this. Nothing in this document has been tested in
game. The minimum set:

1. **Confirm the gate is the cause.** With the current build, take screenshots at the same spot with the periphery
   off and on. The gate predicts no change in route. Add the logging from P0-2 to make the route visible.
2. **Blend A/B (no code needed; a settings change, so ask first).** Same camera, same scene, alternating between
   `subrectBlendMode = 1` (feather) and `2` (dither). Watch the oval band in flat sky for grain and shimmer across
   several frames. Save 4-panel PNGs (raw | feather | dither | amplified diff) per `AGENTS.md` §4.
3. **Hybrid A/B (after P0-1).** Same scene, hybrid off versus on. Measure sky chroma against the vanilla frame
   (per-band metrics per `AGENTS.md` §4) and the NR cost in the profiler rows.

---

## 7. What would change this analysis

- A different display height. Only the `% 8` term matters for the gate. A different SteamVR setting may take the
  route through the hybrid.
- A live log showing a periphery line. Nothing in the current log suggests one, but the logging gap means absence is
  not proof of absence. Treat §2 as **SOURCE-VERIFIED reasoning with LOCAL-MEASURED inputs**, not a measured route
  trace.
- The default resolve mode being classic. You run matched residual, which is additive. If you switch back to
  classic, the crop's chroma is replaced by the model's chroma, and the colour comparison changes.
