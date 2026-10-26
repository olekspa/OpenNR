"""Static source contracts for NR runtime (nvngx_dlssnr*.dll) discovery, selection and signature policy."""

from pathlib import Path
import json
import re
import subprocess


ROOT = Path(__file__).resolve().parents[3]
OPEN_SHADERS = ROOT / "runtime/open-shaders"
UPSCALE = OPEN_SHADERS / "src/Features/Upscaling"
NR = UPSCALE / "NeuralRendering"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_settings_exist_with_expected_defaults_and_are_persisted():
    header = _read(UPSCALE / "FoveatedRender.h")
    cpp = _read(UPSCALE / "FoveatedRender.cpp")
    assert "std::string neuralRenderingRuntimeFile;" in header
    assert "bool neuralRenderingRequireSignedRuntime = true;" in header
    fields = cpp.split("#define OPENNR_FOVEATED_SETTINGS_FIELDS(X)", 1)[1].split("template <typename BasicJsonType>", 1)[0]
    assert "X(neuralRenderingRuntimeFile)" in fields
    assert "X(neuralRenderingRequireSignedRuntime)" in fields
    # Recommended-settings reset must not silently change the runtime trust policy.
    body = cpp.split("void ApplyRecommendedSettings(", 1)[1].split("\n\t}\n", 1)[0]
    assert "neuralRenderingRuntimeFile" in body and "neuralRenderingRequireSignedRuntime" in body


def test_version_gate_is_310_8_and_310_9_not_310_8_only():
    runtime = _read(NR / "Runtime.cpp")
    policy = _read(NR / "RuntimeSelectionPolicy.h")
    assert "expected DLSSNR 310.8.x," not in runtime + policy
    assert "version->minor() != 8" not in runtime
    assert "v.major == 310 && (v.minor == 8 || v.minor == 9)" in policy
    assert "RuntimeSelection::Select(" in runtime
    assert "WinVerifyTrust" in runtime
    assert "WTD_REVOKE_NONE" in runtime  # offline: no revocation network lookups
    assert '"NVIDIA Corporation"' in runtime or 'L"NVIDIA Corporation"' in runtime


def test_discovery_and_candidate_log_format():
    runtime = _read(NR / "Runtime.cpp")
    assert "Shaders/Upscaling/Streamline" in runtime and "Shaders/Upscaling/StreamlineDX12" in runtime
    assert '"[DLSSNR] runtime candidates:"' in runtime
    assert '" [{} {} {} {}]"' in runtime
    assert '"; selected={} reason={}"' in runtime
    assert '"[DLSSNR] runtime refused: {} unsigned/invalid"' in runtime
    policy = _read(NR / "RuntimeSelectionPolicy.h")
    for reason in ('"auto"', '"user"', '"fallback"'):
        assert reason in policy


def test_feature_creation_failure_logs_runtime_and_gpu_once():
    runtime = _read(NR / "Runtime.cpp")
    assert '"[DLSSNR] feature creation failed with runtime {} {} {}; GPU={}"' in runtime


def test_model_preset_is_preserved_through_startup_and_adaptive_prewarm():
    runtime_h = _read(NR / "Runtime.h")
    runtime_cpp = _read(NR / "Runtime.cpp")
    renderer = _read(NR / "Renderer.cpp")
    assert "const Feature18GuideContract& guide, const Tuning& tuning" in runtime_h
    assert "return EnsureFeature(commandList, slot, guide, tuning);" in runtime_cpp
    assert "PrewarmFeature(commandList, slot, guide, tuning)" in renderer
    assert "stableColorWidth, stableColorHeight, tuning);" in renderer


def test_signature_check_is_lazy_and_off_the_render_thread():
    runtime = _read(NR / "Runtime.cpp")
    assert "std::async(std::launch::async" in runtime
    assert "WarmUpRuntimeSelection" in runtime
    foveated = _read(UPSCALE / "FoveatedRender.cpp")
    post = foveated.split("void FoveatedRender::PostPostLoad()", 1)[1][:600]
    assert "WarmUpRuntimeSelection()" in post
    # No inline WinVerifyTrust outside the worker function.
    assert runtime.count("WinVerifyTrust(window, &action, &data)") == 2  # VERIFY + CLOSE inside VerifyRuntimeSignature


def test_ui_strings_exist_and_have_no_download_links():
    ui = _read(UPSCALE / "FoveatedRender.cpp")
    table = ui.split("constexpr SettingsText kSettingsText[] = {", 1)[1].split("\n\t};", 1)[0]
    for text in (
        "NR runtime",
        "NR runtime file",
        "Auto (recommended)",
        "Require NVIDIA-signed runtime (recommended)",
        "Active: %s | %s | %s",
        "Selected after restart: %s",
        "applies after restarting the game",
        "Turn off only to use a modified runtime you supply and trust (for example one patched for RTX 20/30/40). Modified runtimes run inside the game process.",
        "NVIDIA's official runtime build targets RTX 50 (Blackwell). Older RTX cards may need a runtime you supply that was built for them.",
    ):
        assert text in table, text
    assert "DescribeRuntimeCandidates(" in ui
    assert "IsBlackwellAdapterName(NeuralRendering::RuntimeGpuName())" in ui
    assert "SignatureDisplayText" in ui and "RefusalText(" in ui
    assert "ImGui::BeginDisabled(refused)" in ui
    runtime_ui = "".join(re.findall(r'\{ "nr_runtime_[^\n]*', table))
    assert "http" not in runtime_ui.lower() and "download" not in runtime_ui.lower()
    translations = json.loads(_read(OPEN_SHADERS / "package/SKSE/Plugins/CommunityShaders/Translations/en.json"))
    assert translations["feature.upscaling.nr_runtime_file.label"] == "NR runtime file"
    assert translations["feature.upscaling.nr_runtime_require_signed.label"] == "Require NVIDIA-signed runtime (recommended)"


def test_package_validators_do_not_pin_a_hash_or_require_variants():
    for name in ("ValidateOpenNRPackage.cmake", "ValidateOpenNRSourceContracts.cmake", "Streamline-Runtime.cmake"):
        text = _read(OPEN_SHADERS / "cmake" / name).lower()
        assert "e16bcf15" not in text
        assert "nvngx_dlssnr_" not in text.replace("nvngx_dlssnr_<version>", "")
    assert "nvngx*" in _read(ROOT / "tools/make_lean_release_package.ps1")
    assert "nvngx_dlssnr_310_9_16.dll" in _read(ROOT / "tools/make_lean_release_package.ps1")


def test_no_dll_is_tracked_in_git():
    tracked = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files"], capture_output=True, text=True, check=True
    ).stdout.splitlines()
    dlls = [path for path in tracked if path.lower().endswith(".dll")]
    assert dlls == [], dlls
    assert not [path for path in tracked if Path(path).name.lower().startswith("nvngx") and path.lower().endswith((".dll", ".bin", ".exe"))]
