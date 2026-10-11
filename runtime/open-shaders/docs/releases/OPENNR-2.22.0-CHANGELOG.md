# OpenNR 2.22.0 — spring-cleaning performance and fixes release

Built on 2.21.8. Five parallel work streams (core fixes, defaults, periphery, performance, upstream ports) after a full audit
of the code, the live 2.21.8 log, the shipped defaults and the community implementations. The audit pack is in
`docs/opennr-2.22.0-review/` in the source tree.

## Faster by default
- **Recommended / Reset / first run now select the measured hybrid**: Full Eye route, full-resolution NR over 55 % centre coverage,
  whole-eye Periphery NR at 50 %, staggered, projection-centred crop, dither edge blend (strength 1.45, 128 px, curve 1.23).
  In the 2.20.4 in-game benchmark (one scene) this cut NR work by about 41 % (30.1 ms vs 39.7 ms GPU frame) against full-eye NR.
  The old "Recommended" button reset to the slowest path (coverage 100, periphery off); that is fixed, so it can no longer undo tuning.
- New presets: **Quality: full-eye NR (slowest)** and **Safe mode** (NR off, tuning kept).
- A fresh install now starts on the Full Eye route, so the hybrid is reachable (before, the first-run crop preset blocked it).
- Coverage presets 60 and 50 added for A/B testing.
- **Startup prewarm (setting `neuralRenderingPrewarm`, default ON):** on a menu/loading frame, once the display size is latched, both eyes' Feature 18 slots (full-eye, or the hybrid's centre crop and 50% periphery) are created ahead of time through the same code path the first world frame would use, with a fallback to lazy creation on any failure. Aim: move the one-time ~4.7 s first-frame hitch to the loading screen. Not yet measured in game (benchmark variants `nr-prewarm-on` / `nr-prewarm-off`).
- Shader-cache clears no longer shut Neural Rendering down (they used to repeat the multi-second NGX start-up).
- Crop-route blending no longer re-creates its shader views every frame; the no-UAV crop path copies only the changed crop rectangles.
- Removed dead per-frame work: the unused accumulated-motion clear, flat-route staging copies, OpenVR frame-timing reads while adaptive NR is forced off,
  idle benchmark locking, per-draw annotation formatting without a capture tool, per-frame profiler name allocation.
- Periphery hybrid: removed redundant output snapshot, staged copy-in and unused reuse-eye downsample when the hybrid owns the whole eye.

## Fixes
- **VR world map display-resolution route** was never applied for users running the CS menu in desktop-only mode (it was gated on the VR menu presentation setting). The gate now follows the VR game, with a named reason logged when it is denied.
- **Periphery hybrid**:
  - signed low-frequency data is now kept in float16 textures (a UNORM scene format clamped negative values);
  - coarse-grid vertical sampling corrected to true 1/8 spacing;
  - a rounded crop transition; eye-border sides are no longer feathered;
  - 100 % coverage with periphery on is refused with a visible reason instead of costing more than full-eye NR;
  - persistent failures back off with exponential retry instead of resetting history and logging every frame; centre history is kept on periphery-only failures;
  - every silent early return now publishes a reason to the NR status panel (new plain-language terms).
- NR robustness: primary-path history reset after skipped frames, deferred manual reset no longer dropped, tolerant left/right UV compare (was an exact float compare that could silently disable NR),
  full-eye output extents, transient fence timeouts retry twice before NR is torn down; a transient pre-upscale fence skip no longer forces a cold NGX rebuild.
- Foveated route resource failures log once and retry after two seconds (or on a mode/scale/size change) instead of every frame; `ClearHMDMask` checks its constant-buffer map.
- Bounded frame-latency waits (1 s) and `fmod` angle normalisation (no infinite loop on huge angles); the DX12 swap-chain frame fence wait is bounded with a logged skip.
- Stale `SettingsDefault.json` base layers (old versions) are ignored in memory; invalid periphery tier values log a one-time warning.
- Log noise: MapProbe target-bind records capped at 20 per map open (it logged ~1000 lines in 9 s), first-frame invalid-index warning demoted, ENB/EFFECTS11/legacy-adapter/CJK/height-map warnings reduced to info or once.

## Upstream ports (alandtse/open-shaders, GPL-3.0-or-later)
- Skylighting hardening: R16 temporal state with shift-register history, invalid cascade/near-plane projections rejected, missing directional-shadow inputs fail lit and reseed on recovery, editor-marker roof meshes included,
  corrected max-zenith radius and fade-grid offset (Skylighting feature 1-7-2). OpenNR's interior-off default, cell/worldspace reset, neutral-output control and dungeon diagnostics are kept.
- VR grass projection uses the active eye camera; world map disables height and volumetric fog; Detours attach failures are observable and fail-safe.
- Already present and verified: unit-SH/lit-history reset, DLSS VRAM-warning handling, right-eye depth copies, SCM/ISL radius anchor, Skylighting cost controls.

## New experimental settings (default OFF — measure before enabling)
- `neuralRenderingParallelEyeQueue`: records eye 1 on a second compute queue (concept from upstream PR #890, which reported -1.2 to -2.0 ms NR evaluate with the same image). Benchmark variant `nr-parallel-eye-queue`.
- `neuralRenderingFlushAfterSignal`: flush after the D3D11 fence signal to remove a suspected hand-off bubble. Benchmark variant `nr-flush-after-signal`.
- New benchmark variants: `nr-hybrid-c50-p50-staggered`, `nr-hybrid-c60-p50-staggered`, `frame-annotations-off`, `skylighting-neutral-output`.
- Diagnostics: D3D12 interop, NGX init and first-evaluate timings are logged.

## Removed
- The disabled guided periphery filter (code and shader). The legacy `neuralRenderingPeripheryGuidedFilter` key still loads and is ignored.

## Not verified
Nothing in this release was run in the headset. CPU builds and the C++/source-contract suites pass; the shader GPU tests, visual seam quality, Skylighting behaviour in dungeons
and all performance numbers need an in-game check (see `docs/releases/OPENNR-2.22.0-TEST-PLAN.md`).
