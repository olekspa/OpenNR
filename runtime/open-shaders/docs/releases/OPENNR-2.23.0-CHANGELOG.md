# OpenNR 2.23.0

Selectable Neural Rendering runtime, with support for the new NVIDIA 310.9.x NR runtime. Built on 2.22.2.

## Neural Rendering runtime (new)
- **310.9.x accepted.** The plugin used to refuse anything but NVIDIA's 310.8.x `nvngx_dlssnr.dll`. It now accepts 310.8.x and 310.9.x. A static comparison of the NVIDIA-signed 310.8.0 and 310.9.16 files found the same exported functions and the same NR parameter names, plus one new optional parameter (`DLSSNR.MaskBlendMode`) which OpenNR leaves at the runtime default. How 310.9.16 behaves in the headset is not verified.
- **Choose the runtime in the menu.** Any `nvngx_dlssnr*.dll` placed next to the default file (`Shaders/Upscaling/Streamline` or `Shaders/Upscaling/StreamlineDX12`) is a candidate, for example `nvngx_dlssnr_310_9_16.dll`. The Neural Rendering page has a new "NR runtime" block: the active file, version and signature; an "NR runtime file" list (Auto first); and "Require NVIDIA-signed runtime (recommended)". NVIDIA's runtime loads once per game session, so a change applies after restarting the game.
- **Auto keeps today's behaviour.** Auto uses the file named exactly `nvngx_dlssnr.dll` when it is accepted, otherwise the newest accepted file. It never picks an unsigned file while a signed one is available.
- **Signature check, on by default.** The plugin refuses a runtime that does not carry a valid NVIDIA Corporation signature (offline Authenticode check, cached per file, computed on a worker thread). Turn the checkbox off only to use a modified runtime you supply and trust (for example one patched for RTX 20/30/40); such files appear as "Unsigned (modified?)" and run inside the game process.
- **GPU hint.** On non-RTX 50 adapters the menu shows a read-only note that NVIDIA's official runtime build targets RTX 50.
- **Logs.** `[DLSSNR] runtime candidates: [file version signature accepted] ...; selected=<file> reason=auto|user|fallback`, `[DLSSNR] runtime refused: <file> unsigned/invalid`, and on a feature-creation failure the runtime file, version, signature and GPU name.
- **Packaging.** The package checks no longer assume 310.8 or a fixed hash, and variant runtime files are never required. OpenNR still does not ship NVIDIA's runtime; users supply it.

## Not verified
Nothing here was run in the headset. The signature check was compiled and unit-tested through its policy only; it was not run against real signed/unsigned files in the game. Whether a patched RTX 20/30/40 runtime loads through the existing path is unknown.
