# OpenNR 2.21.0 — architecture reference

Status: reference written 2026-10-08 from source reading and the live log. No source was changed.

**Source state.** The 2.21.0 code is the *uncommitted working tree* of the worktree
`<REPO>`
(branch `codex/opennr-2.21.0`, base commit `b9f27136`). That worktree's `VERSION` file says `2.21.0`, and
`runtime/open-shaders/docs/releases/OPENNR-2.21.0-CHANGELOG.md` exists there, but the branch tip itself still
says `2.20.10`. Commit a clean 2.21.0 before any release or test build. All paths below are relative to
`runtime/open-shaders/` in that worktree unless stated otherwise.

**Evidence tags** (from `AGENTS.md` §4.2): `SOURCE-VERIFIED` (read in code, file:line given),
`LOCAL-MEASURED` (live log or settings file on this PC), `INFERENCE`, `HYPOTHESIS`.

---

## 0. What was read, and what was not

| Area | Coverage |
|---|---|
| NR integration (`src/Features/Upscaling/NeuralRendering/Integration.cpp`, 1,581 lines) | Read the coverage, hybrid, blend and dispatch sections in full. |
| NR shaders (`features/Upscaling/Shaders/Upscaling/NeuralRendering/`, 11 HLSL files) | Read `PeripheryHybridCS`, its math include, `ModelResolutionCS`, `ResultShapingCS`. Skimmed the adaptive handoff and guided-filter shaders. |
| Foveated render blend (`features/.../FoveatedRender/SubrectBlendCS.hlsl`, `package/Shaders/Common/FoveatedMask.hlsli`, `src/Features/Upscaling/FoveatedRender*.{cpp,h}`) | Read the blend constant setup and the settings tables. |
| PerfMode display/render sizing (`src/Features/Upscaling/PerfMode/ResolutionHook.cpp`) | Read the latch. The rest of PerfMode is only listed. |
| Repo layout, changelogs, design docs, tests | Surveyed by counts and names. Read `CLAUDE.md`, `AGENTS.md`, `docs/development/architecture.md`, `docs/development/neural-rendering-adaptive.md`, the 2.21.0 changelog. |
| Remaining ~1,000 source files (`src/` 557 files, `features/` 325, `package/` 247, `vendor/`) | **Not read line by line.** This pack is not a full audit. It is an architecture map plus a focused investigation of the NR, periphery and blend path. |

---

## 1. What OpenNR is

OpenNR is a Skyrim VR runtime (SKSE plugin plus shader packs) in the Open Shaders / Community Shaders lineage.
Its distinctive part is **Neural Rendering (NR)**: a learned Feature 18 (F18) model runs on the upscaled frame,
and the NR *effect* (model output minus model input) is added back onto the frame. Around that sit DLSS/Streamline
upscaling, foveated rendering, a performance mode (PerfMode), and about 50 feature packs (lighting, grass, water, sky…).

Vocabulary used throughout:

- **display eye**: the per-eye size the headset expects, from OpenVR's recommended render target (2240×2428 on this PC).
- **render eye**: the size the engine renders at, from PerfMode (1316×1428 on this PC, quality mode 2).
- **DLSS frame**: the upscaled frame at display size. NR runs on this frame.
- **coverage / crop**: the sub-rectangle of the eye where F18 runs at full (100%) model resolution.
- **periphery**: the *whole-eye* F18 pass at reduced model resolution (50% on this PC). Used only by the hybrid route.
- **effect**: `model_output − model_input`. Everything NR adds is an effect.

---

## 2. Repository layout (2.21.0 tree)

