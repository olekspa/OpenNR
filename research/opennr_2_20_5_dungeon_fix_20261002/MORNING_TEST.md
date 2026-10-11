# OpenNR 2.20.5 morning headset checklist

Use the same crypt/cave save and starting position for every A/B. Allow the scene to settle after loading. Keep headset, render scale, and lighting settings fixed except for the named toggle. OpenNR 2.20.5 is selected in the new profile `<MO2_PROFILE>`. The build thread did not launch the game.

1. **F1 spotlight fail-open:** enable `Show Shadow Overlay`. Compare `Fail open spotlights without tile` ON (default) against OFF. Save an overlay screenshot in each state. If invalid spot tiles occur, expect `[LLF-Dungeon]` counters to show failed-open spots with F1 ON and forced-dark spots with F1 OFF. Record any illumination change.
2. **F3 scene-history reset:** compare `Reset histories on cell/worldspace change` ON (default) against OFF. Re-enter the crypt or cross an interior door while looking at the same view. Expect one `[OpenNR-Scene]` transition line, an `[Upscaling-History] reset requested` line, and a consumed-reset record. Check whether the old view/double exposure clears after transition.
3. **SCM overlay capture:** leave the LLF shadow overlay visible in the interior. Save a screenshot showing pending-reset age, gate/readers, schedule skips, render skips, and spotlight counters. If reset work is pending, expect a rate-limited non-zero `[SCM-Dungeon]` line while inside.
4. **Interior depth-buffer culling:** turn `EnableDepthBufferCullingInterior` OFF and repeat the view/movement. Record changes to pitch-black areas and the ragged lit island.
5. **SSGI stereo reprojection:** turn SSGI `UseStereoReproject` OFF and repeat slow head movement. Record any change in L/R mismatch or stale double exposure.
6. **Periphery stagger:** turn periphery eye stagger OFF and repeat motion. Record changes in ghosting or eye mismatch. If the dungeon remains pitch black, compare the same save in OpenNR 2.20.1 and 2.20.5; send screenshots and INFO lines beginning `[OpenNR-Scene]`, `[Upscaling-History]`, `[SCM-Dungeon]`, and `[LLF-Dungeon]`.

For each A/B, note the toggle state, screenshot filename, whole-scene brightness, light flicker, eye lag, and whether old imagery persists during motion.