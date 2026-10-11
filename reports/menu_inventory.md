# OpenNR 2.21.6 Neural Rendering and Upscaling Menu Inventory

**Scope [SOURCE-VERIFIED]:** This inventory maps the controls drawn by `Upscaling::DrawDLSSNRPage()`, `FoveatedRender::DrawSettings()`, and `Upscaling::DrawSettings()` in the current source. Each line number points to the control call or helper call. For keyed controls, visible labels resolve through the adjacent `kSettingsText` table or the `TKEY` fallback shown in source. A row with multiple source lines lists each control individually in its label cell. Paths are relative to `runtime/open-shaders/src/`.

## Neural Rendering page

| File:line [SOURCE-VERIFIED] | Current label/control | Group | Can turn NR off or make its route unavailable? |
|---|---|---|---|
| `Features/Upscaling.cpp:356` → `Features/Upscaling/FoveatedRender.cpp:1258-1292` | `NEURAL RENDERING | ON/OFF` state button | Master | **Only direct NR off control on this page.** |
| `Features/Upscaling/FoveatedRender.cpp:1336` | Foveated stretch mode | Stretched periphery | No. |
| `Features/Upscaling/FoveatedRender.cpp:1347` | Foveated blur radius (Gaussian stretch only) | Stretched periphery | No. |
| `Features/Upscaling/FoveatedRender.cpp:1352` | Foveated stretched-periphery AA | Stretched periphery | No. |
| `Features/Upscaling/FoveatedRender.cpp:1356` | Foveated history blend (temporal AA only) | Stretched periphery | No. |
| `Features/Upscaling/FoveatedRender.cpp:1428` | NR coverage (%) | Route / eye region | No; changes coverage. |
| `Features/Upscaling/FoveatedRender.cpp:1432` | Projection-centred crop | Route / eye region | No; changes crop behavior. |
| `Features/Upscaling/FoveatedRender.cpp:1436,1439` | Shared crop offset X; Shared crop offset Y | Route / eye region | No. |
| `Features/Upscaling/FoveatedRender.cpp:1444,1447,1450,1453` | Left eye X/Y offset; Right eye X/Y offset | Route / eye region | No. |
| `Features/Upscaling/FoveatedRender.cpp:1458` | Recommended | Route | No; applies defaults and explicitly keeps `neuralRenderingEnabled=true`. |
| `Features/Upscaling/FoveatedRender.cpp:1465` | Reset to default | Route | No; resets to the default-on master value. |
| `Features/Upscaling/FoveatedRender.cpp:1483` | Reset to default | Whole-eye Periphery NR | No. |
| `Features/Upscaling/FoveatedRender.cpp:1487` | Enable periphery NR | Whole-eye Periphery NR | Disables only the optional periphery pass; central NR remains on. |
| `Features/Upscaling/FoveatedRender.cpp:1496` | Stagger periphery frames | Whole-eye Periphery NR | No. |
| `Features/Upscaling/FoveatedRender.cpp:1504` | Periphery model resolution (%) | Whole-eye Periphery NR | No. |
| `Features/Upscaling/FoveatedRender.cpp:1510` | Periphery lighting gain | Whole-eye Periphery NR | No. |
| `Features/Upscaling/FoveatedRender.cpp:1512` | Centre blend width (px) | Whole-eye Periphery NR | No. |
| `Features/Upscaling/FoveatedRender.cpp:1521` | Reset to default | Subrect edge blend | No. |
| `Features/Upscaling/FoveatedRender.cpp:1525` | NR edge blend mode | Subrect edge blend | No. |
| `Features/Upscaling/FoveatedRender.cpp:1529` | Feather width (px; Feather mode) | Subrect edge blend | No. |
| `Features/Upscaling/FoveatedRender.cpp:1533` | Dither band width (px; Dither mode) | Subrect edge blend | No. |
| `Features/Upscaling/FoveatedRender.cpp:1535` | Dither strength (Dither mode) | Subrect edge blend | No. |
| `Features/Upscaling/FoveatedRender.cpp:1539` | Edge falloff (non-Hard Copy mode) | Subrect edge blend | No. |
| `Features/Upscaling/FoveatedRender.cpp:1542` | Edge mask shape | Subrect edge blend | No. |
| `Features/Upscaling/FoveatedRender.cpp:1548` | Reset to default | Colour and tone | No. |
| `Features/Upscaling/FoveatedRender.cpp:1578` | Natural style; Fabric Detail style; Cinematic style (three style buttons) | Visual style | No. |
| `Features/Upscaling/FoveatedRender.cpp:1606` | Model resolution | Neural Rendering tuning | No. |
| `Features/Upscaling/FoveatedRender.cpp:1624` | Model Preset | Neural Rendering tuning | No. |
| `Features/Upscaling/FoveatedRender.cpp:1629,1631,1633,1635` | Intensity; Local Tone; Local Structure; Skin Structure | Neural Rendering tuning | No. |
| `Features/Upscaling/FoveatedRender.cpp:1637,1639` | Automatic Mask; UI Correction | Neural Rendering tuning | No. |
| `Features/Upscaling/FoveatedRender.cpp:1645` | Shape the NR result | NR Result Shaping | Disables result shaping only; NR remains active. |
| `Features/Upscaling/FoveatedRender.cpp:1648-1677` | Edit strength; Brightening; Darkening; Color; Hue shift; Shadows; Midtones; Highlights; Max brightening; Max darkening; Max color change; Large-scale tone; Fine detail; Detail radius; Halo suppression | NR Result Shaping | No. |
| `Features/Upscaling/FoveatedRender.cpp:1686` | Result stabilization mode | Result Stabilization | No. |
| `Features/Upscaling/FoveatedRender.cpp:1691,1693,1695,1697` | Stabilize time; Stabilize fine detail; Stabilizer depth threshold; Stabilizer color tolerance | Result Stabilization | No. |
| `Features/Upscaling/FoveatedRender.cpp:1714,1722,1732` | Reduced NR Resolve; Experimental pre-upscale NR; Experimental sequential NR | Experimental Pipeline | No direct off control. Pre-upscale has route restrictions. |
| `Features/Upscaling/FoveatedRender.cpp:1761,1767,1770,1774,1779` | Temporal residual reuse; Depth rejection threshold; Color rejection tolerance; Stagger eyes; Reset native history after a skipped frame | Temporal Stability | No direct off control; temporal reuse has implementation route restrictions. |
| `Features/Upscaling/FoveatedRender.cpp:1791,1797` | Reset to default (two controls for Adaptive NR) | Adaptive NR | No. |
| `Features/Upscaling/FoveatedRender.cpp:1800` | Enable adaptive NR resolution | Adaptive NR | No; changes resolution policy. |
| `Features/Upscaling/FoveatedRender.cpp:1814,1819,1833` | Use custom FPS target; FPS target; headset refresh-rate preset buttons | Adaptive NR | No. |
| `Features/Upscaling/FoveatedRender.cpp:1854,1859,1863,1867,1870` | Adaptive minimum NR cost; NR downshift response; NR restore response; NR tier minimum dwell; Adaptive reserved headroom | Adaptive NR | No. |
| `Features/Upscaling/FoveatedRender.cpp:1886` | Enable adaptive crop | Adaptive crop | No; changes crop coverage policy. |
| `Features/Upscaling/FoveatedRender.cpp:1902,1916,1922,1926,1930,1934` | Adaptive maximum crop coverage; Adaptive minimum crop coverage; Crop downshift response; Crop restore response; Crop tier minimum dwell; Crop handoff duration | Adaptive crop | No. |
| `Features/Upscaling/FoveatedRender.cpp:1984` | Reset to default | Diagnostics | No. |
| `Features/Upscaling/FoveatedRender.cpp:1988,1991,1994` | Show foveated region overlay; MapProbe diagnostics; Show adaptive handoff diagnostics | Diagnostics | No. |
| `Features/Upscaling/FoveatedRender.cpp:2006` | Reset Neural Rendering Failure | Diagnostics / recovery | No; retries route setup. |
| `Features/Upscaling/FoveatedRender.cpp:2024,2029,2033` | Native OpenVR gaze provider; Fixation smoothing (ms); Crop movement quantization (input px) | Eye-tracked Foveation | No. Eye-tracking is the final section. |

