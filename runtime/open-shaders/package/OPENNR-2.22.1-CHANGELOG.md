# OpenNR 2.22.1

Follow-up to 2.22.0 after the first in-game test. Review notes: `docs/opennr-2.22.1-review/`.

## Map
- **World map symbols are supersampled at the source** (new setting `vrMapSymbolSupersample`, default 2.0, range 1.0-2.0, 1.0 = off). While the map is open, Skyrim VR's fixed 2048x2048 map UI target (`kHUDMENU`, colour + depth) is replaced by a larger one (up to 4096x4096, 8-pixel aligned, hard-capped at 64 MiB per plane), the map's Scaleform viewport is scaled by the same factor around `MapMenu::PostDisplay`, and everything is restored on close, reset or any failure (fail-open to the native target). This targets the blocky marker icons and labels, which come from that 2048x2048 source; the terrain is unchanged. Concept from the CSX fork's map-symbol supersample (credited in `BRANDING_AND_ATTRIBUTIONS.md`). Log lines: `[PerfMode][MapSS] scale=... hud=2048x2048->4096x4096 viewportScaled=true failOpen=none` and `restored=true`.
- If the map looks wrong, set `vrMapSymbolSupersample` to 1.0.

## Startup
- **Staged startup prewarm** now actually runs before the first world frame: it uses PerfMode's latched render/display size (no live guide textures needed), advances one setup or eye-slot step per menu/loading frame (about three frames for full eye, six for the hybrid) and logs `[DLSSNR][PREWARM] skipped reason=...` when a gate blocks it and `reused route=...` when the first real frame finds the pre-created slots. In 2.22.0 the prewarm only ran after the first world frame, so the ~2 s first-frame hitch remained. Each Feature 18 create call is still synchronous.

## Experimental toggles (default OFF)
- `neuralRenderingFlushAfterSignal` now also reaches the staggered periphery submission (it was not forwarded there in 2.22.0, so the toggle did nothing in the default hybrid).
- The parallel eye queue and flush toggles now log when they are requested, used or unavailable, and the NR route panel shows both states. With the periphery's eye stagger on, the periphery evaluates one eye per frame, so the parallel queue can only overlap the centre crop's two eyes (logged as `reason=stagger-single-eye`).
- New benchmark variants against the default hybrid: `nr-hybrid-parallel-eye-queue`, `nr-hybrid-flush-after-signal`.

## Diagnostics
- One `[RendererSetup]` identity line per setup pass (thread, caller, D3D device, renderer, `ReInit` reason) to tell a duplicated setup from a legitimate lifecycle. Behaviour unchanged.
- The stale `SettingsDefault.json` version gate is a tested policy (stale -> ignored in memory, current -> kept, missing -> regenerated).
- `[LLF] bad NiLight` messages stay warnings: they report a real guard that clamps the light count before the engine can dereference a stale pooled light.

## Not verified
Nothing was run in the headset. Builds and C++/source-contract tests pass; the map supersample, prewarm timeline and toggle effects need an in-game check (log lines above).