| Path | Contents | Size (files / lines, approx.) |
|---|---|---|
| `src/` | C++ runtime: `Features/` (57 feature classes), `Utils/`, `Hooks`, `Deferred`, UI. | 557 files |
| `src/Features/Upscaling/` | DLSS/Streamline, FSR, foveated render, NR, PerfMode, adaptive controllers. | core of the work |
| `src/Features/Upscaling/NeuralRendering/` | NR: `Integration.cpp` (coverage, hybrid, blend), `Renderer.cpp` (model passes), `Runtime*.{h,cpp}`, `RuntimePolicy.h` (presets, coverage math), `AdaptiveController`, `AdaptiveCropController`, `SettingsBenchmark`. | ~9k lines |
| `src/Features/Upscaling/FoveatedRender/` | Non-NR foveated route: `Core`, `Modes`, `Params`, `Preprocess`, `Postprocess`, `Bridge`, `RouteHistory`. | |
| `src/Features/Upscaling/PerfMode/` | Display/render sizing latch, DSSwap, PostIntercept, MenuBridge, map-menu display-resolution policy, MapProbe. | |
| `features/` | Shader packs, one folder per feature (57 folders), each with `Shaders/` and a `Features/<Name>.ini`. | 325 files |
| `features/Upscaling/Shaders/Upscaling/` | Upscale/foveated/NR shaders: `NeuralRendering/*.hlsl` (11), `FoveatedRender/*.hlsl` (SubrectBlend, SubrectStretch, PeripheryTemporalSmooth, DepthToPerEye…). | |
| `package/` | What ships: `Shaders/` (core Skyrim shader overrides plus `Common/FoveatedMask.hlsli`), `SKSE/`, `Interface/`, `OPENNR-2.21.0-CHANGELOG.md`, `OpenNR-EyeTracking.md`. | 247 files |
| `tests/` | Python source contracts (`test_periphery_nr_source_contract.py`, `test_upscaling_source_contracts.py`, …) and C++ shader-compile / runtime-include tests (`tests/cpp/`, `tests/shaders/`). | 90 files |
| `docs/` | Development docs (`docs/development/`), release notes (`docs/releases/`), new-feature template. | |
| `cmake/`, `CMakePresets.json`, `Build*.bat`, `containerbuild.ps1` | Build. Scripts: `BuildDev`, `BuildDevFast`, `BuildRelease`, `BuildPR`. | |

---

## 3. Frame pipeline in VR (as implemented)

```
OpenVR GetRecommendedRenderTargetSize -> display eye 2240x2428            [LOCAL-MEASURED 11:57:48]
PerfMode latch: render = display / FSR-quality-ratio, rounded down to even
                -> render eye 1316x1428 (quality mode 2)                   [SOURCE-VERIFIED PerfMode/ResolutionHook.cpp; LOCAL-MEASURED]
Scene renders at render size -> DLSS upscales to display size (DLSS frame)
Foveated route (route=standard+foveated, both eyes)
Neural Rendering on the DLSS frame, FoveatedLdrBeforeUI pass              [SOURCE-VERIFIED Integration.cpp ~1270-1520]
   ├─ NR-only coverage (coverageCrop):  F18 on centred/projection crop at 100%;
   │                                     result edge-blended over DLSS      (SubrectBlendCS)
   └─ Hybrid (hybridRequested):          F18 on whole eye at 50% (periphery)
                                         + F18 at 100% on crop (centre)
                                         composed in PeripheryHybridCS -> whole eye written
UI composite -> submit
```

The NR-only and hybrid routes are mutually exclusive per frame. The gate for each is in
`Integration.cpp:1196-1199` (hybrid) and `Integration.cpp:1118-1200` (coverage); see §6.

---

## 4. The NR model contract (`ModelResolutionCS.hlsl`)

The shader converts the F18 model's output into the frame that is written back. The mode is chosen in `Renderer.cpp`
around `2540-2556`: resolve mode 0 maps to shader mode 1, resolve mode 1 maps to shader mode 3, and the periphery
path sets mode 4 directly (`Integration.cpp:1404`).

| Shader mode | Output written | Used by |
|---|---|---|
| 0 | `SampleValidSource(proxy)`: resample of the model input | internal |
| 1 | **classic bounded**: `original × luma_ratio`, chroma hue taken from the model (OKLab), luma ratio clamped to `[1/maxRatio, maxRatio]`, blended by `transferStrength` and `colourStrength` | `neuralRenderingResolveMode = 0` (the *default*) |
| 2 | exact-area resample of the proxy | pre-upscale experiments |
| 3 | **matched residual**: `original + (model − proxy)`, luma-bounded by `maxRatio`, scaled by `residualStrength` | `neuralRenderingResolveMode = 1`. **Your current setting** |
| 4 | **periphery residual**: `original + (model − proxy)`, no clamp, no scaling | hybrid periphery only |
| 5 | low-resolution effect `(model − proxy)` written into the periphery residual atlas | hybrid periphery only |

