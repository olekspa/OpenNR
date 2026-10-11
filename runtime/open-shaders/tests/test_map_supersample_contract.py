import unittest
from pathlib import Path

ROOT = Path(__file__).parents[1]
POLICY = (ROOT / "src/Features/Upscaling/PerfMode/MapSupersamplePolicy.h").read_text(encoding="utf-8")
PERF_H = (ROOT / "src/Features/Upscaling/PerfMode.h").read_text(encoding="utf-8")
DSSWAP = (ROOT / "src/Features/Upscaling/PerfMode/DSSwap.cpp").read_text(encoding="utf-8")
CORE = (ROOT / "src/Features/Upscaling/PerfMode/Core.cpp").read_text(encoding="utf-8")
UPSCALE_H = (ROOT / "src/Features/Upscaling.h").read_text(encoding="utf-8")
UPSCALE_CPP = (ROOT / "src/Features/Upscaling.cpp").read_text(encoding="utf-8")
ATTRIBUTIONS = (ROOT / "BRANDING_AND_ATTRIBUTIONS.md").read_text(encoding="utf-8")


class MapSupersampleContracts(unittest.TestCase):
    def test_mapmenu_postdisplay_hook_is_verified_slot_and_scoped(self):
        self.assertIn("write_vfunc<0x6, MapMenuPostDisplay_Hook>(RE::VTABLE_MapMenu[0])", CORE)
        self.assertIn("globals::state->isMapMenuOpen", DSSWAP)
        self.assertIn("hookActive && setting > 1.0f", DSSWAP)
        self.assertIn("SetViewport(scaled)", DSSWAP)
        self.assertIn("SetViewport(nativeViewport)", DSSWAP)
        self.assertIn("mapSymbolSupersampleFailureLatched", DSSWAP)

    def test_every_replacement_entry_has_restore_and_reset_paths(self):
        self.assertIn("RE::RENDER_TARGETS::kHUDMENU", DSSWAP)
        self.assertIn("RE::RENDER_TARGETS_DEPTHSTENCIL::kHUDMENU", DSSWAP)
        self.assertIn("ReleaseMapSymbolSupersample(\"map-closed\")", DSSWAP)
        self.assertIn("ReleaseMapSymbolSupersample(\"resource-reset\")", CORE)
        self.assertIn("colorEntry.texture = Util::AsW32(mapSymbolSavedColorTexture)", DSSWAP)
        self.assertIn("depthEntry.texture = Util::AsW32(mapSymbolSavedDepthTexture)", DSSWAP)
        self.assertIn("DIRTY_RENDERTARGET", DSSWAP)
        self.assertIn("restored=true", DSSWAP)

    def test_setting_default_ui_cost_and_policy_caps(self):
        self.assertIn("float vrMapSymbolSupersample = 2.0f", UPSCALE_H)
        self.assertIn("vrMapSymbolSupersample", UPSCALE_CPP)
        self.assertIn("Applies only while the world map is open", UPSCALE_CPP)
        self.assertIn("64 MiB per plane", UPSCALE_CPP)
        self.assertIn("CPU allocation on open", UPSCALE_CPP)
        self.assertIn("kMapSupersamplePlaneByteCap", POLICY)
        self.assertIn("kMapSupersamplePairByteCap", POLICY)
        self.assertIn("GetMapDisplayCoverageScale", POLICY)
        self.assertIn("ClampMapSupersampleScale", POLICY)

    def test_upstream_concept_is_credited_without_copying_names(self):
        self.assertIn("ParticleTroned", ATTRIBUTIONS)
        self.assertIn("7cdfa07c5", ATTRIBUTIONS)


if __name__ == "__main__":
    unittest.main()
