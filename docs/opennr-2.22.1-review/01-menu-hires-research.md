# Skyrim VR world map / menu sharpness under PerfMode: research (2026-10-09)

Clones (read-only, blobless) in scratchpad/menu_research/: `csx` (ParticleTroned main-VR), `os` (alandtse/open-shaders dev, not checked out), `pr625.diff`, `csx_menu.md`.
Tags: VERIFIED = read in source/PR text. UNVERIFIED = inference.

## 0. Bottom line

1. The marker-icon blockiness is almost certainly a SOURCE-resolution problem, not a destination/viewport problem. Map icons/labels are Scaleform rasterized into fixed 2048x2048 targets (kHUDMENU, kWORLDUI0/1). Routing ISCopy or the UI pass at display resolution only resamples that 2048^2 texture. CSX hit exactly this and fixed it by raising the Scaleform rasterization size itself (commit 7cdfa07c5, below). VERIFIED (commit message + code). OpenNR has no kHUDMENU / Scaleform-viewport code (grep of runtime/open-shaders/src: zero hits for kHUDMENU, kPROJECTEDMENU, kWORLDUI, SetViewport-on-movie, PostDisplay). VERIFIED.
2. Nobody (CSX or Alan) renders the map TERRAIN scene at display res. CSX tried full-res substitution of kMENUBG+depth (RC88) and it failed (engine clears the substituted target to [0,0,0,1], opaque black map); they reverted to "terrain stays reduced and vendor-upscaled, only widgets go sharp". VERIFIED (csx_menu.md "RC88 Map policy (superseded)", Non-Goals).
3. "Alan's way for other menus" = PR #625 `feat(vr): upscale frames at eye submission` (branch vr-submit-upscaling): menu UI drawn ONCE into a separate full-res transparent layer, composited after reconstruction. It was CLOSED UNMERGED 2026-09-19 (renderdoc incompatibility, headset blanking, no perf gain; Alan also noted "need to fix the world map breakage since we hide the map background"). It is not in open-shaders dev. VERIFIED. What dev actually has for items/inventory is the older PerfMode "enlarged kMENUBG at display res" path (section 1B).
4. The user's "CSX makes a higher-res snapshot before downscale and copies it back" is a loose description of CSX's "final-resolution menu layer": exact menu bridge draws are REPLAYED into a display-size transparent layer and alpha-composited at submit, original low-res draws suppressed. It is not a snapshot of the scene. VERIFIED.

## 1. alandtse/open-shaders

### 1A. Menu-related PRs/issues (all VERIFIED by `gh pr view`)
| # | State / date | Relevance |
|---|---|---|
| #24 https://github.com/alandtse/open-shaders/pull/24 | merged 2026-05-24 | DLSSperf (now PerfMode) origin. Enlarges kMENUBG, kIMAGESPACE_TEMP_COPY, kTOTAL to display res via 3 per-site CreateRT thunks; ISCopyRender_Hook stretches dest viewport; MaybeBlitMenuBG from tonemap hook. |
| #42 .../pull/42 | merged 2026-05-26 | Generalized to FSR, renamed PerfMode. |
| #131 .../pull/131 | merged 2026-06-08 | Clear fakeDS per frame (hand-shaped cutout in menu compositor). |
| #192 .../pull/192 | merged 2026-06-22 | Hook ISHDRTonemapBlendCinematicFade variant. |
| #317 .../issues/317 | open | "in-game HUD menus render cropped under DLSS/FSR" (inventory cropped L/R under PerfMode); suspects UIPassDispatch_Hook / resolutionScale forced {1,1}. |
| #601 .../pull/601 | merged 2026-09-06 | State::HandlePostProcessing must bypass kMENUBG on VR only (world-space menus rely on vanilla compositor handoff for kMENUBG; Effects11 tonemap does not replicate it). |
| #588 | merged 2026-09-04 | UI darkened by Effects11 grading. |
| #625 .../pull/625 | CLOSED unmerged 2026-09-19 | Submit-stage upscaling + separate full-res menu UI layer (section 1C). |
| #782 .../pull/782 | merged 2026-09-26 | MaybeBlitMenuBG stale copy under FSR (IsPerfModeSharpenRedirectActive must require DLSS). |
| #783 .../pull/783 | merged 2026-09-26 | Frozen menu backdrop: sharpening selector used IsRouteActive() instead of routeHandledThisFrame. |
| #790 .../pull/790 | merged 2026-09-26 | Foveated route now runs in gameplay pause menus (Tween, inventory, magic, stats, map); off only in main/loading (`IsMainOrLoadingMenuOpen`). PR text: "UI is composited after upscaling" in those menus. Map "tracks head" test mean diff 14-17. |
| #573 .../issues/573 | open | "2.10 breaks menu in VR: map and skill tree do not open"; Quest 3 + DLSS; unresolved. |

