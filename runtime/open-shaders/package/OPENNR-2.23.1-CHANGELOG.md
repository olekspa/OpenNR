# OpenNR 2.23.1

A follow-up to 2.23.0. Adds an opt-in network choice for NVIDIA Neural Rendering runtime 310.9+ and keeps two exceptional shader warnings visible when routine VR overlay output is suppressed.

- **Original** is the default and selects the same 310.8-compatible network on 310.9+ (render preset hint 1). 310.8 runtimes retain their original hint 0.
- **Newest** selects the faster 310.9 network (hint 2). Upstream reports lower GPU cost but a different, weaker visual effect; OpenNR's V5/Turbo quality floor has not been checked against it. Restart the game after changing the selector.
- Shader feature-set changes and Effects 11 compilation failures remain visible in the overlay even when ordinary VR HUD output is hidden.

No NVIDIA runtime DLL is included. No in-game or headset validation was run for this release.
