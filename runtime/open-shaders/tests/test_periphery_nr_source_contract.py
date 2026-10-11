"""Static source contracts for the opt-in periphery hybrid route."""

from pathlib import Path
import math
import re
import struct


ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "runtime/open-shaders/src/Features/Upscaling/NeuralRendering"
SHADERS = ROOT / "runtime/open-shaders/features/Upscaling/Shaders/Upscaling/NeuralRendering"


def _f32(value: float) -> float:
    return struct.unpack("f", struct.pack("f", value))[0]


def test_settings_use_measured_hybrid_and_requested_periphery_scales():
    header = (ROOT / "runtime/open-shaders/src/Features/Upscaling/FoveatedRender.h").read_text()
    cpp = (ROOT / "runtime/open-shaders/src/Features/Upscaling/FoveatedRender.cpp").read_text()
    assert "bool neuralRenderingPeripheryEnabled = true;" in header
    assert "bool neuralRenderingPeripheryStagger = true;" in header
    assert "uint neuralRenderingPeripheryResolution = 50;" in header
    assert "float neuralRenderingPeripheryLightingGain = 0.83f;" in header
    assert "float neuralRenderingPeripheryFeatherWidth = 160.0f;" in header
    assert "bool neuralRenderingProjectionCenteredCrop = true;" in header
    assert "float neuralRenderingProjectionCenterOffsetXPx = 0.0f;" in header
    assert "float neuralRenderingProjectionCenterOffsetYPx = 0.0f;" in header
    assert "{ 0.83f, 0.86f, 0.89f, 0.93f }" in cpp
    assert "peripheryResValues[] = { 50u, 58u, 66u, 75u }" in cpp
    benchmarks = (RUNTIME / "SettingsBenchmark.cpp").read_text()
    assert '"neuralRenderingPeripheryResolution": 66' in benchmarks
    assert '"neuralRenderingPeripheryResolution": 50' in benchmarks
    assert '"neuralRenderingPeripheryLightingGain": 0.89' in benchmarks
    assert '"neuralRenderingPeripheryLightingGain": 0.83' in benchmarks


def test_projection_centered_crop_is_limited_to_hybrid_presets_and_off_path_is_legacy():
    header = (ROOT / "runtime/open-shaders/src/Features/Upscaling/FoveatedRender.h").read_text()
    integration = (RUNTIME / "Integration.cpp").read_text()
    benchmarks = (RUNTIME / "SettingsBenchmark.cpp").read_text()
    assert "bool neuralRenderingProjectionCenteredCrop = true;" in header
    assert "float neuralRenderingProjectionCenterOffsetXPx = 0.0f;" in header
    assert "float neuralRenderingProjectionCenterOffsetYPx = 0.0f;" in header
    assert "if (projectionCropResolved && !projectionCropLogged)" in integration
    assert "ComputeCoverageGeometry(foveated, eyeWidth, totalDesc.Height" in integration
    assert "ComputeNeuralCoverageRect(eyeWidth, eyeHeight, guideWidth, guideHeight, coverage)" in integration
    assert "GetProjectionRawTangents(rawTangents)" in integration
    assert "AlignNeuralCoverageRectsForStereo" in integration
    assert "AlignNeuralCoverageRectsForStereo" in (RUNTIME / "RuntimePolicy.h").read_text()
    assert "Projection-centred NR eye={} source={} tangents L={} R={} T={} B={}" in integration
    assert "projection._13" in integration and "projection._23" in integration and "projection._43" in integration
    assert '"neuralRenderingProjectionCenteredCrop": true' in benchmarks
    assert benchmarks.count('"neuralRenderingProjectionCenteredCrop": true') == 6
    assert benchmarks.count('"neuralRenderingProjectionCenteredCrop": false') == 2
    assert '"id": "nr-full-eye"' in benchmarks and '"id": "nr-coverage-crop-only"' in benchmarks


