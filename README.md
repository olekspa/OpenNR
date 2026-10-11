# OpenNR

**DLSS Neural Rendering for Skyrim VR.** OpenNR is a Skyrim VR build of [Open Shaders](https://github.com/alandtse/open-shaders) (itself a fork of [Community Shaders](https://github.com/community-shaders/skyrim-community-shaders)) that integrates NVIDIA's DLSS Neural Rendering model and adds VR-specific work to make it practical in a headset.

> **Status: test builds.** The latest release is v2.22.2. The developer builds and tests the code, but has not verified every feature in a headset; each release lists what is and is not verified. Please report what you see (see [Reporting problems](#reporting-problems)).

## What it adds

- **Neural Rendering (NR) in VR**, evaluated per eye at full model resolution over the part of the image where it matters.
- **Hybrid "Periphery NR" (default since 2.22.0):** a full-resolution NR crop over the centre of each eye (default 55% coverage), plus a whole-eye NR pass at 50% resolution for the periphery, blended at the crop edge. In the developer's 2.20.4 in-game benchmark (one scene) this cut NR cost by about 41% compared with full-eye NR. Your numbers will differ; use the built-in benchmark.
- **NR coverage crop**, an optional projection-centred crop, and **per-eye X/Y crop offsets** (up to +/-200 px) for headsets whose lens centres differ from Quest 3.
- **Settings Benchmark:** stand still in a scene and OpenNR measures, with the SteamVR compositor's GPU/CPU frame times, what each candidate setting saves on your machine, then restores your settings.
- **Startup prewarm** of the NR slots on menu/loading frames, meant to move the first-frame hitch to the loading screen (effect not yet measured).
- **VR world map:** the map is drawn at display resolution and map icons and labels are supersampled (`vrMapSymbolSupersample`, default 2.0). Optional map-only DLSS preset, sharpening and mip-bias controls exist (off by default). Map terrain is still soft.
- **Skylighting hardening** ported from Open Shaders, aimed at black dungeon interiors (2.20.7, 2.22.0; not yet verified in a headset for 2.22.x).
- Experimental, off by default: a parallel per-eye compute queue and flush-after-signal toggles; eye tracking.

The full list is in [CHANGELOG.md](CHANGELOG.md).

## Requirements

- **Skyrim VR 1.4.15** (the version OpenNR is developed and tested on), [SKSE VR](https://skse.silverlock.org/) and the VR Address Library for SKSEVR.
- An **NVIDIA RTX GPU** and a current NVIDIA driver. The developer tests on an RTX 5070 Ti with a Quest 3; these are not minimum requirements.
- A mod manager is recommended (Mod Organizer 2 or Vortex).
- **The NVIDIA Neural Rendering runtime, `nvngx_dlssnr.dll`, version 310.8.x - not included.** It is NVIDIA's proprietary file and this project does not distribute it, link to it or tell you where to find it. You must supply a build that you are entitled to use. Put it in `Data/Shaders/Upscaling/Streamline/` (with Mod Organizer 2, in the same mod folder). What the plugin checks (from `Runtime.cpp`): it looks for the file in `Shaders/Upscaling/Streamline/`, then `Shaders/Upscaling/StreamlineDX12/`, reads the file's version resource and accepts only version 310.8.x; it then loads the DLL and requires a fixed set of NGX exports. It does **not** check a SHA-256 allow-list. If the file is missing, has no version resource, is not 310.8.x, fails to load or lacks the exports, Neural Rendering stays off, the reason is logged and shown in the menu, and the rest of OpenNR keeps working.
- **DLSS and Streamline runtime files** (`nvngx_dlss.dll`, `nvngx_dlssg.dll`, `sl.*.dll`) and, if you use them, the AMD FidelityFX DLLs are also not part of this project's download. Take them from the vendors' official releases. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- Enough VRAM for DLSS at your render scale (OpenNR logs a VRAM warning when it is tight).

OpenNR keeps the `CommunityShaders.dll` name and the `SKSE/Plugins/CommunityShaders/` folder, so existing Community Shaders / Open Shaders settings and themes carry over. **Install only one of Community Shaders, Open Shaders or OpenNR.**

## Install

1. Download the OpenNR archive for your version from the [Releases](https://github.com/olekspa/OpenNR/releases) page. The release is a lean "plugin-only" package: the OpenNR plugin and its shaders, interface assets, translations and licence files, but none of the NVIDIA or other third-party runtime binaries. The release page lists the exact contents.
2. Install it as a mod (Mod Organizer 2: *Install a new mod from an archive*; Vortex or manual: extract into the game's `Data` folder). Make sure no other Community Shaders / Open Shaders mod provides `CommunityShaders.dll`.
3. Add the third-party runtime files from their official sources as described under Requirements, including your own `nvngx_dlssnr.dll`.
4. Start the game through SKSE, open the in-game menu, select NVIDIA DLSS as the upscaler, then enable Neural Rendering.
5. **Back up your saves and use a new MO2 profile the first time**, and keep the previous version so you can roll back. First launch compiles shaders; expect a long first load.

## Choosing settings

Menu pages (labels as in the code): **Neural Rendering** (the Neural Rendering page, header "DLSS Neural Rendering") and **Upscaling** (DLSS controls and the "Settings Benchmark (measure in headset)" section; moved there in 2.21.6).

| Goal | What to do |
|---|---|
| Start here | Press **Recommended** on the Neural Rendering page (also what a fresh install and "Reset" select since 2.22.0): Full Eye route, full-resolution NR over 55% centre coverage, staggered 50% whole-eye periphery NR, projection-centred crop. |
| Best quality, slowest | **Quality: full-eye NR (slowest)**: full-resolution NR over the whole eye. |
| Something looks wrong | **Safe mode**: turns Neural Rendering off but keeps your tuning. Then report the problem. |
| Find what is cheap on your PC | Run the **Settings Benchmark** (Upscaling page, VR). Stand still, close the menu, wait. It writes a JSON report to `Documents\My Games\Skyrim VR\SKSE\OpenNR-SettingsBenchmark\` (`benchmark-<timestamp>.json`, next to `CommunityShaders.log`). |
| Crop not centred on your lens | Adjust the per-eye X/Y crop offsets (+/-200 px). |
| Map icons look wrong | Set `vrMapSymbolSupersample` to 1.0 to turn the supersample off. |

Saved user settings are not rewritten by an upgrade. If a value looks odd after upgrading, press Recommended.

## Known limitations

- Releases are not tested in a headset by CI. v2.22.x as a whole is "builds and automated tests pass, headset check pending".
- World map terrain is still soft; only icons and labels were improved.
- The first world frame may hitch while Neural Rendering initialises (prewarm is meant to move it to the loading screen; unmeasured).
- A visual seam between the NR centre crop and the periphery may be visible in some scenes; compare with the full-eye preset.
- Experimental toggles (parallel eye queue, flush-after-signal, eye tracking) are off by default and unverified.
- Neural Rendering needs an NVIDIA GPU and the user-supplied runtime above.

## Troubleshooting

- **Log:** `Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`. Lines to look for: `[DLSSNR]` (including `Periphery NR active/inactive` with the reason, `[DLSSNR][PREWARM]`, `[DLSSNR][Benchmark]`), `[PerfMode][MapSS]` and `[PerfMode][MapQuality]`. The Neural Rendering page also states why NR or Periphery NR is off.
- **NR is off / "not found" / "unsupported version":** the runtime DLL is missing, has no version resource, or is not a 310.8.x build.
- **Black dungeon or interior:** try the Skylighting neutral-output setting, then report it with the log.
- **Roll back:** disable the OpenNR mod or select the previous MO2 profile.

## Reporting problems

Open an [issue](https://github.com/olekspa/OpenNR/issues/new/choose) and attach: `CommunityShaders.log` from the session with the problem (play about two minutes, then quit the game normally), the OpenNR version, headset, GPU and driver version, your NR settings (or a screenshot of the Neural Rendering page), and whether the problem goes away with Safe mode. For performance reports attach the benchmark JSON.

## Building from source

See [CONTRIBUTING.md](CONTRIBUTING.md) and [runtime/open-shaders/README.md](runtime/open-shaders/README.md) (CMake presets and packaging targets). Building needs Visual Studio 2026, CMake 4.2+, vcpkg and the git submodules (`git clone --recursive`). Note: the manual AIO packaging path in `runtime/open-shaders/cmake` was written to take a locally supplied `nvngx_dlssnr.dll`; check the cmake files for the current behaviour before relying on it.

## Repository layout

| Path | Contents |
|---|---|
| `runtime/open-shaders/` | The game plugin (C++/HLSL) and release packaging. This is the part most people want. |
| `runtime/open-shaders/docs/releases/` | Per-version changelogs. See also [CHANGELOG.md](CHANGELOG.md). |
| `docs/` | Release notes, audits and dated development notes; see [docs/README.md](docs/README.md). |
| `tools/`, `native/`, `experiments/`, `research/`, `tests/` | Research tooling and tests. Not needed to use or build the mod. |

## Credits and licence

- OpenNR builds on [Open Shaders](https://github.com/alandtse/open-shaders) by Alan Tse and contributors and [Community Shaders](https://github.com/community-shaders/skyrim-community-shaders) by doodlum, Tim and contributors. Most of the code is theirs. OpenNR is not an official Open Shaders or Community Shaders project and does not use their logos.
- Ideas and ports are credited in [BRANDING_AND_ATTRIBUTIONS.md](runtime/open-shaders/BRANDING_AND_ATTRIBUTIONS.md), including DoNotHeat/openshaders-nr (per-eye offsets), Open Shaders PR #890 (parallel eye queue), ParticleTroned/skyrim-community-shaders (map symbol supersample) and the upstream Skylighting, grass and map-fog ports.
- NVIDIA DLSS, Streamline, Reflex and Neural Rendering are NVIDIA technologies and trademarks; OpenNR claims no ownership of vendor binaries and does not distribute them.
- Licence: **GPL-3.0-or-later with the Modding Exception and the GPL-3.0 Linking Exception** - see [LICENSE](LICENSE), [EXCEPTIONS.md](EXCEPTIONS.md) and [NOTICE.md](NOTICE.md). The OpenNR logo and name are not part of the GPL-licensed work (see NOTICE.md). Third-party components keep their own licences: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
