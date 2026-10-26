"""Source contracts for selected upstream VR bug fixes ported to OpenNR."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def test_world_map_disables_height_and_volumetric_fog():
    fog = (SRC / "Features/ExponentialHeightFog.cpp").read_text(encoding="utf-8")
    common = fog.split("ExponentialHeightFog::Settings ExponentialHeightFog::GetCommonBufferData() const", 1)[1]
    common = common.split("void ExponentialHeightFog::SaveSettings", 1)[0]
    prepass = fog.split("void ExponentialHeightFog::Prepass()", 1)[1]

    assert "data.enabled = data.enabled && !globals::state->isMapMenuOpen;" in common
    assert "globals::state->isMapMenuOpen" in prepass.split("EnsureVolumetricResources()", 1)[0]
    assert "ReleaseVolumetricResources();" in prepass.split("EnsureVolumetricResources()", 1)[0]


def test_detour_attach_failures_abort_report_and_leave_thunk_disabled():
    hooks = (ROOT / "include/PCH.h").read_text(encoding="utf-8")
    attach = hooks.split("inline LONG detour_attach", 1)[1].split("template <class T>", 1)[0]
    thunk = hooks.split("long detour_thunk(std::uintptr_t a_address)", 1)[1].split("template <class T>", 1)[0]
    vfunc = hooks.split("void detour_vfunc(void* target)", 1)[1].split("\n\t}", 1)[0]

    assert "DetourTransactionAbort();" in attach
    assert "DetourUpdateThread(GetCurrentThread())" in attach
    assert "DetourAttach(a_target, a_detour)" in attach
    assert "T::func = 0;" in thunk
    assert "SKSE::log::error" in thunk
    assert "Util::VTableHookFallback" in vfunc


def test_vr_background_compile_hud_keeps_feature_and_effect_warnings_visible():
    overlay = (SRC / "Menu/OverlayRenderer.cpp").read_text(encoding="utf-8")
    compile_status = overlay.split("void OverlayRenderer::RenderShaderCompilationStatus", 1)[1]
    exceptional_info = compile_status.split("const bool hasExceptionalInfo =", 1)[1].split(";", 1)[0]
    assert "shaderCache->HasFeatureSetChanges()" in exceptional_info
    assert "effectFailed" in exceptional_info
    suppression = compile_status.index("if (hideRoutineHud && !hasExceptionalInfo)")
    effects_warning = compile_status.index("DrawEffects11Errors(themeSettings);", suppression)
    assert suppression < effects_warning