Nothing in open-shaders issues/PRs says "map blurry / low resolution" (searches: map blurry, low resolution, pixelated, map markers: no hits). The problem has no upstream ticket. VERIFIED by search.

### 1B. How PerfMode (merged dev code) makes inventory/items sharp. VERIFIED in local tree runtime/open-shaders/src/Features/Upscaling/PerfMode/*
- kMENUBG, kIMAGESPACE_TEMP_COPY, kTOTAL are created at DisplayRes*2 x DisplayH (MenuBridge.cpp `InstallCreateRTThunks`, `EnlargeProps`). Everything else (kMAIN, depth) is RenderRes.
- The VR world-space UI pass (renderMode kVRWorldSpaceUIPass, 24) draws the Scaleform-sourced quads INTO the display-res kMENUBG. `UIPassDispatch_Hook` swaps kMAIN depth -> fakeDS and forces the viewport to display size:
```
vp.width  = static_cast<float>(perfMode.displayEyeWidth * 2);
vp.height = static_cast<float>(perfMode.displayEyeHeight);
globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_VIEWPORT);
```
  (MenuBridge.cpp, UIPassDispatch_Hook::thunk). So the item menu panel is rasterized at display res by sampling its 2048^2 (kPROJECTEDMENU, or 1024^2 per CSX RC logs) Scaleform texture. It looks sharp because the QUAD is rasterized at display res and the Scaleform text is vector at 2048^2, not because of any post-upscale composite. This is a "UI drawn at display res into enlarged target" route, NOT a UI-after-upscale layer.
- Backdrop: tonemap hooks (PostIntercept.cpp `RenderTonemapWithSwap`) redirect kMAIN SRV to testTexture (display-res upscaled) so tonemap writes a display-res backdrop; for static backdrops (main menu / loading / map? see `IsStaticMenuBackdropOpen`) `MaybeBlitMenuBG` runs DLSS/FSR itself and blits testTexture -> kTOTAL/kMENUBG with MenuBGBlitPS.
- `ISCopyRender_Hook` (PostIntercept.cpp): after engine ISCopy, if dest RT > engine viewport it replays `DrawIndexed(6,0,0)` with viewport = full dest (stretch). Source < dest so the clamp sampler stretches the small source. Excerpt:
```
bool needsStretch = numVP > 0 && (rtDesc.Width > static_cast<UINT>(savedVP[0].Width + 1.0f) || ...
context->RSSetViewports(1, &stretchVP); context->DrawIndexed(6, 0, 0);
```
- OpenNR already layered on top (local commits 03885e26 ... 99514b10): `BeginMapMenuDisplayResolution` (DSSwap.cpp) forces RT0=kMENUBG, VP/scissor = display composite size, swaps depth views to fakeDSV around the map ISCopy. This is approach (a) below and is destination-side only.