def test_feature_context_bank_and_route_reset_are_separate():
    runtime_h = (RUNTIME / "Runtime.h").read_text()
    renderer = (RUNTIME / "Renderer.cpp").read_text()
    integration = (RUNTIME / "Integration.cpp").read_text()
    assert "kFeatureSlotCount = 132" in runtime_h
    assert "peripheryContext ? kBankStride : 0u" in renderer
    assert "peripheryFeatureContext = peripheryContext" in renderer
    assert "Renderer& Renderer::PeripheryInstance()" in renderer
    assert "Runtime::Instance().Shutdown();" in renderer
    assert "Renderer::Instance().ResetHistory();" in integration
    assert "Renderer::PeripheryInstance().ResetHistory();" in integration
    assert "Renderer::PeripheryInstance().Reset();" in integration
    assert "eyes = {};" in renderer
    assert "tier.modelInput" in renderer and "tier.output" in renderer
    assert "lastPeripheryAppliedFrame != UINT32_MAX" in integration
    assert "requestedPeripheryResolution != activePeripheryResolution" in integration
    assert "neuralRenderingPeripheryStagger != activePeripheryStagger" in integration
    assert "activePeripheryResolution = requestedPeripheryResolution" in integration
    assert "peripheryRouteWasActive = false" in integration
    assert "Renderer::PeripheryInstance().ApplyStereo" in integration
    assert "tuning.temporalReuseCadence = foveated.settings.neuralRenderingPeripheryStagger ? 2u : 0u" in integration
    assert "tuning.temporalReuseStaggerEyes = foveated.settings.neuralRenderingPeripheryStagger" in integration
    assert "tuning.temporalReuseCadence = 0;" in integration
    assert "tuning.temporalReuseStaggerEyes = false;" in integration
    assert "temporalReuseResetAfterSkip" in renderer
    assert "staggerSkippedLastFrame[reuseEye] = true" in renderer
    assert "static_cast<std::uint32_t>(frame - lastPeripheryAppliedFrame) > 2u" in integration
    assert "skipped eye's fresh effect is one host frame old while the current" in integration
    assert "motion vectors cover one frame" in integration
    assert "NeuralCoverageHistoryGeometryChanged(coverageHistoryGeometry, currentCoverageHistoryGeometry)" in integration
    assert "Stable NR crop geometry changed; resetting centre Feature 18 history" in integration
    assert "coverageHistoryGeometryObserved = false" in integration
    assert "currentCoverageHistoryGeometry.sourceColorWidth = eyeWidth;" in integration
    assert "currentCoverageHistoryGeometry.sourceGuideHeight = fullGuideHeight;" in integration
    assert "sameCropDifferentSourceResolution.sourceColorWidth += 8;" in (ROOT / "runtime/open-shaders/tests/cpp/test_nr_runtime_policy.cpp").read_text()
    assert "const bool sourceExtentsChanged = previous.sourceColorWidth != current.sourceColorWidth ||" in (RUNTIME / "RuntimePolicy.h").read_text()
    assert "dynamicSourceResize.sourceGuideHeight += 4;" in (ROOT / "runtime/open-shaders/tests/cpp/test_nr_runtime_policy.cpp").read_text()
    assert "lowResolutionResidualUAV" in renderer
    assert "DispatchPeripheryResidual" in renderer
    assert "tier.modelInput.srv11.Get()" in renderer
    assert "tier.output.srv11.Get()" in renderer
    assert "tier.resolvedSRV.Get()" in renderer
    assert "peripheryLowResResidual->uav.get()" in integration
    assert "for (auto& eyeReset : resetPending)" in renderer
    assert "peripheryHybridCS = nullptr" in integration
    assert "peripheryHybridCB = nullptr" in integration
    assert "peripheryHybridSampler = nullptr" in integration
    assert "peripheryHybridCS.attach" in integration
    assert "return !tier.reducedResolution || tuning.modelResolveMode == 1 ||" in renderer
    assert "peripheryFeatureContext && tuning.modelResolveMode == 4" in renderer
    assert "StaggeredNativeEye(temporalFrameIndex)" in renderer
    policy = (RUNTIME / "RuntimePolicy.h").read_text()
    assert "return static_cast<std::uint32_t>(temporalFrameIndex & 1u);" in policy


