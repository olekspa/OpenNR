"""Contracts for the in-headset NR A/B presets and the package asset."""

import json
import os
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "runtime/open-shaders/src/Features/Upscaling/NeuralRendering"
PACKAGE_ASSET = ROOT / "runtime/open-shaders/package/SKSE/Plugins/CommunityShaders/OpenNR-SettingsBenchmark.json"
REQUIRED_PRESET_IDS = [
    "nr-full-eye",
    "nr-coverage-crop-only",
    "nr-hybrid-c55-p66",
    "nr-hybrid-c55-p66-staggered",
    "nr-hybrid-c55-p50-staggered",
    "nr-hybrid-c45-p50-staggered",
    "nr-off",
]
SUPPORTED_COVERAGE = {100, 95, 90, 85, 80, 75, 70, 60, 55, 50, 45}
SUPPORTED_PERIPHERY = {50, 58, 66, 75}


def _source_builtin():
    source = (RUNTIME / "SettingsBenchmark.cpp").read_text(encoding="utf-8")
    match = re.search(r'kBuiltInDefinitions = R"json\((.*?)\)json";', source, re.DOTALL)
    if not match:
        raise AssertionError("could not extract SettingsBenchmark.cpp built-in JSON")
    return json.loads(match.group(1))


def _package_json_path():
    """Use the package staging root when supplied; otherwise check tracked asset."""
    staging_root = os.environ.get("OPENNR_STAGED_PACKAGE_ROOT")
    if staging_root:
        return Path(staging_root) / "SKSE/Plugins/CommunityShaders/OpenNR-SettingsBenchmark.json"
    return PACKAGE_ASSET


