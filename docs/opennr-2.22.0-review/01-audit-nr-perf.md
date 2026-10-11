# OpenNR 2.21.8 / worktree opennr-2-22-0 - per-frame NR path performance + correctness audit

Read-only. No repo file was edited. Source root: `<REPO>\runtime\open-shaders\src` (abbreviated `src/`).
Tags: **SOURCE-VERIFIED** = read in code and/or confirmed by the live log; **INFERENCE** = estimate that needs a live A/B or PIX/GPUView.

## 0. What "default" is, and what the live machine actually runs

* There is no packaged `SettingsDefault/SettingsUser.json` (`package/` has none). Defaults are the in-code structs:
  `src/Features/Upscaling/FoveatedRender.h:138-247` and `src/Features/Upscaling/NeuralRendering/Runtime.h:13-79` (`Tuning`):
  NR enabled, model 100 %, coverage 100, periphery OFF (`neuralRenderingPeripheryEnabled=false`), result shaping OFF, stabilize 0, multipass 0, temporal-reuse cadence 0, adaptive NR/crop OFF, pre-upscale 0, intensity 1.70 / local structure 1.70 / tone 1.0 / skin -1 / auto-mask on / style 0.
  First-run crop preset is seeded to "Center 75%" (`FoveatedRender.cpp:514-533`, default arg `kPresetCenter75`) so a fresh install runs the **crop + feather blend** route; the Quality profile (`Upscaling.cpp:424`) is Full Eye.
* The user's real config (`<MO2_INSTANCE>\overwrite\SKSE\Plugins\CommunityShaders\SettingsUser.json`): `SelectedPresetIndex 0` (Full Eye), all NR knobs at the defaults above, `qualityMode 2`, periphery off.
* Live log (`...\My Games\Skyrim VR\SKSE\CommunityShaders.log`, 2026-10-08 23:40, plugin 2-21-8-0): display eye **2496x2688**, guides **1468x1580**, `path=direct-copy`, `edgeBlend=false`.
  So the steady-state default path is: Full Eye -> no blend -> 2 colour uploads, 2 depth copies, 2 MV copies, 1 D3D12 submission with 2 NGX evaluates, 2 output copies. **No compute shader runs at all** in this path (ModelResolution / CopyDepthGuide / ResultShaping / TemporalReuse / AdaptiveHandoff / hybrid are all skipped by early-outs - verified, see section 4).
* Cost model confirmed by `OpenNR-SettingsBenchmark/benchmark-20261001-150911.json` (Crop 55 % only: -14.9 ms of 39.6 ms, i.e. NR time scales with pixels; ~1.2 ms fixed + ~1.6 ms/MP per eye per `docs/OPENNR_2.19.1_AUDIT...md:30`).

### Bandwidth sanity check (explains why the glue is NOT mostly copies)
Assuming 4 B/px for the SBS colour target (not verified; 8 B/px if RGBA16F) per frame, default Full Eye:
colour in 13.42 Mpix x 8 B (R+W) = 107 MB, colour out 107 MB, guides 4.64 Mpix x (depth 8 B + MV 8 B) = 74 MB -> **~290 MB, i.e. ~0.4-0.6 ms at the 5070 Ti's realistic copy rate** (896 GB/s peak).
The measured `FoveatedLdrBeforeUI` overhead is ~2.8 ms. So **~2.2 ms or more is not bandwidth**: it is queue hand-off bubbles / GPU idle / cache flushes, or NGX per-evaluate fixed cost accounted inside the D3D11 pass. Only F3 below targets that, and it is INFERENCE until measured.
Measurement caveat: the `CS_GPU_PASS_DYNAMIC(... "NGXEvaluate")` and `Interop::*` passes wrap only **CPU recording** of D3D12 calls on a different queue; the D3D11 timestamp pair around them is ~0 GPU time. The NR GPU time lands in `Interop::D3D11QueueWait`. Add D3D12 timestamp queries around each eye's evaluate if the split matters.

---

## 1. Ranked findings (impact x confidence)