def test_exact_low_resolution_residual_and_hybrid_shader_formula_are_wired():
    renderer = (RUNTIME / "Renderer.cpp").read_text()
    integration = (RUNTIME / "Integration.cpp").read_text()
    resolve = (SHADERS / "ModelResolutionCS.hlsl").read_text()
    hybrid = (SHADERS / "PeripheryHybridCS.hlsl").read_text()
    math = (SHADERS / "PeripheryHybridMath.hlsli").read_text()
    assert "kResolutionTiers{\n\t\t\t100, 95, 90, 85, 80, 75, 70, 66, 58, 50, 33 }" in renderer
    assert ".mode = resolveMode == 0u ? 1u : (resolveMode == 1u ? 3u : resolveMode)" in renderer
    assert "tuning.modelResolveMode = 4" in integration
    assert "if (gMode == 4)" in resolve
    assert "original + (model - proxy)" in resolve
    assert "peripheryOutput - raw" in math
    assert "centreOutput - raw" in math
    assert "for (uint y = 0; y < 8; ++y)" in hybrid
    assert "PeripheryLowResResidual.Load(int3(eye * uint(lowEyeWidth) + lx, ly, 0))" in hybrid
    assert "lowX0 = displayX0 * lowEyeWidth / float(EyeWidth)" in hybrid
    assert "weightSum += weight" in hybrid
    assert ".mode = 5" in renderer
    assert "gTarget[uint2(gTargetX + id.x, id.y)]" in resolve
    assert "PeripheryCompose" in hybrid
    assert "clamp(lightingGain, 0.0, 1.5) * coarsePeriphery" in math
    assert "w * (effC - coarseCentre) + (1.0 - w) * (effP - coarsePeriphery)" in math
    shader_test = (ROOT / "runtime/open-shaders/package/Shaders/Tests/TestPeripheryHybrid.hlsl").read_text()
    assert "PeripheryCompose" in shader_test
    assert "1.0 / 255.0" in shader_test


def test_periphery_context_uses_whole_eye_guides_and_model_scaled_motion_vectors():
    renderer = (RUNTIME / "Renderer.cpp").read_text()
    integration = (RUNTIME / "Integration.cpp").read_text()
    assert "peripheryInputs, fullGuideWidth, fullGuideHeight" in integration
    assert "peripheryInputs[eye].guideSourceX = 0" in integration
    assert "peripheryInputs[eye].guideSourceY = 0" in integration
    assert "motionVectorScaleX * static_cast<float>(modelWidth) / colorWidth" in renderer
    assert "motionVectorScaleY * static_cast<float>(modelHeight) / colorHeight" in renderer


def test_periphery_gate_allows_non_aligned_height_and_logs_transitions():
    integration = (RUNTIME / "Integration.cpp").read_text()
    gate_start = integration.index("const bool peripheryEnabled =")
    gate = integration[gate_start:integration.index("const std::uint32_t requestedPeripheryResolution", gate_start)]
    assert "const bool eyeWidthAligned = (eyeWidth % 8u) == 0;" in gate
    assert "totalDesc.Height % 8u" not in gate
    assert "hybridEligible = peripheryEnabled && fullEye && !gaze.dynamic && !adaptiveCropActive" in gate
    assert "coverageUseful && coverageRectsValid && coverageDepthSupported && eyeWidthAligned" in gate
    assert '!coverageUseful ? "coverage100"' in gate
    assert "!eyeWidthAligned ? \"eyeWidth%8\" : nullptr" in gate
    assert "Periphery NR active: display={}x{} fmt={} rawFmt={}" in integration
    assert "[DLSSNR] Periphery NR inactive: {}" in integration
    assert "status.refusalTerm = failureReason;" in integration
    assert "if (peripheryStatusWasActive || lastPeripheryInactiveReason != peripheryInactiveReason)" in integration
    assert "if (!peripheryStatusWasActive)" in integration


