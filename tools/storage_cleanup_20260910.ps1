param(
    [switch]$Execute
)

$ErrorActionPreference = 'Continue'

if (-not $Execute) {
    throw 'This script is a destructive cleanup. Re-run with -Execute after the manifest has been reviewed.'
}

$protected = @(
    '<OPENNR_DATA>\Training\semantic_pixel_l1_pair_20260909\joint\best_all_cohorts.pt',
    '<OPENNR_DATA>\Training\semantic_pixel_l1_pair_20260909\joint\run.json',
    '<OPENNR_DATA>\Training\semantic_pixel_l1_pair_20260909\joint\history.json',
    '<OPENNR_DATA>\Training\semantic_pixel_l1_pair_20260909\joint\status.json',
    '<OPENNR_DATA>\Training\semantic_pixel_l1_pair_20260909\joint\step_zero_replay.json',
    '<OPENNR_DATA>\TrainingCache\RendererPairsStrict_20260908',
    '<OPENNR_DATA>\TrainingCache\crop_temporal_every_frame_20260909_all18',
    '<OPENNR_DATA>\TrainingCache\full_eye_periodic_spatial_20260909',
    '<OPENNR_DATA>\TrainingCache\sparse_full_eye_anchor_crop_20260909',
    '<OPENNR_DATA>\TrainingCache\full_eye_renderer_conditioning_strict_20260909_retry2',
    '<DATA_ROOT>\OpenNR_TrainingInputs\AlignedGuides_AllCohorts_20260907',
    '<DATA_ROOT>\OpenNR_TrainingInputs\AlignedGuides_HighEffect_20260907',
    '<DATA_ROOT>\OpenNR_TrainingInputs\AlignedGuides_RendererPilot_20260907',
    '<DATA_ROOT>\OpenNR_TrainingInputs\RendererConditioningFreshSession_20260907_2114',
    '<DATA_ROOT>\OpenNR_TrainingInputs\RendererStateDistillationStrict_20260908'
)

$fixedTargets = @(
    '<OPENNR_DATA>\TrainingCache\student_v1',
    '<OPENNR_DATA>\TrainingCache\full_eye_temporal_pilot_strict_20260908',
    '<OPENNR_DATA>\TrainingCache\sparse_full_eye_anchor_crop_clean7_20260909',
    '<OPENNR_DATA>\TrainingCache\full_eye_renderer_conditioning_strict_20260909',
    '<OPENNR_DATA>\Inference',
    '<OPENNR_DATA>\research',
    '<OPENNR_DATA>\Models',
    '<OPENNR_DATA>\_Captures',
    '<DATA_ROOT>\OpenNR_Cache_NextGridSpatialAllCrops_0.5.5_20260906',
    '<DATA_ROOT>\OpenNR_Cache_StrictAllCohortsVariedHighEffect_0.5.5_20260907',
    '<DATA_ROOT>\OpenNR_Cache_VariedHighEffectSpatialAllCrops_0.5.5_20260907',
    '<DATA_ROOT>\OpenNR_Captures_2xDLSSNR_0.5.7_20260907',
    '<DATA_ROOT>\OpenNR_Captures_FullEyeStateProbe_EveryFrame_20260909',
    '<DATA_ROOT>\OpenNR_Captures_FullEyeTemporalEveryFrame_20260909',
    '<DATA_ROOT>\OpenNR_Captures_FullEyeTemporalPilot_20260908',
    '<DATA_ROOT>\OpenNR_Captures_RendererConditioningPilot_0.5.5_20260907',
    '<DATA_ROOT>\OpenNR_Captures_RendererConditioningPilot_0.5.5_20260907_discarded_no_renderer_conditionings_20260907',
    '<DATA_ROOT>\OpenNR_Captures_RendererStateDistillation_0.5.7_20260908',
    '<DATA_ROOT>\OpenNR_Captures_RendererStatePairs_0.5.7_20260908',
    '<DATA_ROOT>\OpenNR_Captures_Temporal_20260905',
    '<DATA_ROOT>\OpenNR_Captures_Temporal_Crops_0.5.3_NoPreview_20260905',
    '<DATA_ROOT>\OpenNR_Captures_Temporal_Crops_20260905',
    '<DATA_ROOT>\OpenNR_Captures_TemporalCrops_EveryFrame_20260909',
    '<DATA_ROOT>\OpenNR_Captures_TemporalCrops_SparseFullEye_20260909',
    '<DATA_ROOT>\OpenNR_Captures_VariedHighEffect_0.5.5_20260907',
    '<DATA_ROOT>\OpenNR_GEN_Static_20260910',
    '<DATA_ROOT>\OpenNR_TinyEnhancementStudy_20260909',
    '<OPENNR_DATA>\python312_ml_deps_20260908',
    '<OPENNR_DATA>\python312_ml_deps_flux_main_20260910',
    '<OPENNR_DATA>\python312_ml_deps_flux_main_20260910b',
    '<OPENNR_DATA>\python312_ml_deps_flux_main_20260910c',
    '<OPENNR_DATA>\python312_ml_deps_gen_20260910',
    '<DATA_ROOT>\OpenNR_TrainingInputs\OpenNR_RawCropCache_StrictAllCohortsNextGrid_0.5.5_20260906',
    '<DATA_ROOT>\OpenNR_TrainingInputs\RendererConditioningPilotAligned_20260907',
    '<DATA_ROOT>\OpenNR_TrainingInputs\RendererConditioningTwoPass_20260907',
    '<DATA_ROOT>\OpenNR_TrainingInputs\RendererStateDistillation_20260908',
    '<DATA_ROOT>\OpenNR_Captures_StrictTemporal_0.5.5_20260906',
    '<DATA_ROOT>\OpenNR_Captures_Temporal_Crops_0.5.5_20260906',
    '<DATA_ROOT>\OpenNR_Captures_Temporal_MasterRaw_0.5.5_20260905',
    '<DATA_ROOT>\OpenNR_MergedSpatialCache_AllSpatialSources_0.5.5_20260906',
    '<DATA_ROOT>\OpenNR_LegacySpatialAux_0.5.5_20260906_v2',
    '<DATA_ROOT>\OpenNR_RawCropCache_VariedHighEffectTemporalClean_0.5.5_20260907',
    '<DATA_ROOT>\OpenNR_FullResMasterSpatialAux_0.5.5_20260906',
    '<DATA_ROOT>\OpenNR_SpatialAux_ContentExcluded_0.5.5_20260906',
    '<DATA_ROOT>\OpenNR_Training',
    '<DATA_ROOT>\OpenNR_Captures_NextGridPilot_0.5.5_20260906',
    '<DATA_ROOT>\OpenNR_Training',
    '<DATA_ROOT>\OpenNR_TinyEnhancementStudy_20260909',
    '<DATA_ROOT>\OpenNR_ExternalResearch',
    '<DATA_ROOT>\OpenNR_ColdStorage',
    '<USER_HOME>\AppData\Local\pip\Cache\http-v2',
    '<USER_HOME>\AppData\Local\pip\Cache\wheels'
)