The only NR master-off control is the button called once from the page at `Upscaling.cpp:356` [SOURCE-VERIFIED]. Route-support status requires DLSS, no configured Frame Generation, and, in VR, Default DLSS mode plus active PerfMode [SOURCE-VERIFIED]. Changing those prerequisites can make the route unavailable without turning off the master [SOURCE-VERIFIED]. All group headers in `FoveatedRender::DrawSettings()` use `ImGuiTreeNodeFlags_Leaf`; they cannot collapse to hide settings [SOURCE-VERIFIED]. The route controls draw regardless of the master state; unsupported-route controls are disabled in place when applicable [SOURCE-VERIFIED]. Dependent controls remain conditional on their parent feature/mode, such as periphery, dither, or adaptive settings [SOURCE-VERIFIED].

Static and status content includes the page title and description at `Upscaling.cpp:352-354`, master-button details at `FoveatedRender.cpp:1274-1318`, route status/reason/dimensions at `FoveatedRender.cpp:1400-1417`, and eye-tracking provider diagnostics at `FoveatedRender.cpp:2046-2058` [SOURCE-VERIFIED]. These are explanatory/status elements rather than interactive settings.

## Upscaling page

`Upscaling::DrawSettings()` is at `Features/Upscaling.cpp:629-1136`; its VR PerfMode helper is called at line 809 and defined around lines 284-303 [SOURCE-VERIFIED].

