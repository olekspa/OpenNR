# OpenNR 2.21.1

## Map display-resolution readiness

- Log the PerfMode map-route prerequisites at map-menu open: hook state, fake depth view, renderer, shadow state, and state update flags.
- Retry creation of the display-resolution fake depth view once at the first map draw if PerfMode resource setup ran before the main depth texture was available.
- Keep the display-resolution map route gated on every prerequisite; if it remains unavailable, the existing render-resolution path is used.

Runtime validation of the user's map route is pending a headset test. This candidate does not change SettingsUser.json or the selected MO2 profile.
