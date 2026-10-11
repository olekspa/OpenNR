# Third-party notices

OpenNR is licensed under GPL-3.0-or-later with the Modding and Linking exceptions (see [NOTICE.md](NOTICE.md),
[LICENSE](LICENSE), [EXCEPTIONS.md](EXCEPTIONS.md)). The components below keep their own licences. This file is a
good-faith inventory, not legal advice. Items marked **[unverified]** could not be confirmed from the repository
contents alone.

"Redistributed here" means the component's source or binary is committed to this repository or shipped in an OpenNR
release archive. "No" means it is fetched or built from its official source at build time, or must be supplied by the
user.

Paths below are relative to the repository root. `RS` stands for `runtime/open-shaders`.

## 1. Project lineage and code ports

| Component | Licence | Source | Redistributed here |
|---|---|---|---|
| Community Shaders (doodlum, Tim and contributors) | GPL-3.0-or-later with the Modding and Linking exceptions | https://github.com/community-shaders/skyrim-community-shaders | Yes (inherited source, via Open Shaders) |
| Open Shaders (Alan Tse and contributors) | GPL-3.0-or-later with the Modding and Linking exceptions | https://github.com/alandtse/open-shaders | Yes (OpenNR is a fork; selected upstream fixes were ported, see `RS/BRANDING_AND_ATTRIBUTIONS.md`) |
| DoNotHeat/openshaders-nr (per-eye offset concept, commit `bfc9bf9`) | GPL-3.0 | https://github.com/DoNotHeat/openshaders-nr | Concept only; no source copied |
| ParticleTroned/skyrim-community-shaders (map symbol supersample concept, commit `7cdfa07c5`) | GPL-3.0 (repository also contains an LGPL file) | https://github.com/ParticleTroned/skyrim-community-shaders | Concept only; no source copied |
| OpenDLSS-NR (maanHimself) | MIT | https://github.com/maanHimself/OpenDLSS-NR | Research reference only; no source included |

## 2. Libraries compiled into the plugin (CommunityShaders.dll)

Most of these come from vcpkg (`RS/vcpkg.json`, baseline `dddca6fa87f177e0678e2545c4b4636a44aa05bd`) and are
linked statically. Only the versions pinned by `RS/vcpkg.json` overrides are stated; other versions are whatever that
vcpkg baseline provides. The licence column was taken from the vcpkg `copyright` files of the local release build
and is **[unverified]** against upstream for every row except where a licence file is listed in this repository.