| File:line [SOURCE-VERIFIED] | Current label/control | Group | Can turn NR off or make its route unavailable? |
|---|---|---|---|
| `Features/Upscaling.cpp:667` | Method | Upscaler | **Selecting None, TAA, or FSR makes the NR route unavailable** because it requires DLSS; does not clear the master setting. |
| `Features/Upscaling.cpp:738` | Upscale Preset | Upscaler | Can affect route setup/availability; no direct NR-off setting. |
| `Features/Upscaling.cpp:756` | Sharpness (FSR branch) | FSR | No. |
| `Features/Upscaling.cpp:759` | Use Runtime FSR4 (when eligible) | FSR | No. |
| `Features/Upscaling.cpp:769` | Enable Sharpening (DLSS) | DLSS | No. |
| `Features/Upscaling.cpp:777` | Sharpness (DLSS) | DLSS | No. |
| `Features/Upscaling.cpp:788` | DLSS Model Preset | DLSS | No. |
| `Features/Upscaling.cpp:796` | Settings Benchmark (measure in headset) opener | DLSS diagnostics | No; it opens a diagnostic panel. |
| `Features/Upscaling/FoveatedRender.cpp:1217` (called by `Upscaling.cpp:803`) | DLSS Mode | VR DLSS | Faster mode can make the VR NR route unavailable; does not toggle the NR master. |
| `Features/Upscaling.cpp:292` (called at `Upscaling.cpp:809`) | Render engine at upscaled resolution | VR PerfMode | Turning it off can make the VR NR route unavailable because active PerfMode is required. |
| `Features/Upscaling.cpp:296,300` (called within `DrawPerfModeToggle`) | Map: render at display resolution; Map probe diagnostics | VR PerfMode | No direct NR-off control. |
| `Features/Upscaling.cpp:820` | Frame Generation | Frame Generation (flat only) | Configuring it makes the NR route unavailable; does not clear the NR master. |
| `Features/Upscaling.cpp:847` | Prefer AMD FSR Frame Generation | Frame Generation | Can choose the frame-generation backend; configured FG still gates NR. |
| `Features/Upscaling.cpp:858` | DLSS-G Frame Multiplier (DLSS-G branch) | Frame Generation | No independent NR toggle; configured FG remains a route gate. |
| `Features/Upscaling.cpp:884` | Frame Limit (Variable Refresh Rate) | Frame Generation | No direct NR-off control. |
| `Features/Upscaling.cpp:892` | Force Enable Frame Generation | Frame Generation | Can configure FG and thereby make NR unavailable. |
| `Features/Upscaling.cpp:900` | Frame Generation in Menus | Frame Generation | No direct NR-off control; configured FG remains a route gate. |
| `Features/Upscaling.cpp:928,937` | Low Latency Mode; Low Latency Boost | NVIDIA Reflex | No. |
| `Features/Upscaling.cpp:946,959` | Use Markers To Optimize; Use FPS Limit | NVIDIA Reflex | No. |
| `Features/Upscaling.cpp:974` | FPS Limit | NVIDIA Reflex | No. |
| `Features/Upscaling.cpp:999` | Streamline Logging | Backend diagnostics | No. |
| `Features/Upscaling.cpp:1011,1073` | View Resize (two VR diagnostic viewers) | Backend diagnostics | No. |
| `Features/Upscaling.cpp:1013,1075` | Upscaling Intermediates (two viewer instances) | Backend diagnostics | No; opens buffer views. |
| `Features/Upscaling.cpp:1037,1099` | Native Inputs (two viewer instances) | Backend diagnostics | No; opens buffer views. |
| `Features/Upscaling.cpp:1049,1111` | Runtime-generated render-target labels (for example `kMAIN (Color Input)`, `Motion Vectors`, `Depth`) | Backend diagnostics | No; viewer expanders. |

