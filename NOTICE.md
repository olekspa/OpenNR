# OpenNR - notice

OpenNR is a Skyrim VR build of Open Shaders, which is itself a fork of Community Shaders. It adds Neural Rendering
and VR work on top of that code base. Most of the code in this repository was written by the Community Shaders and
Open Shaders contributors; the full authorship record is the git history.

## Copyright

- Copyright (C) 2026 the OpenNR authors (see the git history).
- Copyright (C) the Open Shaders and Community Shaders contributors (see the git history, and the upstream
  contributor lists linked in [runtime/open-shaders/BRANDING_AND_ATTRIBUTIONS.md](runtime/open-shaders/BRANDING_AND_ATTRIBUTIONS.md)).
- Third-party components keep their own copyright and licence: see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Licence

OpenNR is released under **GPL-3.0-or-later** (see [LICENSE](LICENSE)) **with the Modding Exception and the GPL-3.0
Linking Exception (with Corresponding Source)** (see [EXCEPTIONS.md](EXCEPTIONS.md)). This is the licence expression of
the upstream project, as documented in [runtime/open-shaders/README.md](runtime/open-shaders/README.md); OpenNR's own
changes use the same terms unless a file states otherwise.

In short (the text of [EXCEPTIONS.md](EXCEPTIONS.md) is authoritative):

- Skyrim and the hardware drivers and proprietary SDKs it is used with (for example NVIDIA DLSS and AMD FidelityFX)
  are "Modded Code".
- SKSE and CommonLib are "Modding Libraries".

`LICENSE` is a copy of `runtime/open-shaders/COPYING` and `EXCEPTIONS.md` is a copy of
`runtime/open-shaders/EXCEPTIONS.md`; both are identical to the upstream files. There is deliberately no
`COPYING.LESSER`: OpenNR is not licensed under the LGPL.

## What is not covered by the GPL

- **The OpenNR logo and name**: created by the project owner (olekspa); included to identify the project; all rights
  reserved by the owner; not part of the GPL-licensed work. See also
  [runtime/open-shaders/.github/assets/logo/LICENSE](runtime/open-shaders/.github/assets/logo/LICENSE).
- **NVIDIA, AMD and other vendor binaries and SDKs** are governed by their vendors' licences, not by the GPL. Vendor
  licence texts that exist in this repository sit next to the files they cover (see
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). The NVIDIA DLSS Neural Rendering runtime (`nvngx_dlssnr.dll`)
  is **not** distributed by this project.

## Lineage and non-affiliation

OpenNR derives from [Community Shaders](https://github.com/community-shaders/skyrim-community-shaders) (doodlum, Tim
and contributors) via [Open Shaders](https://github.com/alandtse/open-shaders) (Alan Tse and contributors). OpenNR is
not an official project of Open Shaders or Community Shaders and is not affiliated with or endorsed by NVIDIA, AMD or
Bethesda. NVIDIA, DLSS, Reflex, GeForce and RTX are trademarks or registered trademarks of NVIDIA Corporation. AMD and
FidelityFX are trademarks of Advanced Micro Devices, Inc. The Elder Scrolls V: Skyrim is a trademark of ZeniMax Media.

## Source offer

The corresponding source for every OpenNR release is the tag of the same name in this repository (for example
`v2.22.2`). Build instructions are in [CONTRIBUTING.md](CONTRIBUTING.md).