| Component | Version | Licence | Source | Redistributed here |
|---|---|---|---|---|
| CommonLibSSE-NG (fork `alandtse/CommonLibVR`) | submodule pin `adb3e2c4` | GPL-3.0 with Modding exceptions; also MIT and HDE64 notices in its `licenses/` folder | https://github.com/alandtse/CommonLibVR | No (git submodule; linked into the plugin) |
| SKSE / SKSE VR | user installs | Own terms | https://skse.silverlock.org/ | No (required by the user) |
| Dear ImGui (`docking` branch features) | 1.92.6 | MIT | https://github.com/ocornut/imgui | No (vcpkg) |
| spdlog | vcpkg baseline | MIT | https://github.com/gabime/spdlog | No (vcpkg) |
| {fmt} | 12.2.0 | MIT | https://github.com/fmtlib/fmt | No (vcpkg) |
| DirectXTex, DirectXTK, DirectXMath, DirectX-Headers, Effects11 | vcpkg baseline; DirectX-Headers 1.614.1 | MIT | https://github.com/microsoft | No (vcpkg) |
| nlohmann/json | vcpkg baseline | MIT | https://github.com/nlohmann/json | No (vcpkg) |
| xbyak | vcpkg baseline | BSD-3-Clause (MITSUNARI Shigeo) | https://github.com/herumi/xbyak | No (vcpkg) |
| Tracy profiler | 0.14.1 | BSD-3-Clause (Bartosz Taudul) | https://github.com/wolfpld/tracy | No (vcpkg) |
| EASTL / EABase | EASTL 3.27.1, EABase 2025-08-01 | BSD-3-Clause | https://github.com/electronicarts/EASTL | No (vcpkg) |
| xxHash | vcpkg baseline | BSD-2-Clause | https://github.com/Cyan4973/xxHash | No (vcpkg) |
| Microsoft Detours | 4.0.1 | MIT | https://github.com/microsoft/Detours | No (vcpkg) |
| bshoshany-thread-pool | 5.1.0 | MIT | https://github.com/bshoshany/thread-pool | No (vcpkg) |
| clib-util, cppwinrt, devbench-api, exprtk, efsw, magic-enum, unordered-dense, RenderDoc in-app API header | vcpkg baseline | MIT (as reported by the vcpkg copyright files) | via vcpkg | No (vcpkg) |
| rapidcsv, pystring | vcpkg baseline | BSD-3-Clause | via vcpkg | No (vcpkg) |
| Catch2 (unit tests only) | vcpkg baseline | BSL-1.0 | https://github.com/catchorg/Catch2 | No (vcpkg; tests only) |
| stb | vcpkg baseline | MIT or public domain | https://github.com/nothings/stb | No (vcpkg) |
| `stb_image_write_hdr_png.h` (from ReShade, commit `34e108a6`) | n/a | Public domain / Unlicense (see `RS/extern/stb_image_write_hdr_png/PROVENANCE.md`) | https://github.com/crosire/reshade | Yes (vendored header) |
| FidelityFX SDK headers (vendored) and SDK fork `alandtse/FidelityFX-SDK-DX11` | headers in `RS/vendor/include/FidelityFX/`; SDK submodule pin `b685e46b` | MIT (AMD header in each file) | https://github.com/alandtse/FidelityFX-SDK-DX11 | Headers: yes. SDK: no (submodule) |
| NVIDIA NVAPI | submodule pin `cd6918f6` | MIT (NVIDIA, 2024) | https://github.com/NVIDIA/nvapi | No (submodule) |
| NVIDIA Streamline SDK (source) | submodule pin `2122257e` (v2.14.1 according to the local release audit, **[unverified]**) | MIT for the source; NVIDIA licences for binaries and third-party parts | https://github.com/NVIDIA-RTX/Streamline | No (submodule) |
| TrueDirectionalMovement API header | n/a | **[unverified]** inherited from upstream; the header invites modders to copy it | `RS/vendor/include/TDM/` | Yes |

## 3. Shader and data snippets with their own licences (in this repository)

Licence files already in the tree must be kept next to the files they cover.

| Where | Licence | Notes |
|---|---|---|
| `RS/package/Shaders/LICENSE` | MIT, Copyright (c) 2022 Ilya Perapechka | Shared shader code |
| `RS/package/Shaders/Common/Spherical Harmonics/LICENSE` | MIT, Copyright (c) 2018 SebH | |
| `RS/features/Cloud Relight/Shaders/CloudRelight/LICENSE` | MIT, NVIDIA | Also an SPDX header in `Draine.hlsli` |
| `RS/features/Procedural Sun/Shaders/ProceduralSun/LICENSE` | MIT | |
| `RS/features/Post Processing/Shaders/PostProcessing/ColorGrading/Include/RenoDX.MIT.LICENSE` | MIT, RenoDX (Carlos Lopez Jr.) | |
| Files with an SPDX header (Apache-2.0 snippet in `Color.hlsli`, MIT in several Screen-Space GI files) | as stated in each file | Keep the headers |

## 4. Fonts (`RS/package/Interface/CommunityShaders/Fonts/`)