Static/status content includes method lock/status at `Upscaling.cpp:670-689`, PerfMode/restart notices at `699-715`, DLSS resolution warnings at `717-725`, preset restart notices at `740-752`, Runtime FSR4 messages at `760-765`, Reflex support status at `910-926` and `955-957`, and diagnostics/version information at `1013-1133` [SOURCE-VERIFIED].

## Removed or moved controls

| 2.21.5 baseline source [SOURCE-VERIFIED] | Baseline control | Result in 2.21.6 [SOURCE-VERIFIED] |
|---|---|---|
| `Features/Upscaling/FoveatedRender.cpp:1185-1204` | `Enable Foveated Upscaling (region source)` checkbox and its route status | Removed. `Upscaling.foveatedRender.enabled` defaults to 1 and remains loadable. `LatchEnabled()` normalizes it from `settings.neuralRenderingEnabled`; a saved legacy `enabled: 0` cannot switch off the route or required boot resource allocation. The same mirror is updated by the master button and performance-profile helper. |
| `Features/Upscaling.cpp:452-470` | `Foveated Rendering Controls` collapsible and disabled wrapper | Removed. Its controls now draw directly on Neural Rendering; all section headers are leaf headers with no collapse affordance. |
| `Features/Upscaling/FoveatedRender.cpp:1453-1520` | Duplicate `Enable Neural Rendering` checkbox and `Safe mode` action | Removed. Recommended remains and explicitly keeps Neural Rendering enabled; Reset restores the default-on route. |
| `Features/Upscaling/FoveatedRender.cpp:1332-1375` | DLSS Mode | Moved; one VR control appears on Upscaling. |
| `Features/Upscaling.cpp:349-446` | Method, Upscale Preset, sharpening, DLSS model preset, Streamline Logging, PerfMode | Retained once on Upscaling. |
| `Features/Upscaling.cpp:492-493` | Settings Benchmark | Moved to the DLSS group on Upscaling; no benchmark call remains on Neural Rendering. |
| `Features/Upscaling/FoveatedRender.cpp:1542-1580` | Eye-tracked Foveation section | Placed last on Neural Rendering. |

## Logo path finding

`Menu/IconLoader.cpp` previously joined every icon filename to `GetIconsPath()` [SOURCE-VERIFIED]. The 2.21.5 extracted archive places the colored and monochrome logos at `Interface/CommunityShaders/OpenNR Logo/opennr-logo.png` and `Interface/CommunityShaders/OpenNR Logo/Monochrome/opennr-logo.png`, not below `Icons/` [PACKAGE-VERIFIED]. The fixed loader resolves `OpenNR Logo\\...` names against `GetInterfacePath()` and all other icon names against `GetIconsPath()` [SOURCE-VERIFIED]. A source contract verifies both packaged logo files and all 22 menu icon definitions [TESTED: `test_menu_icon_path_contract.py`].

## Findings and checks

- `Upscaling.foveatedRender.enabled` previously gated boot-latched foveated route activation and FSR resource setup; it did not flip the separate `neuralRenderingEnabled` master value. That could leave the checked NR master on while its required VR route/resources were unavailable [SOURCE-VERIFIED]. It now defaults to 1, remains deserializable, is normalized from the NR master at load/toggle, and no longer independently influences route/resource setup [SOURCE-VERIFIED].
- The Neural Rendering page has one direct off control, the master button [SOURCE-VERIFIED]. DLSS settings have one copy on Upscaling, eye tracking is last, and no page section header can collapse to hide settings [SOURCE-VERIFIED].
- Menu inventory and icon path contracts passed in the Python contract run [TESTED].
- In-game visual inspection was not performed; no game or UI was launched [NOT-RUN].
