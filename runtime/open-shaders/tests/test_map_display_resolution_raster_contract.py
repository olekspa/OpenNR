import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
UPSCALING_H = (ROOT / "src" / "Features" / "Upscaling.h").read_text(encoding="utf-8")
UPSCALING_CPP = (ROOT / "src" / "Features" / "Upscaling.cpp").read_text(encoding="utf-8")
PERF_MODE_H = (ROOT / "src" / "Features" / "Upscaling" / "PerfMode.h").read_text(encoding="utf-8")
DSSWAP_CPP = (ROOT / "src" / "Features" / "Upscaling" / "PerfMode" / "DSSwap.cpp").read_text(encoding="utf-8")
FRAME_ANNOTATIONS = (ROOT / "src" / "FrameAnnotations.cpp").read_text(encoding="utf-8")
POST_INTERCEPT = (ROOT / "src" / "Features" / "Upscaling" / "PerfMode" / "PostIntercept.cpp").read_text(encoding="utf-8")
PERF_MODE_CORE = (ROOT / "src" / "Features" / "Upscaling" / "PerfMode" / "Core.cpp").read_text(encoding="utf-8")
STATE_CPP = (ROOT / "src" / "State.cpp").read_text(encoding="utf-8")


class MapDisplayResolutionRasterContractTests(unittest.TestCase):
    def test_setting_defaults_on_and_is_exposed_as_map_render_resolution(self):
        self.assertIn("bool vrMapMenuDisplayResolution = true;", UPSCALING_H)
        self.assertIn('"Map: render at display resolution"', UPSCALING_CPP)

    def test_map_copy_route_uses_map_state_before_the_destination_bind(self):
        self.assertIn("EffectType == RE::ImageSpaceManager::ISCopy", FRAME_ANNOTATIONS)
        self.assertIn("ShouldRouteMapCopyAtDisplayResolution", FRAME_ANNOTATIONS)
        self.assertIn("globals::game::isVR", FRAME_ANNOTATIONS)
        self.assertIn("globals::state->isMapMenuOpen", FRAME_ANNOTATIONS)
        self.assertIn("ShouldRouteMapCopyAtDisplayResolution", POST_INTERCEPT)
        self.assertNotIn("Menu::GetSingleton()->ShouldRenderVRMenu()", FRAME_ANNOTATIONS)
        self.assertNotIn("Menu::GetSingleton()->ShouldRenderVRMenu()", POST_INTERCEPT)
        self.assertNotIn("Menu::GetSingleton()->ShouldRenderVRMenu()", DSSWAP_CPP)
        self.assertIn("first ISCopy route denied reason={}", FRAME_ANNOTATIONS)
        self.assertNotIn("IsMapMenuBackgroundBound", FRAME_ANNOTATIONS)
        self.assertNotIn("IsMapMenuBackgroundBound", POST_INTERCEPT)
        self.assertNotIn("OMGetRenderTargets", DSSWAP_CPP)
        self.assertIn("depthSelectionPending = sourceDS == UINT32_MAX", DSSWAP_CPP)
        self.assertIn("CanRouteMapCopyWithDepthState", DSSWAP_CPP)
        self.assertIn("if constexpr (EffectType == RE::ImageSpaceManager::ISCopy)", FRAME_ANNOTATIONS)
        self.assertIn("BeginMapMenuDisplayResolution(&sourceRT)", FRAME_ANNOTATIONS)
        self.assertIn("if (ownsMapDisplayRoute)", FRAME_ANNOTATIONS)
        self.assertIn("EndMapMenuDisplayResolution();", FRAME_ANNOTATIONS)
        self.assertIn("func(imageSpaceShader, shape, param);", FRAME_ANNOTATIONS)
        self.assertIn("if (logMapResolution)", FRAME_ANNOTATIONS)
        render_hook = FRAME_ANNOTATIONS.split("struct BSImagespaceShader_Render", 1)[1].split(
            "struct BSImagespaceShader_Dispatch", 1
        )[0]
        self.assertLess(
            render_hook.index("func(imageSpaceShader, shape, param);"),
            render_hook.index("if (logMapResolution)"),
        )
        self.assertIn("isCopyHookInstalled", FRAME_ANNOTATIONS)
        self.assertIn("RE::VTABLE_BSImagespaceShaderCopy[3]", FRAME_ANNOTATIONS)

    def test_route_uses_display_depth_viewport_scissor_then_restores(self):
        self.assertIn("Util::AsW32(fakeDSV.get())", DSSWAP_CPP)
        self.assertIn("GetMapDisplayExtent(displayEyeWidth, displayEyeHeight)", DSSWAP_CPP)
        self.assertIn("displayExtent.compositeWidth", DSSWAP_CPP)
        self.assertIn("displayExtent.compositeHeight", DSSWAP_CPP)
        self.assertIn("RSSetViewports(1, &viewport)", DSSWAP_CPP)
        self.assertIn("RSSetScissorRects(1, &scissor)", DSSWAP_CPP)
        self.assertIn("RSSetViewports(mapMenuSavedViewportCount, mapMenuSavedRasterViewports)", DSSWAP_CPP)
        self.assertIn("RSSetScissorRects(mapMenuSavedScissorCount, mapMenuSavedScissors)", DSSWAP_CPP)
        self.assertIn("ApplyMapMenuDisplayRasterState();", DSSWAP_CPP)
        self.assertIn("void ApplyMapMenuDisplayRasterState();", PERF_MODE_H)

    def test_map_open_logs_each_readiness_prerequisite(self):
        self.assertIn("OnMapMenuStateChanged(isMapMenuOpen)", STATE_CPP)
        readiness_log = DSSWAP_CPP.split('"[PerfMode][Map] open readiness', 1)[1].split('"', 1)[0]
        for name in ("hookActive", "fakeDSV", "renderer", "shadowState", "stateUpdateFlags"):
            self.assertIn(name, readiness_log)

    def test_missing_fake_dsv_is_retried_at_first_map_draw(self):
        begin = DSSWAP_CPP.split("bool PerfMode::BeginMapMenuDisplayResolution", 1)[1]
        self.assertLess(begin.index("EnsureFakeDSV()"), begin.index("const bool resourcesReady"))
        self.assertIn("mapMenuFakeDepthRetryAttempted", begin)
        self.assertIn("bool PerfMode::EnsureFakeDSV()", PERF_MODE_CORE)
        self.assertIn("CreateDepthStencilView(fakeDS.get(), &dsvDesc, fakeDSV.put())", PERF_MODE_CORE)

    def test_probe_reports_actual_iscopy_route_count(self):
        self.assertIn('effect count shader=ISCopy raw={} draws={} routedDisplayRes={}', FRAME_ANNOTATIONS)
        self.assertIn("++mapProbe.isCopyDraws", FRAME_ANNOTATIONS)
        self.assertIn("++mapProbe.routedDisplayResDraws", FRAME_ANNOTATIONS)

    def test_actual_map_copy_variants_are_hooked_even_without_annotations(self):
        for name in ("ISCopy", "ISCopyDynamicFetchDisabled", "ISCopyCustomViewport"):
            self.assertIn(f"EffectType == RE::ImageSpaceManager::{name}", FRAME_ANNOTATIONS)
        install_block = FRAME_ANNOTATIONS.split("void OnPostPostLoad()", 1)[1].split("if (!globals::state->frameAnnotations)", 1)[0]
        self.assertIn("BSImagespaceShaderCopyDynamicFetchDisabled[3]", install_block)
        self.assertIn("BSImagespaceShaderCopyCustomViewport[3]", install_block)

    def test_once_per_open_size_chain_is_always_on(self):
        self.assertIn('[PerfMode][Map] size-chain', FRAME_ANNOTATIONS)
        self.assertIn("LogMapResolutionOnce(mapDisplayRoute)", FRAME_ANNOTATIONS)
        self.assertIn("mapResolutionLogged", FRAME_ANNOTATIONS)
        self.assertIn("GetMapCopySourceSize()", FRAME_ANNOTATIONS)
        self.assertIn("GetMapRenderTargetSize(RE::RENDER_TARGETS::kTOTAL)", FRAME_ANNOTATIONS)


if __name__ == "__main__":
    unittest.main()
