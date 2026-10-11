# Third-party binaries

OpenNR's git tree and its "plugin-only" (lean) release archive contain **no**
NVIDIA, AMD, RenderDoc or imgui-vr-helper binaries, no `.pdb` files and no
`nvngx_dlssnr.dll`. Those components belong to their respective owners and are
obtained by you, the builder or end user, from the official sources below. This
document lists what is needed, where it comes from and where it goes.

Licence texts for these components stay in the tree next to the place they are
used (for example `license.txt` and `reflex.license.txt` under
`runtime/open-shaders/features/Upscaling/Shaders/Upscaling/StreamlineDX12/`).

## Components

Destinations are relative to the mod root (the folder that contains `SKSE/` and
`Shaders/` once installed, for example your mod manager's mod folder).

| Component | Needed for | Official source | Destination |
| --- | --- | --- | --- |
| `nvngx_dlss.dll` (DLSS) | DLSS upscaling | NVIDIA DLSS SDK GitHub releases, <https://github.com/NVIDIA/DLSS/releases> (also bundled in the Streamline SDK below) | `Shaders/Upscaling/Streamline/` |
| `sl.interposer.dll`, `sl.common.dll`, `sl.dlss.dll`, `sl.pcl.dll`, `sl.reflex.dll` (Streamline) | DLSS / Reflex plumbing | NVIDIA Streamline SDK GitHub releases, <https://github.com/NVIDIA-RTX/Streamline/releases> (tested with v2.14.1: `bin/x64/`) | `Shaders/Upscaling/Streamline/` |
| `nvngx_dlssg.dll`, `sl.dlss_g.dll` (+ the Streamline files above) | DLSS Frame Generation (optional, DX12 path) | Streamline SDK, same release (`bin/x64/`) | `Shaders/Upscaling/StreamlineDX12/` |
| `nvngx_dlssnr.dll` | DLSS Neural Rendering (Feature 18) | Supplied by the user (accepted 310.8.x build). OpenNR never commits, redistributes or links to a download for it. | `Shaders/Upscaling/Streamline/` |
| `amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll` (and `amd_fidelityfx_framegeneration_dx12.dll` for frame generation) | FSR upscaling / frame generation | AMD FidelityFX SDK, <https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases> | `Shaders/Upscaling/FidelityFX/` |
| `imgui-vr-helper.dll` | VR overlay helper (the source build pins SHA-256 `bf2941d4...eba6`, the v1.7.0 build) | imgui-vr-helper upstream release, <https://github.com/alandtse/imgui-vr-helper/releases> | `SKSE/Plugins/` |
| `renderdoc.dll` (optional) | RenderDoc capture feature | RenderDoc, <https://renderdoc.org/> (MIT) | `Renderdoc/` |
| `fa-solid-900.ttf`, Crimson Pro, Roboto Slab fonts (optional) | UI icon font / extra font choices; excluded from the lean archive because no licence text ships with them | Font Awesome Free <https://fontawesome.com/download>, Google Fonts <https://fonts.google.com/> | `Interface/CommunityShaders/Fonts/` |
| `TerrainHelper.esp` / `Shaders/Features/TerrainHelper.ini` (optional) | Terrain Helper feature; redistribution permission is not documented for this fork | Nexus Mods mod 143149 by hakaspl | game `Data/` as the mod instructs |

Always check the licence that ships with each download. Versions other than
those named here are untested.

## Building from source

1. Configure as usual. The build downloads the hash-pinned Streamline SDK
   itself (`cmake/Streamline-Runtime.cmake`) and stages the `sl.*`, `nvngx_dlss`
   and `nvngx_dlssg` DLLs into the build payload, including the
   `StreamlineDX12` set. The former tracked copies of those DLLs were redundant
   and have been removed.
2. Two inputs must exist before configure succeeds (the build fails early with a
   message that points here):
   * `nvngx_dlssnr.dll`: pass `-DOPENNR_DLSSNR_RUNTIME_SOURCE=<path>` or place
     it at `runtime/open-shaders/package/Shaders/Upscaling/Streamline/nvngx_dlssnr.dll`
     (git-ignored).
   * `imgui-vr-helper.dll`: pass `-DOPENNR_IMGUI_HELPER_SOURCE=<path>` or place
     it at `runtime/open-shaders/package/SKSE/Plugins/imgui-vr-helper.dll`
     (git-ignored).
3. Optional: `renderdoc.dll` at
   `runtime/open-shaders/features/RenderDoc/Renderdoc/renderdoc.dll`
   (git-ignored) is picked up by the full ("AIO") package. The lean package
   excludes it.

## Local staging helper

If you keep your own copies of the binaries in one directory that mirrors the
paths relative to `runtime/open-shaders/` (for example
`package/SKSE/Plugins/imgui-vr-helper.dll`), `tools/stage_local_thirdparty.ps1`
copies them into the git-ignored locations of a checkout:

```powershell
$env:OPENNR_THIRDPARTY_DIR = '<your staging directory>'
./tools/stage_local_thirdparty.ps1 -DryRun   # preview
./tools/stage_local_thirdparty.ps1           # copy (skips identical files)
```

Nothing it copies is ever committed: `*.dll`, `*.pdb` and `*.exe` are ignored
by `.gitignore` and `runtime/open-shaders/.gitignore`.

## Lean (plugin-only) release archive

`tools/make_lean_release_package.ps1` builds the redistributable archive from a
verified package folder. It copies an explicit allow-list (the OpenNR plugin
DLL, the owner-created OpenNR logo, shaders, feature INIs, translations, themes, licence-bearing fonts,
changelog and notices) and fails if any denied file (any other `.dll`,
`nvngx*`, `sl.*`, `.pdb`, `.exe`, `.esp`, or any file over 30 MB) is present in
the output.
