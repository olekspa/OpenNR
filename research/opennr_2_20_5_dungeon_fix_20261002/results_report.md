# OpenNR 2.20.5 dungeon lighting/history safeguards — results report

## Status

**DONE: packaged, independently verified, installed to a new MO2 mod/profile, selected, and committed locally.** No game was launched. The package and install preserve OpenNR 2.20.4 and its profile.

## Source and commit

- [SOURCE-VERIFIED] Branch `codex/opennr-2.20.5`, based on requested `70c7a08c42f431ab25e2880fc8878c115010e610`.
- [SOURCE-VERIFIED] Implementation commit `8ab8e79bd9ed8bc6587dbf13a9c0483e3893a221` (`feat(opennr): add dungeon lighting and history safeguards`); no push.
- [SOURCE-VERIFIED] Changed files: `VERSION`; CMake version and source contracts; release changelog and eye-tracking package note; `LightLimitFix.cpp/.h`, `LightLimitFix/ShadowCasterManager.h`, `ShadowRenderer.cpp`, `ShadowScheduler.cpp`, new `SceneHistoryResetPolicy.h` and `ShadowTilePolicy.h`; `Upscaling.cpp`; `ProfilingRenderer.cpp`, `Profiler.h`; C++ test CMake and new reset/tile policy tests; dungeon source-contract and profiler tests.
- [SOURCE-VERIFIED] Implemented F1 fail-open for unsuppressed spotlights without a valid tile (toggle default ON); F2 bounded reset drain at the safe schedule boundary while retaining the VR display guard; F3 scene identity history-reset fan-out; F4 low-cost scene/reset/SCM diagnostics and overlay counters; F5 profiler timer capacity of 1024 and visible dropped-pass warning.

## Tests and build

- [LOCAL-MEASURED] Build configured with `OPENNR_PERFORMANCE_OVERLAY=ON`; cache: `<BUILD_DIR>\2.20.5\build\CMakeCache.txt`.
- [LOCAL-MEASURED] Package target reran and passed the C++ suite: 10,426 assertions in 276 test cases.
- [LOCAL-MEASURED] Package target ran shader tests via the local GPU test wrapper; 209 HLSL functions discovered and 217 assertions passed in 3 cases. The broker lease was released at job completion.
- [LOCAL-MEASURED] Prior focused source-contract pytest: 25 passed. CMake source-contract and AIO package-manifest validation passed during packaging.
- [INVALIDATED] Broad `python -m pytest runtime/open-shaders/tests -q`: 146 passed, 8 skipped, 28 failed, 223 subtests passed. Failures are environment imports for missing `hlslkit` and `yaml`; recorded without changing the Python environment.
- [LOCAL-MEASURED] Archive integrity check (`7z t`) passed. Extracted 531 files; SHA-256 map matched the manual package stage for all files. Package verification artifact: `<RESEARCH_DIR>\opennr_2_20_5_dungeon_fix_20261002\package_manifest.json`.
- [LOCAL-MEASURED] Package DLL file version `2.20.5.0`, SHA-256 `565845b83cde97bd83565525151d2e6e8e5d2c4eef70c432076efe731a1a0403`; it matches the built DLL. `nvngx_dlssnr.dll` SHA-256 `e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e`.
- [LOCAL-MEASURED] Shipped Settings Benchmark contains the 21 variant IDs defined by source. Performance overlay marker and `OPENNR_PERFORMANCE_OVERLAY=ON` were verified in build/package inputs.

## Package and MO2 install

- [LOCAL-MEASURED] Package: `<BUILD_DIR>\2.20.5\build\dist\OpenNR 2.20.5.7z` (228,871,241 bytes; SHA-256 recorded in `package_manifest.json`).
- [LOCAL-MEASURED] Extracted package: `<RESEARCH_DIR>\opennr_2_20_5_dungeon_fix_20261002\extracted-package`; all 531 extracted file hashes match the stage and installed mod.
- [LOCAL-MEASURED] New mod: `<MO2_INSTANCE>\mods\OpenNR 2.20.5`. Independent install verification matched all 531 packaged file hashes; only this mod is an enabled `CommunityShaders.dll` provider in the new profile.
- [LOCAL-MEASURED] New profile `<MO2_PROFILE>` cloned from `<MO2_PROFILE>`; all 56 profile files match except the intentional modlist switch (`-OpenNR 2.20.4`, `+OpenNR 2.20.5`). Overwrite `SettingsUser.json` and `CommunityShaders_ImGui.ini` were backed up and their hashes remained unchanged.
- [LOCAL-MEASURED] Independent profile readback confirms `selected_profile=@ByteArray(<MO2_PROFILE>)` in `<MO2_INSTANCE>\ModOrganizer.ini`. Install details and hashes: `<RESEARCH_DIR>\opennr_2_20_5_dungeon_fix_20261002\install_manifest.json`.
- [LOCAL-MEASURED] Pre-install MO2 INI backup: `<RESEARCH_DIR>\opennr_2_20_5_dungeon_fix_20261002\ModOrganizer.ini.before-install`; rollback: `<RESEARCH_DIR>\opennr_2_20_5_dungeon_fix_20261002\rollback.md`.
- [LOCAL-MEASURED] No SkyrimVR, SteamVR, or Mod Organizer process was running during installation. No game launch or headset test was performed.

## Morning headset checklist

See `MORNING_TEST.md`. It uses the same crypt/cave save and isolates F1, F3, SCM diagnostics, interior depth-buffer culling, SSGI stereo reprojection, and periphery stagger, with expected log lines and a 2.20.1 comparison if the dungeon remains dark.

## Deviations / evidence limits

- [LOCAL-MEASURED] The requested archive is under the build's configured `build\dist` directory, within `<BUILD_DIR>\2.20.5`; it was not copied to a second archive location.
- [INVALIDATED] The complete Python pytest suite is not green because of missing environment dependencies noted above. C++/shader/source-contract/package acceptance gates passed.
- [INFERENCE] Runtime diagnosis is not established by this release build alone. The user should perform the A/B checklist in the headset and share screenshots plus `[OpenNR-Scene]`, `[Upscaling-History]`, `[SCM-Dungeon]`, and `[LLF-Dungeon]` lines.

## Asks

After the headset session, share the toggles/screenshots and the listed INFO/diagnostic lines; no other decision is needed before the A/B.