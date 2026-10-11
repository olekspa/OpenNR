# OpenNR Neural Rendering settings inventory

Inventory captured from base `8902e8a1` before the 2.21.5 UI edits. Line references point to `runtime/open-shaders/src/Features/Upscaling/FoveatedRender.cpp` in that base. “None” means the control had no hover tooltip; inline descriptive text is noted separately.

## Existing foveated edge and periphery controls

| File:line | Current label | Saved key | Existing tooltip or inline explanation | Existing group |
|---|---|---|---|---|
| FoveatedRender.cpp:1071 | Periphery Rendering | — | Header tooltip explains stretched background, Periphery AA, and edge blending. | Foveated controls |
| FoveatedRender.cpp:1090 | Stretch | `stretchMode` | No hover tooltip; following description explains Bilinear, Point, or Gaussian Blur. | Periphery Rendering |
| FoveatedRender.cpp:1100 | Blur Radius | `peripheryBlurRadius` | No hover tooltip; shown only for Gaussian Blur. | Periphery Rendering |
| FoveatedRender.cpp:1104 | Periphery AA | `peripheryAAMode` | Inline description: motion compensated history blending reduces flicker in the stretched area. | Periphery Rendering |
| FoveatedRender.cpp:1107 | Smoothing | `peripheryTemporalAlpha` | “Lower = more temporal history (smoother but may ghost). Higher = more responsive.” | Periphery Rendering |
| FoveatedRender.cpp:1113 | Edge Blend | `subrectBlendMode` | Inline mode descriptions: Hard Copy is sharp/lowest cost; Feather smooths across N pixels; Dither uses a noise fade. | Periphery Rendering |
| FoveatedRender.cpp:1120 | Feather Width | `subrectFeatherWidth` | Inline description: smoothstep fade over N pixels. | Periphery Rendering / Feather mode |
| FoveatedRender.cpp:1121 | Falloff Curve | `subrectFalloffCurve` | Tooltip describes how the oval transition shifts between neural and periphery result. | Periphery Rendering / Feather mode |
| FoveatedRender.cpp:1127 | Band Width | `subrectFeatherWidth` | Inline description: width of the noise-dithered fade. | Periphery Rendering / Dither mode |
| FoveatedRender.cpp:1128 | Falloff Curve | `subrectFalloffCurve` | No separate tooltip in Dither mode. | Periphery Rendering / Dither mode |
| FoveatedRender.cpp:1129 | Noise Amount | `subrectDitherStrength` | None. | Periphery Rendering / Dither mode |
| FoveatedRender.cpp:1133 | Edge Shape | `subrectMaskMode` | Inline Rectangle/Oval descriptions; Oval changes only the composite mask, not the rectangular Feature 18 region. | Periphery Rendering |
| FoveatedRender.cpp:1149 | Visualize regions | `debugVisualize` | Tooltip says the cheap-stretched area is tinted red, with no performance impact; also shown briefly while dragging. | Subrect Region |

## Existing Neural Rendering route and whole-eye periphery controls

