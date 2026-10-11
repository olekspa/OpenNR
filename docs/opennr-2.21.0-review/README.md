# OpenNR 2.21.0 - review pack

Written 2026-10-08. Read-only analysis of the 2.21.0 development tree; no source, settings or package was changed by
this pack.

## Contents

1. **[01-architecture.md](01-architecture.md)** - how 2.21.0 is put together: frame pipeline, NR model contract,
   coverage and crop, the periphery hybrid and its gates, foveated render, PerfMode, settings, build and tests.
2. **[02-seam-and-colour-analysis.md](02-seam-and-colour-analysis.md)** - why the NR seam shows as circles and why the
   edges have a different colour. Evidence chain, with the in-game log and settings file as inputs.
3. **[03-improvement-backlog.md](03-improvement-backlog.md)** - ranked improvements, with cost, risk and how to verify each.

## Short answers

- **Edge colour.** The whole-eye periphery route that carries NR colour to the edges was silently off on the
  developer's machine in this session. Its gate required the display height to be a multiple of 8, and the display
  height was 2428. This was fixed in 2.21.3 (see [CHANGELOG](../../CHANGELOG.md)).
- **Circles.** The NR-only blend uses an oval mask with a 128 px band and dither noise that re-randomises every frame.
  The oval is the circle; the dither makes it shimmer.
- **Performance.** Extending NR colour this way adds roughly one periphery F18 pass per eye. Estimated +41 % of the
  NR centre cost per eye with stagger (INFERENCE). It has to be measured in game.

## Evidence tags

- **SOURCE-VERIFIED** - read in the code; file and line given.
- **LOCAL-MEASURED** - from the developer's live log (`CommunityShaders.log`) or settings file.
- **INFERENCE** - estimated from pixel counts or code structure; not measured.
- **HYPOTHESIS** - a plausible cause that has not been tested in game.

## Scope and limits

- Read in depth: the NR integration core (`Integration.cpp`), the periphery and coverage shaders,
  `SubrectBlendCS.hlsl`, `ResultShapingCS.hlsl`, `ModelResolutionCS.hlsl`, the foveated blend setup, the PerfMode
  display latch and the 2.21.0 changelog.
- Surveyed only by folder and file count: the rest of the tree. This is not a full audit of the codebase.
- No in-game test, no GPU job. All visual claims come from code reading and the log.
- Paths of the original development machine have been replaced with placeholders such as `<REPO>`.
