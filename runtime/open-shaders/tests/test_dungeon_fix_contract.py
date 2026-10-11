"""Source contracts for the 2.20.5 interior lighting/history safeguards."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def test_fail_open_is_enabled_persisted_and_overlay_toggleable():
    manager = (SRC / "Features/LightLimitFix/ShadowCasterManager.h").read_text()
    renderer = (SRC / "Features/LightLimitFix/ShadowRenderer.cpp").read_text()
    assert "bool FailOpenSpotlightWithoutShadowTile = true;" in manager
    assert "\n\t\tFailOpenSpotlightWithoutShadowTile," in manager
    assert '"Fail open spotlights without tile"' in renderer
    assert "ApplySpotMissingTilePolicy" in renderer


def test_bounded_reset_waits_for_scene_and_portal_safety_then_precedes_vr_shadow_guard():
    scheduler = (SRC / "Features/LightLimitFix/ShadowScheduler.cpp").read_text()
    body = scheduler.split("void ScheduleShadowCasters()", 1)[1].split("void RenderScheduledShadowLights()", 1)[0]
    assert "ShouldDrainPendingResetBeforeVR" in (SRC / "Features/LightLimitFix/SceneHistoryResetPolicy.h").read_text()
    assert "if (globals::game::isVR && !GetVRDrawShadows())" in body
    assert body.index("s_pendingSessionReset.exchange(false") < body.index("if (globals::game::isVR && !GetVRDrawShadows())")
    assert "BoundedSessionResetDrain = true" in (SRC / "Features/LightLimitFix/ShadowCasterManager.h").read_text()


def test_scene_transition_reset_fanout_is_toggleable_and_covers_histories():
    renderer = (SRC / "Features/LightLimitFix/ShadowRenderer.cpp").read_text()
    settings = (SRC / "Features/LightLimitFix.h").read_text()
    assert "ResetTemporalHistoryOnWorldChange = true" in settings
    for call in (
        "Core::InvalidateTemporalState()", "ResetAdaptiveState()", "QueueHistoryReset()",
        "NeuralRendering::ResetHistory()", "pendingDLSSReset.exchange(true",
    ):
        assert call in renderer
    assert "route=standard+foveated eyes=both" in renderer


def test_profiler_capacity_and_drop_warning_are_visible():
    header = (SRC / "Profiler.h").read_text()
    renderer = (SRC / "Menu/ProfilingRenderer.cpp").read_text()
    assert "kMaxTimers = 1024" in header
    assert "passes dropped (capacity)" in renderer
    assert "GetSlotRefusals()" in renderer


def test_shadow_radius_anchor_is_runtime_toggleable_and_shared_once_per_frame():
    manager = (SRC / "Features/LightLimitFix/ShadowCasterManager.h").read_text()
    scheduler = (SRC / "Features/LightLimitFix/ShadowScheduler.cpp").read_text()
    formula = (SRC / "Features/LightLimitFix/ShadowFormula.cpp").read_text()
    ui = (SRC / "Features/LightLimitFix/ShadowCasterUI.cpp").read_text()
    assert "AnchorLightRadiusForShadowDecisions = true" in manager
    assert "AnchorLightRadiusForShadowDecisions)" in manager
    assert "AdvanceRadiusAnchor(ni," in scheduler
    assert "RadiusAnchorPolicy::Advance" in scheduler
    assert "float AnchoredRadius(const RE::NiLight* ni, float liveRadius)" in scheduler
    assert "LegacyAnchoredRadiusForHash" in scheduler
    assert "ShadowDecisionRadius(nilight" in formula
    assert '"Anchor light radius for shadow decisions"' in ui
