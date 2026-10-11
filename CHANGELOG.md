# Changelog

All notable changes to OpenNR, from 2.20.0 to 2.22.2. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Per-version detail for most releases lives in
[runtime/open-shaders/docs/releases/](runtime/open-shaders/docs/releases/).

Nothing below has been verified in a headset unless it says so. "Not verified" means the code builds and the automated
tests pass, but no in-headset check has been done. Versions marked *(summarised from commit history)* have no
changelog file in the current tree; they were summarised from the repository history (commit subjects, the changelog
text those commits carried at the time, and diffs) and are thinner than the others. Dates are commit dates.

## [2.22.2] - 2026-10-09
Map terrain quality test build on 2.22.1. Everything new is off or neutral by default.
### Added
- World map live settings, applied only while the map is open: `vrMapDlssPreset` (0 = unchanged, 1-4 = DLSS preset J/K/L/M), `vrMapSharpness` (0..1, default 0, RCAS sharpening on the map backdrop), `vrMapMipBiasOffset` (-3..0, default 0; affects only OpenNR shader paths that read the SharedData mip bias).
- One `[PerfMode][MapQuality]` diagnostic log line per map open.
### Not verified
- Nothing was run in the headset. Whether the native map terrain material uses the mip bias value is unknown.

## [2.22.1] - 2026-10-09
Follow-up to 2.22.0 after the first in-game test.
### Added
- `vrMapSymbolSupersample` (default 2.0, range 1.0-2.0, 1.0 = off): the 2048x2048 map UI target is replaced by a larger one (up to 4096x4096) while the map is open, so markers and labels are sharper. Fails open to the native target. Terrain is unchanged.
- Benchmark variants `nr-hybrid-parallel-eye-queue` and `nr-hybrid-flush-after-signal`.
- `[RendererSetup]` identity log line per setup pass.
### Fixed
- Staged startup prewarm now runs before the first world frame (in 2.22.0 it only ran after it).
- `neuralRenderingFlushAfterSignal` now also reaches the staggered periphery submission.
- Experimental parallel-queue and flush toggles log when requested, used or unavailable.
### Not verified
- Map supersample, prewarm timeline and toggle effects need an in-game check.

