# OpenNR 2.21.8 default-settings performance audit (READ-ONLY)

Date 2026-10-09. No repo, game or MO2 file was edited. Evidence tags: [SRC] read in source, [FILE] read in a file, [MEAS] in-game benchmark JSON, [INF] inference.

Abbreviations. `R:` = `<REPO>\runtime\open-shaders\src`. `FR.h` = `R:\Features\Upscaling\FoveatedRender.h`, `FR.cpp` = `R:\Features\Upscaling\FoveatedRender.cpp`, `INT.cpp` = `R:\Features\Upscaling\NeuralRendering\Integration.cpp`. `LIVE` = `<MO2_INSTANCE>\overwrite\SKSE\Plugins\CommunityShaders\SettingsUser.json` (mtime 2026-10-08 21:23). `BENCH` = `<USER_HOME>\Documents\My Games\Skyrim VR\SKSE\OpenNR-SettingsBenchmark\benchmark-20261001-150911.json` (byte-identical copy in `<RESEARCH_DIR>\ingame_kit_20261001\`).

---------------------------------------------------------------------------
## 0. Headline findings (read these first)

1. **The live settings are the code defaults, and the code defaults are the slowest NR configuration.** LIVE has `neuralRenderingCoverage=100`, `PeripheryEnabled=false`, `ProjectionCenteredCrop=false`, `subrectBlendMode=1`, `subrectDitherStrength=1.0`, `subrectFeatherWidth=64`. Every backup from `SettingsUser.json.bak-20261001-before-hybrid` through `...bak-20261008-pre-recommended` (11 files checked) instead has the user's tuned set: blend 2 (dither), strength 1.45, feather 128, and, from 2026-10-02 on, coverage 55 + periphery on + P50 + stagger + projection-centred crop. The "Recommended" button (`FR.cpp:328-334`, `settings = Settings{}` then coverage = `kNeuralCoveragePresets[0]` = 100) wrote the defaults over the tuned set on 2026-10-08 (`bak-20261008-pre-recommended` is the last tuned file). Log of the 2.21.8 session confirms the result: `[DLSSNR] LDR output ... edgeBlend=false mode=Feather path=direct-copy`, `Periphery NR inactive: disabled` (`CommunityShaders.log` lines 649, 656). So the user is currently paying full NR cost (BENCH: 39.7 ms vs 30.1 ms for C55+P50 staggered, a 9.84 ms saving).
2. **Best measured quality-per-ms configuration: Full Eye crop preset + NR coverage 55 + projection-centred crop + Periphery NR on + P50 + stagger on** (BENCH id `nr-hybrid-c55-p50-staggered`). It cuts NR cost 23.5 -> 13.86 ms (-41 %), reproducing the 2.20.4 figure in the brief. C45 is a further -2.25 ms but costs centre area (55 % -> 45 % linear = 30 % -> 20 % of the eye).
3. **Fresh-install crop preset is wrong for the hybrid.** `FR.cpp:533` seeds the default crop preset as "Center 75%". The hybrid and NR-only coverage route require the Full Eye preset (`INT.cpp:1250-1255`, term `fullEye`; UI warning `FR.cpp:1511`). A fresh install therefore cannot reach the best measured configuration without changing the crop preset first. (LIVE has `SelectedPresetIndex=0` = Full Eye, set by the user.)
4. **Everything except NR is small.** In the same BENCH, no non-NR variant saved more than 1.15 ms (parallax off), and most are inside the run's own uncertainty. NR configuration is >90 % of available savings.
5. **One user-side setting costs CPU while "diagnostic": `Advanced/Frame Annotations = true` in LIVE** (code default false, `State.h:285`). It installs per-draw hooks (`FrameAnnotations.cpp:868-889`) that do two `std::format` calls and a COM `BeginEvent` per draw (`FrameAnnotations.cpp:432-447`, `State.cpp:1281-1291`). Not in BENCH (2.20.4 session had it false per `bak-20261007-before-frameannot`), so un-measured. [INF] CPU-only, GPU-neutral.
6. **`SettingsDefault.json` in the MO2 overwrite is stale (version `2-10-1-0`, mtime 2026-10-07 14:04)** and is loaded as the base layer (`State.cpp:520-555`, Default -> User merge at top-level-feature-key granularity: `settings[key] = value`). It holds old defaults (SSGI 3 slices/6 steps, `qualityMode` 1, NR style 2, intensity 1.9, tone 1.5, structure 1.5, `foveatedRender.enabled` 0, `vrEnableVRS` 0, Frame Annotations false, `ShowInOverlay` true). Any feature block missing from `SettingsUser.json` would silently get these instead of the 2.21.8 C++ defaults. LIVE currently has every block, so this is latent, but "Restore defaults"-style reasoning from that file is wrong.

---------------------------------------------------------------------------
## 1. In-game benchmark results (source d)

Only one result file exists: `benchmark-20261001-150911.json` (2026-10-01 15:00:41-15:09:11, 2.20.4 code, metric "SteamVR compositor application GPU time (pre+post submit), per frame", warmup 45 / measure 90 frames, **repeats 2**, `cell` empty, every block 90/90 reprojected frames). No newer JSON exists anywhere searched (`OneDrive...\SKSE\OpenNR-SettingsBenchmark\`, `<RESEARCH_DIR>\ingame_kit_20261001\`; a recursive `find` over `E:\` max-depth 3 for `*SettingsBenchmark*` found nothing else). The shipped package JSON (`runtime\open-shaders\package\SKSE\Plugins\CommunityShaders\OpenNR-SettingsBenchmark.json`) is identical to the one in `<MO2_INSTANCE>\mods\OpenNR 2.21.8\...` and has `repeats: 1` (the run used 2, so the user edited it or the 2.20.4 default differed).

Baseline for every pair was the user's then-live config = Full Eye NR at 100 % (`nr-full-eye` delta +0.01 ms). `gpuUncertaintyMs` is a drift/repeat envelope, NOT a confidence interval (`<RESEARCH_DIR>\ingame_kit_20261001\README.md`).

| variant id | GPU ms | delta vs bracketing baseline | uncertainty | NR cost (GPU - NR-off 16.28) | verdict |
|---|---|---|---|---|---|
| nr-off (reference) | 16.28 | -23.50 | 0.21 | 0 | not acceptable (user wants NR) |
| nr-full-eye | 39.68 | +0.01 | 0.16 | 23.40 | current live (defaults) |
| nr-coverage-crop-only (C55, no periphery) | 24.74 | -14.87 | 0.25 | 8.46 | cheapest but periphery has no NR colour (seam/edge colour issue, `docs\opennr-2.21.0-review\README.md`) |
| nr-hybrid-c55-p66 (no stagger) | 37.40 | -2.40 | **1.26** | 21.12 | worse than staggered; noisy (CPU block also drifted to 15.7 ms) |
| nr-hybrid-c55-p66-staggered | 32.06 | -7.94 | 0.44 | 15.78 | dominated by P50 |
| **nr-hybrid-c55-p50-staggered** | **30.14** | **-9.84** | 0.28 | **13.86 (-41 % vs 23.40)** | **best measured quality/ms** |
| nr-hybrid-c45-p50-staggered | 27.89 | -12.03 | 0.60 | 11.61 (-50 %) | extra -2.25 ms for 30 % -> 20 % centre area |
| dlss-preset-j | 40.00 | +0.17 | 0.29 | - | no gain |
| dlss-preset-e (CNN) | 39.88 | +0.02 | 0.07 | - | no gain |
| llf-shadow-formula | 39.85 | -0.07 | 0.41 | - | no signal |
| llf-shadow-cap (8 / 2 ms) | 39.78 | -0.06 | 0.60 | - | no signal |
| ssgi-ao-lite (1 slice / 3 steps) | 39.65 | -0.07 | 0.10 | - | no signal |
| ssgi-ao-off | 39.27 | -0.46 | 0.22 | - | small real signal; AO touches character contact shading |
| volumetric-q1 | 39.71 | -0.09 | 0.43 | - | no signal |
| parallax-shadows-off | 39.55 | -0.37 | 0.41 | - | within noise |
| parallax-off | 38.67 | -1.15 | 0.31 | - | only real non-NR signal; affects world/armor height maps |
| hair-specular-off | 39.70 | -0.16 | 0.34 | - | no signal |
| terrain-blending-off | 39.16 | -0.64 | 0.64 | - | within noise |
| grass-mesh-lod | 39.64 | -0.04 | 0.17 | - | no signal |
| screen-space-shadows-off | 39.28 | -0.35 | 0.31 | - | marginal; SSS shadows face contact |
| wetness-off | 39.53 | -0.01 | 0.04 | - | no signal |

Derived cost model [INF, fitted to 3 measured points, reproduces the measured C45 within 0.06 ms]:
NR centre cost ~ 1.9 + 21.6 * (coverage/100)^2 ms; periphery P50 staggered ~ +5.4 ms; frame ~ 16.28 + NR cost.

| coverage (hybrid, P50, stagger) | predicted GPU ms | status |
|---|---|---|
| 45 | 28.0 | measured 27.89 |
| 55 | 30.1 | measured 30.14 |
| 70 | 34.2 | [INF] |
| 75 | 35.7 | [INF] |
| 80 | 37.4 | [INF] |
| 85 | 39.2 | [INF] ~ break-even with full eye |
| 90 | 41.1 | [INF] worse than full eye |

So the hybrid only pays off for coverage <= ~80; above ~85 plain full eye is cheaper than hybrid. Coverage values outside `{100,95,90,85,80,75,70,55,45}` normalise to **100** silently (`R:\Features\Upscaling\NeuralRendering\RuntimePolicy.h:17` and `:42-48`; one-time warning only, `FR.cpp:666-669`).

Caveats on this evidence (be explicit):
- 2.20.4 code, not 2.21.8. Hybrid composite and stagger code changed in 2.20.4 -> 2.21.x (2.21.3 removed the height-multiple-of-8 gate, `package\OPENNR-2.21.3-CHANGELOG.md`). Re-run on 2.21.8 before treating ms as final.
- One scene (static, `cell` empty), 2 repeats. Display in that session was 2496x2688, guides 1468x1580 (`<RESEARCH_DIR>\major_perf_map_patch_20261008`/2.20.5 notes and the 2.21.8 log line 652). The brief says ~2240x2428; the 2.21.8 log (`CommunityShaders.log:314`) says "Latched display 2496x2688 -> render 1468x1580 per eye (quality mode 2)". Treat 2496x2688 as current.
- Metric is compositor GPU pre+post submit, which overlaps frames (NR-off frame interval is 8.5-9.3 ms while its GPU median is 16 ms). Deltas are meaningful, absolute values are not frame budgets.
- **No quantitative quality metric (LPIPS/SSIM/face crops) for hybrid vs full eye exists in the repo or `<RESEARCH_DIR>`** (grep of the 2.20.3/2.20.4 periphery reports and kit found none; only timing). "Quality risk" below is therefore [INF] plus the fact the user ran C55+P50 staggered as daily config from 2026-10-02 to 2026-10-08.

---------------------------------------------------------------------------
## 2. Recommended defaults (the main table)

Columns: setting | current C++ default | LIVE value | recommended default | evidence | quality risk (detail regions) | confidence.

### 2a. Neural Rendering / foveated route (the only high-value group)

| setting | C++ default (file:line) | LIVE | recommended | evidence | quality risk | confidence |
|---|---|---|---|---|---|---|
| `neuralRenderingCoverage` | 100 (`FR.h:142`) | 100 | **55** | BENCH hybrid c55-p50-stag -9.84 ms vs full eye; crop-only -14.87 | centre 55 % linear keeps full-res NR on face/skin/clothing in the straight-ahead region; off-centre gaze (no eye tracking) falls to P50 NR + DLSS. Medium; user daily-drove it 10/02-10/08 | cost: high; quality: medium |
| `neuralRenderingPeripheryEnabled` | false (`FR.h:154`) | false | **true** | same; periphery NR is what removes the crop-only edge colour/seam (`docs\opennr-2.21.0-review\README.md` "Edge colour") | lowers risk vs crop-only; adds +5.4 ms [MEAS-derived] | high |
| `neuralRenderingPeripheryResolution` | 50 (`FR.h:157`; only {50,58,66,75} accepted, others -> 50, `FR.cpp:678-680`, `RuntimePolicy.h:19-22`) | 50 | keep **50** | P50 stag 30.14 vs P66 stag 32.06 (-1.92 ms) | periphery only | high |
| `neuralRenderingPeripheryStagger` | true (`FR.h:156`) | true | keep **true** | P66: stagger -5.34 ms (37.40 -> 32.06) | alternating-eye periphery update; ghosting risk noted in `<RESEARCH_DIR>\PROGRESS_20261002.md` ("periphery stagger (2.20.3+ only)") but fixed F10/F11 in 2.20.4 | high cost / medium quality |
| `neuralRenderingPeripheryLightingGain` | 0.83 (`FR.h:158`) | 0.83 | keep (UI auto-sets 0.83/0.86/0.89/0.93 per resolution, `FR.cpp:~1505-1509`; a JSON-edited resolution does NOT get the matching gain) | BENCH variants use 0.83 for P50, 0.89 for P66 | colour match | high |
| `neuralRenderingPeripheryFeatherWidth` | 160 (`FR.h:159`; clamp 2-256 `FR.cpp:682`) | 160 | keep | 2.21.0 changelog: "Raise the default Neural Rendering centre blend width to 160 px" | - | high |
| `neuralRenderingProjectionCenteredCrop` | false (`FR.h:145`; comment "Presets opt in") | false | **true** (must pair with coverage < 100) | every hybrid BENCH variant sets it true; crop-only sets false; `INT.cpp:1164` only acts when `coverage < 100 && fullEye` | aligns crop to lens centre; wrong crop = detail region off-axis. Offsets default 0 (`FR.h:146-151`) | medium-high |
| crop preset (`subrectController` seed) | **"Center 75%"** (`FR.cpp:533`) | Full Eye | **Full Eye** (`kPresetFullEye`) | hybrid/coverage need `IsFullEye()` (`INT.cpp:1133,1250-1255`, `Subrect.h:31`); Center 75% would also engage DLSS-foveated stretch path (`Modes.cpp:116-121`) | Center 75% gives NR on the 75 % region only and a stretched periphery without NR | high |
| `subrectBlendMode` | 1 Feather (`FR.h:127`) | 1 | **2 Dither** (user-tuned) | all backups 10/01-10/08 have 2 | quality/seam taste, not perf. The 2.21.0 review flags dither noise re-randomising each frame as the shimmer source (`docs\opennr-2.21.0-review\02-seam-and-colour-analysis.md`, backlog P1-1..3), so this is a preference call | low (quality), cost-neutral [INF] |
| `subrectDitherStrength` | 1.0 (`FR.h:134`; clamp 0-2 `FR.cpp:652`) | 1.0 | **1.45** (user-tuned) | all backups | as above | low |
| `subrectFeatherWidth` | 64 (`FR.h:129`; **hard clamp max 128**, `FR.cpp:650`; UI slider max 128 `FR.cpp:1530,1534`) | 64 | **128** (the maximum) | all backups | as above. Note changelog "160 px" refers to the periphery feather (160), not this field | low |
| `subrectMaskMode` | 1 Oval (`FR.h:128`) | 1 | keep | - | - | high |
| `subrectFalloffCurve` | 1.0 (`FR.h:133`) | 1.0 | keep | - | - | high |
| `neuralRenderingTemporalReuseCadence` | 0 off (`FR.h:206`; only 0/2/3/4 accepted `FR.cpp:~724-727`) | 0 | keep 0 until measured | **no BENCH variant for it**; header says experimental, "moving crop origins remain disabled" (`FR.h:202-205`) | temporal reprojection of NR detail = risk to faces in motion | low (unmeasured) |
| `neuralRenderingModelResolution` | 100 (`FR.h:139`) | 100 | keep (cannot change) | forced to 100 at load (`FR.cpp:683-690`, `RuntimePolicy.h:14`) | - | high |
| `neuralRenderingIntensity / LocalTone / LocalStructure` | 1.70 / 1.00 / 1.70 (`FR.h:161-163`) | 1.7/1.0/1.7 | keep | match LIVE; stale `SettingsDefault.json` has 1.9/1.5/1.5 | not a perf knob | high |
| `neuralRenderingStyle` | 0 Natural (`FR.h:165`) | 0 | keep | stale default file has 2 | - | high |
| `neuralRenderingPeripheryGuidedFilter` | false (`FR.h:155`) | false | keep (dead, see section 4) | hard-gated by `kPeripheryGuidedFilterQualified = false` (`INT.cpp:98,1549`) | - | high |
| `qualityMode` (DLSS ratio) | 1 Quality (`Upscaling.h:82`) | 2 Balanced | **leave to user** (not a safe default change) | LIVE 2 -> render 1468x1580 for 2496x2688; Quality mode would raise render cost. Not benchmarked | lower render ratio is not "NR-neutral": NR guides are render-res (`guides=1468x1580`) | n/a |
| `presetDLSS` | 0 (`Upscaling.h:99`) | 0 | keep | BENCH J +0.17, E +0.02 ms: no difference | - | high |
| `sharpnessEnabledDLSS` / `sharpnessDLSS` | false / 0.0 (`Upscaling.h:97-98`) | same | keep | extra RCAS pass avoided by design (comment `Upscaling.h:94-95`) | - | high |
| `renderAtUpscaleRes` (PerfMode) | true (`Upscaling.h:109`) | true | keep | - | - | high |

### 2b. Other rendering features (small wins only; defaults match LIVE unless stated)

All C++ defaults below equal LIVE; "recommended" is "keep" except where noted. None of these may be changed on BENCH evidence alone because each gain is <= 1.15 ms and mostly inside uncertainty.

| setting | C++ default (file:line) | LIVE | recommended | evidence | quality risk | confidence |
|---|---|---|---|---|---|---|
| SSGI `Enabled`, `EnableGI`, `NumSlices`, `NumSteps` | true, false (VR AO-only), 2, 4 (`R:\Features\ScreenSpaceGI.h:102-112`) | same | keep (optionally AO off = -0.46 ms +/-0.22 as an opt-in profile) | BENCH ao-off -0.46; ao-lite -0.07 (no gain) | AO off removes contact shading on clothing/neck/face; do not default | medium |
| SSGI `ResolutionMode` | 1 half (`ScreenSpaceGI.h:116`) | 1 | keep | already half-res | - | high |
| SSGI `UseStereoReproject` | true (`ScreenSpaceGI.h:~134`) | true | keep | skips eye-1 march | - | high |
| Screen Space Shadows `Enable`, `SampleCount`, `EnableFoveated` | 1, 1, 0 (`R:\Features\ScreenSpaceShadows.h:33-35`; `ShadowContrast` 4.0 in VR :32) | same | keep; `EnableFoveated` is **inert under Full Eye** (section 4) | BENCH SSS off -0.35 +/-0.31 | contact shadows on faces; keep | medium |
| Volumetric Lighting Q | 2 / 2, both enabled (`R:\Features\VolumetricLighting.h:18-22`) | same | keep | BENCH q1 -0.09 +/-0.43 = no gain | god-ray softness only | high |
| LLF shadow caster scheduler | enabled, 16 lights, atlas 8192, redraw 16/5 ms, formula `1 + isinterior` (`R:\Features\LightLimitFix\ShadowCasterManager.h:209-245`, `:221,230,261`) | same | keep | BENCH formula / cap: -0.07 / -0.06 (no signal in this scene; scene-dependent; interiors not tested) | shadows on characters | medium. Candidate separate A/B: 4096 atlas (`<RESEARCH_DIR>\major_perf_map_patch_20261008\PERFORMANCE_AUDIT.md` row "Visual/resource defaults") |
| Extended Materials parallax | `EnableParallax` 1, `EnableShadows` 1 (`R:\Features\ExtendedMaterials.h:30-34`) | same | keep; parallax is the single biggest non-NR lever (-1.15 +/-0.31) | BENCH | affects world and some armor/clothing height maps; offer as "Performance" profile, not default | medium |
| Terrain Blending | 1 (`TerrainBlending.h:35`) | 1 | keep | -0.64 +/-0.64 = noise | - | high |
| Hair Specular | enabled (`HairSpecular.h:36`) | same | keep | -0.16 | hair quality | high |
| Wetness Effects | all on (`WetnessEffects.h:35-78`) | same | keep | -0.01 | - | high |
| Grass Optimizations | Beta -> disabled at boot (`GrassOptimizations.ini`, `Feature.h:180`); `EnableMeshLOD` false (`GrassOptimizations.h:50`) | **enabled** (user turned it on) | n/a; mesh LOD -0.04 = no gain | BENCH | - | high |
| Skylighting | `ProbeGridQuality` 2 (256x256x128 legacy), `EnableIncrementalProbeUpdates` = VR, `EnableReducedUpdateFrequency` = VR, intervals 2/2, `EnableInInteriors` false (`R:\Features\Skylighting.h:54-69`) | same | keep; **no BENCH variant exists**; add one (probe quality / update interval) | unmeasured; interior-black history in `PROGRESS_20261002.md` | ambient sky occlusion on everything incl. characters | n/a |
| VR `EnableDepthBufferCullingExterior/Interior` | true / true (`R:\Features\VR.h:144-145`) | true / true | keep | already on | - | high |
| VR StereoOptimizations `StereoMode` | Off (`VRStereoOptimizations.h:81`) | Off | keep Off | un-benchmarked; changes shading for eye 1 | stereo disocclusion artifacts | n/a |
| VRS | `vrEnableVRS` 1 (`R:\Features\VRS.h:46`) but feature is **Beta -> disabled at boot** (`VRS.ini`: `Beta = True`; `Feature.h:180`; log line 129 "Feature 'VRS' is disabled at boot") | `vrEnableVRS` 1, Disable-at-Boot VRS true | keep disabled | VRS shades periphery coarser, which the NR periphery would then consume; un-benchmarked | NR input quality | n/a |
| Subsurface Scattering | `EnableCharacterLighting` false, `SSMode` 1, `BurleySamples` 16 (`SubsurfaceScattering.h:29-35`) | same | keep | face/skin quality; no BENCH variant | high | n/a |
| Advanced Skin | Beta -> disabled at boot | disabled | keep disabled (user choice) | face/skin quality | - | n/a |

### 2c. Diagnostics / overhead switches

| setting | C++ default (file:line) | LIVE | recommended | evidence | quality risk | confidence |
|---|---|---|---|---|---|---|
| `Advanced/Frame Annotations` | false (`State.h:285`) | **true** | **false** | when true, `Install` adds `BSShader_SetupGeometry/RestoreGeometry` for Lighting, Effect, Water, Utility, plus `RenderBatches`, `RenderPlayerView` etc. (`FrameAnnotations.cpp:868-889`); per draw: 2x `std::format_to`, a wide-string copy, `ID3DUserDefinedAnnotation::BeginEvent` (`FrameAnnotations.cpp:432-447`, `State.cpp:1281-1291`). Needed only for RenderDoc/PIX naming and NR profiler row names | none | high (cost unmeasured, [INF] CPU only) |
| `Performance Overlay` block | `ShowInOverlay` true (`R:\Features\PerformanceOverlay.h:287`), others false (:288-291 except FPS) | `ShowInOverlay` false; `ShowCSPasses/ShowDrawCalls/ShowVRAM` true | **entire block is inert in 2.21.8**: CMake option `OPENNR_PERFORMANCE_OVERLAY` default OFF (`R:\..\CMakeLists.txt:97-101`), 2.21.8 build cache has OFF (`<BUILD_DIR>\2.21.8-map\build-release\CMakeCache.txt`), so `IsOverlayVisible()` returns false (`PerformanceOverlay.h:130-134`) and `ShowInOverlay` is forced false on load (`PerformanceOverlay.cpp:221-223`); the tab is compiled out (`Menu\PerformanceRenderer.cpp:329`). Raise to code default false anyway so an overlay-ON dev build does not draw by default | none | high |
| Profiler (`Profiler::userEnabled`) | false (`Profiler.h:328`) | n/a | keep false | idle: one atomic check per pass; capture requested only while overlay/menu page asks (`PerformanceOverlay.cpp:1615`, `ProfilingRenderer.cpp:393`) | - | high |
| Tracy | `TRACY_SUPPORT` OFF (`CMakeLists.txt:70`; cache OFF) | - | keep OFF | zones compiled out | - | high |
| `mapProbeDiagnostics` | false (`Upscaling.h:113`) | false | keep false | MapProbe lines (976 "target bind", `CommunityShaders.log` 23:42) appear because the probe was active in the last session even though the saved value is false; LIVE was saved 21:23, session started 23:37, so it was toggled in-session [INF]. Gate: `FrameAnnotations.cpp:371` | - | medium |
| Streamline log | 0 Off (`Upscaling.h:93`) | 0 | keep | - | - | high |
| Skylighting diagnostics | false (`Skylighting.h`, `EnableDiagnostics`) | false | keep | - | - | high |
| OpenNR Capture | `enableCapture` false (`OpenNRCapture.h:34`); feature Beta -> disabled at boot; compiled into 2.21.8 (`BUILD_OPENNR_CAPTURE=ON` in cache) | disabled at boot | keep; optional `BUILD_OPENNR_CAPTURE=OFF` for the shipping package | removes capture hooks from the DLL; benefit unmeasured | - | low |
| RenderDoc | `enableCapture` false (`RenderDoc.h:128`) | false | keep; `renderdoc.dll` ships in `mods\OpenNR 2.21.8\Renderdoc\` but is not loaded unless enabled (`RenderDoc.cpp:44-47`) | - | - | high |
| Developer Mode / Dump Shaders / FileWatcher | false / false / false (`State.h:65`, `ShaderCache.h:1204-1206`) | same | keep | - | - | high |
| Log Level | info (2) (`State.h:64`) | 2 | keep; but `flush_on(info)` flushes every info line (`State.cpp:986-989`). Event-based logs only, ~1.7k lines/session | negligible except map probe bursts | - | high |
| Settings Benchmark | idle, no autostart; `Tick()` takes a mutex and tests phase each frame, returns when idle (`SettingsBenchmark.cpp:909-916`; `<RESEARCH_DIR>\major_perf_map_patch_20261008\PERFORMANCE_AUDIT.md` row "Settings Benchmark") | - | keep | - | - | high |
| Reflex / FG / frame limit | all off or VR-dead | - | keep | section 4 | - | high |
| Partial Precision (fxc `D3DCOMPILE_PARTIAL_PRECISION`) | false (`State.h:296`; the comment above it says "On by default" - stale) | false | **candidate for an A/B**, do not default blind | un-benchmarked; changes shader precision of lighting/skin | banding / precision on skin and lighting possible | n/a |

---------------------------------------------------------------------------
## 3. Discrepancy list across the four sources

(a) = C++ default, (b) = shipped package, (c) = LIVE, (d) = BENCH/measurements.

| # | item | (a) C++ | (b) package | (c) LIVE | (d) measured | note |
|---|---|---|---|---|---|---|
| 1 | NR coverage | 100 (`FR.h:142`) | benchmark JSON c55 variants only | 100 | c55+P50 stag best | defaults are the slow path |
| 2 | Periphery NR | off (`FR.h:154`) | benchmark sets on | off | -9.84 ms with it | |
| 3 | Projection-centred crop | false (`FR.h:145`) | benchmark sets true for hybrid | false | - | |
| 4 | Crop preset | Center 75% (`FR.cpp:533`) | - | Full Eye | hybrid needs Full Eye | fresh install cannot use hybrid |
| 5 | Edge blend | Feather / 1.0 / 64 | - | Feather / 1.0 / 64 | - | user tuned = Dither / 1.45 / 128 (backups) |
| 6 | Recommended button | `ApplyRecommendedSettings` = defaults + coverage 100 (`FR.cpp:328-334`) | changelog 2.21.5: "Recommended preset that restores shipped defaults" | result of pressing it | - | it deleted the tuned config |
| 7 | Frame Annotations | false | - | **true** | - | |
| 8 | Perf overlay settings | `ShowInOverlay` true | overlay compiled out | false (+ other keys) | - | inert block |
| 9 | `SettingsDefault.json` | n/a | not shipped in package | stale, version 2-10-1-0 | - | loaded as base layer |
| 10 | SSGI slices/steps | 2/4 VR (`ScreenSpaceGI.h`) | - | 2/4 | - | stale default file says 3/6 |
| 11 | `qualityMode` | 1 Quality (`Upscaling.h:82`) | profiles: Balanced profile = 2 (`Upscaling.cpp:~410-420`) | 2 | - | user choice; stale file 1 |
| 12 | `vrEnableVRS` | 1 (`VRS.h:46`) | VRS.ini Beta -> off at boot | 1 but VRS boot-disabled | - | dead value |
| 13 | Benchmark `repeats` | 1 (shipped JSON) | 1 | - | run used 2 | |
| 14 | Changelog vs code | 2.21.0 "centre blend width 160 px" | - | `subrectFeatherWidth` 64 | - | 160 is `neuralRenderingPeripheryFeatherWidth`; `subrectFeatherWidth` max 128 |
| 15 | Partial precision comment | code false, comment says on (`State.h:296`) | - | false | - | stale comment |
| 16 | Display size | - | - | 2496x2688 per eye (log `:314`) | brief says ~2240x2428; review pack used 2240x2428 | confirm; 2428 was the old height-%8 gate case, already removed in 2.21.3 (`INT.cpp:1249` now checks width only) |
| 17 | Fresh-install enabled features | Beta/Alpha features start disabled (`Feature.h:180`, `State.cpp:750-752`): GrassOptimizations, OpenNRCapture, Skin, UnifiedWater, VRS (Beta), PostProcessing (Alpha) | `.ini` flags in `mods\OpenNR 2.21.8\Shaders\Features\` | GrassOptimizations enabled by user; rest disabled | - | ExponentialHeightFog is `Unreleased` (hidden) |

---------------------------------------------------------------------------
## 4. Inert / dead settings

Verified by reading the gating code.

**Dead (can never take effect in this build/route):**
- `neuralRenderingModelResolution` (forced to 100 at load): `FR.cpp:683-690`, `RuntimePolicy.h:14` (`kFullResolutionNeuralRenderingOnly = true`).
- All `neuralRenderingAdaptive*` incl. `AdaptiveEnabled`, `AdaptiveCropEnabled`, `RefreshHz`, `TargetFps`, `MinimumResolution`, `Down/UpshiftFrames`, `MinimumDwellFrames`, `GuardTimeMs`, `AdaptiveCrop{Max,Min}imumCoverage`, `...CropDownshift/Upshift/Dwell/Transition`: `AdaptiveEnabled` and `AdaptiveCropEnabled` are forced false on every load/clamp (`FR.cpp:683-690`). They are also still round-tripped in JSON (`FR.cpp:~448-465`) and have clamps (`FR.cpp:728-790`).
- `neuralRenderingPreUpscale` (forced 0, same block). `neuralRenderingMultiPass` is read but gated away for pre-upscale and cropped VR paths (`FR.h:198-201`, `INT.cpp:645`): effectively dead for the recommended crop route.
- `neuralRenderingPeripheryGuidedFilter` (`INT.cpp:98,1549`, `kPeripheryGuidedFilterQualified = false`; UI removed in 2.21.5).
- Frame generation family (`frameGenerationMode`, `frameGenerationForceEnable`, `frameGenerationAllowInMenus`, `preferFSRFrameGen`, `dlssgFramesToGenerate`) and `frameLimitMode`: the DX12 proxy/FG path is only taken when `!isVR` (`Upscaling.cpp:138`, `:2472-2473`). `frameLimitMode` is only consulted on the D3D12 swapchain path (`Upscaling.cpp:2388`). `fsr4Runtime*` and `sharpnessFSR` only matter when FSR is the method (LIVE `upscaleMethod` 3 = DLSS).
- Entire `Performance Overlay` settings block, because the 2.21.8 DLL is built with `OPENNR_PERFORMANCE_OVERLAY=OFF` (see 2c).
- `VR/EnableSSRFoveation*`, `Screen Space Shadows/EnableFoveated`, and any foveated-SSR/SSS consumer: they read `FoveatedRender::GetFoveationProfile()` which returns `available=false` for a full eye (`FR.cpp:1087-1104`), and it derives from the DLSS crop UVs, **not** from the NR coverage. With the Full Eye preset (required for the hybrid) they are always off even if ticked.
- `Variable Rate Shading/*`, `Advanced Skin/*`, `Unified Water/*`, `Post Processing/*`, `OpenNR Capture/*` while their features are disabled at boot (Beta/Alpha default; LIVE `Disable at Boot` true for VRS, Skin, UnifiedWater, PostProcessing, OpenNRCapture). They are not loaded (`State.cpp:598-602`).
- `Effects11` (`presetLocation`): "stays inert without a preset" (`R:\Features\Effects11\EffectManager.cpp:107`); log shows `enbeffect.fx` not found, so no ENB-like post runs.
- `Remote Control` DevBench bridge: the log shows "Failed to dispatch message to devbench", i.e. nothing is listening.

**Dead only under the Full Eye preset (needed for hybrid):**
- `dlssMode` (Faster), `stretchMode`, `peripheryBlurRadius`, `peripheryAAMode`, `peripheryTemporalAlpha`, `debugVisualize` red tint: `ExecuteDefaultMode` skips background stretch and copy-back for a full eye (`R:\Features\Upscaling\FoveatedRender\Modes.cpp:116-121`, `:346`). They only matter with a crop preset. `dlssMode = kFaster` is also incompatible with NR (`FR.cpp:~866`, `nrEligible` requires `GetDlssMode() == kDefault`).

**Silently normalised (value in file is not the value used):**
- Coverage outside `{100,95,90,85,80,75,70,55,45}` -> 100 (`FR.cpp:666`).
- Periphery resolution outside `{50,58,66,75}` -> 50 (`FR.cpp:678-680`). Note 58 and 75 exist in code and UI but not in the shipped benchmark.
- `subrectFeatherWidth` > 128 -> 128 (`FR.cpp:650`); `subrectDitherStrength` > 2 -> 2 (:652); `neuralRenderingTemporalReuseCadence` not in {0,2,3,4} -> 0.
- `ModelResolution` any -> 100.

**Disabled but still doing work (per-frame):**
- `FoveatedRender::UpdateAdaptiveState` runs every DLSS-route frame (`Modes.cpp:61`) and, whenever NR is eligible in VR, calls `IVRCompositor::GetFrameTiming` once per engine frame (`FR.cpp:927-934`) even though adaptive NR is hard-disabled and the result is unused. [INF] microseconds, but it is dead work behind a setting that cannot be on.
- Frame Annotations hooks (see 2c) when the boot value is true.
- `Settings Benchmark::Tick` mutex acquire each frame (`SettingsBenchmark.cpp:909-916`), idle.
- `OpenNRCapture` code is compiled in (`BUILD_OPENNR_CAPTURE=ON`) and `nrEligible` reads `openNRCapture.settings.enableCapture` (`FR.cpp:864-872`); negligible.

---------------------------------------------------------------------------
## 5. Concrete default-change list (what to set, ordered by value)

1. `FoveatedRender::Settings` (`FR.h`): `neuralRenderingCoverage = 55`, `neuralRenderingPeripheryEnabled = true`, `neuralRenderingProjectionCenteredCrop = true`. Keep `PeripheryResolution = 50`, `Stagger = true`, `LightingGain = 0.83`, `FeatherWidth = 160`. Expected (2.20.4 MEAS) -9.84 ms GPU vs today, NR cost -41 %.
2. `FR.cpp:533`: seed default crop `kPresetFullEye` instead of `kPresetCenter75` (otherwise (1) is refused with term `fullEye`).
3. `ApplyRecommendedSettings` (`FR.cpp:328-334`): make "Recommended" apply the hybrid set in (1) instead of coverage 100, or the button keeps erasing the optimum. Also stop it from resetting the user's crop-preset-dependent route silently (log what changed).
4. Edge blend defaults to the user's tuned values if the user still wants them: `subrectBlendMode = 2`, `subrectDitherStrength = 1.45`, `subrectFeatherWidth = 128` (quality preference; perf-neutral [INF]).
5. User profile: set `Advanced/Frame Annotations = false` in LIVE (requires restart; hooks are boot-installed).
6. Package hygiene: delete or regenerate `<MO2_INSTANCE>\overwrite\...\SettingsDefault.json` (version 2-10-1-0) so the base layer matches 2.21.8; ship no copy in the package.
7. Optional "Performance" profile (not default): parallax off (-1.15 +/-0.31), SSGI AO off (-0.46 +/-0.22), C45 (-2.25). Do not default: each touches character/clothing/lighting detail or is below measurement noise.
8. Re-benchmark on 2.21.8 with these extra variants added to `OpenNR-SettingsBenchmark.json` (none exist today): C70/C75/C80 hybrid P50 stag (verify the cost model and the ~85 % break-even), P58 stag, projection-centred on/off at C55, `TemporalReuseCadence` 2 with `StaggerEyes`, Skylighting probe quality/update interval, LLF atlas 4096, Frame Annotations on/off (CPU), and `repeats` 3. Run in at least one interior and one populated exterior scene; record `cell`.

---------------------------------------------------------------------------
## 6. What I did not / could not verify

- No 2.21.8 timing of anything. All ms are 2.20.4, one scene.
- No image-quality metric for hybrid vs full eye; "quality risk" is reasoning plus the user's own week of use.
- Whether the installed `CommunityShaders.dll` (mtime 2026-10-08 23:12, newer than its PDB at 21:09) is exactly the `2.21.8-map\build-release` build; I read the CMake cache of that build dir, not the DLL.
- MO2 game INIs (`skyrimprefs.ini`: `iShadowMapResolution=4096`, `fShadowDistance=6000`, `iBlurDeferredShadowMask=3`) are outside the Community Shaders defaults; not benchmarked.
- Per-feature settings for features not listed in section 2b (Cloud Shadows, Sky Sync, Terrain Shadows, Wind, Procedural Sun, Grass Collision, etc.) were not read for cost; all LIVE values match their absence from `SettingsDefault` diff except Wind/Procedural Sun/Grass Optimizations which are new blocks.
