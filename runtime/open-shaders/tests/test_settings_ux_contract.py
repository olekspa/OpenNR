from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "runtime/open-shaders"
UPSCALE = RUNTIME / "src/Features/Upscaling"


def _function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated function {signature}")


def _settings_defaults(source: str) -> dict[str, str]:
    body = source.split("struct Settings", 1)[1].split("\n\t};", 1)[0]
    return dict(re.findall(r"(?:bool|uint|float|std::uint32_t)\s+(\w+)\s*=\s*([^;]+);", body))


def test_recommended_defaults_match_previous_settings_except_intended_performance_defaults():
    old = subprocess.run(
        ["git", "show", "fea91a13:runtime/open-shaders/src/Features/Upscaling/FoveatedRender.h"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    current = (UPSCALE / "FoveatedRender.h").read_text(encoding="utf-8")
    old_defaults = _settings_defaults(old)
    current_defaults = _settings_defaults(current)
    changed = {
        "subrectBlendMode", "subrectFeatherWidth", "subrectFalloffCurve", "subrectDitherStrength",
        "neuralRenderingCoverage", "neuralRenderingProjectionCenteredCrop", "neuralRenderingPeripheryEnabled",
    }
    shared = {key: value for key, value in old_defaults.items() if key in current_defaults and key != "enabled" and key not in changed}
    assert shared
    assert {key: current_defaults[key] for key in shared} == shared


def test_default_foveation_and_recommended_are_the_measured_hybrid():
    header = (UPSCALE / "FoveatedRender.h").read_text(encoding="utf-8")
    ui = (UPSCALE / "FoveatedRender.cpp").read_text(encoding="utf-8")
    defaults = _settings_defaults(header)
    assert defaults["enabled"] == "1"
    assert defaults["neuralRenderingEnabled"] == "true"
    assert defaults["neuralRenderingCoverage"] == "55"
    assert defaults["neuralRenderingProjectionCenteredCrop"] == "true"
    assert defaults["neuralRenderingPeripheryEnabled"] == "true"
    assert defaults["neuralRenderingPeripheryResolution"] == "50"
    assert defaults["neuralRenderingPeripheryStagger"] == "true"
    assert defaults["subrectBlendMode"] == "static_cast<uint>(SubrectBlendMode::kDither)"
    assert defaults["subrectFeatherWidth"] == "128.0f"
    assert defaults["subrectDitherStrength"] == "1.45f"
    assert defaults["subrectFalloffCurve"] == "1.23f"
    recommended = _function_body(ui, "void ApplyRecommendedSettings(")
    assert "settings = FoveatedRender::Settings{};" in recommended
    assert "settings.neuralRenderingEnabled = true;" in recommended
    assert "settings.neuralRenderingCoverage =" not in recommended
    assert "upscalingSettings.mapProbeDiagnostics = false;" in recommended
    assert "upscalingSettings.streamlineLogLevel = 0;" in recommended


def test_neural_rendering_page_has_only_the_master_off_control():
    ui = (UPSCALE / "FoveatedRender.cpp").read_text(encoding="utf-8")
    upscaling = (RUNTIME / "src/Features/Upscaling.cpp").read_text(encoding="utf-8")
    status = _function_body(ui, "void FoveatedRender::DrawNeuralRenderingStatusButton(")
    assert status.count("settings.neuralRenderingEnabled = !settings.neuralRenderingEnabled;") == 1
    assert 'ImGui::Checkbox(SettingsLabel("nr_enable")' not in ui
    assert '"nr_safe", "Safe mode"' in ui
    assert 'SettingsLabel("nr_safe")' in ui
    safe = ui.split('if (ImGui::Button(SettingsLabel("nr_safe"))) {', 1)[1].split("}", 1)[0]
    assert "settings.neuralRenderingEnabled = false;" in safe
    assert "settings.enabled = 0;" in safe
    recommended = _function_body(ui, "void ApplyRecommendedSettings(")
    reset = _function_body(ui, "void ResetRouteSettings(")
    assert "settings.neuralRenderingEnabled = true;" in recommended
    assert "settings.neuralRenderingEnabled = defaults.neuralRenderingEnabled;" in reset
    assert "settings.enabled = settings.neuralRenderingEnabled ? 1u : 0u;" in reset
    assert '"nr_full_eye_slowest", "Quality: full-eye NR (slowest)"' in ui
    full_eye = ui.split('if (ImGui::Button(SettingsLabel("nr_full_eye_slowest"))) {', 1)[1].split("}", 1)[0]
    assert "ApplyFullEyeQualitySettings(settings);" in full_eye
    assert "subrectController.ApplyPresetByName(kPresetFullEye);" in full_eye
    dlss_mode = _function_body(ui, "void FoveatedRender::DrawDLSSModeControl()")
    assert "const bool fullEyePreset = subrectController.GetUV().IsFullEye()" in dlss_mode
    assert "if (isFSR || fullEyePreset)" in dlss_mode
    assert "Faster mode only changes the cropped-eye composition path." in dlss_mode
    assert 'foveatedRender.DrawNeuralRenderingStatusButton();' in upscaling


def test_old_enabled_key_loads_but_neural_rendering_route_uses_master_toggle():
    header = (UPSCALE / "FoveatedRender.h").read_text(encoding="utf-8")
    ui = (UPSCALE / "FoveatedRender.cpp").read_text(encoding="utf-8")
    assert "X(enabled)" in ui
    assert "json.value(#field, defaults.field)" in ui
    assert "settings = o_json;" in ui
    assert "json.value(#field, defaults.field)" in ui
    active = _function_body(ui, "bool FoveatedRender::IsActive() const")
    assert "settings.neuralRenderingEnabled" in active
    assert "!IsLoaded()" in active
    assert "uint enabled = 1;" in header
    latch = _function_body(header, "void LatchEnabled()")
    assert "settings.enabled = settings.neuralRenderingEnabled ? 1u : 0u;" in latch
    assert "enabledAtBoot = settings.neuralRenderingEnabled;" in latch
    assert "LatchEnabled();" in _function_body(ui, "void FoveatedRender::LoadSettings(")
    status_button = _function_body(ui, "void FoveatedRender::DrawNeuralRenderingStatusButton(")
    assert "settings.enabled = settings.neuralRenderingEnabled ? 1u : 0u;" in status_button
    is_loaded = _function_body(header, "bool IsLoaded() const")
    assert "enabledAtBoot" in is_loaded
    upscaling = (RUNTIME / "src/Features/Upscaling.cpp").read_text(encoding="utf-8")
    assert "foveatedRender.settings.enabled = foveatedRender.settings.neuralRenderingEnabled ? 1u : 0u;" in upscaling
    assert "foveatedRender.settings.enabled = preset.foveation" not in upscaling


def test_explicit_saved_values_are_read_without_migrating_the_user_settings():
    ui = (UPSCALE / "FoveatedRender.cpp").read_text(encoding="utf-8")
    fields = ui.split("#define OPENNR_FOVEATED_SETTINGS_FIELDS(X)", 1)[1].split("#define", 1)[0]
    deserialize = _function_body(ui, "void from_json(const BasicJsonType& json, FoveatedRender::Settings& settings)")
    assert "FoveatedRender::Settings defaults{};" in deserialize
    assert "settings.field = json.value(#field, defaults.field);" in deserialize
    for key in ("neuralRenderingCoverage", "neuralRenderingPeripheryEnabled", "subrectDitherStrength"):
        assert f"X({key})" in fields
        assert f'"{key}"' not in _function_body(ui, "void ApplyRecommendedSettings(")
    load = _function_body(ui, "void FoveatedRender::LoadSettings(")
    assert "settings = o_json;" in load
    assert "ApplyRecommendedSettings" not in load


def test_stale_default_base_is_ignored_without_rewriting_user_files():
    state = (RUNTIME / "src/State.cpp").read_text(encoding="utf-8")
    load = _function_body(state, "void State::Load(")
    assert "LOADING ORDER: Default" in load
    assert "Ignoring stale SettingsDefault.json version" in load
    assert "SettingsDefaultPolicy::ApplyVersionGate(settings, Plugin::VERSION.string())" in load
    policy = (RUNTIME / "src/SettingsDefaultPolicy.h").read_text(encoding="utf-8")
    assert "a_settings = json::object();" in policy
    assert "GetStaleVersion" in policy
    assert "Save(ConfigMode::DEFAULT)" in load  # only the missing-file generation path
    assert "remove(defaultConfigFilePath" not in load


def test_dead_per_frame_timing_and_idle_benchmark_lock_are_gated():
    feature = (UPSCALE / "FoveatedRender.cpp").read_text(encoding="utf-8")
    adaptive = _function_body(feature, "void FoveatedRender::UpdateAdaptiveState(")
    assert "if constexpr (!NeuralRendering::kFullResolutionNeuralRenderingOnly)" in adaptive
    assert "(settings.neuralRenderingAdaptiveEnabled || settings.neuralRenderingAdaptiveCropEnabled)" in adaptive
    assert "frame != sampledEngineFrame" in adaptive
    benchmark = (UPSCALE / "NeuralRendering/SettingsBenchmark.cpp").read_text(encoding="utf-8")
    tick = _function_body(benchmark, "void Tick()")
    assert tick.index("g_tickActive.load(std::memory_order_acquire)") < tick.index("std::scoped_lock lock(g_mutex)")


def test_neural_rendering_page_has_no_foveated_or_shared_dlss_container():
    upscaling = (RUNTIME / "src/Features/Upscaling.cpp").read_text(encoding="utf-8")
    foveated = (UPSCALE / "FoveatedRender.cpp").read_text(encoding="utf-8")
    page = _function_body(upscaling, "void Upscaling::DrawDLSSNRPage()")
    nr_settings = _function_body(foveated, "void FoveatedRender::DrawSettings(")
    upscaling_settings = _function_body(upscaling, "void Upscaling::DrawSettings(")
    assert "Foveated Rendering Controls" not in upscaling
    assert "DrawFoveationControls" not in upscaling
    assert "DrawDLSSNRSharedControls" not in upscaling
    assert "Enable Foveated Upscaling (region source)" not in foveated
    for label in ("Method", "Upscale Preset", "Enable Sharpening", "DLSS Model Preset", "DLSS Mode"):
        assert f'"{label}"' not in page
    assert 'foveatedRender.DrawSettings(true, false, false);' in page
    assert 'foveatedRender.DrawSettings(false, false, false);' in page
    assert nr_settings.count("DrawDLSSModeControl()") == 0
    assert upscaling.count('T(TKEY("method"), "Method")') == 1
    assert upscaling.count('T(TKEY("upscale_preset"), "Upscale Preset")') == 1
    assert upscaling.count('T(TKEY("enable_sharpening"), "Enable Sharpening")') == 1
    assert upscaling.count('T(TKEY("dlss_model_preset"), "DLSS Model Preset")') == 1
    assert upscaling.count("foveatedRender.DrawDLSSModeControl();") == 1
    assert "NeuralRendering::SettingsBenchmark::DrawPanel();" in upscaling_settings


def test_eye_tracking_is_the_last_neural_rendering_section():
    ui = (UPSCALE / "FoveatedRender.cpp").read_text(encoding="utf-8")
    draw_settings = _function_body(ui, "void FoveatedRender::DrawSettings(")
    eye = draw_settings.index('ImGui::CollapsingHeader("Eye-tracked Foveation",')
    assert eye > draw_settings.index("if (!vrControlsFirst)\n\t\tdrawVrControls();")
    assert draw_settings.rfind("ImGui::CollapsingHeader(") == eye


def test_neural_rendering_settings_groups_do_not_hide_controls():
    ui = (UPSCALE / "FoveatedRender.cpp").read_text(encoding="utf-8")
    draw_settings = _function_body(ui, "void FoveatedRender::DrawSettings(")
    headers = [line for line in draw_settings.splitlines() if "ImGui::CollapsingHeader(" in line]
    assert all("ImGuiTreeNodeFlags_Leaf" in line for line in headers)
    assert "if (settings.neuralRenderingEnabled)" not in draw_settings


def test_legacy_guided_filter_key_loads_but_cannot_affect_rendering():
    header = (UPSCALE / "FoveatedRender.h").read_text(encoding="utf-8")
    ui = (UPSCALE / "FoveatedRender.cpp").read_text(encoding="utf-8")
    integration = (UPSCALE / "NeuralRendering/Integration.cpp").read_text(encoding="utf-8")
    benchmark = (UPSCALE / "NeuralRendering/SettingsBenchmark.cpp").read_text(encoding="utf-8")
    assert "X(neuralRenderingPeripheryGuidedFilter)" in ui
    assert "bool neuralRenderingPeripheryGuidedFilter = false;" in header
    assert "neuralRenderingPeripheryGuidedFilter" not in integration
    assert "neuralRenderingPeripheryGuidedFilter" not in benchmark
    assert '"nr_periphery_guided_filter"' not in ui
    assert "settings = o_json;" in ui


def test_version_and_release_artifacts_exist():
    assert (ROOT / "reports/menu_inventory.md").is_file()
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    assert f"VERSION {version}" in (RUNTIME / "CMakeLists.txt").read_text(encoding="utf-8")
    assert (RUNTIME / f"package/OPENNR-{version}-CHANGELOG.md").is_file()
    assert (RUNTIME / f"docs/releases/OPENNR-{version}-CHANGELOG.md").is_file()


def test_refusal_terms_have_plain_language_and_status_is_published():
    ui = (UPSCALE / "FoveatedRender.cpp").read_text()
    integration = (UPSCALE / "NeuralRendering/Integration.cpp").read_text()
    table = ui.split("constexpr SettingsText kSettingsText[] = {", 1)[1].split("\n\t};", 1)[0]
    for term in (
        "disabled", "fullEye", "gaze", "adaptiveCrop", "coverageRects",
        "depthFormat", "eyeWidth%8", "foveatedRoute", "upscaler", "dlssMode",
        "frameGeneration", "hybridPass",
    ):
        assert f'{{ "{term}"' in ui or f'{{ "{term}",' in ui
        assert f'nr_refusal_{term}' in table
        label = re.search(rf'\{{ "nr_refusal_{re.escape(term)}", "([^"]+)"', table)
        assert label and label.group(1).strip(), f"missing plain-language refusal label for {term}"
    assert "Coverage rectangles are unavailable" in table
    assert "Eye width is not a multiple of 8" in table
    assert "status.route = RouteKind::Off;" in integration
    assert "status.route = RouteKind::Refused;" in integration
    assert "RouteKind::NrOnlyCrop : RouteKind::FullEye" in integration
    assert "status.route = RouteKind::PeripheryHybrid;" in integration
    assert integration.count("PublishRouteStatus(std::move(status));") >= 5
    assert "GetRouteStatus()" in ui


def test_published_route_refusal_terms_have_plain_language_status_mappings():
    ui = (UPSCALE / "FoveatedRender.cpp").read_text(encoding="utf-8")
    integration = (UPSCALE / "NeuralRendering/Integration.cpp").read_text(encoding="utf-8")
    route_text = _function_body(ui, "const char* RouteRefusalText(")
    mappings = dict(re.findall(r'\{ "([^"]+)", "([^"]+)" \}', route_text))
    table = ui.split("constexpr SettingsText kSettingsText[] = {", 1)[1].split("\n\t};", 1)[0]

    published_terms = set()
    for expression in re.findall(r"status\.refusalTerm\s*=\s*([^;]+);", integration):
        published_terms.update(re.findall(r'"([^"]+)"', expression))
    for expression in re.findall(r"const char\* peripheryInactiveReason\s*=\s*([^;]+);", integration):
        published_terms.update(re.findall(r'"([^"]+)"', expression))

    # Keep coverage for the PERIPHERY stream's in-flight publisher change even
    # when this branch is checked before that commit is integrated.
    required_terms = {
        "coverage100", "hybridBackoff", "staleGuides", "missingGuides",
        "uvMismatch", "shader", "resources", "unknown",
    }
    assert required_terms <= mappings.keys()
    for term in published_terms | required_terms:
        assert term in mappings, f"Integration publishes unmapped refusal term {term}"
        key = mappings[term]
        label = re.search(rf'\{{ "{re.escape(key)}", "([^"]+)"', table)
        assert label and label.group(1).strip(), f"missing non-empty plain-language label for {term}"

def test_new_group_controls_reference_nonempty_tooltips_in_one_table():
    ui = (UPSCALE / "FoveatedRender.cpp").read_text()
    table = ui.split("constexpr SettingsText kSettingsText[] = {", 1)[1].split("\n\t};", 1)[0]
    required = (
        "nr_coverage", "nr_projection_crop", "nr_shared_x", "nr_shared_y",
        "nr_eye_offsets", "nr_left_x", "nr_left_y", "nr_right_x", "nr_right_y",
        "nr_periphery_enable", "nr_periphery_resolution", "nr_periphery_gain", "nr_centre_blend",
        "nr_periphery_stagger", "nr_edge_mode", "nr_feather", "nr_band",
        "nr_noise", "nr_falloff", "nr_mask", "nr_reset", "nr_recommended", "nr_full_eye_slowest", "nr_safe",
        "nr_overlay", "nr_mapprobe", "nr_adaptive_diagnostics",
        "fov_stretch", "fov_blur", "fov_aa", "fov_smoothing",
        "nr_model_preset", "nr_intensity", "nr_local_tone", "nr_local_structure",
        "nr_skin_structure", "nr_auto_mask", "nr_ui_correction", "nr_model_resolution",
        "tone_shape", "tone_edit", "tone_brightening", "tone_darkening", "tone_color",
        "tone_hue", "tone_shadows", "tone_midtones", "tone_highlights", "tone_max_bright",
        "tone_max_dark", "tone_max_color", "tone_large", "tone_fine", "tone_radius",
        "tone_halo", "tone_stabilize", "tone_stabilize_time", "tone_stabilize_detail",
        "tone_stabilize_depth", "tone_stabilize_color", "nr_adaptive_enable",
        "nr_adaptive_custom_fps", "nr_adaptive_fps", "nr_adaptive_min", "nr_adaptive_down",
        "nr_adaptive_up", "nr_adaptive_dwell", "nr_adaptive_headroom", "nr_adaptive_crop",
        "nr_adaptive_crop_max", "nr_adaptive_crop_min", "nr_adaptive_crop_down",
        "nr_adaptive_crop_up", "nr_adaptive_crop_dwell", "nr_adaptive_crop_transition",
        "nr_adaptive_refresh", "nr_gaze", "nr_gaze_smoothing",
        "nr_gaze_quantization", "nr_temporal_mode", "nr_temporal_depth", "nr_temporal_color",
        "nr_temporal_stagger", "nr_temporal_reset", "nr_experimental_resolve",
        "nr_experimental_pre", "nr_experimental_multi", "nr_style_natural", "nr_style_fabric",
        "nr_style_cinematic", "nr_reset_failure",
    )
    for key in required:
        match = re.search(rf'\{{ "{re.escape(key)}", "[^"]*", "([^"]*)",', table)
        assert match and match.group(1).strip(), f"missing tooltip for {key}"
        if key.startswith("nr_style_"):
            assert f'"{key}"' in ui and "SettingsTooltip(styleKeys[styleIndex])" in ui
        else:
            assert f'SettingsTooltip("{key}")' in ui, f"tooltip not attached to control {key}"
    assert 'feature.upscaling.{}.label' in ui
    assert 'feature.upscaling.{}.tooltip' in ui
    assert 'FindSettingsText("nr_periphery_section").purpose' in ui
    assert 'SettingsLabel("nr_adaptive_unavailable")' in ui
    assert ui.count('{ "nr_temporal_mode",') == 1

def test_six_primary_sections_keep_requested_order():
    ui = (UPSCALE / "FoveatedRender.cpp").read_text()
    headers = (
        'CollapsingHeader(SettingsLabel("nr_route")',
        'CollapsingHeader(SettingsLabel("nr_periphery_section")',
        'CollapsingHeader(SettingsLabel("nr_edge")',
        'CollapsingHeader(SettingsLabel("nr_tone")',
        'CollapsingHeader(SettingsLabel("nr_adaptive")',
        'CollapsingHeader(SettingsLabel("nr_diagnostics")',
    )
    positions = [ui.index(header) for header in headers]
    assert positions == sorted(positions)
    assert ui.index('CollapsingHeader("Experimental Pipeline",') > positions[3]
    assert ui.index('CollapsingHeader("Eye-tracked Foveation",') > positions[-1]