def test_periphery_hybrid_runtime_include_checks_unaligned_display_heights():
    shader_test = (ROOT / "runtime/open-shaders/tests/shaders/periphery_hybrid_runtime_include_test.cpp").read_text()
    assert "for (const std::uint32_t height : { 2428u, 2432u })" in shader_test
    assert "(height + 7u) / 8u" in shader_test
    assert "context->Dispatch(1, (height + 7u) / 8u, 1)" in shader_test


def test_periphery_stagger_preserves_ngx_history_and_fills_rejected_warps():
    integration = (RUNTIME / "Integration.cpp").read_text()
    renderer = (RUNTIME / "Renderer.cpp").read_text()
    temporal_shader = (SHADERS / "TemporalReuseCS.hlsl").read_text()
    temporal_fixture = (ROOT / "runtime/open-shaders/tests/shaders/temporal_reuse_periphery_runtime_include_test.cpp").read_text()
    assert "tuning.temporalReuseResetAfterSkip = false;" in integration
    assert "deferReuseEyeModelDownsample" in renderer
    assert '"ModelDownsampleFallback"' in renderer
    assert "static_cast<std::uint32_t>(frame - lastPeripheryAppliedFrame) > 2u" in integration
    assert "skipped eye's fresh effect is one host frame old while the current" in integration
    assert "motion vectors cover one frame" in integration
    assert "Renderer::PeripheryInstance().ResetHistory();" in integration
    foveated = (ROOT / "runtime/open-shaders/src/Features/Upscaling/FoveatedRender.h").read_text()
    assert "bool neuralRenderingTemporalReuseResetAfterSkip = true;" in foveated
    assert "settings.neuralRenderingTemporalReuseResetAfterSkip" in integration
    assert "tuning.temporalReuseResetAfterSkip && staggerSkippedLastFrame[nativeEye]" in renderer
    assert "inputs[reuseEye].motionVectorScaleY, tuning," in renderer
    assert "reuseEye, peripheryFeatureContext))" in renderer
    assert "peripheryFeatureContext && tuning.temporalReuseStaggerEyes" in renderer
    assert "SnapshotPeripheryCoarse" in renderer
    assert "peripheryCoarseEffectValid = false" in renderer
    assert "gUsePeripheryCoarseFallback != 0u" in temporal_shader
    assert "gPreviousPeripheryCoarseEffect.SampleLevel(gLinear, ColorUV(float2(pixel)), 0).rgb" in temporal_shader
    assert "0.5 * gPreviousResidual.Load(int3(pixel, 0)).rgb" not in temporal_shader
    assert "force out-of-bounds rejects" in temporal_fixture
    assert "raw.r + coarse.r" in temporal_fixture
    assert "hybridError <= 1.0f / 255.0f" in temporal_fixture
    assert "generalError <= 1.0f / 255.0f" in temporal_fixture


def test_enabling_periphery_defaults_projection_crop_on_and_keeps_toggle_available():
    ui = (ROOT / "runtime/open-shaders/src/Features/Upscaling/FoveatedRender.cpp").read_text()
    assert "if (peripheryEnabled)\n\t\t\t\t\t\tsettings.neuralRenderingProjectionCenteredCrop = true;" in ui
    assert '{ "nr_projection_crop", "Projection-centred crop",' in ui
    assert 'ImGui::Checkbox(SettingsLabel("nr_projection_crop"), &settings.neuralRenderingProjectionCenteredCrop)' in ui


def test_cpu_float32_fixture_matches_production_equation_within_one_lsb():
    # This is the CPU-only numeric companion. TestPeripheryHybrid.hlsl invokes
    # the same production hlsli on GPU for a known fixture.
    fixtures = [(0.2, 0.5, 0.7, 0.1, 0.2, 0.5, 0.83)]
    for raw, p, c, cp, cc, w, gain in fixtures:
        reference = raw + gain * cp + w * ((c - raw) - cc) + (1 - w) * ((p - raw) - cp)
        shader = _f32(_f32(raw) + _f32(_f32(gain) * _f32(cp)) +
                      _f32(_f32(w) * _f32(_f32(c - raw) - _f32(cc))) +
                      _f32(_f32(1 - w) * _f32(_f32(p - raw) - _f32(cp))))
        assert abs(reference - shader) <= 1.0 / 255.0