$outTargets = @(
    '<REPO>\out\quality_phase_20260905',
    '<REPO>\out\native_final_20260905',
    '<REPO>\out\opennr_poc',
    '<REPO>\out\conditioning_twopass_20260907',
    '<REPO>\out\conditioning_64frame_gated_20260907',
    '<REPO>\out\student_v1_20260904',
    '<REPO>\out\student_v2_20260904',
    '<REPO>\out\student_long_20260905',
    '<REPO>\out\conditioning_content_64frame_20260907',
    '<REPO>\out\conditioning_content_fresh18_20260907',
    '<REPO>\out\teacher_native_smoke_20260905',
    '<REPO>\out\student_d3d12_smoke_20260905',
    '<REPO>\out\teacher_native_benchmark_20260905',
    '<REPO>\out\student_d3d12_benchmark_20260905',
    '<REPO>\out\fullres_audit_20260904'
)

$targets = [System.Collections.Generic.List[string]]::new()
foreach ($p in $fixedTargets + $outTargets) { [void]$targets.Add($p) }

# The protected training root is handled explicitly: remove every other immediate child,
# then remove only stale copies below the retained model directory.
$trainingRoot = '<OPENNR_DATA>\Training'
$trainingKeep = 'semantic_pixel_l1_pair_20260909'
if (Test-Path -LiteralPath $trainingRoot) {
    Get-ChildItem -LiteralPath $trainingRoot -Force |
        Where-Object { $_.Name -ne $trainingKeep } |
        ForEach-Object { [void]$targets.Add($_.FullName) }
}
[void]$targets.Add((Join-Path $trainingRoot "$trainingKeep\control"))
[void]$targets.Add((Join-Path $trainingRoot "$trainingKeep\continuation_800_1600"))
[void]$targets.Add((Join-Path $trainingRoot "$trainingKeep\joint_independent_replay"))
[void]$targets.Add((Join-Path $trainingRoot "$trainingKeep\joint\last.pt"))

# Remove duplicate target strings while preserving order.
$targets = @($targets | Select-Object -Unique)

foreach ($p in $targets) {
    if (-not (Test-Path -LiteralPath $p)) { continue }
    $protectedHit = $false
    foreach ($keep in $protected) {
        if ($p -eq $keep -or $p.StartsWith($keep + '\')) { $protectedHit = $true; break }
    }
    if ($protectedHit) { throw "Refusing protected target: $p" }
    $leaf = Split-Path -Leaf $p
    if ([string]::IsNullOrWhiteSpace($leaf)) { throw "Refusing root-like target: $p" }
    Write-Output "DELETE $p"
    try {
        Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction Stop
        if (Test-Path -LiteralPath $p) { Write-Output "FAIL STILL_EXISTS $p" }
        else { Write-Output "OK $p" }
    }
    catch {
        Write-Output "FAIL $p :: $($_.Exception.Message)"
    }
}

Write-Output "CLEANUP_FINISHED"
