# OpenNR 2.21.8

## VR world map resolution

- Route the VR map copy using the bound `kMENUBG` render target, avoiding the shadow-state target mismatch at imagespace draw entry.
- Rasterize the 2048x2048 map copy across the display-sized side-by-side destination using the existing copy sampler.
- Log the map source, destination, route state, and final composite once per map open.
- The in-game visual result remains to be confirmed by the user.
