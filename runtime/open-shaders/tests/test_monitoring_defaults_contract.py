import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
PROFILER_H = (ROOT / "src" / "Profiler.h").read_text(encoding="utf-8")
GPU_PASS_CPP = (ROOT / "src" / "GpuPass.cpp").read_text(encoding="utf-8")
OVERLAY_H = (ROOT / "src" / "Features" / "PerformanceOverlay.h").read_text(encoding="utf-8")
UPSCALING_H = (ROOT / "src" / "Features" / "Upscaling.h").read_text(encoding="utf-8")
SHADOW_SCHEDULER_CPP = (ROOT / "src" / "Features" / "LightLimitFix" / "ShadowScheduler.cpp").read_text(encoding="utf-8")
FRAME_ANNOTATIONS_CPP = (ROOT / "src" / "FrameAnnotations.cpp").read_text(encoding="utf-8")


class MonitoringDefaultsContractTests(unittest.TestCase):
    def test_internal_profiler_and_tracy_pass_scopes_are_inactive_by_default(self):
        self.assertIn("std::atomic_bool userEnabled{ false };", PROFILER_H)
        self.assertGreaterEqual(GPU_PASS_CPP.count("profiler && profiler->IsEnabled()"), 2)

    def test_overlay_detailed_monitoring_defaults_off_and_history_is_small(self):
        self.assertIn("bool ShowDrawCalls = false;", OVERLAY_H)
        self.assertIn("bool ShowCSPasses = false;", OVERLAY_H)
        self.assertIn("bool ShowVRAM = false;", OVERLAY_H)
        self.assertIn("int FrameHistorySize = 600;", OVERLAY_H)

    def test_probe_and_shadow_dungeon_logs_are_opt_in(self):
        self.assertIn("bool mapProbeDiagnostics = false;", UPSCALING_H)
        self.assertIn("if (!globals::features::upscaling.settings.mapProbeDiagnostics)", FRAME_ANNOTATIONS_CPP)
        self.assertIn("if (!s_shadowDemand.instrumentation)", SHADOW_SCHEDULER_CPP)


if __name__ == "__main__":
    unittest.main()
