import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
FRAME_ANNOTATIONS = (ROOT / "src" / "FrameAnnotations.cpp").read_text(encoding="utf-8")
FRAME_ANNOTATIONS_H = (ROOT / "include" / "FrameAnnotations.h").read_text(encoding="utf-8")
STATE_CPP = (ROOT / "src" / "State.cpp").read_text(encoding="utf-8")
PERF_MODE_H = (ROOT / "src" / "Features" / "Upscaling" / "PerfMode.h").read_text(encoding="utf-8")
MENU_BRIDGE_CPP = (ROOT / "src" / "Features" / "Upscaling" / "PerfMode" / "MenuBridge.cpp").read_text(encoding="utf-8")


class MapProbeContractTests(unittest.TestCase):
    def test_probe_tracks_map_menu_open_close_edges(self):
        self.assertIn("FrameAnnotations::OnMapMenuState(isMapMenuOpen, frameCount);", STATE_CPP)
        self.assertIn("if (previousMapMenuOpen != isMapMenuOpen)", STATE_CPP)
        self.assertIn('logger::info("[MapProbe] open frame=', FRAME_ANNOTATIONS)
        self.assertIn('logger::info("[MapProbe] close frame=', FRAME_ANNOTATIONS)
        self.assertIn("if (!mapProbe.active || !globals::state || !globals::state->isMapMenuOpen)", FRAME_ANNOTATIONS)

    def test_only_map_imagespace_render_hooks_collect_draws(self):
        self.assertIn("ISMap", FRAME_ANNOTATIONS)
        self.assertIn("ISWorldMap", FRAME_ANNOTATIONS)
        self.assertIn("ISWorldMapNoSkyBlur", FRAME_ANNOTATIONS)
        self.assertIn("OnMapShaderDraw(static_cast<std::uint16_t>(EffectType)", FRAME_ANNOTATIONS)
        self.assertIn("if (mapProbe.loggedImageSpaceDraws >= 20)", FRAME_ANNOTATIONS)
        self.assertIn('logger::info("[MapProbe] effect count shader=ISMap raw={} draws={}"', FRAME_ANNOTATIONS)

    def test_probe_records_render_target_depth_and_blit_state(self):
        for field in ("renderTargets[0]", "depthStencil", "GetDesc(&desc)", "inPerfModeEnlargeSet", "swapActive"):
            self.assertIn(field, FRAME_ANNOTATIONS)
        self.assertIn("magic_enum::enum_name(rtEnum)", FRAME_ANNOTATIONS)
        self.assertIn("magic_enum::enum_name(dsEnum)", FRAME_ANNOTATIONS)
        self.assertIn("GetLastMenuBGBlitTarget", PERF_MODE_H)
        self.assertIn("lastMenuBGBlitTarget = boundRTIdx;", MENU_BRIDGE_CPP)

    def test_each_emitted_line_flushes_logger(self):
        self.assertGreaterEqual(FRAME_ANNOTATIONS.count("FlushMapProbeLine();"), 3)
        self.assertIn("spdlog::default_logger()->flush();", FRAME_ANNOTATIONS)


if __name__ == "__main__":
    unittest.main()
