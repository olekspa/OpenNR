# OpenNR 2.21.8 log/telemetry audit (session 2026-10-08 23:37-23:42)

Source: CommunityShaders.log (1678 lines, plugin v2-21-8-0). Counts: 1639 info, 36 warn, 3 error. No D3D validation errors, no shader compile failures, no crash in this session. Latest crash log is crash-2026-10-07-22-45-17.log (prior build, see issue 4). SKSE sibling logs: only warning is ImGuiVRHelper "Failed to dispatch message to devbench" (same as CS). No GPU-ms / overlay rows are in the log (see Timing). Steam screenshots 23:42:13/14 are the map (see Screenshots).

Paths: src = <REPO>\runtime\open-shaders\src

## Ranked issues

### 1. REAL BUG: map display-resolution route never applies (routedDisplayRes=false on every draw) - gated on the wrong predicate
Evidence:
- `23:38:33.429 [MENU] presentation mode requested=2 effective=2 helper=true desktop=true vr=false input=desktop` (Menu.cpp:280). requested=2 = DesktopOnly, so `vrDrawEnabled=false`.
- `[PerfMode][Map] size-chain mapTarget=4992x2688 ISCopySource=4992x2688 ISCopyDestination=4992x2688 displayRouteApplied=false` at 23:42:01.453 and 23:42:31.459.
- Every imagespace draw: `routedDisplayRes=false`; closing line `effect count shader=ISCopy raw=8738 draws=392 routedDisplayRes=0`, `ISMap/ISWorldMap draws=0`.
- `ISCopy ... viewport=(0,0,2048,2048)` at 23:42:31.459 while ISCopyDynamicFetchDisabled is 4992x2688 (the 2048x2048 copy is unrouted).
- `open readiness hookActive=true fakeDSV=true renderer=true shadowState=true stateUpdateFlags=true` so resources are fine; the failure is a policy/predicate.
Cause: both route gates pass `Menu::GetSingleton()->ShouldRenderVRMenu()` (= `vrDrawEnabled`) as `a_vrPresentationEnabled`: FrameAnnotations.cpp:490-492 (`ShouldRouteMapCopyAtDisplayResolution`), PerfMode/PostIntercept.cpp:218, DSSwap.cpp:204 (`ShouldRouteMapAtDisplayResolution`, via Menu::ShouldRenderVRMenu at Menu.h:600). The user runs the CS menu in DesktopOnly (VRMenuPresentation=2), so `vrDrawEnabled` is permanently false and the map route is dead regardless of the 2.21.8 timing fix. The predicate (menu presentation of the ImGui overlay) is unrelated to map rendering. The 2.21.8 follow-up (removing the live-RTV check, size-chain now logged after func() at FrameAnnotations.cpp:511-518) is visible in this log (size-chain prints after the ISCopy draw), so this run exercised the rebuilt package and still failed.
Fix: drop `ShouldRenderVRMenu()` from the map route gates (pass `true`, or replace by a real condition such as `globals::game::isVR`); update MapMenuDisplayResolutionPolicy.h:45-58 and its tests (parameter a_vrPresentationEnabled). Verify next log shows `displayRouteApplied=true` and `routedDisplayRes=true`, and ISCopy viewport 4992x2688. Also add a one-shot log of each route-gate input when the first ISCopy draw is denied (route-skipped reason), so this class of silent-off is visible (cf. memory note on periphery gate silent-off).

### 2. REAL (log spam): `[MapProbe] target bind #N` 976 lines in ~9.4 s (23:42:31.423 - 23:42:40.804)
Single message, 976 repeats, plus 20 `imagespace draw` lines and the one-time resources lines; 59% of all non-startup log volume (and ~half the file). Each line is built with D3D GetDesc/RSGetViewports queries plus a `FlushMapProbeLine()` (flush per line) on the render thread, inside the map open, i.e. on the frame path of a VR map that is already reprojecting.
Source: FrameAnnotations.cpp:318 (LogMapProbeRenderTargetBindUnsafe, 270-330), no cap on `mapProbe.targetBinds`. Imagespace draw log is capped at 20 (loggedImageSpaceDraws) but target binds are not.
Fix: cap at ~20 records per map-open (mirror imagespace cap) and keep only a counter in the `close` summary; or compile the probe out / gate behind a debug setting for release packages. Remove FlushMapProbeLine per record.