| Rank | ID | One-line | Steady-state? | Confidence |
|---|---|---|---|---|
| 1 | F1 | 4.7 s render-thread stall on the first NR frame (D3D12 init + NGX init + 2 feature creates + first evaluate), repeated after any NR reset | no (per load / per reset) | SOURCE-VERIFIED (log) |
| 2 | F2 | ShaderCache clear (UI / file watcher / advanced toggles) fully tears NGX down -> same 4.7 s stall again | no (event) | SOURCE-VERIFIED |
| 3 | F3 | No `Flush()` after `context11->Signal`; D3D11->D3D12->D3D11 hand-off bubbles are the only candidate for the unexplained ~2 ms | yes | INFERENCE |
| 4 | F4 | `IVRCompositor::GetFrameTiming` IPC every VR frame even with adaptive NR OFF | yes (CPU) | SOURCE-VERIFIED call, INFERENCE cost |
| 5 | F5 | Hybrid (periphery) route: ~5 redundant full-SBS/guide copies + 2 interop round trips + duplicated VRAM per Renderer instance | only periphery ON | SOURCE-VERIFIED |
| 6 | F6 | `BlendSubrectToOutput` thrashes its 1-entry SRV cache: 2x `CreateShaderResourceView` + `SetPrivateData` per frame (crop route = first-run default; DLSS subrect route too) | crop routes | SOURCE-VERIFIED |
| 7 | F7 | Crop route with no kTOTAL UAV: whole-SBS `CopyResource` in AND out + full-SBS staging texture | crop routes, conditional | SOURCE-VERIFIED code, INFERENCE if hit |
| 8 | F8 | Standalone shaders are compiled with `D3DCompileFromFile`, optimisation L3, uncached, on the render thread at first use | crop/hybrid/temporal first use | SOURCE-VERIFIED |
| 9 | F9 | Hybrid fail-closed path retries the full hybrid (2 NR evals + fallback 3rd eval + `ResetHistory`) every frame | only on persistent hybrid failure | SOURCE-VERIFIED |
| 10 | F10 | NGX parameter churn: 43 `Set()` + `Reset()` per eye per frame, 2 mutex round trips + wstring compare per `Execute` | yes (CPU) | SOURCE-VERIFIED, INFERENCE cost |
| 11 | F11 | Per-frame `std::string` pass names (8 heap strings + 8 `Register()` lookups) with profiler disabled | yes (CPU) | SOURCE-VERIFIED |
| 12 | F12 | Primary NR path has no frame-gap history reset (periphery has one) | correctness | SOURCE-VERIFIED |
| 13 | F13 | 250 ms backpressure timeout latches a failure -> full NGX teardown -> F1 again | robustness | SOURCE-VERIFIED |
| 14 | F14 | `UpdateFrameState` x3 per frame, `Renderer::Instance()` mutex x4, `SettingsBenchmark::Tick` mutex, ~10 `QueryInterface`+`GetDesc` | yes (CPU, micro) | SOURCE-VERIFIED |
| 15 | F15 | Manual-reset request silently dropped when `Reset()` is deferred | correctness | SOURCE-VERIFIED |
| 16 | F16 | Flat route: `color[0]` staging doubles the framebuffer copies (2 extra full-frame copies + full-frame texture) | flat only | SOURCE-VERIFIED |
| 17 | F17 | `RecordTemporalHistory` clears both accumulated-motion textures and copies depth every native frame (one clear is dead) | cadence >= 2 only | SOURCE-VERIFIED |
| 18 | F18 | Stabilizer: 2 full `CopyResource` per eye per frame for previousBase/previousDepth | stabilize on only | SOURCE-VERIFIED |
| 19 | F19 | Hybrid-shader / guided-shader invalidation gaps and latch semantics (hot reload, transient compile failure) | dev / rare | SOURCE-VERIFIED |
| 20 | F20 | `IsFullEye()` >= 0.999 vs truncating `uint(eyeWidth*uv.w)`; exact-float `leftUV.w != rightUV.w` silently disables NR | correctness, edge | SOURCE-VERIFIED |
| 21 | F21 | Shader micro-costs (ModelResolution exact-area loop, OkLab `pow`, hybrid mode-0 64-tap serial loop, SubrectBlend interior via UAV) | non-default | INFERENCE |
| 22 | F22 | Stale SRV in `BlendSubrect` cache can pin a freed NR output allocation after `NeuralRendering::Reset()` | crop routes | SOURCE-VERIFIED |
| 23 | F23 | Barrier set could be batched across eyes (16 transition descriptors / frame in 4 calls) | yes (tiny) | INFERENCE |

---

## 2. Findings in detail

### F1. First NR frame = 4.7 s render-thread stall (D3D12 + NGX init + feature creation are lazy and synchronous)
* **Where:** `Integration.cpp:1603` -> `Renderer.cpp:923-926` (`InitializeInterop`, `InitializeRuntime`), `Renderer.cpp:3213/3227` (`EnsureResources` -> `EnsureTierResources`/`NeedsRecreation`), `Runtime.cpp:341-420` (`Initialize`), `Runtime.cpp:488-493` (`create(commandList, kFeatureDlssNr, ...)` inside the frame), `Runtime.cpp:300` (`LoadLibraryExW` in `Probe`).
* **What happens per session/reset:** nothing is created until the first `ApplyFoveatedLdr` that passes all gates. Then, on the render thread, in one frame:
  `D3D12CreateDevice` + queue + 3 allocators + shared fence (`D3D12Interop.cpp:35-60`), `LoadLibrary(nvngx_dlssnr.dll)` + `NVSDK_NGX_D3D12_Init_Ext`, shared-texture creation, then two blocking `NVSDK_NGX_D3D12_CreateFeature` calls, then the first evaluate.
* **Evidence (live log, SOURCE-VERIFIED):**
  ```
  [23:40:39.692] Periphery NR inactive: disabled              <- first ApplyFoveatedLdr
  [23:40:40.438] D3D12 interop initialized commandContexts=3  (+0.75 s)
  [23:40:40.652] initialized version=310.8.0                  (+0.21 s NGX init)
  [23:40:43.028] Feature create slot=0 ... ms=2251.60
  [23:40:43.904] Feature create slot=1 ... ms=873.63
  [23:40:44.389] LDR output written before UI composite ...   (+0.49 s first evaluate)
  ```
  Total 39.692 -> 44.389 = **4.70 s with no frame submitted** (log shows the "menu closed" at 39.635, i.e. right as the load screen ends).
