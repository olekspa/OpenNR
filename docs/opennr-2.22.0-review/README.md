# OpenNR 2.22.0 — spring-cleaning release: review pack

Base: 2.21.8 (`d7cf2d30`) plus `main` docs. Release notes: `runtime/open-shaders/docs/releases/OPENNR-2.22.0-CHANGELOG.md`.
In-game checklist: `runtime/open-shaders/docs/releases/OPENNR-2.22.0-TEST-PLAN.md`.

| File | What |
|---|---|
| 01-audit-nr-perf.md | per-frame NR path audit, 23 ranked findings (first-frame stall, missing flush, redundant copies, SRV cache thrash, ...) |
| 02-audit-defaults.md | defaults vs live settings vs benchmark; why "Recommended" undid the tuned hybrid |
| 03-audit-periphery.md | hybrid composite math, signed-residual texture format, failure loop, gates, seam ideas |
| 04-audit-logs.md | 2.21.8 session log: map route dead, MapProbe spam, duplicate init, noise table |
| 06-community-research.md | open-shaders (alandtse) and community DLSS-NR implementations, port candidates, things to avoid |

## Work streams
CORE-FIX (map route, log spam, failure loops), DEFAULTS (hybrid defaults, presets, idle work), PERIPHERY (hybrid hardening),
PERF (NR path waste, opt-in scheduling experiments, startup prewarm), UPSTREAM (Skylighting hardening, hangs, grass, map fog).
Upstream port table: `docs/UPSTREAM_IMPLEMENTATION_SYNC_20261009.md`.

## Verified vs not
Verified: Release AIO build, 294 C++ cases / 10,561 assertions, runtime-include shader compile, 180+ Python contract tests, extracted-package checks
(local package report, not included in the repository). Independent Skylighting regression review: no regression found for the dungeon fix (2.20.6/2.20.7 invariants hold).
NOT verified: anything in the headset (visual seam quality, timings, map route, dungeon Skylighting, prewarm hitch, experimental queue/flush toggles).

## Open items / follow-ups
- Experimental, default OFF, need an in-game A/B: `neuralRenderingParallelEyeQueue`, `neuralRenderingFlushAfterSignal`.
- First-frame stall: prewarm is implemented but unmeasured; NGX init stays on the render thread (NGX is not documented thread-safe).
- Duplicate renderer init ~12 s after boot: not changed (could be a valid second lifecycle); needs runtime logging to decide.
- Not done: NGX parameter-delta updates, standalone shader disk cache, Runtime.cpp unbounded shutdown wait, smoother low-pass kernel for the hybrid (no matched capture pair), per-frame gain calibration.
- Six profiler-harness Python assertions fail on this tree; unchanged from the 2.21.7 baseline (profiler files untouched).
- Not ported on purpose: SBS single-instance NR (#889, breaks per-eye isolation), eye-stagger gain reuse (#880), the undocumented NVIDIA driver-profile DWORD from upstream issue #877.