### 3. REAL (minor): MapProbe invalid-index warnings expose the same bad state that crashed on 10-07
- `23:42:31.420 [W] [MapProbe] invalid index kind=render-target value=0xFFFFFFFFFFFFFFFF bound=114; probe record skipped`
- `23:42:31.423 [W] [MapProbe] invalid index kind=depth-stencil value=0xFFFFFFFF bound=21; probe record skipped`
Source: FrameAnnotations.cpp:159 (LogInvalidMapProbeIndexOnce), callers at ~257-290. At the first map frame the shadow-state `renderTargets[0]` is -1 (unset), then valid on later binds. The guard works (TryIndex), no fault. Context: crash-2026-10-07-22-45-17.log (build from worktree opennr-2-20-10-map-resolution, `CommunityShaders.dll+084DA76 mov rcx,[rax+rdx*8+0xA48]`, access violation reading 0xFFFFFFFFFFFFFFFF) is consistent with indexing the renderer arrays by the same 0xFFFF... sentinel; the 2.21.8 guard fixes the probe. Audit other unguarded `renderTargets[runtime.renderTargets[0]]` / `depthStencils[runtime.depthStencil]` sites: PerfMode/DSSwap.cpp:216-231 (`depthStencils[sourceDS]` is reached only after `CanRouteMapCopyWithDepthState`, which rejects UINT32_MAX for depth but `mapMenuSavedRTValid` handles RT; OK) and FrameAnnotations.cpp resources path (~225-245).
Fix: keep as is; downgrade to info/debug since expected on first draw; confirm there is no other unguarded index.

