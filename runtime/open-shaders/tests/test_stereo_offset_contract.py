"""Contracts for per-eye NR projection-center alignment."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "runtime/open-shaders/src/Features/Upscaling"


def test_per_eye_offsets_are_registered_clamped_and_visible_in_the_ui():
    header = (RUNTIME / "FoveatedRender.h").read_text()
    feature = (RUNTIME / "FoveatedRender.cpp").read_text()
    settings = feature.split("#define OPENNR_FOVEATED_SETTINGS_FIELDS(X)", 1)[1].split("#define", 1)[0]
    for side in ("Left", "Right"):
        for axis in ("X", "Y"):
            name = f"neuralRenderingProjectionCenterOffset{side}{axis}Px"
            assert f"float {name} = 0.0f;" in header
            assert f"X({name})" in settings
            assert f"settings.{name} = clampFinite(settings.{name}, 0.0f, -200.0f, 200.0f);" in feature
            assert f'SettingsLabel("nr_{side.lower()}_{axis.lower()}")' in feature
            assert f'&settings.{name},' in feature
    assert 'SettingsTooltip("nr_eye_offsets")' in feature
    assert "A zero axis falls back to the shared offset for that axis" in feature


def test_projection_crop_applies_each_eye_offset_and_keeps_shared_fallback():
    integration = (RUNTIME / "NeuralRendering/Integration.cpp").read_text()
    for side in ("Left", "Right"):
        for axis in ("X", "Y"):
            assert f"neuralRenderingProjectionCenterOffset{side}{axis}Px" in integration
    assert "const bool leftEye = eye == 0;" in integration
    assert "leftEye ? foveated.settings.neuralRenderingProjectionCenterOffsetLeftXPx :" in integration
    assert "foveated.settings.neuralRenderingProjectionCenterOffsetRightXPx;" in integration
    assert "leftEye ? foveated.settings.neuralRenderingProjectionCenterOffsetLeftYPx :" in integration
    assert "foveated.settings.neuralRenderingProjectionCenterOffsetRightYPx;" in integration
    assert "result.offsetXPx[eye] = eyeOffsetX == 0.0f ? foveated.settings.neuralRenderingProjectionCenterOffsetXPx : eyeOffsetX;" in integration
    assert "result.offsetYPx[eye] = eyeOffsetY == 0.0f ? foveated.settings.neuralRenderingProjectionCenterOffsetYPx : eyeOffsetY;" in integration
    assert "ComputeCoverageGeometry(foveated, eyeWidth, totalDesc.Height" in integration


def test_projection_offsets_are_ordinary_settings_and_not_gated_by_stereo_load_order():
    feature = (RUNTIME / "FoveatedRender.cpp").read_text()
    integration = (RUNTIME / "NeuralRendering/Integration.cpp").read_text()
    load = feature.split("void FoveatedRender::LoadSettings(", 1)[1].split("void FoveatedRender::RestoreDefaultSettings", 1)[0]
    assert "settings = o_json;" in load
    assert "subrectController.LoadSettings(o_json);" in load
    assert "SetStereoEnabled" not in load
    # The stereo controller's late enable preserves explicit per-eye crop UVs.
    assert "if (stereoEnabled && !rightUVLoadedFromJson)" in (ROOT / "runtime/open-shaders/src/Utils/Subrect.cpp").read_text()
    assert "neuralRenderingProjectionCenterOffsetLeftXPx" in integration
    assert "neuralRenderingProjectionCenterOffsetRightXPx" in integration
    assert "stereoOpt.settings.enabled" not in integration


def test_version_and_release_notes_match_candidate():
    version = (ROOT / "VERSION").read_text().strip()
    cmake = (ROOT / "runtime/open-shaders/CMakeLists.txt").read_text()
    assert f"VERSION {version}" in cmake
    validator = (ROOT / "runtime/open-shaders/cmake/ValidateOpenNRSourceContracts.cmake").read_text()
    assert f'"VERSION {version}"' in validator
    for path in (
        f"runtime/open-shaders/package/OPENNR-{version}-CHANGELOG.md",
        f"runtime/open-shaders/docs/releases/OPENNR-{version}-CHANGELOG.md",
    ):
        assert (ROOT / path).is_file()
