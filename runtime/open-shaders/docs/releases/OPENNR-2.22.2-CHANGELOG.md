# OpenNR 2.22.2

Map terrain quality test build on top of 2.22.1. Everything below is OFF/neutral by default; nothing changes unless you set it.

## World map, live settings (PerfMode map section, applied only while the world map is open)
- `vrMapDlssPreset` (0 = unchanged; 1-4 = DLSS preset J/K/L/M): map-only override of the DLSS preset used for the map backdrop upscale.
- `vrMapSharpness` (0..1, default 0): RCAS sharpening on the map backdrop. The map backdrop was previously never sharpened (the one-shot resolve fell back to an unsharpened copy). The SDK's own sharpness field is deprecated, so this uses the existing RCAS pass.
- `vrMapMipBiasOffset` (-3..0, default 0): extra negative texture mip bias added to OpenNR's per-frame SharedData mip bias while the map is open (not a global sampler change; nothing to restore). It only affects OpenNR shader paths that consume that constant; whether the native map terrain material does is unknown.
- One diagnostic line per map open: `[PerfMode][MapQuality] ... dlssPreset ... sharpness ... rcasApplied ... mipBiasOffset ... gameMipBias ... dlssReset ... menuMVsValid ... kMAIN=WxH kMENUBG=WxH kTOTAL=WxH` (shows what the map scene is actually rendered at, and whether DLSS is reset every frame while the map is open). Next to it, the existing `[PerfMode][Map] size-chain` line.
- Not changed: the DLSS reset rule (resetting while the menu has no valid motion vectors stays the safe anti-ghosting behaviour), render target sizes, the NR pipeline. Terrain at display resolution is NOT attempted (CSX's attempt cleared the target to black).

## Test steps
Same map spot, zoom and pose; change one control at a time (live): preset M vs unchanged, sharpness 0.25/0.50 (watch for halos), mip offset -1.0/-2.0. Send the `[PerfMode][MapQuality]` and `[PerfMode][Map] size-chain` lines plus which setting you were viewing. Set a control back to its neutral value to disable it.

## Not verified
Nothing was run in the headset.
