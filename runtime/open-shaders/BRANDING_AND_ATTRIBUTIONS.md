# OpenNR branding and attributions

OpenNR is the public name of this repository and its DLSSNR-focused Skyrim VR distribution.

> A Neural Rendering-focused Open Shaders fork with future support for custom Neural Rendering DLSS models.

## Project lineage

OpenNR is a downstream fork of [Open Shaders](https://github.com/alandtse/open-shaders). Open Shaders is itself a fork of [Community Shaders](https://github.com/community-shaders/skyrim-community-shaders). The inherited architecture, shader pipeline, feature framework, APIs, and much of the source remain attributable to those projects and their contributors.

OpenNR adds and maintains neural-rendering and VR work, including the DLSSNR integration, VR grass-culling improvements, foveated-render fallback safety, and isolated beta/alpha experiment branches. The related feature named **OpenNR Capture** is integrated into the main runtime/package as an explicitly opt-in capture mechanism; it is not the name of the core runtime.

## Compatibility identity

The following names are intentionally retained for binary and mod compatibility:

- `CommunityShaders.dll`
- `SKSE/Plugins/CommunityShaders/`
- `CommunityShaders.log`
- `CommunityShaders` SKSE messages, API names, and interface identifiers

Those compatibility strings do not indicate that OpenNR is the upstream project. They prevent settings, themes, dependent plugins, and existing mod-manager profiles from breaking during the branding transition.

## Licenses and marks

- Source inherited from the fork lineage is distributed under the GNU GPL, as documented in [`COPYING`](./COPYING) and the inherited source headers.
- OpenNR source changes are distributed under the same GPL terms unless a file states otherwise.
- The OpenNR wordmark and icon are project-specific branding assets. They are not part of the GPL-covered source and may be redistributed unchanged only to identify an OpenNR source tree or package. See [`.github/assets/logo/LICENSE`](./.github/assets/logo/LICENSE).
- The upstream Open Shaders logo is intentionally not redistributed or used by OpenNR. Open Shaders and Community Shaders names, logos, Nexus assets, and other marks remain the property of their respective contributors/maintainers.
- Third-party components retain their own licenses. Preserve the license files shipped beside them, including CommonLibSSE-NG, FidelityFX, Streamline, RenderDoc, fonts, ImGui VR Helper, and any vendor runtime notices.
- NVIDIA DLSS/DLSSNR, Streamline, Reflex, and related runtime binaries are vendor components. Their distribution is subject to the vendor license files included with the package; OpenNR does not claim ownership of those binaries.

## Fork obligations

When redistributing OpenNR or publishing a derivative:

1. Keep this notice, [`COPYING`](./COPYING), inherited source headers, and third-party notices.
2. Identify the exact OpenNR, Open Shaders, and Community Shaders source revisions used for a binary package.
3. Do not present OpenNR branding as official Open Shaders or Community Shaders branding.
4. Keep `CommunityShaders.dll` and the compatibility path unless you are making a deliberate ABI-breaking fork and have documented a migration plan.
5. Do not bundle optional proprietary runtimes or experimental alpha features without labeling the package and preserving their licenses.

## Source references

- [OpenNR GitHub repository](https://github.com/olekspa/OpenNR)
- [OpenNR GitLab group](https://gitlab.com/groups/opennr)
- [Open Shaders](https://github.com/alandtse/open-shaders)
- [Community Shaders](https://github.com/community-shaders/skyrim-community-shaders)
- [Open Shaders contributors](https://github.com/alandtse/open-shaders/graphs/contributors)
- [Community Shaders contributors](https://github.com/community-shaders/skyrim-community-shaders/graphs/contributors)
- Per-eye binocular offset concept adapted from [DoNotHeat/openshaders-nr commit `bfc9bf9`](https://github.com/DoNotHeat/openshaders-nr/commit/bfc9bf9) (GPL-3.0).
- Optional parallel per-eye D3D12 compute-queue scheduling concept reimplemented from [alandtse/open-shaders PR #890](https://github.com/alandtse/open-shaders/pull/890) (GPL-3.0-or-later); no upstream source code copied.
- Map symbol source-supersampling concept reimplemented from [ParticleTroned/skyrim-community-shaders commit `7cdfa07c5`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/7cdfa07c5) (GPL-3.0); no upstream source code copied.

## Upstream code ports and fixes

The following upstream changes were re-applied or adapted in OpenNR (GPL-3.0-or-later, compatible with this project's licence). Numbers and commit hashes are those recorded in `docs/UPSTREAM_IMPLEMENTATION_SYNC_20261009.md` (repository root) and in the per-version changelogs under `docs/releases/`. "OS" means [alandtse/open-shaders](https://github.com/alandtse/open-shaders).

- Skylighting hardening (2.22.0): OS #695 (`127b7ebd`), #696 (`bc25f2af`), #697 (`f8b8b86b`), #698 (`d6bfd6b7`), upstream #2844 (`6efd28bb`); #694, #699 and #700 were already present. Earlier (2.20.7) OpenNR ported the #694 lit-history seed and the #700 zenith clamp.
- Hook attach reporting and transaction cleanup: upstream PR #2846 (open PR head `a4970b2`, adapted).
- Bounded frame-latency wait and angle normalisation: OS #845 (`0683d86e`).
- VR grass placement from the active eye camera: OS #817 (`8444f3c6`).
- World-map height and volumetric fog disabled while the map is open: OS #788 (`2ee66279`).
- Shadow/foveated/menu fixes adapted in 2.20.2: OS #779, #782, #783, #791. Shadow-light radius anchoring in 2.20.6: concept of OS #838.
- Concept credits for 2.22.x: parallel per-eye D3D12 compute queue (OS PR #890), map symbol supersample (ParticleTroned/skyrim-community-shaders `7cdfa07c5`), per-eye offsets (DoNotHeat/openshaders-nr `bfc9bf9`), as listed above.

## Other credits

- [OpenDLSS-NR](https://github.com/maanHimself/OpenDLSS-NR) (MIT) was consulted as a research reference; no code from it is included.
- [imgui-vr-helper](https://github.com/alandtse/imgui-vr-helper) (LGPL-3.0) is an optional runtime helper by Alan Tse; it is not part of this source tree.
- Vendor technologies: this software uses NVIDIA DLSS, Streamline and Reflex technology and AMD FidelityFX. NVIDIA, DLSS, Reflex, GeForce RTX and AMD FidelityFX are trademarks of their respective owners.
- The full third-party inventory is in `THIRD_PARTY_NOTICES.md` at the repository root.
