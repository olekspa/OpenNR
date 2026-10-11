# OpenNR documentation index

Users: start with the repository [README](../README.md) and the [CHANGELOG](../CHANGELOG.md). Everything below is developer
documentation and a dated development log; none of it is needed to install or use OpenNR. Paths of the original
development machine were replaced by placeholders such as `<REPO>`, `<BUILD_DIR>` and `<USER_HOME>`.

## Releases and testing
- [releases/RELEASE_NOTES_v2.22.2.md](releases/RELEASE_NOTES_v2.22.2.md) - text for the v2.22.2 release.
- `../runtime/open-shaders/docs/releases/` - per-version changelogs, and the 2.22.0 in-game
  [test plan](../runtime/open-shaders/docs/releases/OPENNR-2.22.0-TEST-PLAN.md).
- `OPENNR_2.xx_RELEASE_*.md` - validation notes for older releases (history).

## Audits and review packs (technical, kept for the community)
- [opennr-2.21.0-review/](opennr-2.21.0-review/README.md) - architecture, seam and colour analysis, improvement backlog.
- [opennr-2.22.0-review/](opennr-2.22.0-review/README.md) - audits of NR performance, defaults, the periphery hybrid, logs, and community implementations.
- [opennr-2.22.1-review/](opennr-2.22.1-review/) - log audit reference and menu hi-res research.
- [UPSTREAM_IMPLEMENTATION_SYNC_20261009.md](UPSTREAM_IMPLEMENTATION_SYNC_20261009.md) - which upstream (Open Shaders) changes were ported.
- `../reports/` - menu inventory, map size chain and periphery gate audits.

## Setup and project layout
- [SETUP.md](SETUP.md), [PROJECT_MAP.md](PROJECT_MAP.md) (older, written at 2.15.0), [PROJECT_CONSOLIDATION_AUDIT.md](PROJECT_CONSOLIDATION_AUDIT.md).

## Research log (historical)
Dated September 2026 notes on capture formats, training experiments and model studies for replacement Neural Rendering
models (`*_2026MMDD.md`, `*_0.5.x_*.md`, `RUNPOD_*`, `STUDENT_*`, ...). They are an engineering diary, not product
documentation, and none of the models they discuss ships with OpenNR.
