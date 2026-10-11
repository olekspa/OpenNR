# OpenNR 2.21.2 candidate

## Route the map copy at display resolution

- Attach the PerfMode display-resolution raster and depth route to the VR `ISCopy` draw only while the map menu is open and its destination is `kMENUBG`.
- Keep the existing map readiness diagnostics and report `ISCopy` draw and successful display-resolution route counts when map diagnostics are enabled.
- Install the `ISCopy` render hook in VR even when Frame Annotations is disabled; the hook leaves unrelated draws unchanged.

The expected runtime confirmation is a positive `routedDisplayRes` count for `ISCopy` in the next map-open probe. This candidate does not alter `SettingsUser.json` or MO2 profile selection.
