"""Source contracts for the 2.20.7 Skylighting interior safety path."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
FEATURE = ROOT / "features/Skylighting/Shaders/Skylighting"


def test_interior_sampling_is_opt_in_and_missing_probe_data_is_neutral():
    header = (SRC / "Features/Skylighting.h").read_text(encoding="utf-8")
    shader = (FEATURE / "Skylighting.hlsli").read_text(encoding="utf-8")
    cpp = (SRC / "Features/Skylighting.cpp").read_text(encoding="utf-8")

    assert "bool EnableInInteriors = false;" in header
    assert "EnableInInteriors," in cpp  # persisted in the settings JSON
    assert "bool ForceNeutralOutput = false;" in header
    assert "uint ForceNeutralOutput;" in header
    shared = (ROOT / "package/Shaders/Common/SharedData.hlsli").read_text(encoding="utf-8")
    assert "uint ForceNeutralOutput;" in shared
    assert "return SharedData::skylightingSettings.ProbeDataReady != 0" in shader
    assert "SharedData::skylightingSettings.ForceNeutralOutput == 0" in shader
    assert "if (SharedData::skylightingSettings.ForceNeutralOutput != 0)\n\t\t\treturn 1.0;" in shader
    assert "if (SharedData::skylightingSettings.ForceNeutralOutput != 0)\n\t\t\treturn;" in shader
    assert "(!SharedData::InInterior || SharedData::skylightingSettings.EnableInInteriors != 0)" in shader
    assert "if (!ShouldUseProbeData())\n\t\t\treturn scaledUnitSH;" in shader
    assert "if (!ShouldUseProbeData())\n\t\t\treturn 1.0;" in shader


def test_disabled_at_boot_consumers_require_loaded_resources_and_neutral_toggle_persists():
    deferred = (SRC / "Deferred.cpp").read_text(encoding="utf-8")
    fog = (SRC / "Features/ExponentialHeightFog.cpp").read_text(encoding="utf-8")
    cpp = (SRC / "Features/Skylighting.cpp").read_text(encoding="utf-8")
    feature_buffer = (SRC / "FeatureBuffer.cpp").read_text(encoding="utf-8")

    assert "dynamicCubemaps.loaded && skylighting.loaded && skylighting.texProbeArray && skylighting.texProbeArray->srv" in deferred
    assert "skylighting.loaded && skylighting.texProbeArray && skylighting.texProbeArray->srv" in fog
    assert "ForceNeutralOutput," in cpp
    assert "settings.ForceNeutralOutput ? 1u : 0u" in cpp
    assert "GetCommonBufferData(a_inWorld)" in feature_buffer


def test_resources_and_probe_history_fail_lit_until_a_successful_dispatch():
    cpp = (SRC / "Features/Skylighting.cpp").read_text(encoding="utf-8")
    shader = (FEATURE / "UpdateProbesCS.hlsl").read_text(encoding="utf-8")

    assert "probeResourcesReady = false;" in cpp
    assert "probeDataReady = false;" in cpp
    assert "neutral visibility fallback is active until retry" in cpp
    assert "probeDataReady = true;" in cpp
    assert "const UINT litHistory[4] = { 0xFFFFFFFFu" in cpp
    assert "ClearDepthStencilView(texOcclusion->dsv.get(), D3D11_CLEAR_DEPTH, 1.0f" in cpp
    assert "isValid ? outShadowBitmask[probeTexID] : 0xFFFFFFFFu" in shader


def test_probe_history_uses_packed_r16_state_and_rejects_invalid_shadow_projections():
    cpp = (SRC / "Features/Skylighting.cpp").read_text(encoding="utf-8")
    shader = (ROOT / "features/Skylighting/Shaders/Skylighting/UpdateProbesCS.hlsl").read_text(encoding="utf-8")

    assert "DXGI_FORMAT_R16_UINT" in cpp
    assert "storedAccumFrames = probeUpdateState & 0xFFu" in shader
    assert "shadowSampleIndex = (probeUpdateState >> 8) & 31u" in shader
    assert "(bitmask << 1) | (shadowSample > 0.5 ? 1u : 0u)" in shader
    assert "outShadowVisibility[probeTexID] = float(countbits(bitmask)) / 32.0;" in shader
    assert "ndcDepth > 0 && ndcDepth < 1 && linearDepth > 0" in shader
    assert "all(positionLS.xy > 0) && all(positionLS.xy < 1)" in shader
    assert "advanceShadowHistory" in shader


def test_missing_directional_shadow_inputs_fail_lit_and_reseed_on_recovery():
    cpp = (SRC / "Features/Skylighting.cpp").read_text(encoding="utf-8")
    header = (SRC / "Features/Skylighting.h").read_text(encoding="utf-8")
    shader = (ROOT / "features/Skylighting/Shaders/Skylighting/UpdateProbesCS.hlsl").read_text(encoding="utf-8")
    shared = (ROOT / "package/Shaders/Common/SharedData.hlsli").read_text(encoding="utf-8")

    assert "bool HasShadowData() const;" in header
    assert "ShadowDataAvailable" in shared
    assert "settings.ShadowDataAvailable == 0" in shader
    assert "previousShadowDataAvailable" in cpp
    assert "ClearUnorderedAccessViewUint(texShadowBitmask->uav.get(), litHistory)" in cpp
    assert "ShadowDataAvailable = !Util::IsInterior() && HasShadowData()" in cpp


def test_editor_markers_are_not_filtered_from_skylighting_roof_occluders():
    cpp = (SRC / "Features/Skylighting.cpp").read_text(encoding="utf-8")
    skip_mask = cpp.split("if (value & (", 1)[1].split("))) {", 1)[0]
    assert "kEditorMarker" not in skip_mask


def test_shadow_fade_uses_the_same_offset_grid_as_probe_sampling():
    shader = (FEATURE / "Skylighting.hlsli").read_text(encoding="utf-8")
    cpp = (SRC / "Features/Skylighting.cpp").read_text(encoding="utf-8")
    fade = shader.split("float GetFadeOutFactor", 1)[1].split("bool ShouldUseProbeData", 1)[0]
    assert "positionMS - SharedData::skylightingSettings.PosOffset.xyz" in fade
    assert "std::sqrt(diskPoint.x) * std::sin(settings.MaxZenith)" in cpp
    version = (ROOT / "features/Skylighting/Shaders/Features/Skylighting.ini").read_text(encoding="utf-8")
    assert "Version = 1-7-2" in version


def test_scene_change_queues_a_render_boundary_reset_and_reports_interior_activity():
    cpp = (SRC / "Features/Skylighting.cpp").read_text(encoding="utf-8")
    header = (SRC / "Features/Skylighting.h").read_text(encoding="utf-8")
    renderer = (SRC / "Features/LightLimitFix/ShadowRenderer.cpp").read_text(encoding="utf-8")

    assert "virtual void GameLoaded() override;" in header
    assert header.count("virtual void GameLoaded() override;") == 1
    assert "scene identity changed interior={}" in cpp
    assert "QueueSceneReset()" in renderer
    assert "[Skylighting-Dungeon]" in cpp
    assert "interiorSampling={} forceNeutral={} occRendered={} occSkipped={} probeDispatch={} probeSkipped={}" in cpp
    assert "Rebuild requested:" in cpp


def test_interior_lighting_permutation_compile_diagnostics_are_time_bounded():
    cache = (SRC / "ShaderCache.cpp").read_text(encoding="utf-8")
    cache_header = (SRC / "ShaderCache.h").read_text(encoding="utf-8")
    renderer = (SRC / "Features/LightLimitFix/ShadowRenderer.cpp").read_text(encoding="utf-8")

    assert "ArmInteriorLightingCompileDiagnostics(bool a_active)" in cache_header
    assert "std::chrono::seconds(10)" in cache
    assert 'LogInteriorLightingPermutationEvent("memory-cache-hit"' in cache
    assert 'LogInteriorLightingPermutationEvent("disk-cache-hit"' in cache
    assert 'LogInteriorLightingPermutationEvent("compile"' in cache
    assert 'LogInteriorLightingPermutationEvent("runtime-cache-hit"' in cache
    assert "ArmInteriorLightingCompileDiagnostics(interior)" in renderer
