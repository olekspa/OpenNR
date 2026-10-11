# OpenNR 2.21.7

## VR world map resolution

- Route the DynamicFetchDisabled and CustomViewport ISCopy variants used by the VR map through the display-resolution map path, including when frame annotations are disabled.
- Size the map raster extent from the latched SteamVR per-eye display dimensions and report the map copy size chain once per map open.
- The in-game visual result remains to be confirmed by the user.