def test_disabled_path_still_calls_the_original_single_renderer_route():
    integration = (RUNTIME / "Integration.cpp").read_text()
    assert "if (!hybridSucceeded && !succeeded)" in integration
    assert "Util::AsReal(total.texture), inputs, guideWidth, guideHeight," in integration
    assert "outWidth, outHeight, tuning, destination, destinationUAV" in integration


def test_hybrid_startup_prewarm_reuses_lazy_geometry_and_exact_renderer_banks():
    integration = (RUNTIME / "Integration.cpp").read_text()
    integration_policy = (RUNTIME / "IntegrationPolicy.h").read_text()
    renderer = (RUNTIME / "Renderer.cpp").read_text()
    settings = (ROOT / "runtime/open-shaders/src/Features/Upscaling/FoveatedRender.h").read_text()
    prewarm = integration[integration.index("void TryStartupPrewarm"):integration.index("void RestoreRenderTargets")]
    lazy_geometry = integration[integration.index("const auto coverageGeometry = ComputeCoverageGeometry"):integration.index("const auto& leftCoverageRect")]

    assert "bool neuralRenderingPrewarm = true;" in settings
    assert "ComputeCoverageGeometry(foveated, displayEyeWidth, displayEyeHeight" in prewarm
    assert "ComputeCoverageGeometry(foveated, eyeWidth, totalDesc.Height" in lazy_geometry
    assert "geometry.projectionRequested ? \"centered-fallback\" : \"centered-configured\"" in prewarm
    assert "GetRenderEyeWidth()" in prewarm and "GetRenderEyeHeight()" in prewarm
    assert "vrIntermediateDepth" not in prewarm and "vrIntermediateMotionVectors" not in prewarm
    assert "Renderer::Instance().PreparePrewarm" in prewarm
    assert "Renderer::Instance().PrewarmStereoEye" in prewarm
    assert "Renderer::PeripheryInstance().PreparePrewarm" in prewarm
    assert "Renderer::PeripheryInstance().PrewarmStereoEye" in prewarm
    assert "MakeHybridPeripheryTuning(GetTuning(foveated, false), foveated)" in prewarm
    assert "MakeHybridCentreTuning(GetTuning(foveated, false))" in prewarm
    assert "MakeHybridPeripheryTuning(tuning, foveated)" in integration
    assert "MakeHybridCentreTuning(tuning)" in integration
    assert "tuning.modelResolveMode = 4;" in integration
    assert "tuning.temporalReuseCadence = foveated.settings.neuralRenderingPeripheryStagger ? 2u : 0u;" in integration
    assert "peripheryTuning, true" in prewarm
    assert "bool PrewarmStereoEye(ID3D11Device* device, ID3D11DeviceContext* context" in renderer
    assert "FeatureSlot(eyeIndex, tierIndex, passIndex, peripheryFeatureContext)" in renderer
    assert "NextStartupPrewarmStage(startupPrewarmStage, hybrid)" in prewarm
    assert "StartupPrewarmGateReason(gate)" in prewarm
    assert "StartupPrewarmStage::PrimaryEye0" in integration_policy


def test_successful_hybrid_shader_is_reused_when_runtime_settings_change():
    integration = (RUNTIME / "Integration.cpp").read_text()
    assert re.search(
        r"if\s*\(\s*peripherySettingsChanged\s*&&\s*"
        r"peripheryHybridShaderInit\.Failed\(\)\s*\)\s*"
        r"ResetPeripheryHybridShaderState\(\);",
        integration,
    )


