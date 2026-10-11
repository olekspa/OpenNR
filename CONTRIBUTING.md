# Contributing to OpenNR

Bug reports with logs and test results are the most useful contribution: use the issue templates.

OpenNR is a downstream fork of Open Shaders / Community Shaders. Generic rendering fixes are better sent upstream first
(https://github.com/alandtse/open-shaders). For OpenNR-specific work (Neural Rendering, VR crop/periphery, map fixes)
open an issue before a large pull request.

## Building

- Requirements and CMake presets are described in [runtime/open-shaders/README.md](runtime/open-shaders/README.md)
  (Visual Studio 2026, CMake 4.2+, vcpkg, git submodules via `git clone --recursive`). Presets such as `Dev-Fast` and
  `ALL` are defined in `runtime/open-shaders/CMakePresets.json`.
- Further developer documentation is in [runtime/open-shaders/docs](runtime/open-shaders/docs). Release changelogs are in
  `runtime/open-shaders/docs/releases/`.
- `runtime/open-shaders/CONTRIBUTING.md` and `runtime/open-shaders/AGENTS.md` are inherited from upstream; the
  "PR to dev, never main" and semantic-release rules in them do not apply to this repository. Pull requests here
  target `main`.

## Rules

- **Never commit NVIDIA binaries** (`nvngx_dlss.dll`, `nvngx_dlssg.dll`, `nvngx_dlssnr.dll`, `sl.*.dll` and similar),
  leaked or recovered model weights, captures, or anything you do not have the right to share.
- Do not document or link places to obtain unofficial copies of proprietary files.
- Keep the `CommunityShaders.dll` / `SKSE/Plugins/CommunityShaders/` compatibility names.
- Settings changes must keep older settings files loadable. Performance claims need a measured number and scene.
- Say plainly whether a change was tested in a headset.

All contributions are licensed GPL-3.0-or-later with the Modding and Linking exceptions, like the project (see
[NOTICE.md](NOTICE.md)).
