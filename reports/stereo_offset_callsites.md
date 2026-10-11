# STEREO-OFFSET call-site inventory

Base: `0d4a153e8c07a38d013a5857d5038d4c1d1097b4` (OpenNR 2.21.3).

| File and lines | Use | Assessment |
|---|---|---|
| `runtime/open-shaders/src/Features/Upscaling/NeuralRendering/Integration.cpp:1129-1156` | Resolves projection-centred crops and computes both eyes' NR coverage rectangles. The shared X/Y offsets originally went to both eyes. | Eye 0 now reads Left fields, eye 1 reads Right fields, and a zero value falls back per axis to the legacy shared value. The no-projection/centred crop overload remains unchanged. **[SOURCE-VERIFIED]** |
| `runtime/open-shaders/src/Features/Upscaling/NeuralRendering/Integration.cpp:1184-1204` | Logs requested crop centres for each projection-resolved eye. | Diagnostic centres use the same effective offsets as the coverage rectangles. **[SOURCE-VERIFIED]** |
| `runtime/open-shaders/src/Features/Upscaling/NeuralRendering/Integration.cpp:1229-1243` | Includes projection settings in the periphery shader failure/reset key. | Added per-eye settings to keep the settings key complete; no shader or composition behavior changes. **[SOURCE-VERIFIED]** |
| `runtime/open-shaders/src/Features/Upscaling/FoveatedRender.h:119-124` | Declares the existing shared offsets and four new per-eye offsets. | Shared defaults and keys remain in place; per-eye defaults are zero. **[SOURCE-VERIFIED]** |
| `runtime/open-shaders/src/Features/Upscaling/FoveatedRender.cpp:101-106` | Registers shared and per-eye values in the settings X-macro. | Values participate in the existing load/save path. **[SOURCE-VERIFIED]** |
| `runtime/open-shaders/src/Features/Upscaling/FoveatedRender.cpp:371-374` | Clamps shared offsets to ±512 px. | Shared range remains unchanged; per-eye offsets clamp to ±200 px. **[SOURCE-VERIFIED]** |
| `runtime/open-shaders/src/Features/Upscaling/FoveatedRender.cpp:1281-1293` | Existing shared-offset UI and new per-eye sliders/reset. | Four controls display px values; reset sets only the per-eye group to zero. **[SOURCE-VERIFIED]** |

The adaptive foveated subrect route does not consume these projection-center offsets: projection-centred coverage is requested only for `fullEye` (`Integration.cpp:1129`), while coverage cropping is separately gated out during adaptive crop (`Integration.cpp:1256`). No subrect settings or helpers are required. **[SOURCE-VERIFIED]**

The upstream `bfc9bf9` concept also contains separate UV subrect alignment, settings persistence, gaze composition, and a safe contiguous `float2` UI storage pattern. This change uses independent scalar sliders and only ports the projection-centred NR coverage behavior; it does not change the subrect controller, gaze, or temporal code. **[UPSTREAM-DIFF-REVIEWED]**
