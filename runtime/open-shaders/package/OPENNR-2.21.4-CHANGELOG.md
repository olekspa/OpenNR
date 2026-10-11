# OpenNR 2.21.4 candidate

## Per-eye Neural Rendering alignment

- Add independent left and right X/Y offsets for projection-centred NR crops, with zero-valued controls falling back to the existing shared offsets.
- Add per-eye alignment sliders and a reset control for headsets whose lens centres differ from Quest 3.
- Clamp each per-eye alignment offset to ±200 px.

Full-resolution NR and the foveated subrect route are unchanged. No live headset or in-game acceptance is included in this candidate.
