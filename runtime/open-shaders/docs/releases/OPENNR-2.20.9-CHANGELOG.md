# OpenNR 2.20.9

- Add a map-render diagnostic probe for PerfMode. While the world map menu is open, it records the image-space shader, render target and depth-stencil names and sizes, PerfMode resize membership, swap state, and menu-background blit state. It does not change rendering behavior.
- Recover the 2.20.8 cropped Neural Rendering depth-format fix: use the route-selected depth guide for coverage format checks, so a cropped route does not dereference an unallocated full-eye depth resource.
