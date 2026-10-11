# OpenNR 2.20.2 — shader correctness and runtime safety

Release branch: `codex/opennr-2.20.2`, based on `9b634087` (2.20.1).
Build root: `<BUILD_DIR>/2.20.2`.
Evidence folder: `<RESEARCH_DIR>/opennr_2_20_2_shader_patch_20260930`.

## Rendering corrections

- [SOURCE-VERIFIED] Standard VR DLSS uses the encoder's current-frame `R32_FLOAT` right-eye depth guide instead of a boxed depth-stencil copy, which D3D11 does not permit. The original left-eye depth route is preserved. Encoder shader caches distinguish DLSS from FSR, including typed-depth permutations.
- [SOURCE-VERIFIED] Menu sharpening requires the active DLSS method and follows the foveated route's actual result for the current frame. The frame result clears before frame-state updates and early returns.
- [SOURCE-VERIFIED] Foveated periphery history is reseeded after a missed/failed route, repeated frame, or resource clear. Only successful route execution advances the continuity stamp.
- [SOURCE-VERIFIED] SSGI's masked/first-person early return initializes every declared output, including AO and GI specular. GI without specular is covered independently.

These changes adapt [Open Shaders #779](https://github.com/alandtse/open-shaders/pull/779), [#782](https://github.com/alandtse/open-shaders/pull/782), [#783](https://github.com/alandtse/open-shaders/pull/783), and [#791](https://github.com/alandtse/open-shaders/pull/791). They preserve full-resolution native Feature 18 detail processing and existing opt-in temporal settings.

## Lifetime corrections

- [SOURCE-VERIFIED] Shadow-light bulk teardown closes a shared admission gate covering scheduler snapshots and render passes. It drains admitted work before invoking the destructive engine routine. Concurrent clears serialize, same-thread recursive clears coalesce/defer while retaining the scene node, and the completed clear republishes the pending reset before reopening admission. Deferred requests execute on their requesting thread after its outermost reader exits. This is a fail-safe change for previously unsafe reentrancy; engine callback behavior still needs runtime acceptance.
- [SOURCE-VERIFIED] Native NR treats `UINT64_MAX` as device removal, verifies the fence after an event wake-up, and retains resources when a bounded wait does not prove completion. Submission is marked untracked before execution and a failed signal needs a completed drain marker before resources can be reused. Reset drains without releasing interop objects, shuts down NGX, then releases interop and eye objects. Timeout failures remain recoverable.
- [SOURCE-VERIFIED] NGX export pointers retain their supplying DLL references. The signed-runtime IAT proxy uses synchronized, process-lifetime state and short active-call scopes. The Runtime singleton has process lifetime, retaining its existing strong device reference; static destruction cannot release its NGX handles ahead of renderer fence checks. An unresolved submission retains the complete renderer state until completion/device removal or process exit.

The D3D11 depth-copy restriction is documented by [Microsoft's copy contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion), and the device-removal sentinel is documented by [the fence API](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue).

These paths assume the existing Skyrim integration's render-thread ownership. The internal APIs are not general concurrent APIs. Explicit dynamic unloading of OpenNR while NGX remains loaded is not supported by the process-lifetime retention policy.

## Source tools and build gates

- [SOURCE-VERIFIED] The existing uncommitted student residual-gain support is retained. Gain must be finite and nonnegative. ONNX reuse verifies checkpoint, exporter/model implementation, PyTorch/opset, resolution, gain, and artifact hash. TensorRT reuse additionally verifies version, build configuration, target GPU identity, ONNX hash, and engine hash. Invalid/missing manifests regenerate; changed ONNX source inputs require a fresh directory.
- [SOURCE-VERIFIED] The manual AIO target depends on C++ and shader tests. The local build helper supplies the mandatory GPU broker wrapper for shader test execution; compilation and packaging do not hold the GPU lease.
- [SOURCE-VERIFIED] Version metadata, source contracts, current README links, example paths, eye-tracking documentation, and the packaged changelog are 2.20.2. The previous changelog is archived rather than rewriting history.

## Acceptance boundary

The supplied log, crash report, and Discord symptom reports are from different users. An implementation fix and a passing build do not establish that a particular report is resolved. The shadow crash's exact cause remains unproven.

No SkyrimVR or SteamVR launch is part of this release preparation. Same-save house/cave testing, loading/menu transitions, both headset eyes, and a matching runtime log are the next visual/runtime checks. Source tools do not promote a research student or alter the accepted V5/Turbo installation.

The dirty teacher-bench/cache research work and experimental timing/flush/batched-transition changes were reviewed separately and are not promoted into this runtime package. A separate possible portal-graph transition/lock-order concern remains an investigation item; it is not established as the supplied crash cause and is not addressed by the shadow-light gate.

## Installation and rollback

The local installation targets a separate mod `<MO2_INSTANCE>/mods/OpenNR 2.20.2` and a clone named `<MO2_PROFILE>`. The old profile, saves, and overwrite settings are preserved. The installer checks the package manifest, DLL version, active DLL providers, cloned profile files, settings hashes, and selected-profile readback.

Detailed validation, hashes, installation evidence, and rollback instructions are recorded in the dated evidence folder. Local preparation does not publish the package or push the branch.

## Completed local validation

- [LOCAL-MEASURED] Release C++ suite: 259 cases, 9,838 assertions passed; GPU shader suite: 207 assertions passed under broker lease `30d7c0dd-9649-4e3d-af73-93edf1f93330`, subsequently released. Evidence: `<RESEARCH_DIR>/opennr_2_20_2_shader_patch_20260930/package_build.log` and `gpu_status_after_tests.json`.
- [LOCAL-MEASURED] Python cache tests: 14 passed; upscaling source contracts: 3 passed. Evidence: `python_final_tests.log` in the same folder.
- [LOCAL-MEASURED] `OpenNR 2.20.2.7z` passed 7-Zip integrity validation. All 528 extracted files match staging, and all 528 installed files match extraction. DLL file version is `2.20.2.0`; SHA-256 is `cc39f4f3fb018d81ab357f720847d1c7d34e3dd900ac6751d91b5e601155bc86`. Evidence: `package_manifest.json` and `independent_install_verification.json`.
- [LOCAL-MEASURED] MO2 selected profile is `<MO2_PROFILE>`, with exactly one enabled mod providing `CommunityShaders.dll`: `OpenNR 2.20.2`. The original profile and overwrite configuration hashes are unchanged; the only MO2 INI content change is selected-profile selection. Evidence: `install_manifest.json`, `independent_install_verification.json`, and `rollback.md`.

The archive is local at `<BUILD_DIR>/2.20.2/dist/OpenNR 2.20.2.7z` (228,850,327 bytes; SHA-256 `dc7bc2b2745413d67877d864401f4e77e0fa2293287d752fcfcc9d9045d63354`). No in-game visual or crash acceptance is claimed.