### 1C. PR #625 menu design (the "Alan" approach). VERIFIED from pr625.diff (scratchpad/menu_research/pr625.diff)
Files: src/Features/Upscaling/VRSubmitUpscaling.{cpp,h} (+1194/+218), VRSubmitUpscalingFoveation.cpp, Shaders NativeMenuCS.hlsl, SubmitColorCS.hlsl, EncodeTexturesCS.
Mechanism:
- Hook on the mode-24 UI accumulator dispatch; accept only if shadow RT0 == kMENUBG, depth == kMAIN, other RTs none (`DrawNativeMenuUI`).
- Allocate `menuColor`/`menuDepth` at OUTPUT size (`plan.outputWidth*2 x outputHeight`), temporarily point the engine's kMENUBG target + kMAIN depth entries at them, set viewport and scaled scissors to output size, call the original UI dispatch, then restore (`target = savedTarget; depth = savedDepth; shadow.viewPort = savedViewport`).
- `CaptureMenuDraw`: draws whose PS SRV0 is kPROJECTEDMENU/kHUDMENU/kWORLDUI0-6 are redirected into a transparent `menuUILayer` (premultiplied blend), excluded from the low-res background; clear requests honored (`SRTM_CLEAR` -> clear transparent).
- Layer composited after DLSS/FSR+foveation at submit.
- Map scene handling: `capturedMapScene = capturedMenuScene && state->isMapMenuOpen`; uses `FillMenuCameraMotionVectors()` and falls back to "engine TAA at render resolution" if map camera motion unavailable. Map background was "hidden" and broke ("Need to fix the world map breakage since we hide the map background", alandtse). Closed with that unresolved.
Takeaway: even Alan's own approach did not solve the map terrain; it only separated UI.

## 2. ParticleTroned/skyrim-community-shaders (CSX), branch main-VR (cloned, read)

Design doc: menu.md (blob at 7cdfa07c5; copy in scratchpad/menu_research/csx_menu.md). Non-goals state: do not port OS PerfMode, broad RT resizing, kTOTAL replacement, tonemap interception. Scene stays at reduced (e.g. 0.33) res.

### 2A. Final-resolution menu layer ("snapshot-and-copy-back" as described by the user). VERIFIED
- Semantic accumulator epoch stack around `BSShaderAccumulator::RenderBatches` (FrameAnnotations.cpp) + "bridge" hooks on the higher/direct draw (`TryInstallVRMenuBridgeHigherCallHook`, `...DirectDrawHook`).
- Exact mode-24 draws that sample kPROJECTEDMENU / kHUDMENU (and for Map/Stats, non-depth-writing kWORLDUI0/1 -> kMENUBG) are replayed into `vrMenuFinalCompositeLayer` (size = `plan.finalOutputSize`, transparent, premultiplied via ONE/INV_SRC_ALPHA); original reduced draw suppressed ONLY after capture succeeds; layer sealed per frame (`vrMenuFrameTransaction`, staging/committed swap) and alpha-composited into both eyes after vendor upscaling at submit; desktop mirror composites left half at Present.
- Entry points: `Upscaling::BeginVRMenuDrawInterface/EndVRMenuDrawInterface` (Upscaling.cpp ~14720-14810), `CaptureVRMapMenuLayer` (~14812).
- Dormant / disabled: map scene target+depth substitution and ISCopy correction. Rationale (csx_menu.md "RC88 Map policy"): replaying whole mode-24 epoch pulls depth-writing 3D map work into the layer; substituted 4936x2740 kMENUBG is cleared by Skyrim to [0,0,0,1] after CS clears it to [0,0,0,0], so alpha composite replaces terrain with opaque black.

