# OpenNR 2.20.3

## Neural Rendering: optional Periphery hybrid

- Added off-by-default **Periphery NR** for VR. The centre continues to use the existing full-resolution Feature 18 coverage crop. A second, independent Feature 18 history evaluates the whole eye at a selected reduced scale (50%, 58%, 66%, or 75%).
- The periphery route derives the low-resolution Feature 18 effect (`output - input`), bilinearly upsamples that residual, and adds it over the original full-resolution frame. It does not upscale the low-resolution final image.
- Added effect-space hybrid compositing: the periphery supplies the coarse lighting band, while the centre and periphery supply their detail bands through an 80 px raised-cosine centre ramp contained inside the crop. The periphery gain defaults by selected scale (50%: 0.83, 58%: 0.86, 66%: 0.89, 75%: 0.93); the 58% value is interpolated and 75% is extrapolated from the measured scale values. Changing resolution selects that scale's default; a manually adjusted gain is retained until the resolution changes again.
- Added **Periphery stagger**, default ON for hybrid presets. It alternates the native periphery evaluation between eyes and motion-reuses the other eye's previous periphery residual; the centre crop still evaluates both eyes every frame. Both histories reset together after gaps or route changes.
- Added in-headset Settings Benchmark variants for Full eye, Coverage crop only, Periphery hybrid P66, Periphery hybrid P66 staggered, and Periphery hybrid P50 staggered. The comparison uses 75% coverage for crop-only and hybrid variants.
- The normal NR route remains full-resolution. The reduced-resolution pass is available only through the explicit Periphery NR opt-in and requires the stable Full Eye VR route.

## Validation boundary

The packaged source/build checks verify implementation and shader behavior. The settings benchmark is intended for a user-run headset A/B. This release does not claim live HMD visual acceptance or a measured gameplay frame-time gain.