* **Fix:** warm up before the first world frame, off the render thread: when PerfMode has latched the display size (log: "Latched display 2496x2688 ...") and NR is enabled, run `D3D12Interop::Initialize` (it needs only the adapter + D3D11 device, not the immediate context's command stream) + `Runtime::Probe/Initialize` + `PrewarmFeature` for both eyes on a worker thread using its own one-shot allocator/list/queue + a fence, then hand the handles to the render thread (`Runtime::featureHandles_`). `PrewarmFeature(...)` already exists (`Runtime.cpp:590`, used by the adaptive prewarm) and `EnsureFeature` already skips creation when `featureHandles_[slot]` matches dims (`Runtime.cpp:447-456`). Guard `Runtime` with a mutex and gate the render thread on a `prewarmDone` atomic (fall back to today's lazy path on any mismatch).
* **Risk:** medium (NGX thread-safety of `CreateFeature` is undocumented; D3D12 device is free-threaded, the D3D11 immediate context must not be touched from the worker). Unrelated to image quality.
* **Bit-exact:** yes (same handles, same dims; only creation time moves).

### F2. A shader-cache clear destroys the NGX runtime (-> F1 again)
* **Where:** `FoveatedRender.cpp:548-554`:
  ```cpp
  void FoveatedRender::ClearShaderCache() {
      FoveatedRenderImpl::Core::ClearShaderCache();
      NeuralRendering::Renderer::Instance().ClearShaderCache();
      NeuralRendering::ClearPeripheryGuidedFilterShaderCache();
      FoveatedRenderImpl::Core::ClearResources();   // <- Core.cpp:1652-1654: NeuralRendering::Reset()
  ```
  `Core::ClearResources()` begins with `NeuralRendering::Reset()` (`Core.cpp:1654`) -> `Renderer::Reset()` x2 -> `Runtime::Shutdown()` (NGX shutdown + `FreeLibrary`) + `D3D12Interop::Shutdown()` + all shared textures.
* **Triggers (SOURCE-VERIFIED):** `ShaderCache::Clear()` -> every loaded feature's `ClearShaderCache()` (`ShaderCache.cpp:2634-2639`): UI "clear cache" (`Menu/AdvancedSettingsRenderer.cpp:145,156,175`, `Utils/UI.cpp:516`), the half-precision / avoid-flow-control checkboxes, shader-define edits, the hlsl file watcher (`ShaderCache.cpp:5240` `RequestClear`), and the scoped/smart clear (`ShaderCacheSmartClear.cpp:273` `ClearShaderCacheScoped` defaults to `ClearShaderCache`). It also ran at game load in the log ("Force clearing cache due to bDepthBufferCulling", `Features/VolumetricLighting.cpp:180`) - harmless there only because NR did not exist yet.
* **Per event cost:** teardown + the full F1 sequence (~4.7 s) on the next frame, plus history loss.
* **Fix:** `ClearShaderCache` must release shader *objects* only (`Core::ClearShaderCache`, `Renderer::ClearShaderCache`, hybrid/guided CS). Keep `Core::ClearResources()` for explicit "reset" paths. If the DLSS intermediates must be rebuilt, split `ClearResources` into `ClearDlssIntermediates` and `NeuralRendering::Reset`.
* **Risk:** low. **Bit-exact:** yes.

### F3. Missing `Flush()` after the D3D11 fence signal - candidate for the unexplained ~2 ms (INFERENCE)
* **Where:** `D3D12Interop.cpp:227-236`
  ```cpp
  result = context11_->Signal(fence11_.Get(), readyValue);
  ...
  result = queue12_->Wait(fence12_.Get(), readyValue);
  ```
  and `EndD3D12` `D3D12Interop.cpp:277-281` (`context11_->Wait(fence11_, completeValue)`). A repo-wide grep finds **no** `Flush()` in the NR path (only FidelityFX, icons, theme code do).
* **What happens:** the colour/guide copies and the `Signal` sit in the D3D11 runtime's *unsubmitted* command buffer; the D3D12 queue's `Wait(readyValue)` cannot be satisfied until the D3D11 stream is submitted (batched / at the next implicit flush). The GPU can sit idle between "pre-NR D3D11 work finished" and "D3D11 tail flushed"; the D3D12 NR then runs; the D3D11 stream blocks on `Wait`. `docs/OPENNR_2.19.1_AUDIT...md:114` already lists this as "verify in PIX".
* **Cost:** INFERENCE: up to the ~2.2 ms not explained by copy traffic (see section 0). Zero if the driver already flushes on `Signal`.
* **Fix / experiment (cheap A/B):** `context11_->Flush()` immediately after `Signal` in `BeginD3D12` (or once after the guide copies, before `BeginD3D12`). Then, in order of effort: (a) create the NR queue with `D3D12_COMMAND_QUEUE_PRIORITY_HIGH` (`D3D12Interop.cpp:38-40`), (b) try `D3D12_COMMAND_LIST_TYPE_COMPUTE` for the NR queue so the D3D11 3D-engine spin-wait does not block the NR channel and the next frame's uploads overlap (NGX/NR on compute queues is unproven - may fail at create), (c) record eye 0 and eye 1 as two submissions so eye-1 uploads overlap eye-0 evaluate (only pays off with (b)).
* **Risk:** (Flush) very low; (a) low; (b)/(c) high. **Bit-exact:** yes (pure scheduling).

