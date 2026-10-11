# OpenNR 2.21.0

- Add the runtime toggle “Map: render at display resolution” for PerfMode world-map draws. The route targets the display-sized map surface and depth, applies a display-sized viewport and scissor during the map draw, and restores the previous raster state afterward. The toggle defaults on.
- Retain the crash-safe MapProbe hooks and add an opt-in diagnostics switch, default off.
- Raise the default Neural Rendering centre blend width to 160 px.
- Keep the Periphery Guided Filter compiled but disabled and unqualified. Existing GPU evidence did not meet the parity and quality gates.
- Disable idle profiler/Tracy capture by default; detailed overlay rows, MapProbe, Skylighting diagnostic summaries, and SCM dungeon census logs default off. Rendering and shadow/SSGI visual defaults are unchanged.
- Standardize section headings in the Neural Rendering and Performance settings and set shared tooltip wrapping to a readable, UI-scaled width.
