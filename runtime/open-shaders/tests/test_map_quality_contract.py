import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
SRC = ROOT / "src"
POLICY = (SRC / "Features/Upscaling/PerfMode/MapQualityPolicy.h").read_text(encoding="utf-8")
UPSCALE_H = (SRC / "Features/Upscaling.h").read_text(encoding="utf-8")
UPSCALE_CPP = (SRC / "Features/Upscaling.cpp").read_text(encoding="utf-8")
STREAMLINE = (SRC / "Features/Upscaling/Streamline.cpp").read_text(encoding="utf-8")
MENU_BRIDGE = (SRC / "Features/Upscaling/PerfMode/MenuBridge.cpp").read_text(encoding="utf-8")
DSSWAP = (SRC / "Features/Upscaling/PerfMode/DSSwap.cpp").read_text(encoding="utf-8")
STATE = (SRC / "State.cpp").read_text(encoding="utf-8")


class MapQualityContractTests(unittest.TestCase):
    def test_map_controls_are_serialized_and_live_in_perfmode_map_ui(self):
        for name in ("vrMapDlssPreset", "vrMapSharpness", "vrMapMipBiasOffset"):
            self.assertIn(name, UPSCALE_H)
            self.assertIn(name, UPSCALE_CPP)
        self.assertIn("Map: DLSS preset", UPSCALE_CPP)
        self.assertIn("Map: sharpness", UPSCALE_CPP)
        self.assertIn("Map: extra mip bias", UPSCALE_CPP)
        self.assertIn("Set to 0 to disable", UPSCALE_CPP)

    def test_dlss_map_preset_is_scoped_and_does_not_change_dispatch_extents(self):
        self.assertIn("ShouldUseMapQualityOverride", STREAMLINE)
        self.assertIn("slDLSSSetOptions(p_viewport, dlssOptions)", STREAMLINE)
        self.assertIn("ePresetJ", STREAMLINE)
        self.assertIn("ePresetK", STREAMLINE)
        self.assertIn("ePresetL", STREAMLINE)
        self.assertIn("ePresetM", STREAMLINE)
        self.assertIn("dlssOptions.outputWidth = width", STREAMLINE)
        self.assertIn("dlssOptions.outputHeight =", STREAMLINE)
        self.assertNotIn("SetRenderTargetSize", STREAMLINE)

    def test_map_sharpening_runs_rcas_at_one_shot_menu_resolve(self):
        self.assertIn("ApplyMapMenuSharpening", UPSCALE_H)
        self.assertIn("return rcas.ApplySharpen(perfMode.GetRefraTempSRV(), perfMode.GetTestTextureUAV()", UPSCALE_CPP)
        self.assertIn("ApplyMapMenuSharpening()", MENU_BRIDGE)
        self.assertIn("if (redirectedSharpen && !mapRcasApplied)", MENU_BRIDGE)
        self.assertIn("CopyResource(testTexture.get(), refraTempTex.get())", MENU_BRIDGE)

    def test_mip_offset_reuses_shareddata_bias_and_is_recomputed_from_game_value(self):
        self.assertIn("lastGameMipBias = data.MipBias", STATE)
        self.assertIn("ApplyMapMipBiasOffset", STATE)
        self.assertIn("sharedDataCB->Update(data)", STATE)
        # No global native sampler-state LOD mutation is introduced.
        hooks = (SRC / "Hooks.cpp").read_text(encoding="utf-8")
        self.assertNotIn("MipLODBias", hooks)
        self.assertNotIn("MipLODBias", DSSWAP)

    def test_map_quality_diagnostics_are_once_per_open_and_include_observed_sizes(self):
        self.assertIn("LogMapQualityState(mapRcasApplied)", MENU_BRIDGE)
        self.assertIn("LogMapQualityState(false)", DSSWAP)
        self.assertIn('"[PerfMode][MapQuality] dlssPreset=', DSSWAP)
        for field in ("gameMipBias=", "dlssReset=", "menuMVsValid=", "upscaleObserved=", "kMAIN=", "kMENUBG=", "kTOTAL="):
            self.assertIn(field, DSSWAP)
        self.assertIn("resetKnown=", DSSWAP)
        self.assertIn("constantsAccepted=", DSSWAP)
        self.assertIn("mapQualityOpenLogged = false", DSSWAP)
        self.assertIn("lastDLSSReset = frameReset", STREAMLINE)
        self.assertIn("lastDLSSConstantsAccepted = true", STREAMLINE)
        self.assertIn("!upscaling.menuCameraMVsValid", STREAMLINE)


if __name__ == "__main__":
    unittest.main()