### 2B. Map-specific source supersampling. VERIFIED
Commit 7cdfa07c5 (2026-07-21, ParticleTroned) "fix(vr-menu): supersample map symbols at source". Message: map location symbols share the fixed kHUDMENU producer and stay soft at aggressive render scales even when the final HUD consumer is replayed after upscaling. Files: src/Features/Upscaling.cpp (+337), Upscaling.h, menu.md. Present on main-VR (`git branch --contains`).
- `Upscaling::EnsureVRMapMenuUISupersampling()` (Upscaling.cpp ~13539): while Map is open and the menu transport contract holds, allocate new color tex + depth tex for kHUDMENU at scale
```
const float supersampleScale = std::clamp(std::max(displayScale, 1.5f), 1.0f, 2.0f); // 8-px aligned
```
  (displayScale = max(finalEyeW/native W, finalH/native H); native is 2048x2048), clear transparent/1.0 depth, then overwrite the engine's `renderTargets[kHUDMENU]` {texture,RTV,SRV; null textureCopy/SRVCopy/UAV} and `depthStencils[kHUDMENU]` {texture,views[8],readOnlyViews[8],depthSRV,stencilSRV}; set DIRTY_RENDERTARGET|DIRTY_VIEWPORT. Fails open to native on any mismatch.
- `Upscaling::VRMapMenuPostDisplayHook` installed with `stl::write_vfunc<0x6, VRMapMenuPostDisplayHook>(RE::VTABLE_MapMenu[0])` (Upscaling.cpp ~22188): around `MapMenu::PostDisplay` it reads `uiMovie->GetViewport`, scales bufferWidth/Height, left/top/width/height and scissor fields by supersample/native, `SetViewport(supersampled)`, calls original, restores native viewport (ScopeExit).
- `ReleaseVRMapMenuUISupersampling()` restores all saved pointers on close / transport loss / reset.
- Also `VRMapMenuCopyRenderHook` on `VTABLE_BSImagespaceShaderCopy[3]` vfunc 0x1 -> `StretchVRMapMenuCopyIfNeeded()`: only when dest is the final composite layer, source SRV < dest: replays DrawIndexed(6,0,0) with viewport+scissor = dest size. Same trick as OS ISCopyRender_Hook.
Takeaway: sharp map icons require raising the Scaleform raster resolution (kHUDMENU 2048 -> 3072-4096); destination-only routing cannot.

### 2C. Known artifacts / pitfalls recorded in csx_menu.md. VERIFIED
- Map is TWO mode-24 epochs per frame; one has HUD bridge (24x2) then 12x2 then 3600x2 Map draws with kMAIN depth and depth writes ON (post-HUD dependency); also 6x2 draws sampling kWORLDUI0 with kMAIN. kMENUBG is cleared inside mode 24 every Map frame; CopyResource to kMAIN.copy outside the accumulator. => ordering/depth dependencies make whole-epoch relocation invalid.
- Substituting kMENUBG without matching depth views: cleared opaque black (RC88).
- Source size varies (kPROJECTEDMENU observed 1024 and 2048; kHUDMENU 2048): always query the bound resource.
- Blend equation: engine uses SRC_ALPHA/INV_SRC_ALPHA with alpha factor ZERO; a transparent staging layer needs source-alpha factor ONE or alpha stays 0.
- Fast-travel Loading after Map has a different bridge (flag=1, mode=516); orphaned BarterMenu reopen after Map->Loading (guard added); closed-map layers retained 30 frames (commit 0f7c8aa71, 2026-09-25 "retire closed map menu layers").
- Open composite / RenderDoc blocks the path (`IsOpenCompositeUpscalingBlocked`, `IsRenderDocUpscalingBlocked`).

## 3. Skyrim VR map pipeline (what is known)
VERIFIED (CSX trace-derived, csx_menu.md): see 2C. Map UI textures: kHUDMENU 2048^2, kWORLDUI0/1 2048^2 (location/fast-travel labels), kPROJECTEDMENU 1024/2048. Map 3D content is drawn inside the mode-24 pass with kMAIN depth, not through a named ISMap shader (OpenNR source comment says the same: "named ISMap shaders are not on that path").
UNVERIFIED: whether the "terrain softness" is the 3600x2 geometry draws sampling an already-low-res map texture vs. a render target. The user-reported "2048x2048 map ISCopy" suggests an ISCopy whose SOURCE is 2048^2, which means stretching it to 4992x2688 can never add detail (soft by construction, and non-uniform aspect: 2048x2048 square -> 2:1 wide composite = "weird/stretched" look unless the UVs/VP crop correctly). Need a RenderDoc capture of one map frame to confirm source/dest of each of the events.
Other non-resolution map pitfalls (upstream): fog/lighting in map: OS #788 `fix(fog): disable height fog in the world map` (2026-09-26), OS #204 IBL off in world map; upstream CS #2763/#2788/#2765.

