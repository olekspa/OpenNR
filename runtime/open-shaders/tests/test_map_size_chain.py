import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
POLICY = (ROOT / "src" / "Features" / "Upscaling" / "PerfMode" / "MapMenuDisplayResolutionPolicy.h").read_text(encoding="utf-8")
DSSWAP = (ROOT / "src" / "Features" / "Upscaling" / "PerfMode" / "DSSwap.cpp").read_text(encoding="utf-8")
MENU_BRIDGE = (ROOT / "src" / "Features" / "Upscaling" / "PerfMode" / "MenuBridge.cpp").read_text(encoding="utf-8")
PERF_MODE_H = (ROOT / "src" / "Features" / "Upscaling" / "PerfMode.h").read_text(encoding="utf-8")
FRAME_ANNOTATIONS = (ROOT / "src" / "FrameAnnotations.cpp").read_text(encoding="utf-8")


class MapSizeChainContractTests(unittest.TestCase):
    def test_map_extent_is_derived_from_steamvr_display_eye_size(self):
        self.assertIn("GetMapDisplayExtent(uint32_t a_displayEyeWidth, uint32_t a_displayEyeHeight)", POLICY)
        self.assertIn("a_displayEyeWidth * 2, a_displayEyeHeight", POLICY)
        self.assertIn("GetMapDisplayExtent(displayEyeWidth, displayEyeHeight)", DSSWAP)
        self.assertIn("displayExtent.compositeWidth", DSSWAP)
        self.assertIn("displayExtent.compositeHeight", DSSWAP)
        self.assertIn("a_props->width = dp.GetEnlargeWidth();", MENU_BRIDGE)
        self.assertIn("a_props->height = dp.GetEnlargeHeight();", MENU_BRIDGE)
        self.assertIn("enlargeWidth = displayEyeWidth * 2;", MENU_BRIDGE)
        self.assertIn("enlargeHeight = displayEyeHeight;", MENU_BRIDGE)

    def test_map_size_log_reports_source_destination_route_and_final_composite(self):
        self.assertIn("GetMapCopySourceSize()", FRAME_ANNOTATIONS)
        self.assertIn("GetMapRenderTargetSize(RE::RENDER_TARGETS::kMENUBG)", FRAME_ANNOTATIONS)
        self.assertIn("GetMapRenderTargetSize(RE::RENDER_TARGETS::kTOTAL)", FRAME_ANNOTATIONS)
        self.assertIn("GetMapFinalCompositeSize()", FRAME_ANNOTATIONS)
        self.assertIn("Util::GetRenderTargetCount()", FRAME_ANNOTATIONS)
        self.assertIn("renderTargets[RE::RENDER_TARGETS::kVR_FRAMEBUFFER]", FRAME_ANNOTATIONS)
        self.assertIn('finalCompositeTarget={}', FRAME_ANNOTATIONS)
        for field in ("mapTarget=", "ISCopySource=", "ISCopyDestination=", "displayRouteApplied=", "finalComposite="):
            self.assertIn(field, FRAME_ANNOTATIONS)
        self.assertIn("if (mapResolutionLogged)", FRAME_ANNOTATIONS)

    def test_size_chain_has_no_fixed_2048_allocation(self):
        self.assertNotIn("2048", POLICY)
        self.assertNotIn("2048", DSSWAP)
        self.assertIn("a_props->width = dp.GetEnlargeWidth();", MENU_BRIDGE)
        self.assertIn("GetDisplayEyeWidth()", PERF_MODE_H)
        self.assertIn("GetDisplayEyeHeight()", PERF_MODE_H)


if __name__ == "__main__":
    unittest.main()
