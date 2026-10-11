# OpenNR 2.20.7

- Make Skylighting probe use in interiors opt-in and default it off. With the option off, interior consumers receive neutral, fully visible probe data.
- Reset Skylighting probe, accumulation, occlusion, and history state after game load and cell/worldspace transitions.
- Seed newly allocated directional shadow history as lit, clear accumulation and occlusion depth to safe values, and keep probe consumers neutral until the resource set and a successful probe dispatch are ready.
- Report Skylighting resource creation or shader initialization failures and provide an explicit Rebuild action that logs whether it is reseeding state or retrying resource creation.
- Add once-per-second interior Skylighting diagnostics for mode, resource/data readiness, occlusion renders/skips, and probe dispatches/skips.
- For 10 seconds after an interior cell transition, log each Lighting shader permutation's compile, memory/disk cache result, or runtime cache hit once, including its defines.
- Add an off-by-default live Skylighting neutral-output switch that leaves resources and updates active while forcing every Skylighting visibility term to fully lit.
- Guard deferred-composite and volumetric-fog optional probe SRV binding against absent resource or view pointers.
- Port the upstream #694 lit-history seed and #700 zenith clamp; defer projection, shadow-input, and probe-readiness PRs whose behavior is not required for this interior path.