### F4. `GetFrameTiming` OpenVR IPC every frame while adaptive NR is off
* **Where:** `FoveatedRender.cpp:927-941`
  ```cpp
  if (globals::game::isVR && nrEligible && frame != sampledEngineFrame) {
      sampledEngineFrame = frame;
      if (auto* compositor = RE::BSOpenVR::GetIVRCompositor()) {
          vr::Compositor_FrameTiming timing{}; ...
          if (compositor->GetFrameTiming(&timing) && ...
  ```
  gated by `nrEligible` only (true for every default VR session), not by `settings.neuralRenderingAdaptiveEnabled` / `neuralRenderingAdaptiveCropEnabled`. `UpdateAdaptiveState` is invoked from `ApplyFoveatedLdr` (`Integration.cpp:1100`) and `ExecuteFoveatedRoute` (`Modes.cpp:61`), the `frame != sampledEngineFrame` guard makes it once per frame; the result only feeds `adaptiveController.Update(...)`, which ignores it when `nrConfig.enabled` is false.
* **Cost:** INFERENCE 20-200 us/frame render-thread CPU (compositor round trip), also takes the compositor-side lock.
* **Fix:** wrap the block in `settings.neuralRenderingAdaptiveEnabled || settings.neuralRenderingAdaptiveCropEnabled` (the `streamline.IsVRAMPressure()` branch right below still works with `workloadMs = -1`). **Risk:** none. **Bit-exact:** yes.

### F5. Hybrid / periphery route - redundant copies, duplicated state, two interop round trips (periphery OFF by default; this is the 2.21-2.22 development line)
All in `Integration.cpp:1490-1551` unless noted. At the live size 4992x2688:
1. **`context->CopyResource(peripheryOutput <- peripheryRaw)` (line 1494) is dead.** The periphery `ApplyStereo` writes `peripheryOutput` over the whole SBS (`fullEyeColorWidth x Height` per eye via `CopySubresourceRegion(writeback, ..., sourceX=eye*eyeWidth, ...)`, `Renderer.cpp:1371`/`1084`/`1504`), and the failure fallback restores from `peripheryRaw`, never from `peripheryOutput`. ~53 MB R+W = ~0.1-0.2 ms. Bit-exact: yes.
2. **`CopyResource(centreOutput <- peripheryRaw)` (line 1495)** is only needed in the ring of 8 px coarse cells around the crop (`PeripheryHybridCS.hlsl:77-91` reads `Centre` up to one cell outside the crop; mode 1 reads `Centre` only where `w>0`, strictly inside). Replace with `CopySubresourceRegion` of the two crop rectangles expanded by 16 px; skip entirely if `coverage==100`. Bit-exact: yes.
3. **Periphery resolve + copy:** `DispatchModelResolve` writes `tier.resolved` (full eye) and `Renderer.cpp:1371` copies it into `peripheryOutput`. Add a target offset to `ModelResolutionCS.hlsl` mode 4 (`gTarget[id.xy + offset]`) and let the resolve write the SBS texture directly: saves one SBS-size copy (~0.15 ms). Bit-exact: yes.
4. **Guides copied twice:** both `Renderer` instances run `CopyStereoGuides` (`Renderer.cpp:975-977`) from the same `vrIntermediateDepth/MotionVectors` into their own shared `eye.depth/eye.motionVectors`. With `guideSource{X,Y}=0` (hybrid, coverage-independent) the second instance can borrow the first instance's shared guide textures: 4 fewer render-res copies (~75 MB R+W) and ~2x(9+9) MB x2 eyes VRAM. Needs the shared textures to live outside `Renderer::State` (e.g. in `D3D12Interop`'s owner).
5. **Two D3D12 devices' worth of queues/fences/allocators and two Signal/Wait hand-offs** (periphery `BeginD3D12/EndD3D12`, then centre `BeginD3D12/EndD3D12`): `Renderer::Instance()` and `PeripheryInstance()` each own a `D3D12Interop` (`Renderer.cpp:3253`, `D3D12Interop.h:68`). `D3D12CreateDevice` returns the per-process adapter singleton so NGX stays consistent, but the queue switch is paid twice (F3). Recording all four evaluates into one command list needs an `ApplyStereoPair` that does all D3D11 uploads first, one `Begin/End`, then both resolves and the composite. INFERENCE: one queue switch (~0.2-0.5 ms). Risk: medium (control-flow refactor of `ApplyStereo`). Bit-exact: yes.
6. **VRAM:** `peripheryRaw`, `peripheryOutput`, `centreOutput` are three full-SBS textures (`Integration.cpp:314-316`, ~54 MB each at 4 B/px) plus per-instance `eye.color`+`tier.output` (2x2x27 MB) plus a second set of guides. After (1)(2)(3) `peripheryOutput` can alias `tier.resolved`.
7. **Four NGX features instead of two** -> F1 stall grows to roughly 2x the feature-create share (~6 s).
* **Cost (sum 1-4):** INFERENCE ~0.4-0.7 ms/frame when periphery is on. **Bit-exact:** 1-4 yes.

### F6. `BlendSubrectToOutput` recreates its source SRV twice per frame (crop route = first-run default; DLSS subrect route also)
* **Where:** `Core.cpp:1247-1270`
  ```cpp
  if (Core::vrBlendSrcSRVOwner != dlssSrc) {
      Core::vrBlendSrcSRV = nullptr;
      ... device->CreateShaderResourceView(dlssSrc, &srvDesc, Core::vrBlendSrcSRV.put()) ...
      Util::SetResourceName(Core::vrBlendSrcSRV.get(), "FoveatedRender::BlendSrcSRV");
      Core::vrBlendSrcSRVOwner = dlssSrc;
  ```
  NR calls it per eye with `writebackOutput` = left then right texture (`Renderer.cpp:1368`, `1081`, `1501`), so the 1-entry cache misses on **every call**: 2 `CreateShaderResourceView` + 2 `SetPrivateData` (`_vsnprintf_s` into a 1 KB buffer, `D3D.cpp:103-116`) per frame. The DLSS subrect path (`Modes.cpp:270`, `vrSubrectColorOut[i]`) behaves identically.