| File:line | Current label | Saved key | Existing tooltip or inline explanation | Existing group |
|---|---|---|---|---|
| FoveatedRender.cpp:951 | NEURAL RENDERING | state button | `neuralRenderingEnabled` | Inline state detail reports unsupported route, runtime failure, startup, or successful evaluation count. | Above NR section |
| FoveatedRender.cpp:1266 | NR Coverage | `neuralRenderingCoverage` | Runs full model resolution on center coverage, keeps DLSS full eye, uses edge blend, says cost scales with area, requires Full Eye foveation. | NR Cost |
| FoveatedRender.cpp:1277 | Projection-centred NR crop | `neuralRenderingProjectionCenteredCrop` | Centers each eye crop on the forward projection ray; otherwise buffer centered. | NR Cost |
| FoveatedRender.cpp:1281 | Projection center offset X | `neuralRenderingProjectionCenterOffsetXPx` | None. | NR Cost / Projection crop |
| FoveatedRender.cpp:1282 | Projection center offset Y | `neuralRenderingProjectionCenterOffsetYPx` | None. | NR Cost / Projection crop |
| FoveatedRender.cpp:1284 | Per-eye alignment (text) | — | Says per-eye crop alignment is for different lens centers; zero “keeps the shared projection offset,” which was ambiguous because fallback is per axis. | NR Cost / Projection crop |
| FoveatedRender.cpp:1286 | Left X | `neuralRenderingProjectionCenterOffsetLeftXPx` | None. | NR Cost / Per-eye alignment |
| FoveatedRender.cpp:1287 | Left Y | `neuralRenderingProjectionCenterOffsetLeftYPx` | None. | NR Cost / Per-eye alignment |
| FoveatedRender.cpp:1288 | Right X | `neuralRenderingProjectionCenterOffsetRightXPx` | None. | NR Cost / Per-eye alignment |
| FoveatedRender.cpp:1289 | Right Y | `neuralRenderingProjectionCenterOffsetRightYPx` | None. | NR Cost / Per-eye alignment |
| FoveatedRender.cpp:1290 | Reset to 0 | — | Resets the four per-eye offsets. | NR Cost / Per-eye alignment |
| FoveatedRender.cpp:1301 | Periphery NR | `neuralRenderingPeripheryEnabled` | None. Enabling from 100% coverage also selected 55% and projection-centred crop. | Unnamed NR block |
| FoveatedRender.cpp:1311 | Periphery stagger | `neuralRenderingPeripheryStagger` | Inline text says the periphery result is reused every other frame while center stays full cadence. | Unnamed NR block |
| FoveatedRender.cpp:1320 | Periphery model resolution | `neuralRenderingPeripheryResolution` | No tooltip; changing resolution also overwrote lighting gain with a tier-specific value. | Unnamed NR block |
| FoveatedRender.cpp:1325 | Periphery lighting gain | `neuralRenderingPeripheryLightingGain` | None. | Unnamed NR block |
| FoveatedRender.cpp:1326 | Centre blend width | `neuralRenderingPeripheryFeatherWidth` | None. | Unnamed NR block |
| FoveatedRender.cpp:1345 | Model Resolution | `neuralRenderingModelResolution` | Tooltip says display remains full size, only model area changes, experimental tiers and adaptive limits. Locked at 100% in this build. | NR Cost |

## Existing appearance, adaptive, temporal, and diagnostics controls