| Font | Licence | Licence text in the repository |
|---|---|---|
| IBM Plex Mono / Sans / Serif, Inter, Jost, OpenDyslexic, Roboto, Rubik | SIL Open Font License 1.1 | Yes (`OFL.txt` in each folder) |
| Sanguis, Sovngarde | SIL OFL, as supplied by the font author | Yes (`... OFL License.txt`) |
| Crimson Pro (Copyright 2018 The Crimson Pro Project Authors) | SIL Open Font License 1.1 (per the font's metadata) | Yes (`CrimsonPro/OFL.txt`, standard OFL 1.1 text with the copyright line from the font) |
| Roboto Slab (Copyright 2018 The Roboto Slab Project Authors) | Apache License 2.0 (per the font's metadata) | **No.** Licence text: http://www.apache.org/licenses/LICENSE-2.0 . A copy should be added next to the font |
| Font Awesome Free 6.5.2 (`fa-solid-900.ttf`) | Font Awesome Free licence: fonts under SIL OFL 1.1, icons under CC BY 4.0 (https://fontawesome.com/license/free) **[unverified: not re-checked offline]** | **No.** Attribution to Font Awesome (https://fontawesome.com) applies |

## 5. NVIDIA components

Credit line (as requested by NVIDIA's RTX SDK supplement): this software uses NVIDIA DLSS, Streamline and Reflex
technology. NVIDIA, DLSS, Reflex, GeForce and RTX are trademarks or registered trademarks of NVIDIA Corporation.

| Component | Licence | Redistributed here |
|---|---|---|
| NVIDIA DLSS SDK: `nvngx_dlss.dll`, `nvngx_dlssg.dll` | NVIDIA RTX SDKs License (text: `RS/features/Upscaling/Shaders/Upscaling/Streamline/nvngx_dlss.license.txt`) | **No.** Binaries are not part of this project's distribution; obtain them from NVIDIA's official DLSS / Streamline releases (https://github.com/NVIDIA/DLSS , https://github.com/NVIDIA-RTX/Streamline) |
| NVIDIA Streamline runtime: `sl.interposer.dll`, `sl.common.dll`, `sl.dlss.dll`, `sl.dlss_g.dll`, `sl.pcl.dll`, `sl.reflex.dll` | MIT notice (`.../Streamline/license.txt`, `.../StreamlineDX12/license.txt`); Reflex parts under the NVIDIA SDK licence (`reflex.license.txt` in both folders) | **No.** Obtain from the official Streamline release |
| **NVIDIA DLSS Neural Rendering runtime: `nvngx_dlssnr.dll` (310.8.x)** | NVIDIA proprietary. No licence permitting redistribution has been found by this project. | **No, never.** OpenNR does not ship it, and this project does not say where to obtain an unofficial copy. The user must supply a build that they are legally entitled to use; the plugin checks its file version (see README). |

The licence texts above are kept in the repository next to the folders where the binaries are expected, so that anyone
who places NVIDIA binaries there has the matching licence text. The NVIDIA licences impose conditions on
redistribution (notably: distribute only inside an application with material additional functionality, pass the
licence terms on, and attribute the SDK). Read them before redistributing any NVIDIA binary.

## 6. AMD, RenderDoc, imgui-vr-helper

| Component | Licence | Redistributed here |
|---|---|---|
| AMD FidelityFX runtime: `amd_fidelityfx_framegeneration_dx12.dll`, `amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll` | AMD binary-redistribution licence (`RS/features/Upscaling/Shaders/Upscaling/FidelityFX/license.md`): binary form only, no reverse engineering, reproduce AMD's copyright, permission and disclaimer notices in documentation | **No.** Built from or downloaded from AMD sources at package build time; not committed. If you redistribute them, include `license.md` and reproduce AMD's notice |
| RenderDoc (`renderdoc.dll`, in-app API) | MIT, Copyright (c) 2015-2026 Baldur Karlsson, Copyright (c) 2014 Crytek (`RS/features/RenderDoc/Renderdoc/LICENSE.md`) | **No.** The licence text stays in the repository; the DLL comes from https://renderdoc.org |
| imgui-vr-helper (alandtse) | LGPL-3.0 (with exceptions), https://github.com/alandtse/imgui-vr-helper | **No.** Optional VR UI helper; users install it from its official release. Redistributing it requires the LGPL-3.0 text and a source pointer |

## 7. Assets with unconfirmed provenance (owner action)

- **OpenNR logo and name** (`RS/.github/assets/logo/`, `RS/package/Interface/CommunityShaders/OpenNR Logo/`): created by the project owner (olekspa); included to identify the project; all rights reserved by the owner; not part of the GPL-licensed work.
- **Action and category icons** (`RS/package/Interface/CommunityShaders/Icons/`): inherited from upstream; their
  upstream licence was not checked **[unverified]**.
- **`TerrainHelper.esp` and `Shaders/Features/TerrainHelper.ini`** (in `RS/features/Terrain Helper/`, author hakaspl): upstream's file says it is
  "redistributed with permission"; whether that permission extends to OpenNR is undocumented **[unverified]**.
- **Inherited data** (DDS textures, terrain height maps, `WindSettings/NatureOfTheWildLands.json`): inherited from
  upstream with the same provenance status as upstream **[unverified]**.
- **`_external/PixRestore`** (git submodule, `csslc/PixRestore`): the upstream repository has no licence; it is an
  optional research dependency and is not redistributed by this repository (only the submodule pointer is).