Defaults (`Renderer.cpp` ~2461-2464): transfer 1.0, colour 1.0, maxRatio 4.0, residual 1.0. The live log confirms
`resolve=transfer:1.00,colour:1.00,maxRatio:4.00,residual:1.00` (`log line 653`).

**Important:** the default (classic) and your setting (matched residual) produce different colour behaviour. Classic
replaces the crop's chroma with the model's chroma. Matched residual adds the model's change. Keep this in mind
when comparing screenshots taken with different resolve modes.

---

## 5. Coverage, the NR-only route, and the edge blend

- **Presets** (`RuntimePolicy.h:17`): coverage `100, 95, 90, 85, 80, 75, 70, 55, 45` (percent, *linear per axis*).
  55% is 30% of the eye area. The live log shows the 55% crop is `1233×1337` display pixels
  (`log line 650`), i.e. 30.3% of the display eye.
- **Shape:** rectangle. With *projection-centred crop* on, the centre is the lens projection centre from OpenVR
  (`log lines 648-649`), otherwise the eye centre. The crop is in display space.
- **Edge blend (NR-only path):** `Integration.cpp:1274` sets `wantsEdgeBlend`. `Core.cpp:1184-1300` runs
  `SubrectBlendCS.hlsl` with:
  - mask: `0 = Rectangle`, `1 = Oval` (default `kOval`, `FoveatedRender.h:101`). Oval = ellipse inscribed in the crop.
  - blend: `0 = hard copy` (forced up to feather for NR), `1 = feather` (smoothstep, falloff curve), `2 = dither`
    (`alpha = falloff + (noise − 0.5)·strength`, noise = interleaved gradient noise with a **per-frame offset**).
  - feather width clamped to `[2, 128]` px (`FoveatedRender.cpp:346`). **You cannot widen the band past 128 px in this build.**
- **Logs:** coverage activity is logged once on transition (`Integration.cpp:1243`):
  `NR-only coverage 55% active color=1233x1337 guides=724x786 (DLSS full eye, model 100%)`.

---

## 6. Periphery hybrid (the whole-eye route)

### 6.1 Composition

`PeripheryHybridCS.hlsl` with `PeripheryHybridMath.hlsli`:

```
Coarse pass (Mode 0), 8×8 blocks:
    PeripheryCoarse[block]  = mean over block of (periphery residual)        [whole eye]
    CentreCoarse[block]     = mean over block of (centre_out − raw)          [crop tiles only]

Composite (Mode 1), per display pixel q:
    edge = min distance to the four crop edges, in display pixels (negative outside the crop)
    w    = 0.5 − 0.5·cos(π·min(edge / feather, 1))   if edge > 0, else 0    [feather default 160 px]
    result = raw + g·coarseP + w·(effC − coarseC) + (1 − w)·(effP − coarseP)
    g = lighting gain, clamped to [0, 1.5]                                    [default 0.83; presets 0.83/0.86/0.89/0.93]
```

What that means:

- The **low-frequency colour and lighting** (`g·coarseP`) comes from the **periphery** and is applied to **every pixel
  of the eye**, including outside the crop. This is the mechanism that carries NR colour to the frame edges.
- The **detail** (`effC − coarseC` inside, `effP − coarseP` outside) is crossfaded across the feather band.
- Outside the crop, `w = 0`, so the result is `raw + g·coarseP + (effP − coarseP)`.

### 6.2 Gates (`Integration.cpp:1196-1199`)

```
hybridRequested =
    neuralRenderingPeripheryEnabled
 && fullEye
 && !gaze.dynamic
 && !foveated.IsAdaptiveCropRuntimeActive()
 && leftCoverageRect.IsValid() && rightCoverageRect.IsValid() && matchingCoverageRects
 && depthFormat ∈ {R32_TYPELESS, R32_FLOAT}
 && (eyeWidth % 8) == 0
 && (totalDesc.Height % 8) == 0          // <-- see §6.4
```

If any gate fails, the frame silently takes the NR-only route. **No log line is printed.** The coverage log at
`Integration.cpp:1243` is printed either way, so it does not tell you which route ran.

### 6.3 Other hybrid facts

- Periphery is computed at `peripheryTuning.modelResolutionPercent` (50 by default; UI presets 50/58/66/75) with
  `modelResolveMode = 4`, `temporalReuseCadence = 2`, eyes staggered (`Integration.cpp:~1395-1412`).
  Each eye's periphery is therefore refreshed every other frame.
