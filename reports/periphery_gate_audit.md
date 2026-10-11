# PERIPHERY-GATE height audit

Status: **AUDIT COMPLETE**

All listed source paths inspected on branch `codex/opennr-2.21.3-periphery-gate`, base `bb3f765557eebbe25b9de163d19652c981b2bf57`. Line references are from the pre-change source unless marked otherwise.

| Periphery height use | Multiple of 8 required? | Evidence |
|---|---:|---|
| `EnsurePeripheryResources`: coarse height, full-size intermediates, and low-resolution residual `modelHeight` | No | `runtime/open-shaders/src/Features/Upscaling/NeuralRendering/Integration.cpp:284-293`; coarse height uses ceiling division, other dimensions use exact or percentage-scaled height. |
| `DispatchPeripheryHybrid`: coarse height and dispatch group count | No | `runtime/open-shaders/src/Features/Upscaling/NeuralRendering/Integration.cpp:462-488`; both use ceiling division. |
| `PeripheryHybridCS.hlsl`, mode 0 | No | `runtime/open-shaders/features/Upscaling/Shaders/Upscaling/NeuralRendering/PeripheryHybridCS.hlsl:35-85`; coarse grid uses ceiling division and full-resolution tail accesses clamp/check against `Height`. |
| `PeripheryHybridCS.hlsl`, mode 1 | No | `runtime/open-shaders/features/Upscaling/Shaders/Upscaling/NeuralRendering/PeripheryHybridCS.hlsl:102-`; full-resolution dispatch uses ceiling division and shader bounds checks against `Height`. |
| `ModelResolutionCS.hlsl`, periphery modes 4 and 5 | No | `runtime/open-shaders/features/Upscaling/Shaders/Upscaling/NeuralRendering/ModelResolutionCS.hlsl:112-149`; per-thread work checks `id.y < gHeight`. |
| Centered `ComputeNeuralCoverageRect` guide height | No | `runtime/open-shaders/src/Features/Upscaling/NeuralRendering/RuntimePolicy.h:173-201`; scales guide top/bottom against source extents without 8-pixel alignment. |
| Projection-centered `ComputeNeuralCoverageRect` guide height | No | `runtime/open-shaders/src/Features/Upscaling/NeuralRendering/RuntimePolicy.h:210-249`; clamps within extents and maps edges proportionally. |
| `AlignNeuralCoverageRectsForStereo` guide/eye heights | No | `runtime/open-shaders/src/Features/Upscaling/NeuralRendering/RuntimePolicy.h:261-320`; aligns stereo rectangles without height modulo or 8-pixel snapping. |
| `hybridRequested` gate | Yes before this change; **No after** | Before: `Integration.cpp:1196-1199` included `(totalDesc.Height % 8u) == 0`. The new gate retains only `(eyeWidth % 8u) == 0` among alignment terms. |

The only discovered multiple-of-8 assumption was the route gate, in one file. **SOURCE-VERIFIED.** The width requirement remains because the coarse grid addresses each eye using `EyeWidth / 8`. **SOURCE-VERIFIED.**

## Scope boundary

This audit is source inspection. It does not establish GPU performance, user-visible edge quality, or in-game route execution. Those require the explicitly deferred live test. **NOT MEASURED.**
