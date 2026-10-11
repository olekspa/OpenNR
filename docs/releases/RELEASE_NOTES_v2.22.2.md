## OpenNR 2.22.2 - map terrain quality test build (pre-release)

OpenNR adds DLSS Neural Rendering and VR-specific work to Skyrim VR (a fork of Open Shaders / Community Shaders).
2.22.2 is a small test build on top of 2.22.1 for people who want to experiment with the soft world-map terrain.
**Everything new is off or neutral by default**, so if you do nothing, 2.22.2 behaves like 2.22.1.

### What's new in 2.22.2
- Three map-only controls, applied only while the world map is open: `vrMapDlssPreset` (0 = unchanged, 1-4 = DLSS preset J/K/L/M), `vrMapSharpness` (0 to 1, default 0), `vrMapMipBiasOffset` (-3 to 0, default 0).
- One log line each time you open the map, `[PerfMode][MapQuality] ...`, showing what resolution the map scene is drawn at.

### Included from 2.22.0 and 2.22.1 (if you are coming from 2.21.x)
- Faster default: Recommended / Reset / first run select the hybrid NR setup (full-resolution NR over the centre 55%, 50% whole-eye periphery NR). In the developer's 2.20.4 benchmark scene this cut NR work by about 41% versus full-eye NR; your numbers will differ.
- New presets "Quality: full-eye NR (slowest)" and "Safe mode" (NR off, your tuning kept).
- Startup prewarm to move the first-frame hitch to the loading screen (effect not yet measured).
- Sharper world-map icons and labels (`vrMapSymbolSupersample`, default 2.0; set 1.0 to turn it off).
- Skylighting hardening ported from Open Shaders, aimed at black dungeon interiors.
- Many NR robustness and log-noise fixes. Full list: [CHANGELOG.md](../../CHANGELOG.md).

### Requirements
- Skyrim VR 1.4.15 (the tested version), SKSE VR, VR Address Library, an NVIDIA RTX GPU with a current driver.
- Third-party runtime files that this download does not contain (see below), including the NVIDIA Neural Rendering runtime `nvngx_dlssnr.dll` 310.8.x that you must supply yourself.
- Install the archive as a mod in Mod Organizer 2 (or extract into `Data`). Back up your saves and try a new profile first.

### What is NOT included in the download, and why
- **`nvngx_dlssnr.dll` (NVIDIA Neural Rendering runtime).** It is NVIDIA's proprietary file and this project has no licence to redistribute it. Without it Neural Rendering stays off and the menu says why; the rest of OpenNR still works. You must supply a build you are entitled to use; the plugin accepts file version 310.8.x only.
- **Other NVIDIA binaries** (DLSS `nvngx_dlss.dll`, `nvngx_dlssg.dll`, Streamline `sl.*.dll`) and the AMD FidelityFX DLLs, RenderDoc and imgui-vr-helper: these belong to their vendors and are not part of this project's download. Take them from the vendors' official releases. The licence texts that apply are in the source repository (see [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)).
- **Debug symbols (`.pdb`)**: not shipped.

The exact file list of the archive is stated by the release page; check it against this section.

### Known issues / not verified
- **Nothing in this release was run in a headset by the developer.** The code builds and automated tests pass; headset behaviour is what testers are for.
- Map terrain is still soft. These controls are experiments and may not improve it; `vrMapMipBiasOffset` only affects OpenNR's own shader paths, and whether the native map terrain material uses that value is unknown.
- 2.22.0 / 2.22.1 items still awaiting an in-game check: hybrid seam quality, performance numbers, dungeon Skylighting, startup prewarm, map symbol supersample, and the experimental parallel-queue and flush-after-signal toggles (off by default).
- A possible duplicate renderer initialisation about 12 s after boot was not changed.
- Eye tracking support is experimental and off by default.

### How to test the map controls
Same map spot, zoom and pose; change one control at a time (live): preset M versus unchanged, sharpness 0.25 then 0.50 (watch for halos), mip offset -1.0 then -2.0. Set a control back to its neutral value to disable it. Report the `[PerfMode][MapQuality]` and `[PerfMode][Map] size-chain` log lines and which setting you were viewing.

### If something goes wrong
Use the Safe mode preset, or roll back to the previous version, then open an issue with `CommunityShaders.log` (`Documents\My Games\Skyrim VR\SKSE\`).

### Source and licence
Source: tag `v2.22.2` of this repository. Licence: GPL-3.0-or-later with the Modding and Linking exceptions (see [NOTICE.md](../../NOTICE.md)).

SHA-256 of the archive: `4e1ae299ee183e1cd890f1222dea2bc447f20182480738d7a3bdfadbbc9efe38` (OpenNR-2.22.2-plugin-only.7z, 12,363,169 bytes)