- **Result shaping is off for the periphery** (`Integration.cpp:1416`). The centre gets your shaping settings, the
  periphery does not. See the seam doc, §4.
- **Guided filter.** `kPeripheryGuidedFilterQualified = false` (`Integration.cpp:78`), so the guided-filter path
  (`Integration.cpp:1446`) never runs in 2.21.0. There is no UI control for it. The key
  `neuralRenderingPeripheryGuidedFilter` lives only in the settings file (your copy is `true`, inert).
- Hybrid failure is fail-closed: if a hybrid pass fails, the frame falls back to centre-only and logs
  `Periphery NR hybrid pass failed` (`Integration.cpp:1462`).

### 6.4 The height gate (the important one)

`(totalDesc.Height % 8) == 0` is the only hybrid gate that fails on this PC. The display eye height is 2428, and
2428 mod 8 = 4 (`SOURCE-VERIFIED Integration.cpp:1199`, `LOCAL-MEASURED log line 648`).

What the code *needs* from the dimensions is less than what the gate asks for:

- The coarse grid is sized with ceiling division on height (`Integration.cpp` dispatch: `(height + 7u) / 8u`), and
  `PeripheryHybridCS` uses `(Height + 7u)/8u` and clamps the last block to `Height`.
- Width *does* need the eye to be a multiple of 8, because the coarse grid maps each eye with `EyeWidth / 8`.
- No test asserts the height rule (grep of `tests/` finds nothing).

So the height term looks like a conservative guard, not a hard requirement. **Verify on the GPU before relaxing it**
(see the backlog, P0-1).

---

## 7. Foveated render (non-NR) and its periphery (a different system)

The foveated route (`src/Features/Upscaling/FoveatedRender/`) renders a gaze-centred subrect at full resolution and
stretches the rest. It has **its own periphery**, unrelated to the NR periphery:

- settings: `peripheryAAMode`, `peripheryBlurRadius`, `peripheryTemporalAlpha` (`FoveatedRender.h:96-98`)
- shader: `PeripheryTemporalSmoothCS.hlsl`

The settings file for NR periphery uses `neuralRenderingPeripheryEnabled`, `…Resolution`, `…LightingGain`,
`…FeatherWidth`, `…Stagger`, `…GuidedFilter`. The two are easy to confuse. Keep them apart when reading settings.

Foveated blend (for both foveated and NR-only): `subrectBlendMode`, `subrectMaskMode`, `subrectFeatherWidth`
(clamped to 2–128), `subrectDitherStrength`, `subrectFalloffCurve`. See §5.

---

## 8. Result shaping (colour and tone controls on the NR effect)

`ResultShapingCS.hlsl` modifies the NR *delta* per pixel: luma tone (shadows/midtones/highlights), brightening and
darkening, chroma strength (`gColorStrength`), hue shift, stop-based limits, halo suppression, and history
stabilisation. It is **off by default** (`neuralRenderingResultShapingEnabled = false`, `FoveatedRender.h:139`).
Because it operates on the NR result, it applies to the NR-only crop and to the periphery-hybrid *centre*, not to
the periphery field that carries the whole-eye colour (see seam doc §4).

---

## 9. Adaptive NR and adaptive crop

Summarised in `docs/development/neural-rendering-adaptive.md` (accurate for what it covers). Code:
`src/Features/Upscaling/NeuralRendering/AdaptiveController.cpp` (hysteretic model-resolution ladder
100→95→90→85→80→75→70, 1 tier per dwell) and `AdaptiveCropController.cpp` (crop coverage 100→70, restore after
headroom). Both are **off** on your config (`neuralRenderingAdaptiveEnabled: false`). The design doc does not describe
the hybrid periphery, the PerfMode latch or projection-centred cropping. It needs updating (backlog P3).

---

## 10. PerfMode (display and render sizing)

- `ResolutionHook.cpp`: reads `GetRecommendedRenderTargetSize` once at install, then sets
  `render = display / scale` rounded down to even (`& ~1u`, "odd widths truncate the engine's half-res buffers").
  `vrRenderScale` overrides the preset ratio. Log: `[PerfMode] Latched display …` (`ResolutionHook.cpp:100`).
