from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src/Features/Upscaling"


def test_flush_setting_reaches_staggered_hybrid_and_prewarms():
    renderer = (SRC / "NeuralRendering/Renderer.cpp").read_text(encoding="utf-8")
    staggered = renderer.split("StaggerResult ApplyStaggeredStereo", 1)[1].split("bool PreparePrewarm", 1)[0]
    assert 'BeginD3D12(&commandList, false, tuning.flushAfterSignal)' in renderer
    assert 'BeginD3D12(&commandList, false, tuning.flushAfterSignal)' in staggered
    assert 'BeginD3D12(&commandList, parallelEyeQueueEnabled, flushAfterSignal)' in renderer


def test_parallel_queue_reports_actual_use_and_stagger_reason():
    renderer = (SRC / "NeuralRendering/Renderer.cpp").read_text(encoding="utf-8")
    assert 'parallel eye queue: requested={} used={} route={} eyes=2' in renderer
    assert 'LogParallelQueueTransition(tuning, parallelQueueUsed)' in renderer
    assert '"stagger-single-eye"' in renderer
    assert '"compute-queue-unavailable"' in renderer


def test_live_panel_exposes_both_schedule_toggle_values():
    ui = (SRC / "FoveatedRender.cpp").read_text(encoding="utf-8")
    assert 'Parallel eye queue: %s | Flush after signal: %s' in ui
    assert 'settings.neuralRenderingParallelEyeQueue ? "ON" : "OFF"' in ui
    assert 'settings.neuralRenderingFlushAfterSignal ? "ON" : "OFF"' in ui


def test_prewarm_skip_messages_are_deduplicated_until_all_gates_pass():
    integration = (SRC / "NeuralRendering/Integration.cpp").read_text(encoding="utf-8")
    prewarm = integration.split("void TryStartupPrewarm(", 1)[1].split("void RestoreRenderTargets(", 1)[0]
    assert "if (lastPrewarmSkipReason != reason)" in prewarm
    assert prewarm.index("lastPrewarmSkipReason.clear()") > prewarm.index("!geometry.matchingSizes")