class NRBenchmarkPackageContractTests(unittest.TestCase):
    def test_source_preset_ids_are_shipped_in_required_order(self):
        source = _source_builtin()
        source_ids = [variant["id"] for variant in source["variants"]]
        self.assertEqual(source_ids[:len(REQUIRED_PRESET_IDS)], REQUIRED_PRESET_IDS)
        for extra_id in ("nr-hybrid-c50-p50-staggered", "nr-hybrid-c60-p50-staggered", "frame-annotations-off", "skylighting-neutral-output"):
            self.assertIn(extra_id, source_ids)
        self.assertEqual(source_ids.count("nr-off"), 1)
        self.assertIn("nr-hybrid-parallel-eye-queue", source_ids)
        self.assertIn("nr-hybrid-flush-after-signal", source_ids)
        self.assertEqual(len(source_ids), len(set(source_ids)))
        self.assertNotIn("nr-coverage-85", source_ids)
        self.assertNotIn("nr-coverage-80", source_ids)

        package_path = _package_json_path()
        self.assertTrue(package_path.is_file(), f"missing shipped benchmark JSON: {package_path}")
        package = json.loads(package_path.read_text(encoding="utf-8-sig"))
        package_ids = [variant["id"] for variant in package["variants"]]
        self.assertEqual(package_ids, source_ids)
        self.assertTrue(set(source_ids).issubset(package_ids))

        for document in (source, package):
            variants = {variant["id"]: variant for variant in document["variants"]}
            baseline = {
                "neuralRenderingEnabled": True,
                "neuralRenderingCoverage": 55,
                "neuralRenderingPeripheryEnabled": True,
                "neuralRenderingPeripheryResolution": 50,
                "neuralRenderingPeripheryStagger": True,
                "neuralRenderingParallelEyeQueue": False,
                "neuralRenderingFlushAfterSignal": False,
            }
            parallel = variants["nr-hybrid-parallel-eye-queue"]["patches"][0]["settings"]["foveatedRender"]
            flush = variants["nr-hybrid-flush-after-signal"]["patches"][0]["settings"]["foveatedRender"]
            self.assertEqual(parallel, {**baseline, "neuralRenderingParallelEyeQueue": True})
            self.assertEqual(flush, {**baseline, "neuralRenderingFlushAfterSignal": True})

    def test_coverage_values_in_ui_and_all_nr_variants_are_valid_presets(self):
        policy = (RUNTIME / "RuntimePolicy.h").read_text(encoding="utf-8")
        presets = re.search(r"kNeuralCoveragePresets\s*\{([^}]*)\}", policy)
        self.assertIsNotNone(presets)
        policy_values = {int(value) for value in re.findall(r"\d+", presets.group(1))}
        self.assertEqual(policy_values, SUPPORTED_COVERAGE)

        ui = (ROOT / "runtime/open-shaders/src/Features/Upscaling/FoveatedRender.cpp").read_text(encoding="utf-8")
        coverage_labels = re.search(r'static const char\* coverageLabels\[\]\s*=\s*\{([^}]*)\}', ui)
        self.assertIsNotNone(coverage_labels)
        label_values = [int(value) for value in re.findall(r'"(\d+)%"', coverage_labels.group(1))]
        self.assertEqual(label_values, [100, 95, 90, 85, 80, 75, 70, 60, 55, 50, 45])
        self.assertEqual(set(label_values), policy_values)
        self.assertIn('{ "nr_coverage", "NR coverage (%)",', ui)
        self.assertIn("static_assert(IM_ARRAYSIZE(coverageLabels) == NeuralRendering::kNeuralCoveragePresets.size());", ui)
        self.assertIn("NeuralRendering::kNeuralCoveragePresets[coverageIndex]", ui)
        self.assertIn('ImGui::Combo(SettingsLabel("nr_coverage"), &coverageIndex, coverageLabels, IM_ARRAYSIZE(coverageLabels))', ui)
        self.assertIn("coverageLabels, IM_ARRAYSIZE(coverageLabels)", ui)
        self.assertNotIn('SliderInt("NR Coverage"', ui)
        self.assertIn("if (peripheryEnabled && settings.neuralRenderingCoverage == 100)", ui)
        self.assertIn("settings.neuralRenderingCoverage = 55;", ui)

        clamp = ui.split("void FoveatedRender::ClampSettings()", 1)[1].split("void FoveatedRender::", 1)[0]
        self.assertIn("requestedCoverage != settings.neuralRenderingCoverage", clamp)
        self.assertIn("static std::atomic_flag warnedInvalidCoverage = ATOMIC_FLAG_INIT", clamp)
        self.assertIn("warnedInvalidCoverage.test_and_set()", clamp)
        self.assertIn('logger::warn("[DLSSNR] unsupported NR coverage {}%; using {}% (', clamp)

        for document in (_source_builtin(), json.loads(_package_json_path().read_text(encoding="utf-8-sig"))):
            for variant in document["variants"]:
                settings = variant.get("patches", [{}])[0].get("settings", {}).get("foveatedRender", {})
                if "neuralRenderingCoverage" in settings:
                    self.assertIn(settings["neuralRenderingCoverage"], policy_values, variant["id"])
                if "neuralRenderingPeripheryResolution" in settings:
                    self.assertIn(settings["neuralRenderingPeripheryResolution"], SUPPORTED_PERIPHERY, variant["id"])

        header = (ROOT / "runtime/open-shaders/src/Features/Upscaling/FoveatedRender.h").read_text(encoding="utf-8")
        defaults = re.search(r"uint neuralRenderingCoverage = (\d+);", header)
        periphery = re.search(r"uint neuralRenderingPeripheryResolution = (\d+);", header)
        self.assertIn(int(defaults.group(1)), policy_values)
        self.assertIn(int(periphery.group(1)), SUPPORTED_PERIPHERY)

        by_id = {item["id"]: item for item in json.loads(_package_json_path().read_text(encoding="utf-8-sig"))["variants"]}
        expected = {
            "nr-full-eye": (100, False),
            "nr-coverage-crop-only": (55, False),
            "nr-hybrid-c55-p66": (55, True),
            "nr-hybrid-c55-p66-staggered": (55, True),
            "nr-hybrid-c55-p50-staggered": (55, True),
            "nr-hybrid-c45-p50-staggered": (45, True),
        }
        for variant_id, (coverage, periphery) in expected.items():
            settings = by_id[variant_id]["patches"][0]["settings"]["foveatedRender"]
            self.assertEqual(settings["neuralRenderingCoverage"], coverage, variant_id)
            self.assertEqual(settings["neuralRenderingPeripheryEnabled"], periphery, variant_id)
        self.assertTrue(by_id["nr-hybrid-c55-p66-staggered"]["patches"][0]["settings"]["foveatedRender"]["neuralRenderingPeripheryStagger"])
        self.assertTrue(by_id["nr-hybrid-c55-p50-staggered"]["patches"][0]["settings"]["foveatedRender"]["neuralRenderingPeripheryStagger"])
        self.assertTrue(by_id["nr-hybrid-c45-p50-staggered"]["patches"][0]["settings"]["foveatedRender"]["neuralRenderingPeripheryStagger"])
        for coverage in (50, 60):
            variant = by_id[f"nr-hybrid-c{coverage}-p50-staggered"]["patches"][0]["settings"]["foveatedRender"]
            self.assertEqual(variant["neuralRenderingCoverage"], coverage)
            self.assertEqual(variant["neuralRenderingPeripheryResolution"], 50)
            self.assertTrue(variant["neuralRenderingPeripheryStagger"])
        self.assertIn("frame-annotations-off", by_id)
        self.assertFalse(by_id["frame-annotations-off"]["patches"][0]["settings"]["Frame Annotations"])
        self.assertIn("skylighting-neutral-output", by_id)
        self.assertTrue(by_id["skylighting-neutral-output"]["patches"][0]["settings"]["ForceNeutralOutput"])
        self.assertNotIn(33, SUPPORTED_PERIPHERY)

        source = (ROOT / "runtime/open-shaders/src/Features/Upscaling/FoveatedRender.cpp").read_text(encoding="utf-8")
        self.assertIn("NormalizePeripheryResolution(requestedResolution)", source)
        self.assertIn("static std::atomic_flag warnedInvalidPeripheryResolution = ATOMIC_FLAG_INIT", source)
        self.assertIn("warnedInvalidPeripheryResolution.test_and_set()", source)
        self.assertIn("kPeripheryResolutionPresets", (RUNTIME / "RuntimePolicy.h").read_text(encoding="utf-8"))

    def test_projection_centering_default_and_hybrid_preset_values(self):
        ui = (ROOT / "runtime/open-shaders/src/Features/Upscaling/FoveatedRender.cpp").read_text(encoding="utf-8")
        self.assertIn("settings.neuralRenderingProjectionCenteredCrop = true;", ui)
        self.assertIn('{ "nr_projection_crop", "Projection-centred crop",', ui)
        self.assertIn('ImGui::Checkbox(SettingsLabel("nr_projection_crop"), &settings.neuralRenderingProjectionCenteredCrop)', ui)

        hybrid_ids = {
            "nr-hybrid-c55-p66",
            "nr-hybrid-c55-p66-staggered",
            "nr-hybrid-c55-p50-staggered",
            "nr-hybrid-c45-p50-staggered",
        }
        controls = {"nr-full-eye", "nr-coverage-crop-only"}
        for label, document in (("source", _source_builtin()), ("package", json.loads(_package_json_path().read_text(encoding="utf-8-sig")))):
            variants = {variant["id"]: variant for variant in document["variants"]}
            for variant_id in hybrid_ids:
                settings = variants[variant_id]["patches"][0]["settings"]["foveatedRender"]
                self.assertIs(settings["neuralRenderingProjectionCenteredCrop"], True, f"{label}:{variant_id}")
            for variant_id in controls:
                settings = variants[variant_id]["patches"][0]["settings"]["foveatedRender"]
                self.assertIs(settings["neuralRenderingProjectionCenteredCrop"], False, f"{label}:{variant_id}")


if __name__ == "__main__":
    unittest.main()