## [2.22.0] - 2026-10-09
Performance and fixes release on 2.21.8, after an audit of the code, a live log, the shipped defaults and community implementations.
### Changed
- "Recommended", "Reset" and first run select the measured hybrid: Full Eye route, full-resolution NR over 55% centre coverage, whole-eye Periphery NR at 50%, staggered, projection-centred crop, dither edge blend. In the 2.20.4 in-game benchmark (one scene) NR work fell about 41% versus full-eye NR. The old Recommended reset to the slowest path (coverage 100, periphery off); fixed.
- New presets "Quality: full-eye NR (slowest)" and "Safe mode" (NR off, tuning kept). Coverage presets 60 and 50 added.
- Fresh installs start on the Full Eye route so the hybrid is reachable.
- Shader-cache clears no longer shut Neural Rendering down.
### Added
- Startup prewarm (`neuralRenderingPrewarm`, default on): creates both eyes' Feature 18 slots on menu/loading frames. Not yet measured in game.
- Experimental, default off: `neuralRenderingParallelEyeQueue` (concept from upstream PR #890), `neuralRenderingFlushAfterSignal`; benchmark variants `nr-hybrid-c50-p50-staggered`, `nr-hybrid-c60-p50-staggered`, `nr-parallel-eye-queue`, `nr-flush-after-signal`, `frame-annotations-off`, `skylighting-neutral-output`.
- D3D12 interop, NGX init and first-evaluate timing logs.
- Upstream ports (alandtse/open-shaders): Skylighting hardening, VR grass projection from the active eye camera, world map disables height and volumetric fog, observable Detours attach failures. See [BRANDING_AND_ATTRIBUTIONS.md](runtime/open-shaders/BRANDING_AND_ATTRIBUTIONS.md).
### Fixed
- The VR world-map display-resolution route was never applied for users running the CS menu in desktop-only mode.
- Periphery hybrid: float16 signed low-frequency data (a UNORM format clamped negatives), 1/8 vertical sampling, rounded crop transition, no eye-border feathering, 100% coverage with periphery refused with a visible reason, exponential retry backoff instead of per-frame logging, every silent early return reports a reason in the NR status panel.
- NR robustness: history reset after skipped frames, deferred manual reset kept, tolerant left/right UV compare (an exact float compare could silently disable NR), transient fence timeouts retry before NR teardown.
- Bounded frame-latency waits, `fmod` angle normalisation, bounded DX12 swap-chain fence wait.
- Log noise: map-probe records capped, several warnings demoted.
### Removed
- The disabled guided periphery filter (code and shader); the legacy key still loads and is ignored.
### Not verified
- Anything in the headset: seam quality, timings, map route, dungeon Skylighting, prewarm hitch, experimental toggles.

## [2.21.8] - 2026-10-08
### Fixed
- VR world map: copy routed using the bound `kMENUBG` target; the 2048x2048 map copy is rasterised across the display-sized destination. In-game result unconfirmed.

## [2.21.7] - 2026-10-08
### Fixed
- VR world map: `DynamicFetchDisabled` and `CustomViewport` ISCopy variants routed through the display-resolution path; raster extent sized from the SteamVR per-eye display size.

## [2.21.6] - 2026-10-08
### Changed
- Neural Rendering menu cleanup: the master toggle controls the active VR NR route; foveated route defaults on; foveated controls shown directly; DLSS controls and the Settings Benchmark moved to the Upscaling page; OpenNR logo path fixed.

## [2.21.5] - 2026-10-08
### Added
- Grouped NR settings, active-route and refusal-reason display, section resets, Recommended and Safe mode presets.
### Removed
- Inactive guided-filter option from the UI and benchmark presets.

## [2.21.4] - 2026-10-08
### Added
- Per-eye X/Y crop alignment offsets (+/-200 px) for headsets whose lens centres differ from Quest 3.

## [2.21.3] - 2026-10-08
### Fixed
- Whole-eye Periphery hybrid works at display heights that are not multiples of 8 (it was silently refused at 2428). Transition-only active/refusal logs added.

## [2.21.2] - 2026-10-08 *(summarised from commit history)*
### Fixed
- VR map `ISCopy` draw routed to the display-resolution surface while the map is open; the render hook is installed in VR even when Frame Annotations is disabled. Candidate build, not tested in game at the time.

## [2.21.1] - 2026-10-08
### Added
- Map route prerequisites logged at map open; fake depth view creation retried once at the first map draw.

## [2.21.0] - 2026-10-08
### Added
- "Map: render at display resolution" toggle (default on); opt-in map diagnostics.
### Changed
- Default NR centre blend width 160 px; idle profiler/Tracy capture and several diagnostics default off; tooltip wrapping.

## [2.20.10] - 2026-10-07 *(summarised from commit history)*
### Added
- Runtime setting "VR map menu at display resolution" (default on): while the VR map menu is open under PerfMode, the map imagespace draws are routed to a display-resolution target and restored afterwards. Fails closed if the target or hooks are unavailable. Also map viewport census logging and guarded map-probe index reads.

## [2.20.9] - 2026-10-07
### Added
- Map-render diagnostic probe for PerfMode (no rendering change).
### Fixed
- Cropped NR depth-format check uses the route-selected depth guide.

## [2.20.8] - 2026-10-07 *(summarised from commit history)*
### Fixed
- Cropped NR route no longer dereferences an unallocated full-eye depth resource during coverage format checks (re-applied in 2.20.9).

## [2.20.7] - 2026-10-02
### Fixed
- Skylighting probe use in interiors is opt-in (default off, neutral data otherwise); state reset after load and cell/worldspace change; lit-history seeding; Rebuild action; live neutral-output switch; interior diagnostics. Targets black dungeon interiors.

## [2.20.6] - 2026-10-02 *(summarised from commit history)*
### Changed
- Light radius anchored once per light per frame for shadow admission and redraw decisions (default on, runtime-toggleable), to reduce shadow flapping.

## [2.20.5] - 2026-10-02 *(summarised from commit history)*
### Fixed
- Fail-open policy for unsuppressed spotlights without a shadow atlas tile; pending shadow-session resets drained at the first safe schedule boundary; foveated NR, adaptive, SSGI and DLSS histories reset on cell/interior/worldspace change; profiler timer capacity increased.

## [2.20.4] - 2026-10-01
### Added
- Opt-in projection-centred NR crop (OpenVR `GetProjectionRaw`, with fallback) with X/Y offsets; the periphery hybrid A/B presets use it.

## [2.20.3] - 2026-10-01
### Added
- Optional (off by default) Periphery NR hybrid: full-resolution centre crop plus a whole-eye reduced-scale (50/58/66/75%) NR pass whose effect (output minus input) is upsampled and added over the full-resolution frame; eye stagger; effect-space compositing; Settings Benchmark variants.

## [2.20.2] - 2026-09-30 *(summarised from commit history)*
### Fixed
- Standard VR DLSS gets a typed current-frame right-eye depth guide (removes an invalid boxed depth-stencil copy that could leave stale depth).
- Menu sharpening follows the route that actually handled the frame (frozen menu backdrop); foveated periphery history reseeded after loading screens and fallbacks; SSGI clears outputs for masked and first-person pixels.
- Shadow-light teardown drains admitted work before freeing lights; native NR distinguishes device removal from fence completion and retains resources after a timeout; NGX exports retain DLL references.
### Notes
- Adapts Open Shaders #779, #782, #783 and #791. It does not establish that every reported cave/house clipping or shadow crash is resolved.

## [2.20.1] - 2026-09-27
### Added
- In-headset Settings Benchmark (live A/B against bracketing baselines, SteamVR compositor timing, JSON report, optional user variants, DevBench actions).
- DLSS presets E and F (CNN).

## [2.20.0] - 2026-09-27
### Added
- NR Coverage (NR on the centred 95-70% of each eye at full model resolution, edge blended into the DLSS image); Stagger eyes option.
### Changed
- Performance Overlay removed; reduced NR model resolution, Adaptive NR tiers and pre-upscale NR locked off (they weaken the effect); link-time code generation enabled.
### Fixed
- Motion-vector scale for pre-upscale NR; temporal reuse and stabiliser applied only about 59% of real motion in the post-upscale VR route; the stabiliser re-injected NR effect that result shaping had removed.