- The display size comes from **SteamVR's recommended target**, not from an OpenNR setting. Changing SteamVR's
  supersampling changes the display height, and with it the hybrid gate in §6.4.
- 2.21.0 additions (from the changelog, not all re-read): map menus render at display resolution
  (`Map: render at display resolution`, default on), MapProbe hooks (opt-in diagnostics, default off), profiler and
  Tracy capture off by default, overlay detail rows off by default.

---

## 11. Settings, presets and persistence

- Live settings file for your install: `<MO2_INSTANCE>\overwrite\SKSE\Plugins\CommunityShaders\SettingsUser.json`
  (the MO2 overwrite folder; modified 11:55 on 2026-10-08).
- Keys are declared in X-macro lists (`FoveatedRender.cpp` ~80-140). Defaults are in `FoveatedRender.h:96-210`.
- Benchmark variants (`SettingsBenchmark.cpp`) must use **values from the preset lists** (for example
  `kNeuralCoveragePresets`). Values outside the list are silently normalised (`AGENTS.md` §4.3).
- Shader includes must use `#include "Upscaling/..."`, never a leading `/` (`AGENTS.md` §4.3).

---

## 12. Other features (one line each)

The 57 feature folders under `features/` cover: lighting (Light Limit Fix, Inverse Square Lighting, Skylighting,
Interior Sun, Linear Lighting, IBL), terrain (Terrain Blending, Shadows, Variation, Helper, LOD Blending), materials
(Extended Materials, Hair Specular, Skin, Subsurface Scattering, TruePBR, Vanilla Fresnel), effects (Screen-Space GI,
Screen-Space Shadows, Volumetric Lighting and Shadows, Cloud Shadows/Relight, Dynamic Cubemaps, Bloom, Fog), grass
(Grass Lighting, Collision, Optimizations), water (Unified Water, Water Effects, Wetness), sky and weather (Procedural
Sun, Sky Sync, Weather Picker, Wind), VR (VR, VRS, Foveated, Stereo blend), and tooling (Performance Overlay,
Remote Control, RenderDoc, Scene Manager, CS Editor, OpenNRCapture, Screenshot).

Known issue in the notes: Skylighting was the root cause of dungeon blackout (`opennr-orchestration-state` memory),
with a live neutral-output switch added in `45311cb1`. Confirmed by the user on 2026-10-02.

---

## 13. Build, package and test

- Build entry points: `BuildDev.bat`, `BuildDevFast.bat`, `BuildRelease.bat`, `BuildPR.bat`, `containerbuild.ps1`,
  `CMakePresets.json`. Toolchain: `<PROJECTS_DIR>\OpenNR-Toolchain` (CMake 4.4.3).
- Package contents are in `package/`. Verify the extracted package (shaders, JSON, version, feature flags) before
  installing it to MO2, and install each build to a new profile (`AGENTS.md` §4.3, `CLAUDE.md`).
- Tests:
  - Python source contracts: `tests/test_periphery_nr_source_contract.py`, `tests/test_upscaling_source_contracts.py`,
    `tests/test_neural_rendering_foveated_scope_contract.py`. These check constants and text, not GPU behaviour.
  - C++ compile tests: `tests/cpp/test_neural_rendering_shader_compile.cpp`, `test_upscaling_shader_compile.cpp`.
  - Runtime-include tests: `tests/shaders/periphery_hybrid_runtime_include_test.cpp`,
    `temporal_reuse_periphery_runtime_include_test.cpp`.
- **Timing rule:** NR cost is only valid when measured in game (kit `<RESEARCH_DIR>\ingame_kit_20261001`), because
  offline harness timings disagreed by up to 2× (`AGENTS.md` §4.3).

---

## 14. Documentation gaps found while reading

- `docs/development/architecture.md` links to `file:///E:/Documents/source/repos/open-shaders/...`, which is not
  this tree. It also describes a generic plugin template, not the NR system. Replace it or mark it as legacy.
- `docs/development/neural-rendering-adaptive.md` covers adaptive NR and crop only. It does not describe the
  periphery hybrid, the PerfMode latch, projection-centred crop, the NR-only oval edge, or the resolve modes.
- Two different "periphery" systems (foveated and NR) share a word and no naming guide.
- No document lists the NR shader files and their roles.

These gaps are in backlog item P3-1.
