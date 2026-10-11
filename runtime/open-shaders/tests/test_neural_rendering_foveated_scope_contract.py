from pathlib import Path


OPEN_SHADERS = Path(__file__).resolve().parents[1]
NEURAL_RENDERING = OPEN_SHADERS / "src/Features/Upscaling/NeuralRendering"


def test_foveated_apply_stereo_steps_have_named_gpu_passes():
    integration = (NEURAL_RENDERING / "Integration.cpp").read_text()
    renderer = (NEURAL_RENDERING / "Renderer.cpp").read_text()
    core_h = (OPEN_SHADERS / "src/Features/Upscaling/FoveatedRender/Core.h").read_text()
    core_cpp = (OPEN_SHADERS / "src/Features/Upscaling/FoveatedRender/Core.cpp").read_text()

    # Include the UAV fallback's crop-only pre-copy in the parent interval.
    assert integration.index('CS_GPU_PASS("NeuralRendering::FoveatedLdrBeforeUI")') < integration.index(
        "context->CopySubresourceRegion(destination, 0, input.sourceX, input.sourceY"
    )
    assert "if (hybridRequested)" in integration
    assert "context->CopyResource(destination, Util::AsReal(total.texture))" in integration
    assert "input.sourceX + outWidth, input.sourceY + outHeight" in integration
    for pass_name in (
        "FoveatedStagedTargetCopyIn",
        "HybridInputSnapshotCopies",
        "PeripheryInstance::ApplyStereo",
        "CentreInstance::ApplyStereo",
        "PeripheryHybridDispatchChain",
        "PeripheryStagedWriteback",
        "HybridFailureRawRestore",
        "CentreInstance::FallbackApplyStereo",
        "FoveatedStagedWriteback",
    ):
        assert f'CS_GPU_PASS("NeuralRendering::{pass_name}")' in integration

    for operation in (
        "ColorUpload",
        "GuideCopies",
        "ModelDownsample",
        "TemporalReuseWarp",
        "NGXEvaluate",
        "ModelResolve",
        "OutputBlendWriteback",
        "OutputCopyWriteback",
    ):
        assert f'"{operation}"' in renderer
    assert 'RendererPassName(peripheryContext, eyeIndex,' in renderer
    assert 'if (operation == "ModelDownsampleFallback")' in renderer
    assert 'NeuralRendering::Periphery::Eye1::ModelDownsampleFallback' in renderer
    assert 'reuseEye, peripheryFeatureContext)' in renderer
    assert 'CS_GPU_PASS_DYNAMIC_REGISTERED(RendererPassName(peripheryFeatureContext, nativeEye, "NGXEvaluate"))' in renderer

    assert "std::array<BlendSourceSRVCacheEntry, 2> vrBlendSrcSRVs" in core_h
    assert "entry.resource == dlssSrc" in core_cpp
    assert "entry.viewDimension == srvDesc.ViewDimension" in core_cpp
    assert "slot->format = srvDesc.Format" in core_cpp
    assert "slot->mipLevels = srvDesc.Texture2D.MipLevels" in core_cpp
    reset = integration.split("void Reset()", 1)[1]
    assert reset.index("Core::ClearBlendSourceSRVCache()") < reset.index("Renderer::Instance().Reset()")


def test_interop_fence_dependencies_and_actual_cpu_wait_are_attributed():
    interop = (NEURAL_RENDERING / "D3D12Interop.cpp").read_text()
    for pass_name in (
        "D3D11FenceSignal",
        "D3D12QueueWait",
        "D3D12CommandListSubmit",
        "D3D12FenceSignal",
        "D3D11QueueWait",
    ):
        assert f'CS_GPU_PASS("NeuralRendering::Interop::{pass_name}")' in interop

    wait_for_fence = interop.split("bool D3D12Interop::WaitForFence", 1)[1].split(
        "bool D3D12Interop::WaitForIdle", 1
    )[0]
    cpu_scope = 'CS_PROFILE_CPU_SCOPE(globals::profiler, "NeuralRendering::Interop::BlockingFenceWaitCPU")'
    assert cpu_scope in wait_for_fence
    assert wait_for_fence.index(cpu_scope) < wait_for_fence.index("WaitForSingleObject(fenceEvent_, 250)")
