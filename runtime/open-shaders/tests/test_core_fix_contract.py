import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
SRC = ROOT / "src"


class CoreFixSourceContractTests(unittest.TestCase):
    def test_map_probe_caps_records_and_keeps_invalid_index_guard_informational(self):
        frame = (SRC / "FrameAnnotations.cpp").read_text(encoding="utf-8")
        self.assertIn("if (mapProbe.targetBinds > 20)", frame)
        self.assertIn("target binds logged={} total={}", frame)
        self.assertIn("logger::info(\"[MapProbe] invalid index", frame)
        self.assertNotIn("logger::warn(\"[MapProbe] invalid index", frame)
        self.assertIn("MapProbeIndexPolicy::TryIndex(renderTargets, rtIndex)", frame)
        self.assertIn("MapProbeIndexPolicy::TryIndex(depthStencils, dsIndex)", frame)

    def test_map_denial_diagnostic_and_renderdoc_annotation_fast_path(self):
        frame = (SRC / "FrameAnnotations.cpp").read_text(encoding="utf-8")
        self.assertIn("first ISCopy route denied reason={}", frame)
        self.assertIn("!mapRouteDenialLogged", frame)
        self.assertIn("globals::state->frameAnnotations && globals::state->IsPerfCaptureActive()", frame)
        self.assertNotIn("Menu::GetSingleton()->ShouldRenderVRMenu()", frame)

    def test_nr_route_status_is_only_published_on_change(self):
        integration = (SRC / "Features/Upscaling/NeuralRendering/Integration.cpp").read_text(encoding="utf-8")
        self.assertIn("if (routeStatus == status)\n\t\t\treturn;", integration)
        status_header = (SRC / "Features/Upscaling/FoveatedRender.h").read_text(encoding="utf-8")
        self.assertIn("friend bool operator==(const RouteStatus&, const RouteStatus&) = default;", status_header)

    def test_clear_hmd_mask_caches_shader_and_checks_map(self):
        upscaling = (SRC / "Features/Upscaling.cpp").read_text(encoding="utf-8")
        self.assertIn("vrClearHMDMaskCS.Get(", upscaling)
        self.assertIn("FAILED(context->Map(vrClearHMDMaskCB.get()", upscaling)
        self.assertIn("!mapped.pData", upscaling)

    def test_foveated_route_has_retry_backoff(self):
        upscaling = (SRC / "Features/Upscaling.cpp").read_text(encoding="utf-8")
        self.assertIn("retryAfter = now + std::chrono::seconds(2)", upscaling)
        self.assertIn("lastRenderScale != settings.vrRenderScale", upscaling)
        self.assertIn("lastScreenWidth != screenWidth", upscaling)
        self.assertIn("if (!failureLogged)", upscaling)

    def test_known_absent_optional_paths_are_not_logged_as_errors(self):
        effect = (SRC / "Features/Effects11/Effects/Effect.cpp").read_text(encoding="utf-8")
        manager = (SRC / "Features/Effects11/EffectManager.cpp").read_text(encoding="utf-8")
        settings = (SRC / "Features/Effects11/SettingManager.cpp").read_text(encoding="utf-8")
        weather = (SRC / "Features/Effects11/WeatherManager.cpp").read_text(encoding="utf-8")
        self.assertIn("No ENB preset directory", effect)
        self.assertIn('logger::info("[EFFECTS11] No preset in use:', manager)
        self.assertIn("Settings path is empty; using defaults", settings)
        self.assertIn('logger::info("[WeatherManager] _weatherlist.ini not found', weather)

    def test_locale_and_heightmap_warnings_are_scoped(self):
        theme = (SRC / "Menu/ThemeManager.cpp").read_text(encoding="utf-8")
        terrain = (SRC / "Features/TerrainShadows.cpp").read_text(encoding="utf-8")
        self.assertIn("if (GetPrimaryCJKGlyphRanges(io.Fonts, availableLocale))", theme)
        self.assertIn("duplicateHeightmapWarnings.insert(metadata.worldspace).second", terrain)


if __name__ == "__main__":
    unittest.main()
