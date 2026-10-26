# OpenNR 2.23.1

Follow-up to 2.23.0 with a network selector for Neural Rendering runtime 310.9+ and improved exceptional overlay warning visibility.

- The **Original** network is the default and maps to the 310.8-compatible model on 310.9+ runtimes (preset hint 1).
- **Newest** opts into the faster 310.9 network (preset hint 2). This has a different visual result and remains unvalidated against OpenNR's V5/Turbo quality floor. 310.8 runtimes keep hint 0 regardless of the setting.
- Changing the selection requires a game restart.
- Shader feature-set changes and Effects 11 compilation failures remain visible when ordinary VR overlay output is suppressed.
- No NVIDIA runtime binaries are packaged. No in-game or HMD validation was performed.