* **Cost:** INFERENCE 40-150 us CPU/frame + driver view allocation churn. **Fix:** pass the already-existing `tier.output.srv11`/`resolvedSRV`/`resultShaping.output.srv` into `BlendSubrectToOutput` (new overload taking an SRV), or keep a 2-entry cache keyed per eye. **Risk:** low. **Bit-exact:** yes.

### F7. Crop route when kTOTAL has no UAV: whole-frame staging copies
* **Where:** `Integration.cpp:1370-1385` (copy-in `CopyResource(stereoBlendTarget, total)` of the entire SBS), writebacks `1573-1576` and `1636-1639` (`CopyResource(total, destination)`), allocation `Integration.cpp:599-621` (full SBS texture).
* **What happens:** two full-SBS copies per frame (~215 MB R+W at 4 B/px, ~0.3-0.4 ms) even though `SubrectBlendCS` only touches the two crop rectangles (`SubrectBlendCS.hlsl`: reads/writes `DstTex[dstPos]` only for `tid < SubWidth x SubHeight`).
* **Fix:** copy-in/out only the two eye crop boxes (`CopySubresourceRegion` with `sourceX/sourceY/outWidth/outHeight` already computed in `inputs[]`). Better: if `total` has an RTV (it is a render target), do the feather as a raster pass into the RTV and drop staging entirely (not bit-exact vs UNORM ROP blend; the box-copy variant is). **Risk:** low (box variant). **Bit-exact:** box variant yes.
* **Note:** the live log only shows `direct-copy` (Full Eye). Which of `direct-uav` / `staged-uav` a crop preset hits on this install is not visible in any log in the tree - check `path=` after enabling a crop.

### F8. Standalone compute/pixel shaders: synchronous, uncached, O3 `D3DCompileFromFile` on the render thread at first use
* **Where:** `Utils/D3D.cpp:142-203` (`CompileShaderBlob`: `D3DCompileFromFile(... flags = ENABLE_STRICTNESS | OPTIMIZATION_LEVEL3 ...)`, no blob cache, `std::filesystem::exists` per compile) via `Utils/LazyShader.h:28-41` and the direct `Util::CompileShader` sites: `Core.cpp:87,813,1224` (DepthToPerEye, SubrectStretch, SubrectBlend), `Renderer.cpp:2475,2616,1906,2070` (ModelResolution, AdaptiveHandoff, ResultShaping, TemporalReuse), `Integration.cpp:283,334` (guided, hybrid).
* **Effect:** the first frame that needs any of them stalls for the compile (tens to hundreds of ms each; hybrid and TemporalReuse have 64-iteration unrolled loops). The shipped default Full-Eye VR path needs none of them in NR (DepthToPerEye belongs to the DLSS route and is compiled during DLSS warm-up), so this bites the first-run Center-75 route, periphery, temporal reuse, result shaping, adaptive.
* **Fix:** compile the NR shader set asynchronously on a worker at boot (the standalone ones are tiny); or add a blob disk cache keyed on (path mtime, defines, flags); at minimum compile `SubrectBlendCS`/hybrid in the F1 warm-up. **Risk:** low. **Bit-exact:** yes (same bytecode).

### F9. Hybrid "fail closed" retries the full hybrid every frame and resets history every frame
* **Where:** `Integration.cpp:1577-1595` sets `peripheryRouteWasActive = false;` on a failed hybrid pass; `Integration.cpp:1462-1467`
  ```cpp
  if (hybridRequested != peripheryRouteWasActive || ...) ResetHistory();
  ```
  fires again next frame (request true vs flag false), then the whole hybrid is re-run.
* **What happens when `DispatchPeripheryHybrid` (or the second NR eval) fails persistently:** per frame = periphery NR (50 %) + centre NR + fallback `Renderer::Instance().ApplyStereo` (third NR eval, `Integration.cpp:1603`) + 3 SBS copies + `CopyResource(destination <- peripheryRaw)` (1581) + `ResetHistory()` for both renderers (NGX history lost every frame) -> cost > 2x a normal frame and no temporal accumulation. Also the centre instance advances its NGX history twice in one frame.
* **Fix:** latch the failure (e.g. `hybridFailedUntilSettingsChange`, like `ShaderInitLatch`) and only retry on a settings change / every N seconds; do not call `ResetHistory()` on the failure-induced flag flip. **Risk:** low. **Bit-exact:** n/a (failure path).