## 4. Approach comparison for OpenNR (keep OS pipeline unchanged)

(a) Render map scene at display res by changing VP/RT/depth for the map draws (what 03885e26..99514b10 do).
- Works only if the map's content is generated by geometry/ISCopy from a source that is itself >= display res. For 2048^2 Scaleform/kHUDMENU-derived content it just resamples: no new detail, aspect distortion risk (2048x2048 -> 2496x2688 per eye / 4992x2688 composite).
- Risk: depth/RT bookkeeping (fakeDS size, kMAIN_COPY), already "weird". Least additional benefit.

(b) Snapshot / replay-to-final-layer (CSX 2A, Alan #625).
- Highest cost and invasiveness (new hooks on draw bridge, transaction/seal logic, submit-stage composite). CSX explicitly could not do it for terrain (RC88). In OpenNR the display-res kMENUBG already exists, so the layer buys little; what it would add is only avoiding kMENUBG->DLSS history. Not recommended.

(c) Item menu: it is "UI pass directly into the display-res enlarged kMENUBG" (1B), not a UI-after-upscale layer (that exists only in unmerged #625). It does not transfer to the map because map sharpness is bounded by the 2048^2 Scaleform producer, plus map terrain is a reduced mixed depth-writing pass.

(d) RECOMMENDED, least invasive, highest payoff: CSX 2B source supersampling, ported on top of the existing OpenNR display-res kMENUBG:
 1. While `isMapMenuOpen` and PerfMode active, swap `renderTargets[kHUDMENU]` (and its depth) for a 1.5-2x (or display-matched, 8-aligned, cap 4096) texture pair with identical formats/views, clear on swap, restore on close.
 2. Hook `MapMenu::PostDisplay` (`write_vfunc<0x6>(VTABLE_MapMenu[0])`) and scale the Scaleform `GViewport` buffer/viewport/scissor fields by the same factor, restore after.
 3. Consumers (UI pass quads sampling kHUDMENU into display-res kMENUBG, with OpenNR's existing fakeDS/VP path) pick up the larger source via the sampler; check mip/aniso: source SRV needs matching view desc (CSX copies desc from the original SRV/RTV). Consider also kWORLDUI0/1 (location/fast-travel labels) which are also 2048^2 but not covered by CSX's change (CSX handles them by final-layer replay only).
 4. Keep the existing map ISCopy stretch for the terrain; accept terrain soft-by-vendor-upscale like CSX, or evaluate whether ISCopy source is a low-res map texture (needs capture).
 Risks: VRAM/fill (1.5-2x of 2048^2 RGBA8 = up to 4096^2 = 64 MB color + depth), other menus sharing kHUDMENU while map open (MessageBox overlap), engine caching of RT size (Scaleform buffer size vs. target size must match or UI is cropped/offset: CSX needed viewport scaling for exactly this), early-out when sizes already equal, thread: do swap on render thread inside PostDisplay/DrawInterface boundaries only.

## 5. First diagnostic to do before coding (cheap)
RenderDoc (OpenNR lists RenderDoc as a supported capture path; note OS PR #625 closed because RenderDoc crashed with submit-stage upscaling, not applicable to PerfMode) one frame with the map open: record, for the mode-24 draws, the SRV textures (names/sizes: kHUDMENU/kWORLDUI0/1/kPROJECTEDMENU), the ISCopy source/dest sizes, and whether marker icons come from a 2048^2 texture. If markers are in a 2048^2 texture magnified across the map quad, (d) is the fix; if the ISCopy source is 2048^2 the terrain softness is also source-bound.