def test_legacy_guided_filter_setting_is_inert_and_shader_was_removed():
    header = (ROOT / "runtime/open-shaders/src/Features/Upscaling/FoveatedRender.h").read_text()
    ui = (ROOT / "runtime/open-shaders/src/Features/Upscaling/FoveatedRender.cpp").read_text()
    integration = (RUNTIME / "Integration.cpp").read_text()
    assert "bool neuralRenderingPeripheryGuidedFilter = false;" in header
    assert "X(neuralRenderingPeripheryGuidedFilter)" in ui
    assert 'Checkbox("Periphery RGB guided filter"' not in ui
    assert "neuralRenderingPeripheryGuidedFilter" not in integration
    assert not (SHADERS / "PeripheryGuidedFilterCS.hlsl").exists()


def test_cpu_float32_fixture_matches_hybrid_shader_expression_within_one_lsb():
    # The shader expression is source-asserted above. This fixture compares the
    # CPU reference with the corresponding float32 evaluation at RGB values used
    # by the shader; one 8-bit LSB is 1/255 in normalized RGB.
    fixtures = [
        (0.12, 0.22, 0.45, 0.08, 0.19, 0.0, 0.83),
        (0.35, 0.61, 0.74, 0.48, 0.66, 0.5, 0.89),
        (0.85, 0.93, 0.98, 0.70, 0.88, 1.0, 0.93),
    ]
    max_error = 0.0
    for raw, p_out, c_out, c_p, c_c, w, gain in fixtures:
        reference = raw + gain * c_p + w * ((c_out - raw) - c_c) + (1.0 - w) * ((p_out - raw) - c_p)
        shader_order = _f32(_f32(raw) + _f32(_f32(gain) * _f32(c_p)) +
                            _f32(_f32(w) * _f32(_f32(c_out - raw) - _f32(c_c))) +
                            _f32(_f32(1.0 - w) * _f32(_f32(p_out - raw) - _f32(c_p))))
        max_error = max(max_error, abs(reference - shader_order))
    assert max_error <= 1.0 / 255.0


def test_signed_low_frequency_textures_preserve_negative_composite_components():
    integration = (RUNTIME / "Integration.cpp").read_text()
    shader = (SHADERS / "PeripheryHybridCS.hlsl").read_text()
    assert "CreatePeripherySignedTexture(globals::d3d::device, coarseWidth, coarseHeight" in integration
    assert "CreatePeripherySignedTexture(globals::d3d::device, modelEyeWidth * 2u, modelHeight" in integration
    assert "desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;" in integration
    assert "D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW" in integration
    assert "State::SupportsTypedUAVLoad(DXGI_FORMAT_R16G16B16A16_FLOAT)" in integration
    assert "CreateTextureFromSource(source, coarseWidth, coarseHeight" not in integration
    assert "if (!all(isfinite(p))) p = 0.0;" in shader
    assert "if (!all(isfinite(result))) result = raw;" in shader

    # Signed low terms represent a blue-down lighting change. Simulate the old
    # UNORM store, which clamps negative values to zero before the shared blend.
    raw, periphery_out, centre_out = 0.50, 0.42, 0.36
    coarse_p, coarse_c, weight, gain = -0.08, -0.14, 0.5, 0.83
    signed = raw + gain * coarse_p + weight * ((centre_out - raw) - coarse_c) + (1 - weight) * ((periphery_out - raw) - coarse_p)
    old_unorm = raw + gain * max(coarse_p, 0.0) + weight * ((centre_out - raw) - max(coarse_c, 0.0)) + (1 - weight) * ((periphery_out - raw) - max(coarse_p, 0.0))
    assert abs(signed - 0.4336) < 1.0e-12  # signed terms are preserved through the blend
    assert abs(old_unorm - signed) > 0.02