### 4. REAL (design): renderer resource initialisation runs twice (about 12 s of startup)
Evidence: second full pass on thread 31184: `TypedUAVLoad probing` (23:38:13.420 then 23:38:25.431), `TruePBR All .jsons loaded` x4 each pass, `SSGI resource profile` (23:38:19.120 then 23:38:31.512), `TextureManager Created downsample texture` (23:38:20.525 then 23:38:32.580), `EFFECTS11 created ...` (23:38:20.538 then 23:38:32.586), `VRStereoOptimizations Resources created` (23:38:21.384 then 23:38:33.382), `PerfMode][Map] Created display-resolution fake depth view 4992x2688` x2, `Streamline`/FidelityFX do not repeat. `Calling original Init3D` only once. The second pass follows the first by exactly ~12.0 s on a different thread id (31184 vs 39124). Likely resources re-created from a second SetupResources/PostPostLoad entry (device or render-thread restart) - verify whether it is a second `Feature::SetupResources` dispatch (Features loop) or a VR submit/hook re-entry.
Also all of the `EFFECTS11 enbeffect.fx` errors/warnings below appear twice because of this.
Fix direction: find the caller of the SetupResources fan-out (State.cpp / Feature setup loop; grep `SetupResources` in Features/*.cpp and Hooks.cpp) and add a one-time guard; cheap win is ~12 s startup and a duplicate ~500 MB(?) of allocations (not logged - add VRAM line, see issue 8).

### 5. Noise that should be silenced or fixed at source (errors that are not errors)
| Count | Level | Message | First/last | Source | Verdict |
|---|---|---|---|---|---|
| 2 | E | `[EFFECTS11] Required effect file not found: 'enbeffect.fx'` | 23:38:20.548 / 23:38:32.594 | Features/Effects11/Effects/Effect.cpp:326 | noise: no ENB preset is installed on purpose; log as info when preset dir is absent |
| 2 | W | `[EFFECTS11] No preset in use: 'enbeffect.fx' not found` | same | Features/Effects11/EffectManager.cpp:109 | noise |
| 2 | W | `[SettingManager] Settings file not found: , using defaults` (empty path) | 23:38:20.526 / 23:38:32.580 | Features/Effects11/SettingManager.cpp:634 | harmless, but the empty path suggests an unset preset name |
| 2 | W | `[WeatherManager] _weatherlist.ini not found at _weatherlist.ini` | same | Features/Effects11/WeatherManager.cpp:29 | noise (path is a bare filename) |
| 1 | E | `Legacy FullScreenBlur stage-count opcode contexts do not match the verified 1.5.97/1.6.1170 sequences; no blur adapters installed` (23:37:28.861) | | ShaderTools/LegacyGraphicsCompatibility.cpp:568 | benign for SkyrimVR 1.4.15 (build is not 1.5.97/1.6.1170); demote to info for VR |
| 1 | W | `Legacy shadow bounds viewport adapter unavailable for VR address library; skipping` (23:37:28.861) | | LegacyGraphicsCompatibility.cpp:493 | benign |
| 14 | W | `<worldspace> has more than one height maps!` (Blackreach, Deadlands, QN, DeepwoodRedoubt, FalmerValley, SoulCairn, HunterHQ, Apocrypha, Solstheim, JaphetsFolly, Markarth, Skuldafn, Sovngarde, Tamriel; 23:38:28.432-433; the 14 matching loads at 23:38:16.67 are the info lines) | | Features/TerrainShadows.cpp:275 | repeats the `Loaded` info; the warn only fires in the second init pass (issue 4); content issue (duplicate .dds), not code |
| 1 | W | `Failed to dispatch message to devbench` (23:37:28.875) and ImGuiVRHelper copy | | grep produced no hit in this tree (message likely from CommonLib/ImGuiVRHelper plugin) | noise |
| 2 | W | `[I18n] No supplemental CJK font path candidates for locale 'es' / 'fr'` (23:38:21.426) | | Menu/ThemeManager.cpp:552 | noise: only warn for CJK locales |
| 2 | W | `Failed to load INISettingCollection setting bAutoWaterSilhouetteReflections:Water` / `bForceHighDetailReflections:Water` (23:37:26.489-490) | | Hit not found in src (grep -rn on that string failed; likely in Utils/Game or Globals) | VR 1.4.15 does not have these INI settings; the `[DC] Changing ...` info lines at 23:39:19.590 show them being forced; benign |
| 1 | W | `[Terrain Helper] LandscapeDefault EDID texture set from TerrainHelper.esp not found. Terrain helper is disabled.` (23:39:19.590) | | Features/TerrainHelper.cpp:17 | benign/content |
| 7 | W | `[TruePBR] ShadowMark{EmptyWood,LootWood,DangerWood,ProtectedWood,LootStone,CacheWood,SafeStone}01 missing required PBR texture(s); skipping decal` | 23:41:06.868 / 23:41:08.983 | TruePBR.cpp:75 | content issue from a Shadow Mark mod (decals lack PBR textures); not engine bug |

### 6. NR route status (as logged; there is no per-frame NR status line)
- `23:38:12.911 [PerfMode] Latched display 2496x2688 -> render 1468x1580 per eye (quality mode 2, preset-derived)` (single latch line).
- `23:39:53.388 [DLSSNR][ADAPTIVE] frame=2857 ... configuredCrop=100 adaptiveCropMax=85 ... nrEligible=true eligible=false cropEligible=false eyeTracking=false` (single line, truncated by my reading).
- `23:39:53.388 [FOVEATED] resource contract changed mode=0 adaptiveEnvelope=false left=1x1 right=1x1; recreating DLSS resources`.
- `23:40:39.692 [DLSSNR] Periphery NR inactive: disabled` -> Integration.cpp:1258, reason string from `peripheryEnabled` false. So periphery/hybrid NR was OFF this session (matches memory note); only NR-full-eye at tier=100.
- `23:40:40.438 D3D12 interop initialized commandContexts=3 cpuFenceWait=backpressure-only`; `initialized version=310.8.0 appId=0x0E658703 api=0x13`.
- `23:40:40.685/705 [DLSSNR] resources eye=0/1 tier=100 guides=1468x1580 color=2496x2688 model=2496x2688 modelPercent=100% modelArea=100.0% passes=1`.
- `23:40:43.028 Feature create slot=0 ... ms=2251.60 result=0x00000001`; `23:40:43.904 slot=1 ... ms=873.63` (Runtime.cpp:491). A 2.25 s + 0.87 s stall creating the NR DLSS features during first gameplay frames. Result 0x00000001 = success presumably.
- `23:40:44.389 LDR output written before UI composite size=2496x2688 edgeBlend=false mode=Feather path=direct-copy gazeCrop=false batchedAsync=true`.
- No "NR-only coverage" line in this log and no `Periphery NR active` line anywhere. NR coverage is not logged when periphery is off.
- Upscaling lines: `23:39:53.009 (Re)creating VR intermediates: per-eye in 1468x1580, out 2496x2688`; `[Upscaling-History] reset consumed reason=queued-load-or-scene-transition route=standard+foveated eyes=both; rebuilding DLSS feature` at 23:39:53.153 and 23:40:35.393 (2 total).
- `Temporal history reset` counts: event 3 (23:39:52.991, 23:40:18.119, 23:40:35.393), overlay closed 1 (23:39:53.260), overlay open 1 (23:40:18.121), menu open 3 (23:41:56.539, 23:42:01.364, 23:42:31.386), menu closed 4 (23:40:39.635, 23:41:57.722, 23:42:17.024, 23:42:40.844). Source Integration.cpp:872. All are event-driven, no per-frame spam.
Issue to note (low): DLSSNR creates DLSS features only at 23:40:40-43 AFTER the first load reset at 23:39:53 (47 s later); NR feature create is deferred to first render, which lands the 3.1 s hitch in gameplay. Consider pre-creating on the loading screen.

### 7. Startup timeline (thread 39124 unless noted)
| Time | Event |
|---|---|
| 23:37:26.353 | CS v2.21.8 loads; feature load 26.458-26.485 etc |
| 23:37:28.914 | hooks installed, `Using disk cache` (2.56 s of hooking/feature load) |
| **23:37:28.914 -> 23:38:05.669** | **GAP 36.8 s: no log activity (game boot / Skyrim data load before Streamline); not CS-attributable** |
| 23:38:05.669 - 23:38:08.232 | Streamline DX11 init 1.18 s, DX12 init 1.23 s, FidelityFX DLLs |
| 23:38:08.232 -> 23:38:12.907 | GAP 4.7 s (DLSS feature check / device creation; probably D3D12 interop + DLSS DLL load) |
| 23:38:12.911 | Latched PerfMode display/render dims |
| 23:38:13.4 | TypedUAVLoad probe (10 formats, all supported), TruePBR jsons |
| 23:38:16.672 -> 19.120 | GAP 3.2 s then 2.4 s (terrain height maps / SSGI profile) |
| 23:38:20.351 - 21.4 | RCAS, SceneSettings, EFFECTS11 (E/W lines), VRStereoOptimizations `mode tex 2936x1580 (full SBS)`, OpenVR detect (SteamVR, compatible), fake depth view 4992x2688, Init3D, theme/icons |
| 23:38:21.957 - 23.818 | BSShader::LoadShaders: Effect 3219 PS, Lighting 9780 PS, Water 2172, Utility 165; all in 1.9 s via disk cache (no compile) |
| 23:38:25 - 23:38:33 | second init pass (thread 31184), see issue 4; includes `[MENU] presentation mode requested=2 ... vr=false` and `ImGuiVRHelper: registered VR overlay combos` at 23:38:33.456 |
| 23:38:33.456 -> 23:39:19.590 | GAP 46.1 s (main menu / save selection) |
| 23:39:19.590 | `Saved disk cache info`, DC INI forcing, TruePBR default land, `Force clearing cache due to bDepthBufferCulling` (info; do verify this does not force a shader cache clear every launch) |
| 23:39:52.991 | first load: DLSSNR history reset, VR intermediates |
| 23:40:12.534 / 23:40:35.291 | `[Skylighting] game load queued a neutral-lit probe and occlusion reset` (x2; two game loads) |
| 23:40:35.951 | SCM shadow atlas: `kSHADOWMAPS 4096x4096 x 8 slices, 32.00 MB/slice -> 256.0 MB (VRAM 5460.7/15227.0 MB used, ShadowLightCount=16)`; `Shadow atlas ready: 8192x8192, base tile 4096, cell 256`; `Static shadow cache ready: 8192x8192`; `Creating shadow texture...` |
| 23:40:36.970 | Skylighting reset `interior=false cell=00000000 worldspace=0001A26F` |
| 23:40:40-44 | DLSSNR init + two feature creates (3.1 s) |
| 23:41:06.360 | `[GRASS OPTIMIZATIONS] HiZ occlusion cull active: 734x395 tiles (1/4 res) in a 768x448 texture, 7 mips, source=LIVE kMAIN copy; screen 2936x1580, ... dynRes ratio 1.000x1.000 lock=0, upscale scale 1.000x1.000` (single line) |
| 23:42:01.364 / 23:42:31.386 | map open #1 and #2 (first map: only `open readiness` + `size-chain`; no probe because `mapProbe.active` is set only on 2nd?) |
| 23:42:40.824 | `[MapProbe] close frame=6360 draws=2353` (2353 draw records, 392 ISCopy) |

Note: boot to first gameplay about 3 min of which log gaps total about 2:41 outside CS control; CS-attributable work is about 3 s hooks + 1 s DLL load + 2 s shaders + 12 s duplicated init.

### 8. VRAM / feature load order
- Feature load order (CS): TruePBR, FoliageLighting, VolumetricShadows, GrassLighting, GrassCollision, GrassOptimizations, ScreenSpaceShadows, ExtendedMaterials, WetnessEffects, LightLimitFix (LLF; `6 matching inis found` particle lights, `Unlocked magic light limit` at 23:39:19.590), DynamicCubemaps, and later ones (lines 61-190). Hooks installed 23:37:28.86-.91 (SCM `Hooks installed (ShadowLightCount=16)`, Skylighting hook `GetPrecipitationOcclusionMapRenderPassesImp`).
- VRAM: only one logged datum: `VRAM 5460.7/15227.0 MB used` at 23:40:35.951 after SCM allocate 256 MB. No allocation logs for Skylighting, DLSSNR (D3D12), VRStereo (mode tex 2936x1580), shadow atlas (8192x8192 plus static cache). Fix: add VRAM-used line after each large allocation (Features/Upscaling/NeuralRendering, VRStereoOptimizations, Skylighting).
- Skylighting: no warnings; dungeon-black cause (per memory) is not visible in this log. Only two `neutral-lit probe and occlusion reset` and one `probe, accumulation, directional-history and occlusion-depth state reset interior=false`. No log of skylighting enable/disable state in interior cells (add one when entering interior).
- LLF, stereo-offset, SCM: no warn/error. `stereo-offset`: no matches. `[VRStereoOptimizations] Resources created: mode tex 2936x1580 (full SBS)` only.
- VR resolutions: render 1468x1580 per eye, display 2496x2688 per eye, full screen/composite 2936x1580 (SBS render) and 4992x2688 (display SBS). Map target 4992x2688 `kMENUBG`, depth `kMAIN` 2936x1580 (wrong size for a 4992x2688 map RT because the route is off; see issue 1).

### 9. In-game timing numbers
- In log: none. Only wall-clock stalls: NR feature create 2251.60 ms and 873.63 ms (23:40:43), `[MapProbe]` map open frame=6164 and close frame=6360 (196 frames in 9.4 s = 20.8 fps map-open average; about 48 ms per frame; frames while the map was open are spam-logging heavy, see issue 2, so this is an upper bound on cost).
- Map open frame 6165 draw timeline: `ISCopy` draw logged at 23:42:31.459 and previous draw at 31.424 = 35 ms gap spanning the ISCopyDynamicFetchDisabled 4992x2688 draw -> ISCopy 2048x2048 draw (first-frame cost).
- From benchmark JSON (E:\..\OpenNR-SettingsBenchmark\benchmark-20261001-150911.json, run 2026-10-01 15:09, two passes): baseline gpuMedian 39.6 ms/frame interval 42.0 ms; nr-full-eye 39.7/42.1; nr-coverage-crop-only 24.7/26.8; nr-hybrid-c55-p66 37.8/45.2 (worse interval than baseline, cpu 15.7); nr-hybrid-c55-p66-staggered 32.3/34.9; c55-p50-staggered 30.3/32.8; c45-p50-staggered 27.7/30.2; nr-off 16.0 gpu/9.3 interval; dlss-preset-j/e 39.8/39.6 (no difference); LLF/ssgi/volumetric/parallax/hair/terrain-blending/grass/SSS/wetness toggles all within +/-0.8 ms (parallax-off -1.0, ssgi-ao-off -0.5). Interpretation: reprojectedFrames=90/90 in every block (frames always reprojected at 42 ms interval = 23.8 fps, i.e. consistently GPU-bound at about 40 ms). nr-off 16 ms GPU but 8-9 ms frame interval shows NR itself is the dominant 24 ms cost; hybrids recover only 8-12 ms.

### 10. Screenshots (newest 6 exist; viewed 3)
- 20261008234214_1.jpg and 234213_1.jpg: map in VR (terrain model with markers, snow ridge, controller). Marker icons are blocky (low-res, aliased edges: consistent with route off, map rendered at 1468x1580 render res and upscaled by the copy) and the terrain has visible polygon seams. Matches issue 1.
- 20261008230518_1.jpg (23:05): water close-up, heavily pixelated shimmer (blocky low-res water normal map / reflection). Earlier session; not attributable from the current log (23:05 predates log boot at 23:37). Possible water render at NR-reduced resolution; low priority.
- 230510_1.jpg and 230509_1.jpg not viewed (same earlier session).

### 11. Build logs (<BUILD_DIR>\2.21.8-map\)
- MAP_REPORT.md says final package: `package-build-final.log` zero ` error `; 7z OK; tests: 286 cases pass; full Python run had same 10 failures + 22 errors as base (missing hlslkit / nlohmann json). Package `OpenNR 2.21.8.7z`, FileVersion 2.21.8.0 (matches log).
- Latest requested log items ("size-chain, ISCopy draw, target-bind") are now available: size-chain mapTarget=4992x2688 ISCopySource=4992x2688 ISCopyDestination=4992x2688 displayRouteApplied=false, ISCopy viewport 2048x2048. Report's next step should therefore be issue 1, not additional timing work.

## Quick summary
Real: (1) map route dead because it is gated on `Menu::ShouldRenderVRMenu()` and user menu mode is DesktopOnly (FrameAnnotations.cpp:490, PostIntercept.cpp:218, DSSwap.cpp:204); (2) MapProbe target-bind spam, 976 lines; (3) first-frame invalid-index guard hit (crash lineage of 2026-10-07); (4) duplicated init pass 12 s later. Noise: ENB/EFFECTS11, legacy FullScreenBlur, height-map duplicates, CJK font, TruePBR decals, devbench, DC INI, TerrainHelper. Open: no GPU ms/overlay rows exist in any current log; periphery NR was disabled this session, so no hybrid/NR-only data.