| File:line | Current label | Saved key | Existing tooltip or inline explanation | Existing group |
|---|---|---|---|---|
| FoveatedRender.cpp:1218 | Visual Style | `neuralRenderingStyle` | Tooltip says style is independent from numeric intensity/structure controls; each card has a short appearance description. | Main NR block |
| FoveatedRender.cpp:1354 | Model Preset | `neuralRenderingPreset` | None. | Advanced NR Tuning |
| FoveatedRender.cpp:1368 | Intensity | `neuralRenderingIntensity` | None. | Advanced NR Tuning |
| FoveatedRender.cpp:1369 | Local Tone | `neuralRenderingLocalTone` | None. | Advanced NR Tuning |
| FoveatedRender.cpp:1370 | Local Structure | `neuralRenderingLocalStructure` | None. | Advanced NR Tuning |
| FoveatedRender.cpp:1371 | Skin Structure | `neuralRenderingSkinStructure` | None. | Advanced NR Tuning |
| FoveatedRender.cpp:1372 | Automatic Mask | `neuralRenderingAutoMask` | None. | Advanced NR Tuning |
| FoveatedRender.cpp:1373 | UI Correction | `neuralRenderingUICorrection` | None. | Advanced NR Tuning |
| FoveatedRender.cpp:1382 | Enable adaptive NR resolution | `neuralRenderingAdaptiveEnabled` | Tooltip says one NR tier changes after sustained pressure and handoffs are blended. Not shown when full-resolution-only is enabled. | Adaptive Performance |
| FoveatedRender.cpp:1397 | Use custom FPS target | `neuralRenderingAdaptiveTargetFps` | None. | Adaptive Performance |
| FoveatedRender.cpp:1401 | FPS target | `neuralRenderingAdaptiveTargetFps` | None. | Adaptive Performance |
| FoveatedRender.cpp:1405 | Headset refresh target | `neuralRenderingAdaptiveRefreshHz` | Button labels show headset Hz and half-refresh FPS budget. | Adaptive Performance |
| FoveatedRender.cpp:1434 | Adaptive minimum NR cost | `neuralRenderingAdaptiveMinimumResolution` | None. | Adaptive Performance |
| FoveatedRender.cpp:1438 | NR downshift fade | `neuralRenderingAdaptiveDownshiftFrames` | None. | Adaptive Performance |
| FoveatedRender.cpp:1441 | NR restore response | `neuralRenderingAdaptiveUpshiftFrames` | None. | Adaptive Performance |
| FoveatedRender.cpp:1444 | NR tier minimum dwell | `neuralRenderingAdaptiveMinimumDwellFrames` | None. | Adaptive Performance |
| FoveatedRender.cpp:1446 | Adaptive reserved headroom | `neuralRenderingAdaptiveGuardTimeMs` | None. | Adaptive Performance |
| FoveatedRender.cpp:1455 | Show handoff diagnostics | `neuralRenderingAdaptiveDiagnostics` | None. | Adaptive Performance |
| FoveatedRender.cpp:1462 | Enable adaptive crop | `neuralRenderingAdaptiveCropEnabled` | Tooltip describes crop/NR ordering and gaze exclusion. | Adaptive Foveated Crop |
| FoveatedRender.cpp:1478 | Adaptive maximum crop coverage | `neuralRenderingAdaptiveCropMaximumCoverage` | Tooltip says this is the tier used when adaptive crop is re-armed. | Adaptive Foveated Crop |
| FoveatedRender.cpp:1492 | Adaptive minimum crop coverage | `neuralRenderingAdaptiveCropMinimumCoverage` | None. | Adaptive Foveated Crop |
| FoveatedRender.cpp:1497 | Crop downshift response | `neuralRenderingAdaptiveCropDownshiftFrames` | None. | Adaptive Foveated Crop |
| FoveatedRender.cpp:1500 | Crop restore response | `neuralRenderingAdaptiveCropUpshiftFrames` | None. | Adaptive Foveated Crop |
| FoveatedRender.cpp:1503 | Crop tier minimum dwell | `neuralRenderingAdaptiveCropMinimumDwellFrames` | None. | Adaptive Foveated Crop |
| FoveatedRender.cpp:1506 | Crop handoff duration | `neuralRenderingAdaptiveCropTransitionFrames` | None. | Adaptive Foveated Crop |
| FoveatedRender.cpp:1547 | Reduced NR Resolve | `neuralRenderingResolveMode` | Tooltip describes matched residual processing; hidden in full-resolution-only build. | Experimental Pipeline |
| FoveatedRender.cpp:1555 | Experimental pre-upscale NR | `neuralRenderingPreUpscale` | Tooltip describes stage order, potential color change, RR incompatibility, and VR constraints. | Experimental Pipeline |
| FoveatedRender.cpp:1565 | Experimental sequential NR | `neuralRenderingMultiPass` | Tooltip describes repeated evaluations for screenshot/benchmark. | Experimental Pipeline |
| FoveatedRender.cpp:1593 | Temporal residual reuse | `neuralRenderingTemporalReuseCadence` | Tooltip explains cadence/reprojection; warns about ghosting, cadence shimmer, and disocclusion. | Experimental Pipeline |
| FoveatedRender.cpp:1599 | Depth rejection threshold | `neuralRenderingTemporalDepthThreshold` | None. | Experimental Pipeline |
| FoveatedRender.cpp:1601 | Color rejection tolerance | `neuralRenderingTemporalColorTolerance` | None. | Experimental Pipeline |
| FoveatedRender.cpp:1604 | Stagger eyes | `neuralRenderingTemporalReuseStaggerEyes` | Tooltip warns about left/right differences during fast motion. | Experimental Pipeline |
| FoveatedRender.cpp:1610 | Reset native history after a skipped frame | `neuralRenderingTemporalReuseResetAfterSkip` | Tooltip warns stale native history may ghost or destabilize motion. | Experimental Pipeline |
| FoveatedRender.cpp:1621 | Shape the NR result | `neuralRenderingResultShapingEnabled` | Tooltip explains post-Feature 18 tone/color/detail edits. | NR Result Shaping |
| FoveatedRender.cpp:1625 | Edit strength | `neuralRenderingResultEditStrength` | Tooltip explains 0 removes, 1 preserves, 2 doubles the correction. | NR Result Shaping |
| FoveatedRender.cpp:1628 | Brightening | `neuralRenderingResultBrightening` | None. | NR Result Shaping |
| FoveatedRender.cpp:1629 | Darkening | `neuralRenderingResultDarkening` | None. | NR Result Shaping |
| FoveatedRender.cpp:1630 | Color | `neuralRenderingResultColor` | Tooltip says it scales how far colors move toward or away from gray. | NR Result Shaping |
| FoveatedRender.cpp:1633 | Hue shift | `neuralRenderingResultHueShiftStrength` | Tooltip explains 0/1/2 relative hue changes. | NR Result Shaping |
| FoveatedRender.cpp:1636 | Shadows | `neuralRenderingResultShadows` | None. | NR Result Shaping |
| FoveatedRender.cpp:1637 | Midtones | `neuralRenderingResultMidtones` | None. | NR Result Shaping |
| FoveatedRender.cpp:1638 | Highlights | `neuralRenderingResultHighlights` | None. | NR Result Shaping |
| FoveatedRender.cpp:1639 | Max brightening | `neuralRenderingResultMaxBrighteningStops` | Inline note: zero leaves change uncapped. | NR Result Shaping |
| FoveatedRender.cpp:1640 | Max darkening | `neuralRenderingResultMaxDarkeningStops` | Inline note: zero leaves change uncapped. | NR Result Shaping |
| FoveatedRender.cpp:1641 | Max color change | `neuralRenderingResultMaxColorChangeStops` | Inline note: zero leaves change uncapped. | NR Result Shaping |
| FoveatedRender.cpp:1643 | Large-scale tone | `neuralRenderingResultLargeScaleTone` | None. | NR Result Shaping |
| FoveatedRender.cpp:1644 | Fine detail | `neuralRenderingResultFineDetail` | None. | NR Result Shaping |
| FoveatedRender.cpp:1645 | Detail radius | `neuralRenderingResultDetailRadius` | Tooltip says it controls where broad lighting change ends and fine detail begins. | NR Result Shaping |
| FoveatedRender.cpp:1648 | Halo suppression | `neuralRenderingResultHaloSuppression` | None. | NR Result Shaping |
| FoveatedRender.cpp:1656 | Result stabilization | `neuralRenderingStabilizeMode` | Tooltip explains static/motion temporal smoothing and depth/color rejection. | Result Stabilization |
| FoveatedRender.cpp:1662 | Stabilize time | `neuralRenderingStabilizeTimeMs` | None. | Result Stabilization |
| FoveatedRender.cpp:1663 | Stabilize fine detail | `neuralRenderingStabilizeDetail` | Tooltip warns that smoothing fine changes raises trailing risk. | Result Stabilization |
| FoveatedRender.cpp:1666 | Stabilizer depth threshold | `neuralRenderingStabilizeDepthThreshold` | None. | Result Stabilization |
| FoveatedRender.cpp:1667 | Stabilizer color tolerance | `neuralRenderingStabilizeColorTolerance` | None. | Result Stabilization |
| FoveatedRender.cpp:1678 | Native OpenVR gaze provider | `neuralRenderingEyeTrackedFoveation` | Tooltip says this requires VR, Default mode, and a cropped region; stale gaze falls back to static crop. | Eye-tracked Foveation |
| FoveatedRender.cpp:1683 | Fixation smoothing | `neuralRenderingEyeTrackedSmoothingMs` | Inline text says eye movement remains immediate, crop motion resets temporal history. | Eye-tracked Foveation |
| FoveatedRender.cpp:1687 | Crop movement quantization | `neuralRenderingEyeTrackedQuantizationPixels` | None. | Eye-tracked Foveation |
| FoveatedRender.cpp:1723 | Reset Neural Rendering Failure | — | Warning says reset retries after a recovery/adaptive limit. | Recovery diagnostics |
| FoveatedRender.cpp:1727 | Developer status / NGX / evaluations | — | Developer-only readout, not a control. | Recovery diagnostics |

## Guided-filter compatibility

`neuralRenderingPeripheryGuidedFilter` existed in the settings schema and benchmark preset JSON, but had no user control and was gated by `kPeripheryGuidedFilterQualified = false`. It remains only as a compatibility field so old settings files continue to parse; it is omitted from the 2.21.5 benchmark presets and UI.

## Scope notes

The foveated `stretchMode`, `peripheryBlurRadius`, `peripheryAAMode`, and `peripheryTemporalAlpha` control the stretched low-resolution background. They are distinct from the `neuralRenderingPeriphery*` whole-eye Feature 18 pass settings. The MapProbe diagnostics option is owned by Upscaling settings in `Upscaling.cpp`, outside the inventory file; 2.21.5 exposes it in the NR Diagnostics group without changing the underlying setting.
