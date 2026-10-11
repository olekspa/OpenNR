# OpenNR 2.22.0 in-game test plan (about 15 minutes)

Nothing in 2.22.0 has been run in the headset. Test in this order. Fallback profile: "<MO2_PROFILE>" (select it in MO2 to roll back).

1. **Boot and log.** Start the game, load a save, play 2 minutes, quit. Read `CommunityShaders.log` and check:
   - `[DLSSNR] Periphery NR active: ... coverage=55% res=50% ... stagger=...` appears (the hybrid is on by default);
   - no `Periphery NR inactive` unless the reason is intended; no repeated per-frame warnings;
   - the hybrid texture format line (`fmt=`) for the coarse/residual textures reads float16 (R16G16B16A16_FLOAT = DXGI 10).
2. **Visual check, hybrid.** Look at a face, skin and clothing close up, then the sky and horizon: no visible oval seam, no edge-colour step, lighting continuous across the crop edge. Compare with the "Quality: full-eye NR (slowest)" preset if anything looks off.
3. **World map.** Open the map twice. Log should show `displayRouteApplied=true` and `routedDisplayRes=true`, ISCopy at the display size, no `[MapProbe] target bind` flood. Marker icons should be crisp.
4. **Dungeon.** Enter a crypt/dungeon (the Skylighting hardening ported from upstream changed the history layout). It must not be black. If it is: select the 2.21.8 profile and report.
5. **Benchmark.** Settings > Upscaling > Settings Benchmark (idle, same scene, same position). Run the variants and send the JSON from `...\SKSE\OpenNR-SettingsBenchmark\`:
   - `nr-hybrid-c55-p50-staggered` (the new default, baseline),
   - `nr-full-eye` (reference), `nr-hybrid-c50-p50-staggered`, `nr-hybrid-c60-p50-staggered`,
   - `nr-parallel-eye-queue` and `nr-flush-after-signal` (experimental; only enable permanently if faster and the image is unchanged),
   - `frame-annotations-off`, `skylighting-neutral-output`.
6. **First frame.** Note the stall when you first enter the world after loading (previously ~4.7 s); the log now has per-step timings (`D3D12 interop initialised in ... ms`, NGX init, first evaluate, feature create).