### F10. NGX parameter churn per evaluate
* **Where:** `Runtime.cpp:528-579`. Each `Execute` does `SignedRuntimePathScope scope(...)` (mutex lock + `wstring ==` compare, unlock in dtor = 2 lock/unlock; `Runtime.cpp:214-236`), `parameters->Reset()` (line 532), then **43** `parameters->Set(<string literal key>, ...)` calls (lines 533-579) - NGX stores these in a string-keyed map (INFERENCE: one small heap alloc per key). x2 eyes x `passCount`.
* **Cost:** INFERENCE ~15-50 us per evaluate (CPU only, 30-100 us/frame). Not GPU.
* **Fix:** keep the parameter object between frames and update only values that change (`Color/Depth/MVec/Output` pointers, `MVecScale*`, `Reset`, tuning floats when `Tuning` changes); constants (subrect bases, dims, flags) are set once per feature. Risk: low-medium - NGX's `Reset()` clears feature-create keys too today, so verify evaluate does not rely on their absence. **Bit-exact:** yes if the same key/value set is presented.

### F11. Pass-name strings built every frame with the profiler disabled
* **Where:** `Renderer.cpp:48-57` `RendererPassName` (`std::string` + `to_string` + 4 appends, >15 chars so heap) feeding `GpuPass.h:105-109`
  ```cpp
  #define CS_GPU_PASS_DYNAMIC(name) const auto& ..._name_ = (name); [[maybe_unused]] const bool ... = GpuPassCapabilities::Register(...name_); ScopedGpuPass ... { name_ }
  ```
  Used at `Renderer.cpp:965,969,975,1078,1281,1325,1363,2325`: **8 string builds + 8 `Register()` (thread_local `std::set<string>` lookup) per frame** on the default path, evaluated before `ScopedGpuPass` can discover the profiler is off.
* **Cost:** INFERENCE ~2-4 us/frame. **Fix:** constexpr table `kNames[periphery][eye][op]` of string literals (`std::string_view`) and use `CS_GPU_PASS(name)`-style static registration; or pass a `string_view` and let `ScopedGpuPass` skip when `!profiler->IsEnabled() && !state->frameAnnotations`. **Risk:** none. **Bit-exact:** yes.

### F12. No history reset after skipped NR frames on the primary path
* **Where:** `Integration.cpp:1063` (`temporalSuppressed` -> `return false`), `1092-1095` (`preUpscaleAppliedFrame`, stale-guide gate), `1129-1130`, and `Renderer.cpp:ApplyStereo` (no `lastFrame` tracking). The periphery path has the guard (`Integration.cpp:1465` `frame - lastPeripheryAppliedFrame > 2u`); the Full-Eye/crop path does not.
* **Effect:** if the DLSS route fails one frame (`neuralGuidesFrame = UINT32_MAX`, `Modes.cpp:93`), or a modal state suppresses NR for N frames, NGX resumes with history from N frames ago and one frame of MVs -> ghost/blend error for several frames. Menu/overlay transitions are handled (`UpdateFrameState`), isolated skips are not.
* **Fix:** record `lastAppliedFrame` (already exists, `Integration.cpp:75`) and call `Renderer::ResetHistory()` when `frame - lastAppliedFrameBeforeThis > 1` for the stable route. **Risk:** low. **Bit-exact:** only differs on the frame after a skip.

### F13. 250 ms fence-wait timeout escalates to a full teardown
* **Where:** `D3D12Interop.cpp:302` `WaitForSingleObject(fenceEvent_, 250)`; `Renderer.cpp:3239-3251` `LatchFailure` (`ERROR_TIMEOUT` is "recoverable") -> `RecoverIfReady` after 2 s -> `Reset(false)` -> `Runtime::Shutdown()` (`Renderer.cpp:1633-1660`).
* **Effect:** one >250 ms GPU hiccup while `EnsureResources` is waiting (resolution change, tier change, `NeedsRecreation`) or on backpressure = NR off for >= 2 s, then F1 (4.7 s) again. **Fix:** retry the wait a few times (or wait 2 s) before latching; do not tear NGX down on a timeout (it is still valid). **Risk:** low.

### F14. Per-frame micro costs (CPU, all bit-exact to remove)
* `UpdateFrameState()` is called from `Upscaling.cpp:2732`, `Integration.cpp:878` (`ApplyPreUpscale`, which then exits at line 887 because `neuralRenderingPreUpscale==0`) and `Integration.cpp:1062`. Each call re-evaluates `IsGameMenuOpen()` + `ui->IsMenuOpen(Console)` (UI map lock + hash) + two atomic exchanges. Memoise per `frameCount`; move the `preUpscale==0` check in `ApplyPreUpscale` ahead of `UpdateFrameState`.
* `Renderer::Instance()` = `CollectCompletedRendererStates()` = a mutex lock every call (`Renderer.cpp:3359`, called ~4x/frame incl. `UpdateAdaptiveState`). Guard with an `atomic<bool> hasDeferred`.
* `SettingsBenchmark::Tick()` takes `g_mutex` every frame (`SettingsBenchmark.cpp:911`, from `Deferred.cpp:263`) even when idle; use an `atomic<Phase>` pre-check.
* `GetTextureDesc()` = `QueryInterface`+`GetDesc` x ~10/frame (`Renderer.cpp:929, 3136, 2837`); cache `D3D11_TEXTURE2D_DESC` per resource pointer or take it from the callers that already have it.
* `ResolveForFrame` takes `stateMutex` twice per frame (DLSS route and NR; `Params.cpp` + `Integration.cpp:1122`) - fine, cached.
* Route-status publishing in the Off/Refused branches (`Integration.cpp:1070-1084`) takes the mutex and assigns a `std::string` **every frame** while NR is disabled/refused in VR (the active route only publishes on change).