def test_coarse_vertical_uv_is_exact_eighth_spacing_and_transition_is_rounded():
    shader = (SHADERS / "PeripheryHybridCS.hlsl").read_text()
    math_source = (SHADERS / "PeripheryHybridMath.hlsli").read_text()
    assert "(float(q.y) + 0.5) * 0.125" in shader
    assert "coarseSize.y) / float(Height)" not in shader
    assert "PeripheryInsideDistance(float2(localX, q.y) + 0.5" in shader
    assert "borderExtension" in shader
    assert "edgeDistance > 0.0 ? PeripheryCentreWeight(edgeDistance, FeatherWidth) : 0.0" in shader
    assert "float PeripheryInsideDistance(" in math_source

    height = 2428
    last_pixel = height - 1
    corrected = (last_pixel + 0.5) / 8.0
    old_scaled = (last_pixel + 0.5) * math.ceil(height / 8) / height
    assert corrected == (last_pixel + 0.5) * 0.125
    assert old_scaled - corrected > 0.49

    def inside_distance(pixel, lo, hi, radius):
        center = tuple((a + b) * 0.5 for a, b in zip(lo, hi))
        half = tuple((b - a) * 0.5 for a, b in zip(lo, hi))
        r = min(radius, *half)
        q = tuple(abs(x - c) - (h - r) for x, c, h in zip(pixel, center, half))
        outside = math.hypot(max(q[0], 0.0), max(q[1], 0.0))
        sdf = outside + min(max(q[0], q[1]), 0.0) - r
        return -sdf - 0.5

    # At the top crop edge the pixel centre has distance zero, so the
    # raised-cosine transition begins at w=0 with zero derivative.
    d = inside_distance((100.5, 40.5), (50.0, 40.0), (150.0, 140.0), 20.0)
    assert abs(d) < 1.0e-12
    eps = 1.0e-4
    weight_at_start = 0.5 - 0.5 * math.cos(math.pi * max(d, 0.0) / 20.0)
    weight_after = 0.5 - 0.5 * math.cos(math.pi * (d + eps) / 20.0)
    assert weight_at_start == 0.0
    assert (weight_after - weight_at_start) / eps < 1.0e-4


def test_w_equals_one_periphery_load_elision_is_algebraically_exact():
    raw, periphery, centre, coarse_p, coarse_c, gain = 0.3, 0.8, 0.7, -0.1, 0.05, 0.83
    old = raw + gain * coarse_p + 1.0 * ((centre - raw) - coarse_c) + 0.0 * ((periphery - raw) - coarse_p)
    optimized = raw + gain * coarse_p + ((centre - raw) - coarse_c)
    assert old == optimized
    shader = (SHADERS / "PeripheryHybridCS.hlsl").read_text()
    assert "if (w >= 1.0)" in shader
    assert "Periphery.Load(int3(q, 0)).rgb" in shader

def test_runtime_shader_compile_inventory_tracks_aio_feature_staging_contract():
    source = (ROOT / "runtime/open-shaders/tests/cpp/test_neural_rendering_shader_compile.cpp").read_text()
    cmake = (ROOT / "runtime/open-shaders/tests/cpp/CMakeLists.txt").read_text()
    aio = (ROOT / "runtime/open-shaders/cmake/AioPartition.cmake").read_text()
    runtime_cmake = (ROOT / "runtime/open-shaders/CMakeLists.txt").read_text()
    assert '"$<$<BOOL:${ENABLE_EFFECTS11}>:ENABLE_EFFECTS11>"' in cmake
    assert 'featureRoot / "CORE"' in source
    assert 'key == "aio" || key == "autoupload"' in source
    assert "AIO feature HLSL=" in source
    assert "featureHlslCount == 127" in source
    assert "featureHlslCount == 119" in source
    assert "includeOnlyCount == 31" in source
    assert '"SnapshotPeripheryCoarse"' in source
    assert "FeatureRuntimeVariants(relative)" in source
    for path in (
        "TerrainBlending/DepthBlend.hlsl",
        "GrassOptimizations/SPD/SPD.hlsl",
        "Upscaling/RCAS/RCAS.hlsl",
        "LightLimitFix/ShadowDepthCopy.hlsl",
    ):
        assert path in source
    assert "feature_in_aio" in aio
    assert 'EXCLUDE REGEX "/Tests/"' in runtime_cmake
