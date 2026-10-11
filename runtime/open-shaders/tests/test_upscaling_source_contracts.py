import re
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
UPSCALE_CPP = (ROOT / "src" / "Features" / "Upscaling.cpp").read_text(encoding="utf-8")
UPSCALE_H = (ROOT / "src" / "Features" / "Upscaling.h").read_text(encoding="utf-8")
STREAMLINE_CPP = (ROOT / "src" / "Features" / "Upscaling" / "Streamline.cpp").read_text(encoding="utf-8")
NR_INTEGRATION_CPP = (ROOT / "src" / "Features" / "Upscaling" / "NeuralRendering" / "Integration.cpp").read_text(encoding="utf-8")
GAME_H = (ROOT / "src" / "Utils" / "Game.h").read_text(encoding="utf-8")


class UpscalingSourceContractTests(unittest.TestCase):
    def test_typed_depth_cache_is_separate_per_upscale_method(self):
        self.assertRegex(UPSCALE_H, r"encodeTexturesCSDepthOutput\[5\]")
        self.assertIn("encodeTexturesCSDepthOutput[methodIndex].Get", UPSCALE_CPP)
        self.assertRegex(UPSCALE_CPP, r"case UpscaleMethod::kDLSS:\s*defines\.push_back\(\{ \"DLSS\", \"\" \}\);")
        self.assertRegex(UPSCALE_CPP, r"case UpscaleMethod::kFSR:\s*defines\.push_back\(\{ \"FSR\", \"\" \}\);")

    def test_vr_dlss_eye_one_uses_typed_depth_without_depth_stencil_box_copy(self):
        self.assertIn("(a_method == UpscaleMethod::kDLSS && globals::game::isVR)", UPSCALE_CPP)
        self.assertIn("vrIntermediateLinearDepth[i]->uav.get()", UPSCALE_CPP)
        self.assertIn("upscaling.vrIntermediateLinearDepth[1]->resource.get()", STREAMLINE_CPP)
        self.assertNotIn("vrIntermediateDepth", UPSCALE_CPP + UPSCALE_H + STREAMLINE_CPP)

    def test_foveated_sharpening_uses_current_frame_route_result(self):
        upscale_body = UPSCALE_CPP.split("void Upscaling::Upscale()", 1)[1].split("void Upscaling::PerformUpscaling()", 1)[0]
        self.assertRegex(upscale_body, r"FoveatedRenderImpl::Bridge::routeHandledThisFrame = false;\s*NeuralRendering::UpdateFrameState\(\);")
        self.assertEqual(upscale_body.count("Bridge::routeHandledThisFrame = routeHandled;"), 2)
        postprocess = UPSCALE_CPP.split("void Upscaling::Main_PostProcessing::thunk", 1)[1]
        self.assertIn("if (FoveatedRenderImpl::Bridge::routeHandledThisFrame)", postprocess)
        self.assertNotIn("if (FoveatedRenderImpl::Bridge::IsRouteActive())", postprocess)

    def test_nr_coverage_depth_format_uses_the_selected_route_depth(self):
        self.assertGreaterEqual(NR_INTEGRATION_CPP.count("depthLeft->desc.Format"), 2)
        self.assertNotIn("vrIntermediateDepth[0]->desc.Format", NR_INTEGRATION_CPP)

    def test_angle_normalization_and_frame_wait_are_bounded(self):
        self.assertIn("degrees = std::fmod(degrees, 360.0f);", GAME_H)
        self.assertNotIn("while (degrees < 0.0f) degrees += 360.0f;", GAME_H)
        frame_limiter = UPSCALE_CPP.split("void Upscaling::FrameLimiter()", 1)[1].split("/*", 1)[0]
        self.assertIn("kFrameLatencyWaitTimeoutMs = 1000", frame_limiter)
        self.assertIn("WaitForSingleObject(waitableObject, kFrameLatencyWaitTimeoutMs)", frame_limiter)
        self.assertNotIn("WaitForSingleObject(waitableObject, INFINITE)", frame_limiter)

    def test_dlss_vram_warning_is_accepted_as_success_without_second_evaluate(self):
        evaluate = STREAMLINE_CPP.split("bool Streamline::EvaluateDLSS", 1)[1].split("void Streamline::Upscale", 1)[0]
        self.assertIn("evalResult == sl::Result::eWarnOutOfVRAM", evaluate)
        warning = evaluate.split("evalResult == sl::Result::eWarnOutOfVRAM", 1)[1].split("if (evalResult != sl::Result::eOk)", 1)[0]
        self.assertIn("return true;", warning)
        self.assertEqual(evaluate.count("slEvaluateFeature("), 1)

    def test_startup_prewarm_is_staged_on_menu_frames_for_full_eye_and_hybrid(self):
        prewarm = NR_INTEGRATION_CPP.split("void TryStartupPrewarm(", 1)[1].split("void RestoreRenderTargets(", 1)[0]
        self.assertIn("StartupPrewarmGateReason(gate)", prewarm)
        self.assertIn("foveated.settings.neuralRenderingPrewarm", prewarm)
        self.assertIn("IsGameMenuOpen() || globals::state->isLoadingMenuOpen", prewarm)
        self.assertIn("upscaling.perfMode.IsHookActive()", prewarm)
        self.assertIn("IsFullEyeStereo(foveated)", prewarm)
        self.assertIn("coverage == 100 || hybrid", prewarm)
        self.assertIn("Renderer::Instance().PreparePrewarm", prewarm)
        self.assertIn("Renderer::Instance().PrewarmStereoEye", prewarm)
        self.assertIn("Renderer::PeripheryInstance().PrewarmStereoEye", prewarm)
        self.assertIn("GetRenderEyeWidth()", prewarm)
        self.assertIn("GetRenderEyeHeight()", prewarm)
        self.assertIn("NextStartupPrewarmStage(startupPrewarmStage, hybrid)", prewarm)
        apply = NR_INTEGRATION_CPP.split("bool ApplyFoveatedLdr()", 1)[1]
        self.assertLess(apply.index("TryStartupPrewarm(upscaling, foveated)"), apply.index("if (temporalSuppressed)"))


if __name__ == "__main__":
    unittest.main()