### F15. Manual reset request dropped
* **Where:** `Integration.cpp:819-823`
  ```cpp
  if (firstUpdate && fullResetRequested.exchange(false, ...)) {
      Reset();
      if (Renderer::Instance().IsFailureLatched()) return;   // deferred reset: request consumed, never re-armed
  ```
* **Effect:** if the GPU fence cannot be drained (`Renderer::Reset` -> `deferReset`), the user's reset is lost and the DLSS/adaptive re-arm block is skipped. **Fix:** `fullResetRequested.store(true)` before returning. Low risk.

### F16. Flat (non-VR) route: staging copies
* `Integration.cpp:774` `CopyResource(color[0] <- framebuffer)` and `782` `CopyResource(framebuffer <- color[0])` wrap `Renderer::Apply`, which itself does `CopyResource(eye.color <- color)` (`Renderer.cpp:636`) and `CopyResource(color <- shapedOutput)` (`Renderer.cpp:663`/`~880`). Passing `framebuffer` directly to `Apply` removes two full-frame copies and the `color[0]` texture (`EnsureColorResources`, `Integration.cpp:575-597`, a full-frame allocation per eye resource). Formats are identical by construction (`CreateTextureFromSource(source, w, h ...)`). Bit-exact. Flat only; not the VR hot path.

### F17. `RecordTemporalHistory` (cadence >= 2 only)
* `Renderer.cpp:2293-2296`: `CopyResource(temporal.depth <- eye.depth)` + `ClearUnorderedAccessViewFloat` on **both** `accumulatedMotion[0]` and `[1]` every native frame. `Accumulate` fully overwrites the "next" texture (`TemporalReuseCS.hlsl:Accumulate` writes every pixel), so clearing `[1]` is dead; ~0.03 ms/eye. Ping-pong the depth snapshot instead of copying. Bit-exact.

### F18. Stabilizer copies (stabilize on only)
* `Renderer.cpp:2056-2057`: `CopyResource(previousBase <- eye.color)` and `CopyResource(previousDepth <- eye.depth)` per eye per frame (the history ping-pong was already done; these two were not; `docs/...2.19.1...:76` P8). `eye.color` is overwritten by the next upload, so double-buffer `eye.color` or fold the base snapshot into the ResultShaping CS (it already reads `eye.color` and writes `history[next]`). ~0.2-0.4 ms when on. Bit-exact.

### F19. Invalidation / latch gaps
* `Integration.cpp:1052-1058` `ClearPeripheryGuidedFilterShaderCache` clears only the 7 guided shaders; `peripheryHybridCS`/`peripheryHybridShaderInit` are never reset by a shader-cache clear (only `ResetPeripheryHybridShaderState()` on settings-change-after-failure, line 1295, or `Reset()`), so a hot-reloaded `PeripheryHybridCS.hlsl` keeps running the old bytecode. Add `ResetPeripheryHybridShaderState()` there.
* `ShaderInitLatch` (`ShaderInitLatch.h`) and `LazyShader` (`LazyShader.h:35`) both latch a *transient* compile failure forever (until a settings key change for hybrid, or `Reset()`); a failed ModelResolution/TemporalReuse/ResultShaping compile becomes `LatchFailure(..., E_FAIL)` -> **not** recoverable (`Renderer.cpp:3243-3245`) -> NR stays off until manual reset. Default path unaffected (no shaders).

### F20. Edge-case correctness at non-preset resolutions / UVs
* `Util::Subrect::UVRegion::IsFullEye()` is `w >= 0.999f && h >= 0.999f` (`Utils/Subrect.h:31`) but `Integration.cpp:1143-1144` computes `outWidth = max(1, uint(eyeWidth * leftUV.w))`. For 0.999 <= w < 1 the route is "full eye" (full-eye guides, no blend) yet NR/writeback cover `outWidth` = e.g. 2493 of 2496 columns, so the last columns are never NR'd and guide<->colour scale is off by 0.1 %. Fix: when `IsFullEye()` set `outWidth/outHeight = eyeWidth/Height`.
* `Integration.cpp:1140` `if (leftUV.w != rightUV.w || leftUV.h != rightUV.h) return false;` is exact float compare while the gaze resolver treats `NearlyEqual` as equal (`NativeOpenVRGaze.cpp:321`): a 1e-7 difference silently disables NR for the frame (no log).
* `eyeWidth = totalDesc.Width / 2` (`Integration.cpp:1123,1142`) drops a column on odd SBS widths; `IsFullEyeStereo`-style odd checks exist only for pre-upscale (`Integration.cpp:937`). Real HMD sizes are even.
* Hybrid requires `eyeWidth % 8 == 0` (`Integration.cpp:1249`) because `PeripheryHybridCS.hlsl` uses `EyeWidth / 8u` (floor) for the coarse split; correct and consistent with the shader (2496 and 2240 pass). Height has no such requirement (verified: `(Height + 7u) / 8u` + clamps in the shader).

### F21. Shader micro-costs (non-default routes)
* `ModelResolutionCS.hlsl:78-100 SampleExactArea`: dynamic `[loop]` over a data-dependent footprint per pixel (4-9 `Load`s at 50 %/33 %), plus `float2` divisions per pixel; at 50 % periphery this runs for 2 eyes x 1.7 Mpix. `ModelResolutionCS.hlsl:36 CbrtSigned` uses `pow(abs(x), 1/3)` x6 per pixel in the classic resolve (`PreserveModelHue`, two OkLab conversions). INFERENCE small (<0.1 ms).
* `PeripheryHybridCS.hlsl:80-90` mode 0: one thread per 8x8 block does 64 serial iterations x 2 `Load`s for centre tiles (no group-shared reduction): low occupancy; INFERENCE 0.05-0.15 ms.
* `SubrectBlendCS.hlsl`: every interior pixel is written through the UAV (`DstTex[dstPos] = dlss`, ~6.7 Mpix x2 at Center 75) and `pow()` per band pixel (`FalloffAlpha`); a `CopySubresourceRegion` of the inscribed rectangle + compute only on the band keeps output bit-exact (the interior branch is a pure copy) and halves UAV write traffic. INFERENCE ~0.05-0.1 ms.

### F22. Stale SRV can pin freed NR allocations
* `Core.cpp:1248-1269` holds `vrBlendSrcSRV` (+ raw `vrBlendSrcSRVOwner`) to the last NR output texture. `NeuralRendering::Reset()`/`eyes = {}` frees the renderer's `tier.output`, but this view keeps the D3D11 resource (and via the NT-shared handle the allocation behind its D3D12 twin) alive until the next blend call replaces it - after a resolution change that is one stale full-eye allocation (~27 MB) until then. Cleared only by `Core::ClearResources()` (`Core.cpp:1714`). Release it in `NeuralRendering::Reset()` or avoid the cache (F6).

### F23. Barrier batching (tiny)
* `Renderer.cpp:97-114`: 4 transition barriers COMMON -> NON_PIXEL_SRV/UAV before and 4 back after, **per eye**, 4 `ResourceBarrier` calls/frame for 16 descriptors. All-subresource transitions on single-mip textures; batch eyes into 2 calls. The transitions themselves must stay (D3D11/D3D12 shared-resource COMMON contract). INFERENCE: negligible CPU, unknown GPU (shared-texture decompress/flush on each COMMON transition could be a slice of F3).

---

## 3. Ideas that are not pure inefficiency (flagged, not recommended without a visual A/B)
* NR at 100 % also processes the HMD hidden-area corners (invisible); a Quest-3/Pimax visible region touches all four edge midpoints so a bounding rectangle saves nothing; only a non-rectangular skip would, which NGX cannot do. Coverage 95 % is the existing knob (quality call).
* Zero-copy colour in (make kTOTAL itself a shared NT texture at PerfMode creation) would remove the two colour uploads (~0.15-0.25 ms) but changes the engine RT creation contract and the compositor submit path - high risk, small gain; the 2.19.1 audit already withdrew P6 for the same reason (`docs/OPENNR_2.19.1...md:161`).

## 4. Checked and found clean on the default (VR, Full Eye, 100 %) path
* No CPU wait: `WaitForFence` only on backpressure / `WaitForIdle` on resource change (`D3D12Interop.cpp:204-221,286-315`); allocator ring of 3 with monotonic shared-fence values is correct; `EndD3D12`'s `submissionUntracked_` handling is correct.
* No compute shader dispatched; `ApplyResultShaping` returns at `Renderer.cpp:1959` (15-float compare), `RecordTemporalHistory` gated by `temporalTierSupported`, `ApplyAdaptiveHandoff` gated by `adaptiveResolution`, `PrepareAdjacentPrewarmResources` resets 12 scalars, `CopyDepthGuide` bypassed because the intermediates are `R32_TYPELESS` (`Renderer.cpp:2845`), `DispatchModelInput/Resolve` bypassed at tier 100.
* No per-frame allocation besides the F11 strings; no `logger::` calls in the hot path (all guarded by once-flags/transition checks); resources recreated only on dimension/format change (`Matches`, `Renderer.cpp:221-226`); `EnsureTierResources` computes descs but returns before any `std::string`/creation.
* Profiler disabled cost: `ScopedGpuPass` ctor = `IsEnabled()` (two atomic loads) + `state->frameAnnotations` check (`GpuPass.cpp:84-120`); ~17 pass objects per frame on this path.
* Handle hygiene: `CloseHandle` for shared texture/fence handles (`D3D12Interop.cpp:59,182`), event closed in `Shutdown`; `RestoreRenderTargets` releases RTV/DSV on every exit after `OMGetRenderTargets` (`Integration.cpp:1389-1655`); `DeferredRendererStates` intentionally leaks only a still-in-flight `State`.
* Eye-split math (`x = (eye ? eyeWidth : 0) + uint(eyeWidth*uv.x)`, `sourceBox`, `outputBox`, MV scale `motionScale * fullGuide*`, `GuideToColorMotionScale`, `ComputeNeuralCoverageRect`/`AlignNeuralCoverageRectsForStereo` parity/rounding and the 17-entry candidate array) is internally consistent; no off-by-one found at 2496x2688 / 1468x1580.

## 5. Suggested order of work
1. F1 + F2 (kills a 4.7 s freeze per load/reset and per cache clear; bit-exact).
2. F3 as a 5-minute A/B (`Flush()` after `Signal`), then compute-queue experiment only if a gap remains in GPUView.
3. F4, F6, F11, F14 (CPU-side, bit-exact, ~100-300 us/frame render thread together; matters if the render thread is the stall behind the GPU queue).
4. F5 (1-4) + F7 + F9 before enabling periphery by default in 2.22.
5. F12/F13/F15/F20 correctness fixes.
