#include "FoveatedRender.h"

#include "../../Globals.h"
#include "../../I18n/I18n.h"
#include "../../Utils/Subrect.h"
#include "../../Utils/UI.h"
#include "../HDRDisplay.h"
#include "../FoveatedCommon.h"
#include "../OpenNRCapture.h"
#include "../Upscaling.h"
#include "../VR.h"
#include "FoveatedRender/Core.h"
#include "NativeOpenVRGaze.h"
#include "NeuralRendering/Integration.h"
#include "NeuralRendering/FuturePipeline.h"
#include "NeuralRendering/Renderer.h"
#include "NeuralRendering/RuntimePolicy.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <format>
#include <string>

#define I18N_KEY_PREFIX "feature.upscaling."

static_assert(!NeuralRendering::Future::AsyncOwnershipContract::kRuntimeEnabled);
static_assert(!NeuralRendering::Future::DepthMatchedResidualFillContract::kRuntimeEnabled);
static_assert(NeuralRendering::Future::PeripheralCompressionContract::kRuntimeEnabled);

namespace
{
	struct SettingsText
	{
		const char* key;
		const char* label;
		const char* tooltip;
		const char* purpose;
	};

	constexpr SettingsText kSettingsText[] = {
		{ "nr_route", "Route", "", "Shows the active NR path and controls its eye region." },
		{ "nr_coverage", "NR coverage (%)", "Runs full-resolution NR inside the selected percentage of each eye. Cost: medium; a smaller area reduces NR work but still runs Feature 18. Change it to trade treated area for GPU time. Requires Full Eye foveation.", "" },
		{ "nr_projection_crop", "Projection-centred crop", "Centers the NR region on each eye's forward projection ray. Cost: low CPU setup. Change it when the buffer center does not match the viewed center.", "" },
		{ "nr_shared_x", "Shared crop offset X (px)", "Moves both eye crops horizontally; each per-eye zero axis falls back to this value. Cost: low. Change it only to align the crop with the viewed center.", "" },
		{ "nr_shared_y", "Shared crop offset Y (px)", "Moves both eye crops vertically; each per-eye zero axis falls back to this value. Cost: low. Change it only to align the crop with the viewed center.", "" },
		{ "nr_eye_offsets", "Per-eye crop offsets", "Fine-aligns each eye independently. A zero axis falls back to the shared offset for that axis. Cost: low. Change only for headset-specific lens alignment.", "" },
		{ "nr_left_x", "Left eye X offset (px)", "Moves the left eye's crop horizontally; zero uses the shared X offset. Cost: low. Change only to align the left view.", "" },
		{ "nr_left_y", "Left eye Y offset (px)", "Moves the left eye's crop vertically; zero uses the shared Y offset. Cost: low. Change only to align the left view.", "" },
		{ "nr_right_x", "Right eye X offset (px)", "Moves the right eye's crop horizontally; zero uses the shared X offset. Cost: low. Change only to align the right view.", "" },
		{ "nr_right_y", "Right eye Y offset (px)", "Moves the right eye's crop vertically; zero uses the shared Y offset. Cost: low. Change only to align the right view.", "" },
		{ "nr_periphery_section", "Periphery (whole eye)", "Adds a reduced-resolution Feature 18 pass outside the center crop. Cost: high GPU and VRAM because it adds a second NR route. Enabling it changes 100% coverage to the shipped 55% tier and enables projection-centred cropping. Change it when you want NR over the full eye and can afford the extra work.", "Fills the area around the center crop with a separate NR pass." },
		{ "nr_periphery_enable", "Enable periphery NR", "Adds a reduced-resolution Feature 18 pass outside the center crop. Cost: high GPU and VRAM because it adds a second NR route. Enabling it changes 100% coverage to the shipped 55% tier and enables projection-centred cropping. Change it when you want NR over the full eye and can afford the extra work.", "" },
		{ "nr_periphery_resolution", "Periphery model resolution (%)", "Sets the model tier for the whole-eye periphery pass. Changing this tier also selects its shipped lighting gain; that gain remains adjustable below. Cost: medium to high GPU and VRAM. Lower tiers reduce work but can soften detail; change only to tune the extra pass.", "" },
		{ "nr_periphery_gain", "Periphery lighting gain (×)", "Scales lighting in the periphery composite. Cost: low. Change it only to match brightness across the center boundary; changing the resolution tier resets it to that tier's shipped gain.", "" },
		{ "nr_centre_blend", "Centre blend width (px)", "Sets the transition width between center and periphery NR. Cost: low composite work. Change it to hide a visible boundary; it does not change crop size.", "" },
		{ "nr_periphery_stagger", "Stagger periphery frames", "Reuses each eye's periphery result on alternating frames. Cost: lower average periphery evaluation work, with more stale detail during motion. Change it if the extra pass is too costly and motion remains acceptable.", "" },
		{ "nr_edge", "Subrect edge blend", "Controls the boundary between a cropped NR result and the full-eye image, and the foveated subrect and stretched background. Cost: low composite work. Change it to soften a visible boundary in either path.", "Blends cropped regions with the surrounding full-eye image." },
		{ "nr_edge_mode", "NR edge blend mode", "Chooses a hard copy, feather, or dither transition at the crop boundary. Cost: low composite work. Change it to control seam visibility.", "" },
		{ "nr_feather", "Feather width (px)", "Sets the width of the smooth NR edge transition. Cost: low. Change it to soften or tighten the boundary.", "" },
		{ "nr_band", "Dither band width (px)", "Sets the width of the dithered NR transition. Cost: low mask work. Change it to soften or tighten the boundary.", "" },
		{ "nr_noise", "Dither strength", "Sets the noise level used to break up a visible edge. Cost: low mask work. Change it if the transition looks patterned or too sharp.", "" },
		{ "nr_falloff", "Edge falloff", "Controls how quickly the blend shifts across the boundary. Cost: low. Change it to move the transition toward the center or outer image.", "" },
		{ "nr_mask", "Edge mask shape", "Chooses a rectangular or oval transition mask. Cost: low composite work. Change it to match the crop shape you want to see.", "" },
		{ "nr_tone", "Colour and tone", "", "Adjusts NR appearance after the model runs." },
		{ "nr_adaptive", "Adaptive", "", "Changes NR cost tiers over time when measured frame-time pressure persists." },
		{ "nr_adaptive_unavailable", "Adaptive NR tiers and crop are unavailable in this full-resolution-only build.", "", "" },
		{ "nr_diagnostics", "Diagnostics", "", "Shows route and runtime details to help explain behaviour." },
		{ "nr_overlay", "Show foveated region overlay", "Tints the stretched foveated background so the upscaled region is easy to locate. Cost: low; it is a display overlay. Change it briefly when checking where foveated rendering runs.", "" },
		{ "nr_mapprobe", "MapProbe diagnostics", "Logs map-menu render-target and draw details. Cost: low CPU and log I/O while enabled. Change it only when diagnosing map rendering, then turn it off.", "" },
		{ "nr_adaptive_diagnostics", "Show adaptive handoff diagnostics", "Shows adaptive NR and crop transition state. Cost: low CPU and display work. Turn on only while diagnosing adaptive behaviour.", "" },
		{ "nr_parallel_eye_queue", "Parallel eye compute queue", "Records eye 1 on an optional D3D12 compute queue. This changes scheduling only; measured gains are upstream reports and must be A/B tested on this system. Default off.", "" },
		{ "nr_flush_after_signal", "Flush D3D11 after NR fence signal", "Submits the D3D11 signal immediately before the D3D12 wait. This scheduling experiment may reduce a hand-off bubble; measure it with the Settings Benchmark. Default off.", "" },
		{ "nr_prewarm", "Prewarm neural rendering", "Creates the configured per-eye NGX features on a loading-screen or main-menu render frame so the first world frame can reuse them. Disable to compare cold-start timing; pair the one-time prewarm log with loading/menu timings. Default on.", "" },
		{ "nr_model_preset", "Model preset", "Selects a shipped NR model tuning set. Cost: low; it changes model parameters but adds no separate pass. Change it to compare a named tuning profile.", "" },
		{ "nr_intensity", "Intensity", "Scales the NR model correction. Cost: low; no extra rendering pass. Change it to make the model effect stronger or weaker.", "" },
		{ "nr_local_tone", "Local tone", "Scales local contrast changes from NR. Cost: low. Change it when local contrast looks too strong or too flat.", "" },
		{ "nr_local_structure", "Local structure", "Scales fine structural detail from NR. Cost: low. Change it when texture detail needs more or less emphasis.", "" },
		{ "nr_skin_structure", "Skin structure", "Adjusts NR structure treatment on skin. Cost: low. Change it when skin detail needs a different balance.", "" },
		{ "nr_auto_mask", "Automatic mask", "Lets the model choose its automatic detail mask. Cost: low. Change it only when tuning the model's detail response.", "" },
		{ "nr_ui_correction", "UI correction", "Applies the model's UI correction behavior. Cost: low. Change it if rendered interface elements need different NR treatment.", "" },
		{ "tone_shape", "Shape the NR result", "Applies optional image-space tone, color, and detail edits after Feature 18. Cost: medium GPU work because it adds a full-screen pass; detail and halo options add neighborhood samples. Change it only when the NR output needs a visual correction.", "" },
		{ "tone_edit", "Edit strength", "Scales the whole NR correction: 0 removes it, 1 preserves it, and 2 doubles it. Cost: low. Change it to reduce or strengthen all result-shaping edits together.", "" },
		{ "tone_brightening", "Brightening", "Scales positive luminance correction from NR wherever it occurs. Cost: low incremental work in the result-shaping pass. Change it to strengthen or reduce positive luminance changes.", "" },
		{ "tone_darkening", "Darkening", "Scales negative luminance correction from NR wherever it occurs. Cost: low incremental work in the result-shaping pass. Change it to strengthen or reduce negative luminance changes.", "" },
		{ "tone_color", "Color", "Scales the model's chroma change relative to the game input: 0 keeps game chroma, 1 keeps the model change, and values above 1 extrapolate it. Cost: low incremental work in the result-shaping pass. Change it when NR changes color too much or too little.", "" },
		{ "tone_hue", "Hue shift", "Scales the hue change from NR: 0 keeps the game's hues, 1 preserves NR's change, and 2 doubles it. Cost: low. Change it when NR shifts object colors.", "" },
		{ "tone_shadows", "Shadows", "Scales result shaping in shadow tones. Cost: low. Change it to tune dark areas without changing the whole image equally.", "" },
		{ "tone_midtones", "Midtones", "Scales result shaping in midtone values. Cost: low. Change it to tune the middle brightness range.", "" },
		{ "tone_highlights", "Highlights", "Scales result shaping in highlight tones. Cost: low. Change it to tune bright areas.", "" },
		{ "tone_max_bright", "Max brightening (stops)", "Caps positive luminance correction in exposure stops. Cost: low incremental work in the result-shaping pass. Change it to limit positive NR luminance changes.", "" },
		{ "tone_max_dark", "Max darkening (stops)", "Caps negative luminance correction in exposure stops. Cost: low incremental work in the result-shaping pass. Change it to limit negative NR luminance changes.", "" },
		{ "tone_max_color", "Max color change (stops)", "Caps color exposure change in stops. Cost: low. Change it to limit large NR color shifts.", "" },
		{ "tone_large", "Large-scale tone", "Scales broad lighting changes. Cost: medium sampling work in the result-shaping pass. Change it to preserve or reduce large illumination variation.", "" },
		{ "tone_fine", "Fine detail", "Scales fine detail changes. Cost: medium sampling work in the result-shaping pass. Change it when surface detail needs a different balance.", "" },
		{ "tone_radius", "Detail radius", "Sets where broad lighting edits end and fine detail edits begin as a share of image height. Cost: low. Change it to adjust the scale separating broad and fine edits.", "" },
		{ "tone_halo", "Halo suppression", "Reduces bright or dark halos around strong edges using neighboring samples. Cost: medium sampling work in the result-shaping pass. Change it if result shaping creates visible edge halos.", "" },
		{ "tone_stabilize", "Result stabilization", "Temporally smooths the NR correction; Motion uses per-eye motion vectors and rejects disocclusions using depth and color. Cost: medium GPU/history work. Change it if the correction flickers and accept possible trails.", "" },
		{ "tone_stabilize_time", "Stabilize time (ms)", "Sets the smoothing time for NR correction history. Cost: low additional work once stabilization is on. Change it to trade flicker against motion trails.", "" },
		{ "tone_stabilize_detail", "Stabilize fine detail", "Includes fine changes in temporal smoothing. Cost: low additional work; more detail can trail in motion. Change it only when broad-only smoothing is insufficient.", "" },
		{ "tone_stabilize_depth", "Stabilizer depth threshold", "Rejects history samples whose depth differs beyond this threshold. Cost: low. Change it to reduce trails across depth edges.", "" },
		{ "tone_stabilize_color", "Stabilizer color tolerance", "Rejects history samples with larger color differences. Cost: low. Change it to reduce trails across changing colors.", "" },
		{ "nr_model_resolution", "Model resolution", "Disabled: this build forces Feature 18 to full resolution so face, skin, hair, clothing and lighting detail are never reduced.", "" },
		{ "nr_adaptive_enable", "Enable adaptive NR resolution", "Disabled: adaptive NR is forced off in this build; fixed full-resolution Feature 18 preserves detail.", "" },
		{ "nr_adaptive_custom_fps", "Use custom FPS target", "Uses a fixed FPS budget instead of headset refresh. Cost: none by itself. Change it when the headset refresh is not the desired target.", "" },
		{ "nr_adaptive_fps", "FPS target", "Sets the adaptive frame-time budget. Cost: none by itself; a higher target can trigger lower NR tiers more often. Change it to match the desired frame rate.", "" },
		{ "nr_adaptive_min", "Adaptive minimum NR cost", "Sets the lowest NR resolution adaptive mode may select. Cost: medium to high image softness at lower limits. Change it to control the quality floor.", "" },
		{ "nr_adaptive_down", "NR downshift fade", "Sets how quickly NR steps down after sustained pressure. Cost: none; slower response may allow more missed budgets. Change it to tune responsiveness.", "" },
		{ "nr_adaptive_up", "NR restore response", "Sets how quickly NR returns to a higher tier after headroom returns. Cost: none; faster response can cause more tier changes. Change it to tune recovery.", "" },
		{ "nr_adaptive_dwell", "NR tier minimum dwell", "Sets how long an NR tier must remain active before another change. Cost: none; longer dwell reduces tier switching but delays response. Change it to limit oscillation.", "" },
		{ "nr_adaptive_headroom", "Adaptive reserved headroom", "Reserves frame-time margin before choosing a higher NR tier. Cost: none; more reserve can keep quality lower. Change it when budgets are frequently missed.", "" },
		{ "nr_adaptive_crop", "Enable adaptive crop", "Lets crop coverage change under sustained pressure after adaptive NR. Cost: high image-area reduction and visible framing change. Change only if extra performance is needed and moving crop geometry is acceptable.", "" },
		{ "nr_adaptive_crop_max", "Adaptive maximum crop coverage", "Sets the largest crop region used by adaptive crop. Cost: lower coverage can reduce work but shows more of the non-NR image. Change to set the adaptive quality ceiling.", "" },
		{ "nr_adaptive_crop_min", "Adaptive minimum crop coverage", "Sets the smallest crop region adaptive crop may select. Cost: lower coverage can reduce NR work but reduces treated eye area. Change to set the crop quality floor.", "" },
		{ "nr_adaptive_crop_down", "Crop downshift response", "Sets how quickly adaptive crop reduces coverage under pressure. Cost: none; faster response can change the visible region sooner. Change to tune response speed.", "" },
		{ "nr_adaptive_crop_up", "Crop restore response", "Sets how quickly adaptive crop expands after headroom returns. Cost: none; slower response keeps a smaller region longer. Change to tune recovery.", "" },
		{ "nr_adaptive_crop_dwell", "Crop tier minimum dwell", "Sets how long crop coverage stays at a tier before changing again. Cost: none; longer dwell reduces oscillation. Change when crop size switches too often.", "" },
		{ "nr_adaptive_crop_transition", "Crop handoff duration", "Sets the duration of a transition between crop sizes. Cost: low blend work; longer handoffs keep mixed geometry active longer. Change to tune transition visibility.", "" },
		{ "nr_adaptive_refresh", "Headset refresh target", "Each button sets the adaptive frame budget to half its listed headset refresh rate. Cost: none by itself; higher refresh targets may trigger lower NR tiers more often. Choose your active headset refresh rate.", "" },
		{ "nr_experimental_resolve", "Reduced NR Resolve", "Selects the reduced-resolution result reconstruction path. Cost: medium extra compute for matched residual. Change only for controlled image comparisons.", "" },
		{ "nr_experimental_pre", "Experimental pre-upscale NR", "Runs NR before DLSS. Cost: high extra GPU work and possible color changes. Change only for isolated comparison; incompatible with Ray Reconstruction.", "" },
		{ "nr_experimental_multi", "Experimental sequential NR", "Runs NR two or three times with separate resources. Cost: high to very high GPU and VRAM; intended for screenshots or benchmarks. Change only for controlled captures.", "" },
		{ "nr_temporal_mode", "Temporal residual reuse", "Runs full Feature 18 every Nth frame and reuses its residual between passes. Cost: lower average NR work but stale detail and temporal artifacts. Change only for fixed-geometry tests.", "" },
		{ "nr_temporal_depth", "Depth rejection threshold", "Rejects reused history samples across depth changes. Cost: low. Change it to reduce ghosting at depth edges.", "" },
		{ "nr_temporal_color", "Color rejection tolerance", "Rejects reused history samples with larger color differences. Cost: low. Change it to reduce ghosting across color changes.", "" },
		{ "nr_temporal_stagger", "Stagger eyes", "Alternates which eye runs full NR each frame. Cost: lower peak work but left/right timing can differ during motion. Change only for an isolated temporal reuse test.", "" },
		{ "nr_temporal_reset", "Reset native history after a skipped frame", "Clears native history after a missed cadence frame. Cost: low reset overhead; disabling can leave stale history. Change only for an isolated diagnostic test.", "" },
		{ "nr_gaze", "Native OpenVR gaze provider", "Moves the crop using native OpenVR eye tracking. Cost: low query work plus temporal history resets as the crop moves. Change only with a supported eye tracker and Default mode.", "" },
		{ "nr_gaze_smoothing", "Fixation smoothing (ms)", "Smooths small fixation noise while eye movements remain responsive. Cost: low. Change it if crop jitter is visible; excess smoothing can lag fixation.", "" },
		{ "nr_gaze_quantization", "Crop movement quantization (input px)", "Sets the minimum input-pixel step before the gaze crop moves. Cost: low; larger steps reduce updates but make movement less precise. Change it if crop movement is noisy.", "" },
		{ "nr_style_natural", "Natural style", "Uses neutral detail with restrained contrast. Cost: no extra pass. Change when you want a restrained image character.", "" },
		{ "nr_style_fabric", "Fabric Detail style", "Emphasizes fine materials and surface texture. Cost: no extra pass. Change when you want stronger fabric and material detail.", "" },
		{ "nr_style_cinematic", "Cinematic style", "Adds more character and local contrast. Cost: no extra pass. Change when you want a stronger stylized image character.", "" },
		{ "nr_reset_failure", "Reset Neural Rendering Failure", "Clears the latched NR failure and retries route setup. Cost: low; initialization may hitch or fail again. Use after correcting a route or resource issue.", "" },
		{ "fov_stretch", "Foveated stretch mode", "Chooses how the low-resolution foveated background fills the rest of the eye. Cost: low to medium GPU work depending on filter. Change it to tune only the stretched background; this does not change NR periphery.", "Fills the foveated background outside the selected upscaled region." },
		{ "fov_blur", "Foveated blur radius (px)", "Sets the blur radius for the stretched foveated background. Cost: low. Change it when that background is too sharp or too soft; this does not affect NR periphery.", "" },
		{ "fov_aa", "Foveated stretched-periphery AA", "Blends motion-compensated history into the stretched background to reduce flicker. Cost: medium due to history sampling. Change it if the background flickers and accept possible trails.", "" },
		{ "fov_smoothing", "Foveated history blend", "Controls how much motion-reprojected history is used in the stretched background. Cost: low to medium. Lower values smooth more but can ghost; raise it if motion trails appear.", "" },
		{ "nr_reset", "Reset to default", "Restores this section's shipped values. Cost: none. Change this to undo tuning in this section.", "" },
		{ "nr_recommended", "Recommended", "Applies the measured hybrid: full-eye route, full-resolution NR over 55% centre coverage, and staggered 50% periphery NR. In the 2.20.4 benchmark this reduced NR cost 41% (23.40 to 13.86 ms) versus full-eye NR in one scene; results vary by scene and this was not remeasured on 2.21.8.", "" },
		{ "nr_full_eye_slowest", "Quality: full-eye NR (slowest)", "Runs full-resolution Feature 18 over the whole eye. The 2.20.4 one-scene benchmark measured 23.40 ms NR cost versus 13.86 ms for Recommended; this is the higher-coverage quality choice.", "" },
		{ "nr_safe", "Safe mode", "Turns Neural Rendering off while keeping your saved tuning values. Use this to return immediately to the selected upscaler if NR causes a route or stability problem.", "" },
		{ "nr_state_off", "Off", "", "" },
		{ "nr_state_full", "Full-eye NR", "", "" },
		{ "nr_state_crop", "NR-only crop", "", "" },
		{ "nr_state_hybrid", "Periphery hybrid", "", "" },
		{ "nr_state_refused", "Route refused", "", "" },
		{ "nr_status_route_fmt", "Route: %s", "", "" },
		{ "nr_status_periphery_fmt", "Periphery: %s", "", "" },
		{ "nr_status_refusal_fmt", "Route refused: %s", "", "" },
		{ "nr_status_dimensions_fmt", "Display %ux%u | NR render %ux%u | Coverage %u%%", "", "" },
		{ "nr_status_waiting", "Waiting for the first route evaluation.", "", "" },
		{ "nr_refusal_disabled", "Periphery NR is off", "", "" },
		{ "nr_refusal_fullEye", "Requires Full Eye foveation", "", "" },
		{ "nr_refusal_gaze", "Dynamic gaze crop is active", "", "" },
		{ "nr_refusal_adaptiveCrop", "Adaptive crop owns the region", "", "" },
		{ "nr_refusal_coverageRects", "Coverage rectangles are unavailable", "", "" },
		{ "nr_refusal_depthFormat", "Depth format is unsupported", "", "" },
		{ "nr_refusal_eyeWidth%8", "Eye width is not a multiple of 8", "", "" },
		{ "nr_refusal_foveatedRoute", "Restart the game to initialize Neural Rendering", "", "" },
		{ "nr_refusal_upscaler", "Select DLSS as the upscaler", "", "" },
		{ "nr_refusal_dlssMode", "VR requires Default DLSS mode", "", "" },
		{ "nr_refusal_frameGeneration", "Disable Frame Generation and restart", "", "" },
		{ "nr_refusal_hybridPass", "Periphery pass failed; center NR continues", "", "" },
		{ "nr_refusal_coverage100", "Periphery NR requires centre coverage below 100%", "", "" },
		{ "nr_refusal_hybridBackoff", "Periphery NR is waiting before retrying after a failure", "", "" },
		{ "nr_refusal_staleGuides", "Motion or depth guides are stale for this frame", "", "" },
		{ "nr_refusal_missingGuides", "Motion or depth guide resources are unavailable", "", "" },
		{ "nr_refusal_uvMismatch", "Left and right guide regions do not match", "", "" },
		{ "nr_refusal_shader", "Periphery NR shader could not initialize", "", "" },
		{ "nr_refusal_resources", "Periphery NR resources are unavailable", "", "" },
		{ "nr_refusal_unknown", "Periphery NR failed for an unknown reason", "", "" },
	};

	const SettingsText& FindSettingsText(const char* key)
	{
		for (const auto& text : kSettingsText)
			if (std::strcmp(text.key, key) == 0)
				return text;
		return kSettingsText[0];
	}

	const char* SettingsLabel(const char* key)
	{
		const auto& text = FindSettingsText(key);
		return T(std::format("feature.upscaling.{}.label", text.key), text.label);
	}

	const char* SettingsTooltip(const char* key)
	{
		const auto& text = FindSettingsText(key);
		return T(std::format("feature.upscaling.{}.tooltip", text.key), text.tooltip);
	}

	const char* RouteStatusText(const NeuralRendering::RouteStatus& status)
	{
		const char* routeKey = "nr_state_off";
		switch (status.route) {
		case NeuralRendering::RouteKind::FullEye: routeKey = "nr_state_full"; break;
		case NeuralRendering::RouteKind::NrOnlyCrop: routeKey = "nr_state_crop"; break;
		case NeuralRendering::RouteKind::PeripheryHybrid: routeKey = "nr_state_hybrid"; break;
		case NeuralRendering::RouteKind::Refused: routeKey = "nr_state_refused"; break;
		case NeuralRendering::RouteKind::Off: break;
		}
		return SettingsLabel(routeKey);
	}

	const char* RouteRefusalText(const std::string& term)
	{
		const std::pair<const char*, const char*> terms[] = {
			{ "disabled", "nr_refusal_disabled" }, { "fullEye", "nr_refusal_fullEye" },
			{ "gaze", "nr_refusal_gaze" }, { "adaptiveCrop", "nr_refusal_adaptiveCrop" },
			{ "coverageRects", "nr_refusal_coverageRects" }, { "depthFormat", "nr_refusal_depthFormat" },
			{ "eyeWidth%8", "nr_refusal_eyeWidth%8" }, { "foveatedRoute", "nr_refusal_foveatedRoute" },
			{ "upscaler", "nr_refusal_upscaler" }, { "dlssMode", "nr_refusal_dlssMode" },
			{ "frameGeneration", "nr_refusal_frameGeneration" }, { "hybridPass", "nr_refusal_hybridPass" },
			{ "coverage100", "nr_refusal_coverage100" }, { "hybridBackoff", "nr_refusal_hybridBackoff" },
			{ "staleGuides", "nr_refusal_staleGuides" }, { "missingGuides", "nr_refusal_missingGuides" },
			{ "uvMismatch", "nr_refusal_uvMismatch" }, { "shader", "nr_refusal_shader" },
			{ "resources", "nr_refusal_resources" }, { "unknown", "nr_refusal_unknown" },
		};
		for (const auto& [source, key] : terms)
			if (term == source)
				return SettingsLabel(key);
		return "";
	}

	void ResetRouteSettings(FoveatedRender::Settings& settings)
	{
		const FoveatedRender::Settings defaults{};
		settings.neuralRenderingEnabled = defaults.neuralRenderingEnabled;
		settings.enabled = settings.neuralRenderingEnabled ? 1u : 0u;
		settings.neuralRenderingCoverage = defaults.neuralRenderingCoverage;
		settings.neuralRenderingPeripheryEnabled = defaults.neuralRenderingPeripheryEnabled;
		settings.neuralRenderingPeripheryStagger = defaults.neuralRenderingPeripheryStagger;
		settings.neuralRenderingPeripheryResolution = defaults.neuralRenderingPeripheryResolution;
		settings.neuralRenderingPeripheryLightingGain = defaults.neuralRenderingPeripheryLightingGain;
		settings.neuralRenderingPeripheryFeatherWidth = defaults.neuralRenderingPeripheryFeatherWidth;
		settings.neuralRenderingProjectionCenteredCrop = defaults.neuralRenderingProjectionCenteredCrop;
		settings.neuralRenderingProjectionCenterOffsetXPx = defaults.neuralRenderingProjectionCenterOffsetXPx;
		settings.neuralRenderingProjectionCenterOffsetYPx = defaults.neuralRenderingProjectionCenterOffsetYPx;
		settings.neuralRenderingProjectionCenterOffsetLeftXPx = defaults.neuralRenderingProjectionCenterOffsetLeftXPx;
		settings.neuralRenderingProjectionCenterOffsetLeftYPx = defaults.neuralRenderingProjectionCenterOffsetLeftYPx;
		settings.neuralRenderingProjectionCenterOffsetRightXPx = defaults.neuralRenderingProjectionCenterOffsetRightXPx;
		settings.neuralRenderingProjectionCenterOffsetRightYPx = defaults.neuralRenderingProjectionCenterOffsetRightYPx;
		settings.neuralRenderingEyeTrackedFoveation = defaults.neuralRenderingEyeTrackedFoveation;
		settings.neuralRenderingEyeTrackedSmoothingMs = defaults.neuralRenderingEyeTrackedSmoothingMs;
		settings.neuralRenderingEyeTrackedQuantizationPixels = defaults.neuralRenderingEyeTrackedQuantizationPixels;
	}

	void ResetPeripherySettings(FoveatedRender::Settings& settings)
	{
		const FoveatedRender::Settings defaults{};
		settings.neuralRenderingPeripheryEnabled = defaults.neuralRenderingPeripheryEnabled;
		settings.neuralRenderingPeripheryStagger = defaults.neuralRenderingPeripheryStagger;
		settings.neuralRenderingPeripheryResolution = defaults.neuralRenderingPeripheryResolution;
		settings.neuralRenderingPeripheryLightingGain = defaults.neuralRenderingPeripheryLightingGain;
		settings.neuralRenderingPeripheryFeatherWidth = defaults.neuralRenderingPeripheryFeatherWidth;
	}

	void ResetEdgeSettings(FoveatedRender::Settings& settings)
	{
		const FoveatedRender::Settings defaults{};
		settings.subrectBlendMode = defaults.subrectBlendMode;
		settings.subrectMaskMode = defaults.subrectMaskMode;
		settings.subrectFeatherWidth = defaults.subrectFeatherWidth;
		settings.subrectFalloffCurve = defaults.subrectFalloffCurve;
		settings.subrectDitherStrength = defaults.subrectDitherStrength;
	}

	void ResetToneSettings(FoveatedRender::Settings& settings)
	{
		const FoveatedRender::Settings defaults{};
		settings.neuralRenderingModelResolution = defaults.neuralRenderingModelResolution;
		settings.neuralRenderingResolveMode = defaults.neuralRenderingResolveMode;
		settings.neuralRenderingPreUpscale = defaults.neuralRenderingPreUpscale;
		settings.neuralRenderingMultiPass = defaults.neuralRenderingMultiPass;
		settings.neuralRenderingTemporalReuseCadence = defaults.neuralRenderingTemporalReuseCadence;
		settings.neuralRenderingTemporalDepthThreshold = defaults.neuralRenderingTemporalDepthThreshold;
		settings.neuralRenderingTemporalColorTolerance = defaults.neuralRenderingTemporalColorTolerance;
		settings.neuralRenderingTemporalReuseStaggerEyes = defaults.neuralRenderingTemporalReuseStaggerEyes;
		settings.neuralRenderingTemporalReuseResetAfterSkip = defaults.neuralRenderingTemporalReuseResetAfterSkip;
		settings.neuralRenderingPreset = defaults.neuralRenderingPreset;
		settings.neuralRenderingIntensity = defaults.neuralRenderingIntensity;
		settings.neuralRenderingLocalTone = defaults.neuralRenderingLocalTone;
		settings.neuralRenderingLocalStructure = defaults.neuralRenderingLocalStructure;
		settings.neuralRenderingSkinStructure = defaults.neuralRenderingSkinStructure;
		settings.neuralRenderingStyle = defaults.neuralRenderingStyle;
		settings.neuralRenderingAutoMask = defaults.neuralRenderingAutoMask;
		settings.neuralRenderingUICorrection = defaults.neuralRenderingUICorrection;
		settings.neuralRenderingResultShapingEnabled = defaults.neuralRenderingResultShapingEnabled;
		settings.neuralRenderingResultEditStrength = defaults.neuralRenderingResultEditStrength;
		settings.neuralRenderingResultBrightening = defaults.neuralRenderingResultBrightening;
		settings.neuralRenderingResultDarkening = defaults.neuralRenderingResultDarkening;
		settings.neuralRenderingResultColor = defaults.neuralRenderingResultColor;
		settings.neuralRenderingResultHueShiftStrength = defaults.neuralRenderingResultHueShiftStrength;
		settings.neuralRenderingResultShadows = defaults.neuralRenderingResultShadows;
		settings.neuralRenderingResultMidtones = defaults.neuralRenderingResultMidtones;
		settings.neuralRenderingResultHighlights = defaults.neuralRenderingResultHighlights;
		settings.neuralRenderingResultMaxBrighteningStops = defaults.neuralRenderingResultMaxBrighteningStops;
		settings.neuralRenderingResultMaxDarkeningStops = defaults.neuralRenderingResultMaxDarkeningStops;
		settings.neuralRenderingResultMaxColorChangeStops = defaults.neuralRenderingResultMaxColorChangeStops;
		settings.neuralRenderingResultLargeScaleTone = defaults.neuralRenderingResultLargeScaleTone;
		settings.neuralRenderingResultFineDetail = defaults.neuralRenderingResultFineDetail;
		settings.neuralRenderingResultDetailRadius = defaults.neuralRenderingResultDetailRadius;
		settings.neuralRenderingResultHaloSuppression = defaults.neuralRenderingResultHaloSuppression;
		settings.neuralRenderingStabilizeMode = defaults.neuralRenderingStabilizeMode;
		settings.neuralRenderingStabilizeTimeMs = defaults.neuralRenderingStabilizeTimeMs;
		settings.neuralRenderingStabilizeDetail = defaults.neuralRenderingStabilizeDetail;
		settings.neuralRenderingStabilizeDepthThreshold = defaults.neuralRenderingStabilizeDepthThreshold;
		settings.neuralRenderingStabilizeColorTolerance = defaults.neuralRenderingStabilizeColorTolerance;
	}

	void ResetAdaptiveSettings(FoveatedRender::Settings& settings)
	{
		const FoveatedRender::Settings defaults{};
		settings.neuralRenderingAdaptiveEnabled = defaults.neuralRenderingAdaptiveEnabled;
		settings.neuralRenderingAdaptiveRefreshHz = defaults.neuralRenderingAdaptiveRefreshHz;
		settings.neuralRenderingAdaptiveTargetFps = defaults.neuralRenderingAdaptiveTargetFps;
		settings.neuralRenderingAdaptiveMinimumResolution = defaults.neuralRenderingAdaptiveMinimumResolution;
		settings.neuralRenderingAdaptiveDownshiftFrames = defaults.neuralRenderingAdaptiveDownshiftFrames;
		settings.neuralRenderingAdaptiveUpshiftFrames = defaults.neuralRenderingAdaptiveUpshiftFrames;
		settings.neuralRenderingAdaptiveMinimumDwellFrames = defaults.neuralRenderingAdaptiveMinimumDwellFrames;
		settings.neuralRenderingAdaptiveGuardTimeMs = defaults.neuralRenderingAdaptiveGuardTimeMs;
		settings.neuralRenderingAdaptiveDiagnostics = defaults.neuralRenderingAdaptiveDiagnostics;
		settings.neuralRenderingAdaptiveCropEnabled = defaults.neuralRenderingAdaptiveCropEnabled;
		settings.neuralRenderingAdaptiveCropMaximumCoverage = defaults.neuralRenderingAdaptiveCropMaximumCoverage;
		settings.neuralRenderingAdaptiveCropMinimumCoverage = defaults.neuralRenderingAdaptiveCropMinimumCoverage;
		settings.neuralRenderingAdaptiveCropDownshiftFrames = defaults.neuralRenderingAdaptiveCropDownshiftFrames;
		settings.neuralRenderingAdaptiveCropUpshiftFrames = defaults.neuralRenderingAdaptiveCropUpshiftFrames;
		settings.neuralRenderingAdaptiveCropMinimumDwellFrames = defaults.neuralRenderingAdaptiveCropMinimumDwellFrames;
		settings.neuralRenderingAdaptiveCropTransitionFrames = defaults.neuralRenderingAdaptiveCropTransitionFrames;
	}

	void ResetDiagnosticsSettings(FoveatedRender::Settings& settings, Upscaling::Settings& upscalingSettings)
	{
		const FoveatedRender::Settings defaults{};
		settings.debugVisualize = defaults.debugVisualize;
		settings.neuralRenderingAdaptiveDiagnostics = defaults.neuralRenderingAdaptiveDiagnostics;
		upscalingSettings.mapProbeDiagnostics = false;
		upscalingSettings.streamlineLogLevel = 0;
	}

	void ApplyRecommendedSettings(FoveatedRender::Settings& settings, Upscaling::Settings& upscalingSettings)
	{
		settings = FoveatedRender::Settings{};
		settings.neuralRenderingEnabled = true;
		upscalingSettings.mapProbeDiagnostics = false;
		upscalingSettings.streamlineLogLevel = 0;
	}

	void ApplyFullEyeQualitySettings(FoveatedRender::Settings& settings)
	{
		settings.neuralRenderingEnabled = true;
		settings.enabled = 1;
		settings.neuralRenderingCoverage = 100;
		settings.neuralRenderingProjectionCenteredCrop = false;
		settings.neuralRenderingPeripheryEnabled = false;
	}

	constexpr float kStereoGeometryEpsilon = 0.0025f;

	bool IsNasalConvergenceGeometry(const Util::Subrect::UVRegion& left,
		const Util::Subrect::UVRegion& right)
	{
		// The known-good SkyrimVR arrangement places the left-eye crop toward the
		// nasal/right side and the right-eye crop toward the nasal/left side. The
		// extents must still match because the shared Feature 18 resource contract
		// is size-symmetric; only the per-eye x origins differ.
		const bool sameExtent = std::abs(left.w - right.w) <= kStereoGeometryEpsilon &&
			std::abs(left.h - right.h) <= kStereoGeometryEpsilon;
		const bool mirrored = std::abs(right.x - (1.0f - left.x - left.w)) <= kStereoGeometryEpsilon;
		return sameExtent && mirrored && left.x > right.x + kStereoGeometryEpsilon;
	}

	Util::Subrect::UVRegion MakeAdaptiveCropUV(float coverage, bool leftEye, bool nasalConvergence)
	{
		const float offset = (1.0f - coverage) * 0.5f;
		if (!nasalConvergence)
			return { offset, offset, coverage, coverage };

		// Keep the two eye crops on their original HMD convergence sides while
		// changing only their shared coverage. At 60% this exactly reproduces the
		// persisted Nasal Convergence 60% preset: left=(.4,.2,.6,.6),
		// right=(0,.2,.6,.6).
		return leftEye ?
			Util::Subrect::UVRegion{ 1.0f - coverage, offset, coverage, coverage } :
			Util::Subrect::UVRegion{ 0.0f, offset, coverage, coverage };
	}

	std::uint32_t AdjacentAdaptiveResolution(std::uint32_t resolution, bool higher)
	{
		const auto& buckets = NeuralRendering::AdaptiveController::ResolutionBuckets();
		for (std::size_t index = 0; index < buckets.size(); ++index) {
			if (buckets[index] != resolution)
				continue;
			if (higher && index > 0)
				return buckets[index - 1];
			if (!higher && index + 1 < buckets.size())
				return buckets[index + 1];
			return resolution;
		}
		return resolution;
	}
}

#define OPENNR_FOVEATED_SETTINGS_FIELDS(X) \
	X(enabled) \
	X(dlssMode) \
	X(stretchMode) \
	X(peripheryBlurRadius) \
	X(debugVisualize) \
	X(peripheryAAMode) \
	X(peripheryTemporalAlpha) \
	X(subrectBlendMode) \
	X(subrectMaskMode) \
	X(subrectFeatherWidth) \
	X(subrectFalloffCurve) \
	X(subrectDitherStrength) \
	X(neuralRenderingEnabled) \
	X(neuralRenderingModelResolution) \
	X(neuralRenderingCoverage) \
	X(neuralRenderingProjectionCenteredCrop) \
	X(neuralRenderingProjectionCenterOffsetXPx) \
	X(neuralRenderingProjectionCenterOffsetYPx) \
	X(neuralRenderingProjectionCenterOffsetLeftXPx) \
	X(neuralRenderingProjectionCenterOffsetLeftYPx) \
	X(neuralRenderingProjectionCenterOffsetRightXPx) \
	X(neuralRenderingProjectionCenterOffsetRightYPx) \
	X(neuralRenderingPeripheryEnabled) \
	X(neuralRenderingPeripheryGuidedFilter) \
	X(neuralRenderingPeripheryStagger) \
	X(neuralRenderingPeripheryResolution) \
	X(neuralRenderingPeripheryLightingGain) \
	X(neuralRenderingPeripheryFeatherWidth) \
	X(neuralRenderingPreset) \
	X(neuralRenderingIntensity) \
	X(neuralRenderingLocalTone) \
	X(neuralRenderingLocalStructure) \
	X(neuralRenderingSkinStructure) \
	X(neuralRenderingStyle) \
	X(neuralRenderingAutoMask) \
	X(neuralRenderingUICorrection) \
	X(neuralRenderingResultShapingEnabled) \
	X(neuralRenderingResultEditStrength) \
	X(neuralRenderingResultBrightening) \
	X(neuralRenderingResultDarkening) \
	X(neuralRenderingResultColor) \
	X(neuralRenderingResultHueShiftStrength) \
	X(neuralRenderingResultShadows) \
	X(neuralRenderingResultMidtones) \
	X(neuralRenderingResultHighlights) \
	X(neuralRenderingResultMaxBrighteningStops) \
	X(neuralRenderingResultMaxDarkeningStops) \
	X(neuralRenderingResultMaxColorChangeStops) \
	X(neuralRenderingResultLargeScaleTone) \
	X(neuralRenderingResultFineDetail) \
	X(neuralRenderingResultDetailRadius) \
	X(neuralRenderingResultHaloSuppression) \
	X(neuralRenderingStabilizeMode) \
	X(neuralRenderingStabilizeTimeMs) \
	X(neuralRenderingStabilizeDetail) \
	X(neuralRenderingStabilizeDepthThreshold) \
	X(neuralRenderingStabilizeColorTolerance) \
	X(neuralRenderingPreUpscale) \
	X(neuralRenderingResolveMode) \
	X(neuralRenderingMultiPass) \
	X(neuralRenderingTemporalReuseCadence) \
	X(neuralRenderingTemporalDepthThreshold) \
	X(neuralRenderingTemporalColorTolerance) \
	X(neuralRenderingTemporalReuseResetAfterSkip) \
	X(neuralRenderingTemporalReuseStaggerEyes) \
	X(neuralRenderingAdaptiveEnabled) \
	X(neuralRenderingAdaptiveRefreshHz) \
	X(neuralRenderingAdaptiveTargetFps) \
	X(neuralRenderingAdaptiveMinimumResolution) \
	X(neuralRenderingAdaptiveDownshiftFrames) \
	X(neuralRenderingAdaptiveUpshiftFrames) \
	X(neuralRenderingAdaptiveMinimumDwellFrames) \
	X(neuralRenderingAdaptiveGuardTimeMs) \
	X(neuralRenderingAdaptiveDiagnostics) \
	X(neuralRenderingAdaptiveCropEnabled) \
	X(neuralRenderingAdaptiveCropMaximumCoverage) \
	X(neuralRenderingAdaptiveCropMinimumCoverage) \
	X(neuralRenderingAdaptiveCropDownshiftFrames) \
	X(neuralRenderingAdaptiveCropUpshiftFrames) \
	X(neuralRenderingAdaptiveCropMinimumDwellFrames) \
	X(neuralRenderingAdaptiveCropTransitionFrames) \
	X(neuralRenderingEyeTrackedFoveation) \
	X(neuralRenderingEyeTrackedSmoothingMs) \
	X(neuralRenderingEyeTrackedQuantizationPixels) \
	X(neuralRenderingParallelEyeQueue) \
	X(neuralRenderingFlushAfterSignal) \
	X(neuralRenderingPrewarm)

template <typename BasicJsonType>
void to_json(BasicJsonType& json, const FoveatedRender::Settings& settings)
{
	json = BasicJsonType::object();
#define OPENNR_WRITE_FOVEATED_SETTING(field) json[#field] = settings.field;
	OPENNR_FOVEATED_SETTINGS_FIELDS(OPENNR_WRITE_FOVEATED_SETTING)
#undef OPENNR_WRITE_FOVEATED_SETTING
}

template <typename BasicJsonType>
void from_json(const BasicJsonType& json, FoveatedRender::Settings& settings)
{
	const FoveatedRender::Settings defaults{};
	if (json.is_null()) {
		settings = defaults;
		return;
	}
#define OPENNR_READ_FOVEATED_SETTING(field) settings.field = json.value(#field, defaults.field);
	OPENNR_FOVEATED_SETTINGS_FIELDS(OPENNR_READ_FOVEATED_SETTING)
#undef OPENNR_READ_FOVEATED_SETTING
}

#undef OPENNR_FOVEATED_SETTINGS_FIELDS

// ============================================================================
// Lifecycle
// ============================================================================

void FoveatedRender::PostPostLoad()
{
	bootSnapshot.LatchIfNeeded(settings);
	adaptiveController.Reset();
	adaptiveCropController.Reset();

	// Opt into the stereo extension so the controller tracks a separate
	// right-eye UV (HMD nose-side overlap symmetry).
	subrectController.SetStereoEnabled(true);

	// Seed sensible foveal presets. Empty-case only — user edits persist.
	// The regular centered sequence is the recommended starting point for the
	// adaptive-crop experiment. Centered presets are symmetric per eye (no rightUV
	// means auto-mirror, which produces an identical right-eye UV). Keep the older
	// asymmetric Nasal Convergence presets available for existing users, but do not
	// select one by default: changing crop ownership during a handoff is easier to
	// reason about when both eyes use the same centered geometry.
	subrectController.SeedDefaultPresets({
		{ .name = kPresetFullEye, .uv = { 0.0f, 0.0f, 1.0f, 1.0f } },
		{ .name = kPresetCenter90, .uv = { 0.05f, 0.05f, 0.90f, 0.90f } },
		{ .name = kPresetCenter80, .uv = { 0.10f, 0.10f, 0.80f, 0.80f } },
		{ .name = kPresetCenter75, .uv = { 0.125f, 0.125f, 0.75f, 0.75f } },
		{ .name = kPresetCenter70, .uv = { 0.15f, 0.15f, 0.70f, 0.70f } },
		{ .name = kPresetCenter60, .uv = { 0.20f, 0.20f, 0.60f, 0.60f } },
		{ .name = kPresetCenter50, .uv = { 0.25f, 0.25f, 0.5f, 0.5f } },
		{ .name = kPresetCenter40, .uv = { 0.30f, 0.30f, 0.4f, 0.4f } },
		{ .name = kPresetCenter30, .uv = { 0.35f, 0.35f, 0.3f, 0.3f } },
		{ .name = kPresetNasalConvergence50,
			.uv = { 0.5f, 0.25f, 0.5f, 0.5f },
			.rightUV = Util::Subrect::UVRegion{ 0.0f, 0.25f, 0.5f, 0.5f } },
		{ .name = kPresetNasalConvergence60,
			.uv = { 0.4f, 0.2f, 0.6f, 0.6f },
			.rightUV = Util::Subrect::UVRegion{ 0.0f, 0.2f, 0.6f, 0.6f } },
		{ .name = kPresetNasalConvergence70,
			.uv = { 0.3f, 0.15f, 0.7f, 0.7f },
			.rightUV = Util::Subrect::UVRegion{ 0.0f, 0.15f, 0.7f, 0.7f } },
	}, kPresetFullEye);
	// PostPostLoad runs after settings load, so a user with an older, shorter
	// persisted preset list (from before these names existed) still sees every
	// current preset in the DrawEditor dropdown, not just whichever ones they
	// happened to click as buttons.
	subrectController.MaterializeNewDefaults();
	stl::write_vfunc<0x1, UICompositeRenderHook>(RE::VTABLE_BSImagespaceShaderCopyDynamicFetchDisabled[3]);
}

void FoveatedRender::UICompositeRenderHook::thunk(void* imageSpaceShader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
{
	NeuralRendering::ApplyFoveatedLdr();
	func(imageSpaceShader, shape, param);
}

void FoveatedRender::ClearShaderCache()
{
	FoveatedRenderImpl::Core::ClearShaderCache();
	NeuralRendering::Renderer::Instance().ClearShaderCache();
}

// ============================================================================
// Settings I/O — driven from Upscaling::Save/LoadSettings under a nested key
// ============================================================================

void FoveatedRender::SaveSettings(json& o_json)
{
	o_json = settings;
	subrectController.SaveSettings(o_json);
}

void FoveatedRender::LoadSettings(const json& o_json)
{
	settings = o_json;
	// Util::Subrect::Controller::LoadSettings takes `const json&` (Subrect.h:68)
	// so no const_cast is needed — keeping it would imply mutation that never
	// happens.
	subrectController.LoadSettings(o_json);
	ClampSettings();
	LatchEnabled();
}

void FoveatedRender::RestoreDefaultSettings()
{
	settings = {};
	ResetAdaptiveState();
	ClampSettings();
}

bool FoveatedRender::ApplyNeuralRenderingPreset(std::string_view presetName)
{
	uint preset = 0;
	if (presetName == "Default")
		preset = 0;
	else if (presetName == "Balanced")
		preset = 1;
	else if (presetName == "Fabric Detail")
		preset = 2;
	else if (presetName == "Natural")
		preset = 3;
	else if (presetName == "Custom")
		preset = 5;
	else
		return false;

	settings.neuralRenderingPreset = preset;
	switch (preset) {
	case 0:
		settings.neuralRenderingIntensity = Settings{}.neuralRenderingIntensity;
		settings.neuralRenderingLocalTone = Settings{}.neuralRenderingLocalTone;
		settings.neuralRenderingLocalStructure = Settings{}.neuralRenderingLocalStructure;
		settings.neuralRenderingSkinStructure = -1.0f;
		settings.neuralRenderingStyle = 0;
		settings.neuralRenderingAutoMask = true;
		break;
	case 1:
		settings.neuralRenderingIntensity = 1.0f;
		settings.neuralRenderingLocalTone = 1.0f;
		settings.neuralRenderingLocalStructure = 1.0f;
		settings.neuralRenderingSkinStructure = 1.0f;
		break;
	case 2:
		settings.neuralRenderingIntensity = 1.35f;
		settings.neuralRenderingLocalTone = 0.9f;
		settings.neuralRenderingLocalStructure = 1.6f;
		settings.neuralRenderingSkinStructure = 1.15f;
		break;
	case 3:
		settings.neuralRenderingIntensity = 0.8f;
		settings.neuralRenderingLocalTone = 0.75f;
		settings.neuralRenderingLocalStructure = 0.9f;
		settings.neuralRenderingSkinStructure = 0.9f;
		break;
	case 5:
		break;
	default:
		return false;
	}
	return true;
}

void FoveatedRender::ClampSettings()
{
	const auto clampFinite = [](float value, float fallback, float minimum, float maximum) {
		return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
	};
	settings.enabled = std::min(settings.enabled, 1u);
	settings.dlssMode = std::min(settings.dlssMode, 1u);
	settings.stretchMode = std::min(settings.stretchMode, 2u);
	settings.debugVisualize = std::min(settings.debugVisualize, 1u);
	settings.peripheryAAMode = std::min(settings.peripheryAAMode, 1u);
	settings.subrectBlendMode = std::min(settings.subrectBlendMode, 2u);
	settings.subrectMaskMode = std::min(settings.subrectMaskMode, 1u);
	settings.peripheryBlurRadius = std::clamp(settings.peripheryBlurRadius, 0.5f, 4.0f);
	settings.peripheryTemporalAlpha = std::clamp(settings.peripheryTemporalAlpha, 0.05f, 0.5f);
	settings.subrectFeatherWidth = std::clamp(settings.subrectFeatherWidth, 2.0f, 128.0f);
	settings.subrectFalloffCurve = std::clamp(settings.subrectFalloffCurve, 0.5f, 2.0f);
	settings.subrectDitherStrength = std::clamp(settings.subrectDitherStrength, 0.0f, 2.0f);
	if (settings.neuralRenderingModelResolution != 33 &&
		settings.neuralRenderingModelResolution != 50 &&
		settings.neuralRenderingModelResolution != 70 &&
		settings.neuralRenderingModelResolution != 75 &&
		settings.neuralRenderingModelResolution != 80 &&
		settings.neuralRenderingModelResolution != 85 &&
		settings.neuralRenderingModelResolution != 90 &&
		settings.neuralRenderingModelResolution != 95 &&
		settings.neuralRenderingModelResolution != 100)
		settings.neuralRenderingModelResolution = settings.neuralRenderingModelResolution < 33 ? 33 :
			(settings.neuralRenderingModelResolution < 70 ? 70 : 100);
		{
			const auto requestedCoverage = settings.neuralRenderingCoverage;
			settings.neuralRenderingCoverage = NeuralRendering::NormalizeNeuralCoverage(requestedCoverage);
			static std::atomic_flag warnedInvalidCoverage = ATOMIC_FLAG_INIT;
			if (requestedCoverage != settings.neuralRenderingCoverage && !warnedInvalidCoverage.test_and_set())
				logger::warn("[DLSSNR] unsupported NR coverage {}%; using {}% (supported presets are 100, 95, 90, 85, 80, 75, 70, 60, 55, 50, 45)",
					requestedCoverage, settings.neuralRenderingCoverage);
		}
		settings.neuralRenderingProjectionCenterOffsetXPx = clampFinite(settings.neuralRenderingProjectionCenterOffsetXPx, 0.0f, -512.0f, 512.0f);
		settings.neuralRenderingProjectionCenterOffsetYPx = clampFinite(settings.neuralRenderingProjectionCenterOffsetYPx, 0.0f, -512.0f, 512.0f);
		settings.neuralRenderingProjectionCenterOffsetLeftXPx = clampFinite(settings.neuralRenderingProjectionCenterOffsetLeftXPx, 0.0f, -200.0f, 200.0f);
		settings.neuralRenderingProjectionCenterOffsetLeftYPx = clampFinite(settings.neuralRenderingProjectionCenterOffsetLeftYPx, 0.0f, -200.0f, 200.0f);
		settings.neuralRenderingProjectionCenterOffsetRightXPx = clampFinite(settings.neuralRenderingProjectionCenterOffsetRightXPx, 0.0f, -200.0f, 200.0f);
		settings.neuralRenderingProjectionCenterOffsetRightYPx = clampFinite(settings.neuralRenderingProjectionCenterOffsetRightYPx, 0.0f, -200.0f, 200.0f);
	{
		const auto requestedResolution = settings.neuralRenderingPeripheryResolution;
		settings.neuralRenderingPeripheryResolution = NeuralRendering::NormalizePeripheryResolution(requestedResolution);
		static std::atomic_flag warnedInvalidPeripheryResolution = ATOMIC_FLAG_INIT;
		if (requestedResolution != settings.neuralRenderingPeripheryResolution && !warnedInvalidPeripheryResolution.test_and_set())
			logger::warn("[DLSSNR] unsupported periphery NR resolution {}%; using {}% (supported presets are 50, 58, 66, 75)",
				requestedResolution, settings.neuralRenderingPeripheryResolution);
	}
	settings.neuralRenderingPeripheryLightingGain = clampFinite(settings.neuralRenderingPeripheryLightingGain, 0.83f, 0.0f, 1.5f);
	settings.neuralRenderingPeripheryFeatherWidth = clampFinite(settings.neuralRenderingPeripheryFeatherWidth, 80.0f, 2.0f, 256.0f);
	if constexpr (NeuralRendering::kFullResolutionNeuralRenderingOnly) {
		// Reduced NR model area weakens the neural effect: this build evaluates every
		// NR pixel at the full display-eye resolution. Saved reduced tiers, adaptive
		// tiering and render-resolution pre-upscale NR are normalized off here.
		settings.neuralRenderingModelResolution = 100;
		settings.neuralRenderingAdaptiveEnabled = false;
		settings.neuralRenderingAdaptiveCropEnabled = false;
		settings.neuralRenderingPreUpscale = 0;
	}
	if (settings.neuralRenderingPreset == 4 || settings.neuralRenderingPreset > 5)
		settings.neuralRenderingPreset = 5;
	settings.neuralRenderingIntensity = std::clamp(settings.neuralRenderingIntensity, 0.0f, 2.0f);
	settings.neuralRenderingLocalTone = std::clamp(settings.neuralRenderingLocalTone, 0.0f, 2.0f);
	settings.neuralRenderingLocalStructure = std::clamp(settings.neuralRenderingLocalStructure, 0.0f, 2.0f);
	settings.neuralRenderingSkinStructure = std::clamp(settings.neuralRenderingSkinStructure, -1.0f, 2.0f);
	settings.neuralRenderingStyle = std::min(settings.neuralRenderingStyle, 2u);
	settings.neuralRenderingResultEditStrength = clampFinite(settings.neuralRenderingResultEditStrength, 1.0f, 0.0f, 2.0f);
	settings.neuralRenderingResultBrightening = clampFinite(settings.neuralRenderingResultBrightening, 1.0f, 0.0f, 2.0f);
	settings.neuralRenderingResultDarkening = clampFinite(settings.neuralRenderingResultDarkening, 1.0f, 0.0f, 2.0f);
	settings.neuralRenderingResultColor = clampFinite(settings.neuralRenderingResultColor, 1.0f, 0.0f, 2.0f);
	settings.neuralRenderingResultHueShiftStrength = clampFinite(settings.neuralRenderingResultHueShiftStrength, 1.0f, 0.0f, 2.0f);
	settings.neuralRenderingResultShadows = clampFinite(settings.neuralRenderingResultShadows, 1.0f, 0.0f, 2.0f);
	settings.neuralRenderingResultMidtones = clampFinite(settings.neuralRenderingResultMidtones, 1.0f, 0.0f, 2.0f);
	settings.neuralRenderingResultHighlights = clampFinite(settings.neuralRenderingResultHighlights, 1.0f, 0.0f, 2.0f);
	settings.neuralRenderingResultMaxBrighteningStops = clampFinite(settings.neuralRenderingResultMaxBrighteningStops, 0.0f, 0.0f, 4.0f);
	settings.neuralRenderingResultMaxDarkeningStops = clampFinite(settings.neuralRenderingResultMaxDarkeningStops, 0.0f, 0.0f, 4.0f);
	settings.neuralRenderingResultMaxColorChangeStops = clampFinite(settings.neuralRenderingResultMaxColorChangeStops, 0.0f, 0.0f, 4.0f);
	settings.neuralRenderingResultLargeScaleTone = clampFinite(settings.neuralRenderingResultLargeScaleTone, 1.0f, 0.0f, 2.0f);
	settings.neuralRenderingResultFineDetail = clampFinite(settings.neuralRenderingResultFineDetail, 1.0f, 0.0f, 2.0f);
	settings.neuralRenderingResultDetailRadius = clampFinite(settings.neuralRenderingResultDetailRadius, 1.0f, 0.1f, 8.0f);
	settings.neuralRenderingResultHaloSuppression = clampFinite(settings.neuralRenderingResultHaloSuppression, 0.0f, 0.0f, 1.0f);
	settings.neuralRenderingStabilizeMode = std::min(settings.neuralRenderingStabilizeMode, 2u);
	settings.neuralRenderingStabilizeTimeMs = clampFinite(settings.neuralRenderingStabilizeTimeMs, 60.0f, 10.0f, 250.0f);
	settings.neuralRenderingStabilizeDepthThreshold = clampFinite(settings.neuralRenderingStabilizeDepthThreshold, 0.05f, 0.0f, 0.25f);
	settings.neuralRenderingStabilizeColorTolerance = clampFinite(settings.neuralRenderingStabilizeColorTolerance, 0.08f, 0.0f, 0.50f);
	settings.neuralRenderingPreUpscale = std::min(settings.neuralRenderingPreUpscale, 1u);
	settings.neuralRenderingResolveMode = std::min(settings.neuralRenderingResolveMode, 1u);
	settings.neuralRenderingMultiPass = std::min(settings.neuralRenderingMultiPass, 2u);
	if (settings.neuralRenderingTemporalReuseCadence != 0 &&
		settings.neuralRenderingTemporalReuseCadence != 2 &&
		settings.neuralRenderingTemporalReuseCadence != 3 &&
		settings.neuralRenderingTemporalReuseCadence != 4)
		settings.neuralRenderingTemporalReuseCadence = 0;
	settings.neuralRenderingTemporalDepthThreshold = std::clamp(settings.neuralRenderingTemporalDepthThreshold, 0.0f, 0.25f);
	settings.neuralRenderingTemporalColorTolerance = std::clamp(settings.neuralRenderingTemporalColorTolerance, 0.0f, 0.50f);
	switch (settings.neuralRenderingAdaptiveRefreshHz) {
	case 70:
	case 72:
	case 80:
	case 90:
		break;
	default:
		settings.neuralRenderingAdaptiveRefreshHz = 80;
		break;
	}
	if (settings.neuralRenderingAdaptiveTargetFps != 0)
		settings.neuralRenderingAdaptiveTargetFps = std::clamp(settings.neuralRenderingAdaptiveTargetFps, 15u, 60u);
	switch (settings.neuralRenderingAdaptiveMinimumResolution) {
	case 70:
	case 75:
	case 80:
	case 85:
	case 90:
	case 95:
	case 100:
		break;
	default:
		settings.neuralRenderingAdaptiveMinimumResolution = 70;
		break;
	}
	settings.neuralRenderingAdaptiveDownshiftFrames = std::clamp(settings.neuralRenderingAdaptiveDownshiftFrames, 1u, 16u);
	settings.neuralRenderingAdaptiveUpshiftFrames = std::clamp(settings.neuralRenderingAdaptiveUpshiftFrames, 4u, 64u);
	settings.neuralRenderingAdaptiveMinimumDwellFrames = std::clamp(settings.neuralRenderingAdaptiveMinimumDwellFrames, 4u, 240u);
	settings.neuralRenderingAdaptiveGuardTimeMs = std::clamp(settings.neuralRenderingAdaptiveGuardTimeMs, 0.0f, 5.0f);
	switch (settings.neuralRenderingAdaptiveCropMinimumCoverage) {
	case 60:
	case 65:
	case 70:
	case 75:
	case 80:
		break;
	default:
		settings.neuralRenderingAdaptiveCropMinimumCoverage = 60;
		break;
	}
	switch (settings.neuralRenderingAdaptiveCropMaximumCoverage) {
	case 60:
	case 65:
	case 70:
	case 75:
	case 80:
	case 85:
		break;
	default:
		settings.neuralRenderingAdaptiveCropMaximumCoverage = 85;
		break;
	}
	// Keep the configured ladder ordered. If a user raises the minimum above
	// the saved maximum, preserving the requested floor is the least surprising
	// repair and avoids a controller with an empty legal range.
	if (settings.neuralRenderingAdaptiveCropMaximumCoverage < settings.neuralRenderingAdaptiveCropMinimumCoverage)
		settings.neuralRenderingAdaptiveCropMaximumCoverage = settings.neuralRenderingAdaptiveCropMinimumCoverage;
	settings.neuralRenderingAdaptiveCropDownshiftFrames = std::clamp(settings.neuralRenderingAdaptiveCropDownshiftFrames, 1u, 16u);
	settings.neuralRenderingAdaptiveCropUpshiftFrames = std::clamp(settings.neuralRenderingAdaptiveCropUpshiftFrames, 8u, 240u);
	settings.neuralRenderingAdaptiveCropMinimumDwellFrames = std::clamp(settings.neuralRenderingAdaptiveCropMinimumDwellFrames, 8u, 600u);
	settings.neuralRenderingAdaptiveCropTransitionFrames = std::clamp(settings.neuralRenderingAdaptiveCropTransitionFrames, 2u, 24u);
	settings.neuralRenderingEyeTrackedSmoothingMs = std::clamp(settings.neuralRenderingEyeTrackedSmoothingMs, 0.0f, 250.0f);
	settings.neuralRenderingEyeTrackedQuantizationPixels = std::clamp(settings.neuralRenderingEyeTrackedQuantizationPixels, 0u, 64u);
	// Preset clamping reads from Upscaling::Settings now.
	auto& sharedPreset = globals::features::upscaling.settings.presetDLSS;
	sharedPreset = std::min(sharedPreset, 6u);
	if (!IsPresetCompatibleWithMode(sharedPreset)) {
		sharedPreset = 3;  // Fall back to L
	}
}

// ============================================================================
// Activation + accessors
// ============================================================================

bool FoveatedRender::IsActive() const
{
	// Gate on DLSS/FSR being the *selected* method, not just available
	// (IsRuntimeSupported): otherwise foveation and its SSR consumer run under
	// TAA/None and the route derefs unallocated upscaler resources — the crash
	// seen under RenderDoc, whose DX12 swapchain disables DLSS.
	if (!settings.neuralRenderingEnabled || !IsLoaded() || !IsRuntimeSupported())
		return false;
	const auto method = globals::features::upscaling.GetUpscaleMethod();
	if (method != Upscaling::UpscaleMethod::kDLSS && method != Upscaling::UpscaleMethod::kFSR)
		return false;

	// Full Eye is a supported no-crop mode.  It still uses the per-eye route so
	// DLSSNR receives isolated left/right guides; the route simply skips the
	// background stretch and subrect copy-back work in ExecuteDefaultMode.
	return true;
}

bool FoveatedRender::ShouldForceVisualize() const
{
	using namespace std::chrono_literals;
	return std::chrono::steady_clock::now() - lastDragTime < 3s;
}

bool FoveatedRender::IsRuntimeSupported() const
{
	// FSR's host path has no adapter/runtime prerequisite, so VR alone is
	// sufficient to enable the option -- IsActive() is what actually gates
	// on the *selected* method (kDLSS/kFSR) being one the route supports.
	return globals::game::isVR;
}

void FoveatedRender::ResetAdaptiveState()
{
	const bool cropWasActive = adaptiveCropController.IsRuntimeActive();
	adaptiveController.Reset();
	adaptiveCropController.Reset();
	adaptiveNextDownshiftIsCrop = true;
	adaptiveCropDiagnosticGeneration = UINT64_MAX;
	if (cropWasActive)
		FoveatedRenderImpl::Core::ResetAdaptiveCropHandoff();
}

bool FoveatedRender::IsEyeTrackedFoveationEnabled() const
{
	// The performance crop must not become a second owner of UV geometry. Keep
	// both the explicit native-gaze setting and the existing stereo eye-tracking
	// setting as hard lockouts.
	return settings.neuralRenderingEyeTrackedFoveation ||
		(globals::game::isVR && globals::features::vr.stereoOpt.settings.useEyeTracking);
}

void FoveatedRender::UpdateAdaptiveState(std::uint32_t frame, bool routeEligible)
{
	const auto& nrRenderer = NeuralRendering::Renderer::Instance();
	const auto& streamline = globals::features::upscaling.streamline;
	const bool recentDLSSFailure = streamline.lastDLSSFailureFrame != UINT32_MAX &&
		frame >= streamline.lastDLSSFailureFrame &&
		frame - streamline.lastDLSSFailureFrame <= 1;
	const bool runtimeHealthy = !nrRenderer.IsFailureLatched() && !recentDLSSFailure;
	const auto method = globals::features::upscaling.GetUpscaleMethod();
	const bool nrEligible = routeEligible && IsActive() &&
		method == Upscaling::UpscaleMethod::kDLSS &&
		GetDlssMode() == DlssMode::kDefault && settings.neuralRenderingEnabled &&
		!globals::features::upscaling.IsFrameGenerationActive() &&
		!globals::features::openNRCapture.settings.enableCapture &&
		settings.neuralRenderingPreUpscale == 0 && runtimeHealthy;
	const auto leftUV = subrectController.GetUV();
	const auto rightUV = subrectController.GetRightEyeUV();
	const bool geometryCompatible = std::abs(leftUV.w - rightUV.w) <= 0.0005f &&
		std::abs(leftUV.h - rightUV.h) <= 0.0005f;
	const std::uint32_t configuredCoverage = static_cast<std::uint32_t>(std::lround(std::clamp(
		std::min({ leftUV.w, leftUV.h, rightUV.w, rightUV.h }) * 100.0f, 0.0f, 100.0f)));
	const bool eyeTrackingOwnsCrop = IsEyeTrackedFoveationEnabled();
	const bool cropPolicyAvailable = nrEligible && settings.neuralRenderingAdaptiveEnabled &&
		settings.neuralRenderingAdaptiveCropEnabled && geometryCompatible && !eyeTrackingOwnsCrop &&
		configuredCoverage >= 60;
	const bool cropCanDownshift = cropPolicyAvailable &&
		!FoveatedRenderImpl::Core::vrSubrectFixedEnvelopeRejected &&
		!FoveatedRenderImpl::Core::vrSubrectNeuralFixedEnvelopeRejected &&
		(adaptiveCropController.IsRuntimeActive() ?
			adaptiveCropController.ActiveCoverage() > adaptiveCropController.MinimumCoverage() :
			configuredCoverage > settings.neuralRenderingAdaptiveCropMinimumCoverage);
	const bool nrCanDownshift = !adaptiveController.IsAtMinimum();
	// Pressure order is deliberately interleaved. Crop gets the first available
	// step, then NR, then crop again. If one controller is already at its floor,
	// the other controller becomes eligible instead of stalling the response.
	const bool cropShouldDownshiftFirst = cropCanDownshift &&
		(adaptiveNextDownshiftIsCrop || !nrCanDownshift);
	const auto activeNRForReadiness = adaptiveController.ActiveResolution();
	const auto higherNR = AdjacentAdaptiveResolution(activeNRForReadiness, true);
	const auto lowerNR = AdjacentAdaptiveResolution(activeNRForReadiness, false);
	// Feature 18 creation is not a frame-safe operation on this route. The
	// renderer prewarms the adjacent tier while the current tier is running;
	// do not let the controller expose a tier until both eyes and its native
	// handle are resident. This turns an on-demand multi-hundred-ms create into
	// a quiet wait on the current image instead of a stale/reprojected frame.
	const bool nrUpshiftReady = adaptiveController.IsAtMaximum() ||
		nrRenderer.IsAdaptiveTierReady(higherNR);
	const bool nrDownshiftReady = adaptiveController.IsAtMinimum() ||
		nrRenderer.IsAdaptiveTierReady(lowerNR);

	NeuralRendering::AdaptiveController::Config nrConfig;
	nrConfig.enabled = settings.neuralRenderingAdaptiveEnabled;
	nrConfig.memoryPressure = streamline.IsVRAMPressure();
	nrConfig.allowDownshift = !cropShouldDownshiftFirst && !adaptiveCropController.IsTransitioning() &&
		nrDownshiftReady;
	nrConfig.allowUpshift = !adaptiveCropController.IsTransitioning() &&
		!nrRenderer.IsRecoveryLimited() && !streamline.IsVRAMPressure() && nrUpshiftReady;
	nrConfig.refreshHz = settings.neuralRenderingAdaptiveRefreshHz;
	nrConfig.targetFps = settings.neuralRenderingAdaptiveTargetFps;
	nrConfig.minimumResolution = settings.neuralRenderingAdaptiveMinimumResolution;
	nrConfig.downshiftFrames = settings.neuralRenderingAdaptiveDownshiftFrames;
	nrConfig.upshiftFrames = settings.neuralRenderingAdaptiveUpshiftFrames;
	nrConfig.minimumDwellFrames = settings.neuralRenderingAdaptiveMinimumDwellFrames;
	nrConfig.guardTimeMs = settings.neuralRenderingAdaptiveGuardTimeMs;

	const auto previousNR = adaptiveController.ActiveResolution();
	const auto previousNRTarget = adaptiveController.TargetResolution();
	float workloadMs = -1.0f;
	float gpuWorkMs = -1.0f;
	float activeSubmitMs = -1.0f;
	static std::uint32_t timingFrame = UINT32_MAX;
	static std::uint32_t sampledEngineFrame = UINT32_MAX;
	if constexpr (!NeuralRendering::kFullResolutionNeuralRenderingOnly) {
	if (globals::game::isVR && nrEligible &&
		(settings.neuralRenderingAdaptiveEnabled || settings.neuralRenderingAdaptiveCropEnabled) &&
		frame != sampledEngineFrame) {
		sampledEngineFrame = frame;
		if (auto* compositor = RE::BSOpenVR::GetIVRCompositor()) {
			vr::Compositor_FrameTiming timing{};
			timing.m_nSize = sizeof(timing);
			if (compositor->GetFrameTiming(&timing) && timing.m_nFrameIndex != timingFrame) {
				timingFrame = timing.m_nFrameIndex;
				const float gpuMs = timing.m_flPreSubmitGpuMs + timing.m_flPostSubmitGpuMs;
				const float cpuMs = timing.m_flNewFrameReadyMs - timing.m_flNewPosesReadyMs;
				gpuWorkMs = gpuMs;
				activeSubmitMs = cpuMs;
				if (std::isfinite(gpuMs) && gpuMs > 0.0f && std::isfinite(cpuMs) && cpuMs >= 0.0f)
					workloadMs = std::max(gpuMs, cpuMs);
			}
		}
	}
	}
	if (streamline.IsVRAMPressure())
		workloadMs = std::max(workloadMs, 1.3f * adaptiveController.ApplicationDeadlineMs());
	const auto previousMemoryCeiling = adaptiveController.MemoryCeiling();
	adaptiveController.Update(frame, nrConfig, nrEligible, workloadMs);
	if (previousMemoryCeiling != adaptiveController.MemoryCeiling())
		logger::info("[DLSSNR][MEMORY] ceiling {}% -> {}% active={}%; higher tiers held until Neural Rendering Reset or restart",
			previousMemoryCeiling, adaptiveController.MemoryCeiling(), adaptiveController.ActiveResolution());
	const bool adaptiveNRActive = nrEligible && adaptiveController.IsEnabled();
	if (nrEligible && previousNR != adaptiveController.ActiveResolution()) {
		logger::info("[DLSSNR] adaptive NR handoff {}% -> {}% alpha={:.2f} work={:.2f}/{:.2f}ms reason={}",
			previousNR, adaptiveController.ActiveResolution(), adaptiveController.HandoffAlpha(),
			adaptiveController.SmoothedFrameTimeMs(), adaptiveController.ApplicationDeadlineMs(), adaptiveController.DecisionReason());
	}

	NeuralRendering::AdaptiveCropController::Config cropConfig;
	cropConfig.enabled = settings.neuralRenderingAdaptiveCropEnabled;
	cropConfig.hold = FoveatedRenderImpl::Core::vrSubrectFixedEnvelopeRejected ||
		FoveatedRenderImpl::Core::vrSubrectNeuralFixedEnvelopeRejected;
	cropConfig.maximumCoverage = settings.neuralRenderingAdaptiveCropMaximumCoverage;
	cropConfig.minimumCoverage = settings.neuralRenderingAdaptiveCropMinimumCoverage;
	cropConfig.downshiftFrames = settings.neuralRenderingAdaptiveCropDownshiftFrames;
	cropConfig.upshiftFrames = settings.neuralRenderingAdaptiveCropUpshiftFrames;
	cropConfig.minimumDwellFrames = settings.neuralRenderingAdaptiveCropMinimumDwellFrames;
	cropConfig.transitionFrames = settings.neuralRenderingAdaptiveCropTransitionFrames;

	const bool cropWasActive = adaptiveCropController.IsRuntimeActive();
	const auto previousCrop = adaptiveCropController.ActiveCoverage();
	const auto previousCropTarget = adaptiveCropController.TargetCoverage();
	const auto previousRenderCrop = adaptiveCropController.RenderCoverage();
	// Crop restoration must wait until NR has completed its handoff to the true
	// maximum tier. A pending 100% target is still a transition and must not
	// expand the crop in the same interval.
	const bool nrAtStableMaximum = adaptiveController.IsAtMaximum() && !adaptiveController.IsTransitioning();
	adaptiveCropController.Update(frame, cropConfig, adaptiveNRActive && geometryCompatible,
		configuredCoverage, geometryCompatible, eyeTrackingOwnsCrop, cropShouldDownshiftFirst,
		adaptiveController.IsTransitioning(), nrAtStableMaximum,
		adaptiveController.LastSampleOverBudget(), adaptiveController.LastSampleHadHeadroom());
	if (cropWasActive && !adaptiveCropController.IsRuntimeActive())
		FoveatedRenderImpl::Core::ResetAdaptiveCropHandoff();
	const auto currentCrop = adaptiveCropController.ActiveCoverage();
	const auto currentRenderCrop = adaptiveCropController.RenderCoverage();
	const auto currentNR = adaptiveController.ActiveResolution();
	static std::uint32_t lastBudgetLog = UINT32_MAX;
	if (settings.neuralRenderingAdaptiveEnabled && frame % 300 == 0 && frame != lastBudgetLog) {
		lastBudgetLog = frame;
		logger::info("[DLSSNR][BUDGET] frame={} source=steamvr-workload fresh={} workMs={:.2f} averageMs={:.2f} deadlineMs={:.2f} pressure={} headroom={} nr={} crop={} cropHeld={} nrTransition={} cropTransition={} nrUpReady={} nrDownReady={} gpuMs={:.2f} activeSubmitMs={:.2f}",
			frame, workloadMs > 0.0f, workloadMs, adaptiveController.SmoothedFrameTimeMs(),
			adaptiveController.ApplicationDeadlineMs(), adaptiveController.LastSampleOverBudget(),
			adaptiveController.LastSampleHadHeadroom(), currentNR, currentCrop, cropConfig.hold,
			adaptiveController.IsTransitioning(), adaptiveCropController.IsTransitioning(), nrUpshiftReady,
			nrDownshiftReady, gpuWorkMs, activeSubmitMs);
	}
	const auto effectiveLeftUV = GetEffectiveLeftUV();
	const auto effectiveRightUV = GetEffectiveRightUV();
	if (adaptiveCropController.LastResetReason() != NeuralRendering::AdaptiveCropController::ResetReason::None &&
		adaptiveCropDiagnosticGeneration != adaptiveCropController.Generation()) {
		adaptiveCropDiagnosticGeneration = adaptiveCropController.Generation();
		const char* resourceMode = FoveatedRenderImpl::Core::vrSubrectResourceMode ==
			FoveatedRenderImpl::Core::SubrectResourceMode::FixedEnvelope ? "fixed-envelope" : "exact-cache";
		logger::info("[DLSSNR][ADAPTIVE] frame={} generation={} reset={} eligible={} nrEligible={} cropEligible={} eyeTracking={} geometry={} configuredCrop={} adaptiveCropMax={} prevNR={} prevNRTarget={} activeNR={} targetNR={} prevCrop={} prevCropTarget={} activeCrop={} targetCrop={} effectiveLUV=({:.3f},{:.3f},{:.3f},{:.3f}) effectiveRUV=({:.3f},{:.3f},{:.3f},{:.3f}) validOut={}x{} validIn={}x{} envelopeOut={}x{} envelopeIn={}x{} resourceMode={} creates={} reuses={} frees={} envelopeChecks={} fallbackEntries={} fallbackEvictions={} neuralFallbackEntries={}",
			frame, adaptiveCropController.Generation(),
			NeuralRendering::AdaptiveCropController::ResetReasonName(adaptiveCropController.LastResetReason()),
			adaptiveNRActive && geometryCompatible, nrEligible, cropPolicyAvailable, eyeTrackingOwnsCrop,
			geometryCompatible, configuredCoverage, cropConfig.maximumCoverage, previousNR, previousNRTarget, currentNR, adaptiveController.TargetResolution(),
			previousCrop, previousCropTarget, currentCrop, adaptiveCropController.TargetCoverage(),
			effectiveLeftUV.x, effectiveLeftUV.y, effectiveLeftUV.w, effectiveLeftUV.h,
			effectiveRightUV.x, effectiveRightUV.y, effectiveRightUV.w, effectiveRightUV.h,
			FoveatedRenderImpl::Core::vrSubrectValidOutW, FoveatedRenderImpl::Core::vrSubrectValidOutH,
			FoveatedRenderImpl::Core::vrSubrectValidInW, FoveatedRenderImpl::Core::vrSubrectValidInH,
			FoveatedRenderImpl::Core::vrSubrectOutW, FoveatedRenderImpl::Core::vrSubrectOutH,
			FoveatedRenderImpl::Core::vrSubrectInW, FoveatedRenderImpl::Core::vrSubrectInH,
			resourceMode, FoveatedRenderImpl::Core::vrSubrectResourceCreates,
			FoveatedRenderImpl::Core::vrSubrectResourceReuses, FoveatedRenderImpl::Core::vrSubrectResourceFrees,
			FoveatedRenderImpl::Core::vrSubrectEnvelopeValidations, FoveatedRenderImpl::Core::vrSubrectFallbackEntries,
			FoveatedRenderImpl::Core::vrSubrectFallbackEvictions,
			FoveatedRenderImpl::Core::vrSubrectNeuralFallbackEntries);
	}
	if (adaptiveCropController.IsRuntimeActive() && previousCrop != currentCrop) {
		// ActiveCoverage changes when the controller makes a new decision, while
		// RenderCoverage remains on the old UV geometry until the handoff is
		// complete. Do not invalidate temporal history here: the renderer is still
		// sampling the old crop geometry during this part of the transition.
		logger::info("[DLSSNR] adaptive crop decision {}% -> {}% render={} -> {}% alpha={:.2f} frame={} generation={} valid={}x{} envelope={}x{}",
			previousCrop, currentCrop, previousRenderCrop, currentRenderCrop,
			adaptiveCropController.HandoffAlpha(), frame,
			adaptiveCropController.Generation(), FoveatedRenderImpl::Core::vrSubrectValidOutW,
			FoveatedRenderImpl::Core::vrSubrectValidOutH, FoveatedRenderImpl::Core::vrSubrectOutW,
			FoveatedRenderImpl::Core::vrSubrectOutH);
		if (currentCrop < previousCrop)
			adaptiveNextDownshiftIsCrop = false;
	}
	if (adaptiveCropController.IsRuntimeActive() && previousRenderCrop != currentRenderCrop) {
		// The fixed envelope keeps the native resource identity stable, but the
		// crop origin/extent changes the meaning of every temporal sample. Reset
		// only when RenderCoverage commits the new UV geometry, so Feature 18 and
		// both eyes cannot blend history produced for the previous crop tier.
		FoveatedRenderImpl::Core::InvalidateTemporalState();
		logger::info("[DLSSNR] adaptive crop geometry commit {}% -> {}% decision={} -> {}% frame={} generation={} valid={}x{} envelope={}x{}",
			previousRenderCrop, currentRenderCrop, previousCrop, currentCrop, frame,
			adaptiveCropController.Generation(), FoveatedRenderImpl::Core::vrSubrectValidOutW,
			FoveatedRenderImpl::Core::vrSubrectValidOutH, FoveatedRenderImpl::Core::vrSubrectOutW,
			FoveatedRenderImpl::Core::vrSubrectOutH);
	}
	if (nrEligible && previousNR != currentNR && currentNR < previousNR)
		adaptiveNextDownshiftIsCrop = true;
	if (!cropPolicyAvailable)
		adaptiveNextDownshiftIsCrop = true;
}

Util::Subrect::UVRegion FoveatedRender::GetEffectiveLeftUV() const
{
	if (!adaptiveCropController.IsRuntimeActive())
		return subrectController.GetUV();
	const float coverage = std::clamp(
		static_cast<float>(adaptiveCropController.RenderCoverage()) / 100.0f, 0.01f, 1.0f);
	const bool nasalConvergence = IsNasalConvergenceGeometry(
		subrectController.GetUV(), subrectController.GetRightEyeUV());
	return MakeAdaptiveCropUV(coverage, true, nasalConvergence);
}

Util::Subrect::UVRegion FoveatedRender::GetEffectiveRightUV() const
{
	if (!adaptiveCropController.IsRuntimeActive())
		return subrectController.GetRightEyeUV();
	const float coverage = std::clamp(
		static_cast<float>(adaptiveCropController.RenderCoverage()) / 100.0f, 0.01f, 1.0f);
	const bool nasalConvergence = IsNasalConvergenceGeometry(
		subrectController.GetUV(), subrectController.GetRightEyeUV());
	return MakeAdaptiveCropUV(coverage, false, nasalConvergence);
}

FoveatedRender::DlssMode FoveatedRender::GetDlssMode() const
{
	if (globals::features::upscaling.GetUpscaleMethod() == Upscaling::UpscaleMethod::kFSR)
		return DlssMode::kDefault;
	return (DlssMode)std::min(settings.dlssMode, 1u);
}

FoveatedRender::FoveationProfile FoveatedRender::GetFoveationProfile() const
{
	FoveationProfile profile;
	if (!IsActive())
		return profile;

	const auto leftUV = GetEffectiveLeftUV();
	const auto rightUV = GetEffectiveRightUV();

	// Map the rectangular subrect onto the centered superellipse the mask helper expects: vertical
	// extent drives coverageScale (radiusY = coverageScale/2), the rect aspect drives the horizontal
	// stretch (radiusX = coverageScale * hScale/2). The mask carries one scale for both eyes (only
	// the center offset is per-eye), so size comes from the less-foveated (larger) extent of the two:
	// the center is the superset enclosing both eyes' full-quality regions, so neither eye's sharp
	// zone is ever foveated (min would shrink it below an eye's sharp region and foveate it). A
	// full eye therefore yields full coverage and disables foveation (the gate below).
	const float coverageH = std::max(leftUV.h, rightUV.h);
	const float coverageW = std::max(leftUV.w, rightUV.w);
	const float coverageScale = FoveatedCommon::ClampCenterScale(coverageH);

	// Availability keys off the clamped scale: if the larger eye rounds up to full coverage there is
	// nothing to foveate, leave the default (available == false).
	if (!FoveatedCommon::IsActiveCoverage(coverageScale))
		return profile;

	profile.available = true;
	profile.coverageScale = coverageScale;
	profile.centerHorizontalScale = FoveatedCommon::ClampCenterHorizontalScale(
		coverageH > 1e-4f ? coverageW / coverageH : 1.0f);
	profile.centerOffsets[0] = float2{ (leftUV.x + leftUV.w * 0.5f) - 0.5f, (leftUV.y + leftUV.h * 0.5f) - 0.5f };
	profile.centerOffsets[1] = float2{ (rightUV.x + rightUV.w * 0.5f) - 0.5f, (rightUV.y + rightUV.h * 0.5f) - 0.5f };
	return profile;
}

void FoveatedRender::LatchQualityMode()
{
	qualityModeAtBoot = std::clamp(globals::features::upscaling.settings.qualityMode, 1u, 4u);
}

uint FoveatedRender::GetActiveQualityMode() const
{
	return std::clamp(globals::features::upscaling.settings.qualityMode, 1u, 4u);
}

uint FoveatedRender::GetActivePresetDLSS() const
{
	return std::min(globals::features::upscaling.settings.presetDLSS, 6u);
}

float FoveatedRender::GetActiveSharpnessDLSS() const
{
	return std::clamp(globals::features::upscaling.settings.sharpnessDLSS, 0.0f, 1.0f);
}

float FoveatedRender::GetRenderScaleForQuality(uint qualityMode)
{
	return Upscaling::GetQualityModeRatio(qualityMode);
}

bool FoveatedRender::IsPresetCompatibleWithMode(uint presetIndex) const
{
	// Preset indices: 0=Default, 1=J, 2=K, 3=L, 4=M, 5=E, 6=F
	// Faster mode: J(1) and K(2) are incompatible; E(5) is unverified there.
	if (GetDlssMode() == DlssMode::kFaster) {
		return presetIndex != 1 && presetIndex != 2 && presetIndex != 5;
	}
	return true;
}

void FoveatedRender::ClampPresetToMode()
{
	auto& sharedPreset = globals::features::upscaling.settings.presetDLSS;
	if (!IsPresetCompatibleWithMode(sharedPreset)) {
		sharedPreset = 3;  // Fall back to L
	}
}

// ============================================================================
// UI — foveated route controls. Shared DLSS settings live on Upscaling.
// ============================================================================

const char* FoveatedRender::DlssModeName(DlssMode mode)
{
	return mode == DlssMode::kFaster ?
	           T(TKEY("foveated_dlss_mode_faster"), "Faster") :
	           T(TKEY("foveated_dlss_mode_default"), "Default");
}

const char* FoveatedRender::StretchModeName(StretchMode mode)
{
	switch (mode) {
	case StretchMode::kPoint:
		return T(TKEY("foveated_stretch_point"), "Point");
	case StretchMode::kGaussianBlur:
		return T(TKEY("foveated_stretch_gaussian"), "Gaussian Blur");
	default:
		return T(TKEY("foveated_stretch_bilinear"), "Bilinear");
	}
}

const char* FoveatedRender::PeripheryAAModeName(PeripheryAAMode mode)
{
	return mode == PeripheryAAMode::kTemporalSmooth ?
	           T(TKEY("foveated_periphery_aa_temporal"), "Temporal Smooth") :
	           T(TKEY("foveated_periphery_aa_none"), "None");
}

const char* FoveatedRender::SubrectBlendModeName(SubrectBlendMode mode)
{
	switch (mode) {
	case SubrectBlendMode::kFeather:
		return T(TKEY("foveated_blend_feather"), "Feather");
	case SubrectBlendMode::kDither:
		return T(TKEY("foveated_blend_dither"), "Dither");
	default:
		return T(TKEY("foveated_blend_hard_copy"), "Hard Copy");
	}
}

const char* FoveatedRender::SubrectMaskModeName(SubrectMaskMode mode)
{
	return mode == SubrectMaskMode::kOval ?
	           T(TKEY("foveated_mask_oval"), "Oval") :
	           T(TKEY("foveated_mask_rectangle"), "Rectangle");
}

void FoveatedRender::DrawDLSSModeControl()
{
	const bool fullEyePreset = subrectController.GetUV().IsFullEye() && subrectController.GetRightEyeUV().IsFullEye();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		Util::UI::DrawWrappedText(T(TKEY("foveated_dlss_mode_tooltip"),
			"Default — highest quality. Each eye gets its own isolated color, depth, and motion-vector inputs so DLSS cannot sample across the stereo midline. All DLSS presets are supported.\n\n"
			"Faster — lower overhead. DLSS reads from the frame buffer using a viewport offset. It may sample a few pixels from the neighboring eye near the stereo center. Presets J and K are incompatible and clamp to L."));
	}
	const bool isFSR = globals::features::upscaling.GetUpscaleMethod() == Upscaling::UpscaleMethod::kFSR;
	if (isFSR || fullEyePreset)
		ImGui::BeginDisabled();
	uint previousMode = settings.dlssMode;
	ImGui::SliderInt(T(TKEY("foveated_dlss_mode_label"), "DLSS Mode"),
		reinterpret_cast<int*>(&settings.dlssMode), 0, 1,
		DlssModeName(static_cast<DlssMode>(std::min(settings.dlssMode, 1u))));
	if (settings.dlssMode != previousMode) {
		const uint previousPreset = globals::features::upscaling.settings.presetDLSS;
		ClampPresetToMode();
		if (globals::features::upscaling.settings.presetDLSS != previousPreset) {
			logger::info("[FOVEATED] DLSS preset clamped from {} to {} after mode switch (J/K incompatible with Faster)",
				previousPreset, globals::features::upscaling.settings.presetDLSS);
		}
	}
	if (isFSR || fullEyePreset) {
		ImGui::EndDisabled();
		if (isFSR)
			ImGui::TextWrapped(T(TKEY("foveated_dlss_mode_fsr_desc"), "Not used by FSR — applies only when DLSS is the selected upscaler."));
		else
			Util::UI::DrawWrappedDisabledText("Unavailable with Full Eye: Faster mode only changes the cropped-eye composition path.");
	} else if (GetDlssMode() == DlssMode::kDefault) {
		ImGui::TextWrapped(T(TKEY("foveated_dlss_mode_default_desc"), "Per-eye isolation: 5 copies per frame, 2 DLSS evaluates. All presets."));
	} else {
		ImGui::TextWrapped(T(TKEY("foveated_dlss_mode_faster_desc"), "Viewport offset: 1 snapshot, 2 mask clears, 2 DLSS evaluates. Presets J/K unavailable."));
	}
}

	void FoveatedRender::DrawNeuralRenderingStatusButton()
	{
		const bool enabled = settings.neuralRenderingEnabled;
		const auto method = globals::features::upscaling.GetUpscaleMethod();
		auto& upscaling = globals::features::upscaling;
		const bool frameGenerationConfigured = upscaling.IsFrameGenerationConfiguredForSession();
		const bool frameGenerationActive = upscaling.IsFrameGenerationActive();
		const bool hdrConfigured = globals::features::hdrDisplay.loaded &&
			globals::features::hdrDisplay.settings.enableHDR;
		const bool routeSupported = method == Upscaling::UpscaleMethod::kDLSS &&
			!frameGenerationConfigured && !frameGenerationActive &&
			(globals::game::isVR ? (IsActive() && GetDlssMode() == DlssMode::kDefault &&
				upscaling.perfMode.IsHookActive()) : !hdrConfigured);
		auto& renderer = NeuralRendering::Renderer::Instance();
		const bool failed = renderer.IsFailureLatched();
		const bool waitingForFirstFrame = renderer.SuccessfulFrames() == 0;
		const char* state = !enabled ? "OFF" : !routeSupported ? "UNAVAILABLE" : failed ? "ERROR" :
			waitingForFirstFrame ? "STARTING" : "ON";
		const std::string label = std::format("NEURAL RENDERING  |  {}", state);

		ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
		if (enabled && routeSupported && !failed && !waitingForFirstFrame)
			color = ImVec4(0.20f, 0.58f, 0.50f, 1.0f);
		else if (failed)
			color = ImVec4(0.58f, 0.20f, 0.22f, 1.0f);
		else if (enabled)
			color = ImVec4(0.66f, 0.37f, 0.20f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, color);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x + 0.08f, color.y + 0.08f, color.z + 0.08f, color.w));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x - 0.05f, color.y - 0.05f, color.z - 0.05f, color.w));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f * Util::GetUIScale(), 10.0f * Util::GetUIScale()));
		if (ImGui::Button(label.c_str(), ImVec2(-1.0f, 46.0f * Util::GetUIScale()))) {
			settings.neuralRenderingEnabled = !settings.neuralRenderingEnabled;
			settings.enabled = settings.neuralRenderingEnabled ? 1u : 0u;
			NeuralRendering::RequestHistoryReset();
		}
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(3);

		std::string detail;
		if (!enabled) {
			detail = "Click to enable Neural Rendering.";
		} else if (frameGenerationConfigured || frameGenerationActive) {
			detail = "Unavailable: Frame Generation is configured for this session.";
		} else if (method != Upscaling::UpscaleMethod::kDLSS) {
			detail = "Unavailable: select NVIDIA DLSS as the upscaler.";
		} else if (!globals::game::isVR && hdrConfigured) {
			detail = "Unavailable: the flat Neural Rendering route does not support HDR Display.";
		} else if (globals::game::isVR && !IsActive()) {
			detail = "Unavailable: restart the game to initialize Neural Rendering route resources.";
		} else if (globals::game::isVR && GetDlssMode() != DlssMode::kDefault) {
			detail = "Unavailable: VR Neural Rendering requires Default DLSS mode.";
		} else if (globals::game::isVR && !globals::features::upscaling.perfMode.IsHookActive()) {
			detail = "Unavailable: VR Neural Rendering requires active PerfMode.";
		} else if (failed) {
			detail = std::format("Runtime error: {}. Use Reset Neural Rendering to retry.", renderer.StatusText());
		} else if (waitingForFirstFrame) {
			detail = std::format("Starting: waiting for the first NR frame. Runtime: {}.", renderer.StatusText());
		} else {
			detail = std::format("Ready. {} successful evaluations this session.", renderer.SuccessfulFrames());
		}
		ImGui::TextDisabled("%s", detail.c_str());
	}

	void FoveatedRender::DrawSettings(bool showSharedPanelNote, bool vrControlsFirst,
		bool showNeuralRenderingStatusButton)
	{
		ClampSettings();
		if (showNeuralRenderingStatusButton)
			DrawNeuralRenderingStatusButton();
		// Keep all Neural Rendering prose inside the shared, width-aware helper so
		// the desktop and VR panel use the same wrapping rules.
		const auto drawWrapped = [](const char* text) { Util::UI::DrawWrappedText(text); };
		const auto drawDisabledWrapped = [](const char* text) { Util::UI::DrawWrappedDisabledText(text); };
		const auto drawWarningWrapped = [](const char* text) { Util::UI::DrawWrappedWarningText(text); };
		const bool fullEyePreset = subrectController.GetUV().IsFullEye() && subrectController.GetRightEyeUV().IsFullEye();
	const auto drawVrControls = [&]() {
	// ── VR-only knobs ──
	if (globals::game::isVR) {
			ImGui::Separator();
			ImGui::Separator();
			ImGui::Text("%s", T(TKEY("foveated_periphery_header"), "Foveated Stretched Periphery"));
			ImGui::TextWrapped("%s", FindSettingsText("fov_stretch").purpose);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				drawWrapped(T(TKEY("foveated_periphery_tooltip"),
									  "The area outside your selected subrect is filled cheaply rather than running\n"
								  "the selected upscaler. These settings control how that cheap fill looks and\n"
								  "whether it flickers.\n"
								  "\n"
								  "Stretch method: how pixels outside the subrect are reconstructed from the lower-res\n"
								  "render buffer. Does not affect the upscaled subrect region at all.\n"
								  "\n"
								  "Periphery AA: reduces temporal flicker in the stretched area using motion-compensated\n"
								  "history blending. Independent of the upscaled subrect.\n"
								  "\n"
								  "Edge Blend: controls how the upscaled subrect edge meets the stretched periphery.\n"
								  "Hard Copy leaves a sharp seam; Feather/Dither soften it. Edge Shape selects a\n"
								  "rectangle or oval composite. The Feature 18 subrect remains rectangular internally."));
		}

		if (fullEyePreset)
			drawDisabledWrapped("Unavailable with Full Eye: the selected upscaler already fills the whole eye, so no stretched periphery is composed.");
		ImGui::BeginDisabled(fullEyePreset);
		ImGui::SliderInt(SettingsLabel("fov_stretch"), reinterpret_cast<int*>(&settings.stretchMode), 0, 2, StretchModeName((StretchMode)settings.stretchMode));
		if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("fov_stretch"));
		switch (GetStretchMode()) {
		case StretchMode::kBilinear:
			ImGui::TextWrapped(T(TKEY("foveated_stretch_bilinear_desc"), "Bilinear: smooth upscale of the render buffer. Looks soft but clean."));
			break;
		case StretchMode::kPoint:
			ImGui::TextWrapped(T(TKEY("foveated_stretch_point_desc"), "Point: cheapest, visibly pixelated. Good for benchmarking foveated savings."));
			break;
		case StretchMode::kGaussianBlur:
			ImGui::TextWrapped(T(TKEY("foveated_stretch_gaussian_desc"), "Gaussian: blurs the periphery further into soft focus. Good default for foveated use."));
			ImGui::SliderFloat(SettingsLabel("fov_blur"), &settings.peripheryBlurRadius, 0.5f, 4.0f, "%.1f px");
			if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("fov_blur"));
			break;
		}

		ImGui::SliderInt(SettingsLabel("fov_aa"), reinterpret_cast<int*>(&settings.peripheryAAMode), 0, 1, PeripheryAAModeName((PeripheryAAMode)settings.peripheryAAMode));
		if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("fov_aa"));
		if (GetPeripheryAAMode() == PeripheryAAMode::kTemporalSmooth) {
			ImGui::TextWrapped(T(TKEY("foveated_periphery_aa_temporal_desc"), "Blends the stretched periphery with motion-reprojected history to reduce flicker."));
				ImGui::SliderFloat(SettingsLabel("fov_smoothing"), &settings.peripheryTemporalAlpha, 0.05f, 0.5f, "%.2f");
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("fov_smoothing"));
		}
		ImGui::EndDisabled();

ImGui::Separator();
		ImGui::Text("%s", T(TKEY("foveated_subrect_region_header"), "Subrect Region"));
		ImGui::TextWrapped(T(TKEY("foveated_subrect_region_desc"),
			"Drag the preview to choose the full-quality region; the rest is stretched."));
		drawDisabledWrapped(T(TKEY("foveated_screenshot_subrect_note"), "Screenshot has its own subrect; align only for pixel-matched captures."));

		// Preview off kVR_FRAMEBUFFER (the final composed SBS image the headset
		// sees) rather than kMAIN. kMAIN is mid-pipeline and carries non-1
		// alpha where Skyrim composited UI plates, so even with the opaque
		// blend callback you see the menu mask outline instead of the rendered
		// world. ScreenshotFeature picks the same RT for the same reason
		// (ScreenshotFeature.cpp:243). Foveated is VR-only so kVR_FRAMEBUFFER
		// is always populated when we get here.
		auto renderer = globals::game::renderer;
		if (renderer) {
			auto& fb = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kVR_FRAMEBUFFER];
			auto* tex = Util::AsReal(fb.texture);
			subrectController.DrawEditor(Util::AsReal(fb.SRV), tex, 0.5f, 0.0f, Util::Subrect::OpaquePreviewBlendCallback);
		} else {
			subrectController.DrawEditor(nullptr, nullptr, 0.5f);
		}

		if (subrectController.IsDragging())
			lastDragTime = std::chrono::steady_clock::now();
	}
	};

	if (globals::game::isVR && showSharedPanelNote)
		drawDisabledWrapped(T(TKEY("foveated_shared_panel_note"), "Quality, sharpness, and DLSS preset are on the Upscaling page."));

	if (vrControlsFirst)
		drawVrControls();
	if (ImGui::CollapsingHeader(T(TKEY("neural_rendering_header"), "DLSS Neural Rendering"), ImGuiTreeNodeFlags_Leaf)) {
		const bool supportedRoute = globals::features::upscaling.GetUpscaleMethod() == Upscaling::UpscaleMethod::kDLSS &&
			!globals::features::upscaling.IsFrameGenerationConfiguredForSession() &&
			(!globals::game::isVR || (GetDlssMode() == DlssMode::kDefault &&
				globals::features::upscaling.perfMode.IsHookActive()));
		const auto routeStatus = NeuralRendering::GetRouteStatus();
		if (ImGui::CollapsingHeader(SettingsLabel("nr_route"), ImGuiTreeNodeFlags_Leaf)) {
			ImGui::TextWrapped("%s", FindSettingsText("nr_route").purpose);
			const bool routeActive = routeStatus.route == NeuralRendering::RouteKind::FullEye ||
				routeStatus.route == NeuralRendering::RouteKind::NrOnlyCrop ||
				routeStatus.route == NeuralRendering::RouteKind::PeripheryHybrid;
			const ImVec4 stateColor = routeStatus.route == NeuralRendering::RouteKind::Refused ?
				ImVec4(0.88f, 0.58f, 0.18f, 1.0f) : routeActive ?
				ImVec4(0.20f, 0.68f, 0.46f, 1.0f) : ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
			ImGui::TextColored(stateColor, SettingsLabel("nr_status_route_fmt"), RouteStatusText(routeStatus));
			ImGui::TextDisabled("Parallel eye queue: %s | Flush after signal: %s",
				settings.neuralRenderingParallelEyeQueue ? "ON" : "OFF",
				settings.neuralRenderingFlushAfterSignal ? "ON" : "OFF");
			if (!routeStatus.refusalTerm.empty()) {
				const char* reasonLabel = routeStatus.route == NeuralRendering::RouteKind::Refused ?
					SettingsLabel("nr_status_refusal_fmt") : SettingsLabel("nr_status_periphery_fmt");
				ImGui::TextColored(ImVec4(0.88f, 0.58f, 0.18f, 1.0f), reasonLabel, RouteRefusalText(routeStatus.refusalTerm));
			}
			if (routeStatus.displayWidth != 0)
				ImGui::Text(SettingsLabel("nr_status_dimensions_fmt"),
					routeStatus.displayWidth, routeStatus.displayHeight,
					routeStatus.renderWidth, routeStatus.renderHeight, routeStatus.coveragePercent);
			else
				ImGui::TextDisabled("%s", SettingsLabel("nr_status_waiting"));
			{
				static const char* coverageLabels[] = {
					"100%", "95%", "90%", "85%", "80%", "75%", "70%", "60%", "55%", "50%", "45%" };
				static_assert(IM_ARRAYSIZE(coverageLabels) == NeuralRendering::kNeuralCoveragePresets.size());
				int coverageIndex = 0;
				for (int index = 0; index < IM_ARRAYSIZE(coverageLabels); ++index)
					if (settings.neuralRenderingCoverage == NeuralRendering::kNeuralCoveragePresets[index]) {
						coverageIndex = index;
						break;
					}
				if (ImGui::Combo(SettingsLabel("nr_coverage"), &coverageIndex, coverageLabels, IM_ARRAYSIZE(coverageLabels)))
					settings.neuralRenderingCoverage = NeuralRendering::kNeuralCoveragePresets[coverageIndex];
				if (auto _tt = Util::HoverTooltipWrapper())
					drawWrapped(SettingsTooltip("nr_coverage"));
				ImGui::Checkbox(SettingsLabel("nr_projection_crop"), &settings.neuralRenderingProjectionCenteredCrop);
				if (auto _tt = Util::HoverTooltipWrapper())
					drawWrapped(SettingsTooltip("nr_projection_crop"));
				if (settings.neuralRenderingProjectionCenteredCrop) {
					ImGui::SliderFloat(SettingsLabel("nr_shared_x"), &settings.neuralRenderingProjectionCenterOffsetXPx,
						-512.0f, 512.0f, "%+.0f px");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_shared_x"));
					ImGui::SliderFloat(SettingsLabel("nr_shared_y"), &settings.neuralRenderingProjectionCenterOffsetYPx,
						-512.0f, 512.0f, "%+.0f px");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_shared_y"));
					ImGui::TextDisabled("%s", FindSettingsText("nr_eye_offsets").label);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_eye_offsets"));
					ImGui::SliderFloat(SettingsLabel("nr_left_x"), &settings.neuralRenderingProjectionCenterOffsetLeftXPx,
						-200.0f, 200.0f, "%+.0f px");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_left_x"));
					ImGui::SliderFloat(SettingsLabel("nr_left_y"), &settings.neuralRenderingProjectionCenterOffsetLeftYPx,
						-200.0f, 200.0f, "%+.0f px");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_left_y"));
					ImGui::SliderFloat(SettingsLabel("nr_right_x"), &settings.neuralRenderingProjectionCenterOffsetRightXPx,
						-200.0f, 200.0f, "%+.0f px");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_right_x"));
					ImGui::SliderFloat(SettingsLabel("nr_right_y"), &settings.neuralRenderingProjectionCenterOffsetRightYPx,
						-200.0f, 200.0f, "%+.0f px");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_right_y"));
				}
			}
			if (ImGui::Button(SettingsLabel("nr_recommended"))) {
				ApplyRecommendedSettings(settings, globals::features::upscaling.settings);
				subrectController.ApplyPresetByName(kPresetFullEye);
				NeuralRendering::RequestHistoryReset();
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				drawWrapped(SettingsTooltip("nr_recommended"));
			ImGui::SameLine();
			if (ImGui::Button(SettingsLabel("nr_full_eye_slowest"))) {
				ApplyFullEyeQualitySettings(settings);
				subrectController.ApplyPresetByName(kPresetFullEye);
				NeuralRendering::RequestHistoryReset();
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				drawWrapped(SettingsTooltip("nr_full_eye_slowest"));
			ImGui::SameLine();
			if (ImGui::Button(SettingsLabel("nr_safe"))) {
				settings.neuralRenderingEnabled = false;
				settings.enabled = 0;
				NeuralRendering::RequestHistoryReset();
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				drawWrapped(SettingsTooltip("nr_safe"));
			ImGui::SameLine();
			if (ImGui::Button(SettingsLabel("nr_reset"))) {
				ResetRouteSettings(settings);
				subrectController.ApplyPresetByName(kPresetFullEye);
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				drawWrapped(SettingsTooltip("nr_reset"));
		}
		if (!supportedRoute) {
			if (globals::features::upscaling.IsFrameGenerationConfiguredForSession())
				drawWarningWrapped("Disable Frame Generation and restart before enabling Neural Rendering.");
			else
				drawWarningWrapped(T(TKEY("neural_rendering_unavailable"), "Requires DLSS. VR also requires Default mode and active PerfMode."));
		}
		if (!supportedRoute && settings.neuralRenderingEnabled)
			ImGui::BeginDisabled();
		{

			if (ImGui::CollapsingHeader(SettingsLabel("nr_periphery_section"), ImGuiTreeNodeFlags_Leaf)) {
				ImGui::TextWrapped("%s", FindSettingsText("nr_periphery_section").purpose);
				if (ImGui::Button(SettingsLabel("nr_reset")))
					ResetPeripherySettings(settings);
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_reset"));
				bool peripheryEnabled = settings.neuralRenderingPeripheryEnabled;
				if (ImGui::Checkbox(SettingsLabel("nr_periphery_enable"), &peripheryEnabled)) {
					settings.neuralRenderingPeripheryEnabled = peripheryEnabled;
					if (peripheryEnabled && settings.neuralRenderingCoverage == 100)
						settings.neuralRenderingCoverage = 55;
					if (peripheryEnabled)
						settings.neuralRenderingProjectionCenteredCrop = true;
				}
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_periphery_enable"));
			if (peripheryEnabled) {
				ImGui::Checkbox(SettingsLabel("nr_periphery_stagger"), &settings.neuralRenderingPeripheryStagger);
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_periphery_stagger"));
				static const char* peripheryResLabels[] = { "50%", "58%", "66%", "75%" };
					static constexpr uint peripheryResValues[] = { 50u, 58u, 66u, 75u };
					int resolutionIndex = 0;
					for (int index = 0; index < IM_ARRAYSIZE(peripheryResValues); ++index)
						if (settings.neuralRenderingPeripheryResolution == peripheryResValues[index])
							resolutionIndex = index;
				if (ImGui::Combo(SettingsLabel("nr_periphery_resolution"), &resolutionIndex, peripheryResLabels, IM_ARRAYSIZE(peripheryResLabels))) {
					settings.neuralRenderingPeripheryResolution = peripheryResValues[resolutionIndex];
					static constexpr float defaultGains[] = { 0.83f, 0.86f, 0.89f, 0.93f };
					settings.neuralRenderingPeripheryLightingGain = defaultGains[resolutionIndex];
				}
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_periphery_resolution"));
				ImGui::SliderFloat(SettingsLabel("nr_periphery_gain"), &settings.neuralRenderingPeripheryLightingGain, 0.0f, 1.5f, "%.2f×");
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_periphery_gain"));
				ImGui::SliderFloat(SettingsLabel("nr_centre_blend"), &settings.neuralRenderingPeripheryFeatherWidth, 2.0f, 256.0f, "%.0f px");
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_centre_blend"));
					if (!(subrectController.GetUV().IsFullEye() && subrectController.GetRightEyeUV().IsFullEye()))
					drawWarningWrapped("Periphery NR requires the Full Eye foveation preset.");
			}
			}
			if (ImGui::CollapsingHeader(SettingsLabel("nr_edge"), ImGuiTreeNodeFlags_Leaf)) {
				ImGui::TextWrapped("%s", FindSettingsText("nr_edge").purpose);
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_edge"));
				if (ImGui::Button(SettingsLabel("nr_reset"))) ResetEdgeSettings(settings);
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_reset"));
				static const char* edgeBlendModes[] = { "Hard copy", "Feather", "Dither" };
				int edgeMode = static_cast<int>(std::min(settings.subrectBlendMode, 2u));
				if (ImGui::Combo(SettingsLabel("nr_edge_mode"), &edgeMode, edgeBlendModes, IM_ARRAYSIZE(edgeBlendModes)))
					settings.subrectBlendMode = static_cast<uint>(edgeMode);
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_edge_mode"));
				if (settings.subrectBlendMode == static_cast<uint>(SubrectBlendMode::kFeather)) {
					ImGui::SliderFloat(SettingsLabel("nr_feather"), &settings.subrectFeatherWidth, 2.0f, 128.0f, "%.0f px");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_feather"));
				}
				if (settings.subrectBlendMode == static_cast<uint>(SubrectBlendMode::kDither)) {
					ImGui::SliderFloat(SettingsLabel("nr_band"), &settings.subrectFeatherWidth, 2.0f, 128.0f, "%.0f px");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_band"));
					ImGui::SliderFloat(SettingsLabel("nr_noise"), &settings.subrectDitherStrength, 0.0f, 2.0f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_noise"));
				}
				if (settings.subrectBlendMode != static_cast<uint>(SubrectBlendMode::kHardCopy)) {
					ImGui::SliderFloat(SettingsLabel("nr_falloff"), &settings.subrectFalloffCurve, 0.5f, 2.0f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_falloff"));
				}
				ImGui::Combo(SettingsLabel("nr_mask"), reinterpret_cast<int*>(&settings.subrectMaskMode),
					(std::array<const char*, 2>{ "Rectangle", "Oval" }).data(), 2);
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_mask"));
			}
			if (ImGui::CollapsingHeader(SettingsLabel("nr_tone"), ImGuiTreeNodeFlags_Leaf)) {
				ImGui::TextWrapped("%s", FindSettingsText("nr_tone").purpose);
				if (ImGui::Button(SettingsLabel("nr_reset"))) ResetToneSettings(settings);
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_reset"));
			// Expose supported styles as named cards with tuning kept independent.
			// what "Style 1" or "Style 2" means.  Keep the lower-level tuning
			// preset and sliders below this row; selecting a style is intentionally
			// independent of those numeric strength controls.
			const char* styleKeys[] = { "nr_style_natural", "nr_style_fabric", "nr_style_cinematic" };
			const char* styleLabels[] = { SettingsLabel(styleKeys[0]), SettingsLabel(styleKeys[1]), SettingsLabel(styleKeys[2]) };
			const int activeStyle = static_cast<int>(settings.neuralRenderingStyle);
			bool custom = false;

			ImGui::SeparatorText(T(TKEY("neural_rendering_visual_style"), "Visual Style"));
			if (auto _tt = Util::HoverTooltipWrapper())
				drawWrapped(T(TKEY("neural_rendering_visual_style_tooltip"), "Select a style. Fine intensity and structure controls remain below."));

			const float minimumStyleCardWidth = 150.0f * Util::GetUIScale();
			const int styleColumnCount = std::clamp(
				static_cast<int>(ImGui::GetContentRegionAvail().x / minimumStyleCardWidth),
				1,
				IM_ARRAYSIZE(styleLabels));
			if (ImGui::BeginTable("##neural_rendering_visual_styles", styleColumnCount,
				ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings)) {
				for (int styleIndex = 0; styleIndex < IM_ARRAYSIZE(styleLabels); ++styleIndex) {
					ImGui::TableNextColumn();
					ImGui::PushID(styleIndex);
					const bool selected = styleIndex == activeStyle;
					if (selected) {
						ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
						ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
					}
					if (ImGui::Button(styleLabels[styleIndex], ImVec2(-1.0f, 0.0f))) {
						settings.neuralRenderingStyle = static_cast<uint>(styleIndex);
						custom = true;
					}
					if (selected)
						ImGui::PopStyleColor(2);
					if (auto _tt = Util::HoverTooltipWrapper())
						drawWrapped(SettingsTooltip(styleKeys[styleIndex]));
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			ImGui::TextDisabled("%s %s", T(TKEY("neural_rendering_active_style"), "Active style:"), styleLabels[activeStyle]);

			if constexpr (NeuralRendering::kFullResolutionNeuralRenderingOnly) {
				ImGui::TextDisabled("%s", T(TKEY("neural_rendering_model_resolution_locked"),
					"Model resolution: 100% (reduced NR resolution is disabled in this build)"));
			} else {
			static const char* modelResolutions[] = {
				"Full (100%)", "95%", "90%", "85%", "80%", "75%", "70%", "50% (experimental)", "33% (experimental)" };
			static constexpr uint modelResolutionValues[] = { 100u, 95u, 90u, 85u, 80u, 75u, 70u, 50u, 33u };
			int modelResolution = 0;
			for (int index = 0; index < IM_ARRAYSIZE(modelResolutionValues); ++index) {
				if (settings.neuralRenderingModelResolution == modelResolutionValues[index]) {
					modelResolution = index;
					break;
				}
			}
				if (ImGui::Combo(SettingsLabel("nr_model_resolution"),
					&modelResolution, modelResolutions, IM_ARRAYSIZE(modelResolutions))) {
				settings.neuralRenderingModelResolution = modelResolutionValues[modelResolution];
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				drawWrapped(SettingsTooltip("nr_model_resolution"));
			}

			ImGui::SeparatorText("Advanced NR Tuning");
			static const char* presets[] = { "Default", "Balanced", "Fabric Detail", "Natural", "Custom" };
			static constexpr uint presetValues[] = { 0u, 1u, 2u, 3u, 5u };
			int preset = 0;
			for (int index = 0; index < IM_ARRAYSIZE(presetValues); ++index) {
				if (settings.neuralRenderingPreset == presetValues[index]) {
					preset = index;
					break;
				}
			}
			if (ImGui::Combo(T(TKEY("neural_rendering_preset"), "Model Preset"), &preset, presets, IM_ARRAYSIZE(presets))) {
				static constexpr std::string_view presetNames[] = { "Default", "Balanced", "Fabric Detail", "Natural", "Custom" };
				ApplyNeuralRenderingPreset(presetNames[std::clamp(preset, 0, IM_ARRAYSIZE(presetNames) - 1)]);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_model_preset"));
			custom |= ImGui::SliderFloat(T(TKEY("neural_rendering_intensity"), "Intensity"), &settings.neuralRenderingIntensity, 0.0f, 2.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_intensity"));
			custom |= ImGui::SliderFloat(T(TKEY("neural_rendering_local_tone"), "Local Tone"), &settings.neuralRenderingLocalTone, 0.0f, 2.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_local_tone"));
			custom |= ImGui::SliderFloat(T(TKEY("neural_rendering_local_structure"), "Local Structure"), &settings.neuralRenderingLocalStructure, 0.0f, 2.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_local_structure"));
			custom |= ImGui::SliderFloat(T(TKEY("neural_rendering_skin_structure"), "Skin Structure"), &settings.neuralRenderingSkinStructure, -1.0f, 2.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_skin_structure"));
			custom |= ImGui::Checkbox(T(TKEY("neural_rendering_auto_mask"), "Automatic Mask"), &settings.neuralRenderingAutoMask);
			if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_auto_mask"));
			custom |= ImGui::Checkbox(T(TKEY("neural_rendering_ui_correction"), "UI Correction"), &settings.neuralRenderingUICorrection);
			if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_ui_correction"));
			if (custom)
				settings.neuralRenderingPreset = 5;

			if (ImGui::CollapsingHeader("NR Result Shaping", ImGuiTreeNodeFlags_Leaf)) {
				ImGui::Checkbox(SettingsLabel("tone_shape"), &settings.neuralRenderingResultShapingEnabled);
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_shape"));
				if (settings.neuralRenderingResultShapingEnabled) {
					ImGui::SliderFloat(SettingsLabel("tone_edit"), &settings.neuralRenderingResultEditStrength, 0.0f, 2.0f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_edit"));
					ImGui::SliderFloat(SettingsLabel("tone_brightening"), &settings.neuralRenderingResultBrightening, 0.0f, 2.0f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_brightening"));
					ImGui::SliderFloat(SettingsLabel("tone_darkening"), &settings.neuralRenderingResultDarkening, 0.0f, 2.0f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_darkening"));
					ImGui::SliderFloat(SettingsLabel("tone_color"), &settings.neuralRenderingResultColor, 0.0f, 2.0f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_color"));
					ImGui::SliderFloat(SettingsLabel("tone_hue"), &settings.neuralRenderingResultHueShiftStrength, 0.0f, 2.0f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_hue"));
					ImGui::SliderFloat(SettingsLabel("tone_shadows"), &settings.neuralRenderingResultShadows, 0.0f, 2.0f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_shadows"));
					ImGui::SliderFloat(SettingsLabel("tone_midtones"), &settings.neuralRenderingResultMidtones, 0.0f, 2.0f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_midtones"));
					ImGui::SliderFloat(SettingsLabel("tone_highlights"), &settings.neuralRenderingResultHighlights, 0.0f, 2.0f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_highlights"));
					ImGui::SliderFloat(SettingsLabel("tone_max_bright"), &settings.neuralRenderingResultMaxBrighteningStops, 0.0f, 4.0f, "%.2f stops");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_max_bright"));
					ImGui::SliderFloat(SettingsLabel("tone_max_dark"), &settings.neuralRenderingResultMaxDarkeningStops, 0.0f, 4.0f, "%.2f stops");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_max_dark"));
					ImGui::SliderFloat(SettingsLabel("tone_max_color"), &settings.neuralRenderingResultMaxColorChangeStops, 0.0f, 4.0f, "%.2f stops");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_max_color"));
					ImGui::TextDisabled("A zero limit leaves that change uncapped.");
					ImGui::SliderFloat(SettingsLabel("tone_large"), &settings.neuralRenderingResultLargeScaleTone, 0.0f, 2.0f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_large"));
					ImGui::SliderFloat(SettingsLabel("tone_fine"), &settings.neuralRenderingResultFineDetail, 0.0f, 2.0f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_fine"));
					ImGui::SliderFloat(SettingsLabel("tone_radius"), &settings.neuralRenderingResultDetailRadius, 0.1f, 8.0f, "%.2f%%");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_radius"));
					ImGui::SliderFloat(SettingsLabel("tone_halo"), &settings.neuralRenderingResultHaloSuppression, 0.0f, 1.0f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_halo"));
				}

			}

			if (ImGui::CollapsingHeader("Result Stabilization", ImGuiTreeNodeFlags_Leaf)) {
				static const char* resultStabilizeModes[] = { "Off", "Static", "Motion" };
				int resultStabilizeMode = static_cast<int>(settings.neuralRenderingStabilizeMode);
				if (ImGui::Combo(SettingsLabel("tone_stabilize"), &resultStabilizeMode,
					resultStabilizeModes, IM_ARRAYSIZE(resultStabilizeModes)))
					settings.neuralRenderingStabilizeMode = static_cast<std::uint32_t>(resultStabilizeMode);
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_stabilize"));
				if (settings.neuralRenderingStabilizeMode != 0) {
					ImGui::SliderFloat(SettingsLabel("tone_stabilize_time"), &settings.neuralRenderingStabilizeTimeMs, 10.0f, 250.0f, "%.0f ms");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_stabilize_time"));
					ImGui::Checkbox(SettingsLabel("tone_stabilize_detail"), &settings.neuralRenderingStabilizeDetail);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_stabilize_detail"));
					ImGui::SliderFloat(SettingsLabel("tone_stabilize_depth"), &settings.neuralRenderingStabilizeDepthThreshold, 0.0f, 0.25f, "%.3f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_stabilize_depth"));
					ImGui::SliderFloat(SettingsLabel("tone_stabilize_color"), &settings.neuralRenderingStabilizeColorTolerance, 0.0f, 0.50f, "%.3f");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("tone_stabilize_color"));
					const bool adaptiveNR = settings.neuralRenderingAdaptiveEnabled && adaptiveController.IsEnabled();
					if (settings.neuralRenderingTemporalReuseCadence != 0 && !adaptiveNR &&
						settings.neuralRenderingPreUpscale == 0)
						drawWarningWrapped("Result stabilization is suspended while temporal residual reuse is configured, to avoid stacking two temporal filters.");
				}

			}



				if (ImGui::CollapsingHeader("Experimental Pipeline", ImGuiTreeNodeFlags_Leaf)) {
					ImGui::SeparatorText("Resolve and Pipeline");
					if (!NeuralRendering::kFullResolutionNeuralRenderingOnly) {
					static const char* resolveModes[] = { "Classic (bounded source)", "Matched Residual (experimental)" };
					int resolveMode = static_cast<int>(std::min(settings.neuralRenderingResolveMode, 1u));
					if (ImGui::Combo(SettingsLabel("nr_experimental_resolve"), &resolveMode,
						resolveModes, IM_ARRAYSIZE(resolveModes))) {
						settings.neuralRenderingResolveMode = static_cast<uint>(resolveMode);
					}
					if (auto _tt = Util::HoverTooltipWrapper())
						drawWrapped(SettingsTooltip("nr_experimental_resolve"));

					bool preUpscale = settings.neuralRenderingPreUpscale != 0;
					if (ImGui::Checkbox(SettingsLabel("nr_experimental_pre"), &preUpscale))
						settings.neuralRenderingPreUpscale = preUpscale ? 1u : 0u;
					if (auto _tt = Util::HoverTooltipWrapper())
						drawWrapped(SettingsTooltip("nr_experimental_pre"));
					if (settings.neuralRenderingPreUpscale)
						drawWarningWrapped(T(TKEY("neural_rendering_pre_upscale_warning"), "Experimental. Disable Ray Reconstruction and use Full Eye + Default mode."));
					}

					static const char* multiPassModes[] = { "Off", "2x sequential NR", "3x sequential NR" };
					int multiPass = static_cast<int>(std::min(settings.neuralRenderingMultiPass, 2u));
					if (ImGui::Combo(SettingsLabel("nr_experimental_multi"),
						&multiPass, multiPassModes, IM_ARRAYSIZE(multiPassModes)))
						settings.neuralRenderingMultiPass = static_cast<uint>(multiPass);
					if (auto _tt = Util::HoverTooltipWrapper())
						drawWrapped(SettingsTooltip("nr_experimental_multi"));
					if (settings.neuralRenderingMultiPass) {
						if (settings.neuralRenderingMultiPass >= 2)
							drawWarningWrapped(T(TKEY("neural_rendering_multi_pass_warning"), "Experimental 3x: very large frame-time and VRAM increase."));
						else
							drawWarningWrapped(T(TKEY("neural_rendering_multi_pass_warning"), "Experimental 2x: major frame-time increase and possible smearing."));
						if (settings.neuralRenderingPreUpscale)
							drawWarningWrapped(T(TKEY("neural_rendering_multi_pass_pre_warning"), "Sequential NR is suppressed while pre-upscale NR is enabled."));
						if (globals::game::isVR && !(subrectController.GetUV().IsFullEye() && subrectController.GetRightEyeUV().IsFullEye()))
							drawWarningWrapped(T(TKEY("neural_rendering_multi_pass_subrect_warning"), "VR sequential NR is limited to Full Eye mode."));
					}

				}


				if (ImGui::CollapsingHeader(T(TKEY("neural_rendering_temporal_header"), "Temporal Stability"), ImGuiTreeNodeFlags_Leaf)) {
					static const char* temporalReuseModes[] = { "Off", "Every 2nd frame", "Every 3rd frame", "Every 4th frame" };
					static constexpr uint temporalReuseValues[] = { 0u, 2u, 3u, 4u };
					int temporalReuseMode = 0;
					for (int index = 0; index < IM_ARRAYSIZE(temporalReuseValues); ++index) {
						if (settings.neuralRenderingTemporalReuseCadence == temporalReuseValues[index]) {
							temporalReuseMode = index;
							break;
						}
					}
					if (ImGui::Combo(SettingsLabel("nr_temporal_mode"),
						&temporalReuseMode, temporalReuseModes, IM_ARRAYSIZE(temporalReuseModes)))
						settings.neuralRenderingTemporalReuseCadence = temporalReuseValues[temporalReuseMode];
					if (auto _tt = Util::HoverTooltipWrapper())
						drawWrapped(SettingsTooltip("nr_temporal_mode"));
					if (settings.neuralRenderingTemporalReuseCadence != 0) {
						ImGui::SliderFloat(SettingsLabel("nr_temporal_depth"),
							&settings.neuralRenderingTemporalDepthThreshold, 0.0f, 0.25f, "%.3f");
						if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_temporal_depth"));
						ImGui::SliderFloat(SettingsLabel("nr_temporal_color"),
							&settings.neuralRenderingTemporalColorTolerance, 0.0f, 0.50f, "%.3f");
						if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_temporal_color"));
						if (settings.neuralRenderingTemporalReuseCadence == 2) {
							ImGui::Checkbox(SettingsLabel("nr_temporal_stagger"),
								&settings.neuralRenderingTemporalReuseStaggerEyes);
							if (auto _tt = Util::HoverTooltipWrapper())
								drawWrapped(SettingsTooltip("nr_temporal_stagger"));
						}
						ImGui::Checkbox(SettingsLabel("nr_temporal_reset"),
							&settings.neuralRenderingTemporalReuseResetAfterSkip);
						if (auto _tt = Util::HoverTooltipWrapper())
							drawWrapped(SettingsTooltip("nr_temporal_reset"));
						drawWarningWrapped(T(TKEY("neural_rendering_temporal_warning"), "Experimental. Watch for ghosting, cadence shimmer, and disocclusion errors."));
					}

				}
			}
			if constexpr (NeuralRendering::kFullResolutionNeuralRenderingOnly) {
				if (ImGui::CollapsingHeader(SettingsLabel("nr_adaptive"), ImGuiTreeNodeFlags_Leaf)) {
					ImGui::TextWrapped("%s", FindSettingsText("nr_adaptive").purpose);
					if (ImGui::Button(SettingsLabel("nr_reset"))) ResetAdaptiveSettings(settings);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_reset"));
					ImGui::TextDisabled("%s", SettingsLabel("nr_adaptive_unavailable"));
				}
			} else if (ImGui::CollapsingHeader(SettingsLabel("nr_adaptive"), ImGuiTreeNodeFlags_Leaf)) {
				ImGui::TextWrapped("%s", FindSettingsText("nr_adaptive").purpose);
				if (ImGui::Button(SettingsLabel("nr_reset"))) ResetAdaptiveSettings(settings);
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_reset"));
				ImGui::SeparatorText("Adaptive Neural Rendering");
				ImGui::Checkbox(SettingsLabel("nr_adaptive_enable"), &settings.neuralRenderingAdaptiveEnabled);
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_enable"));
				if (settings.neuralRenderingAdaptiveEnabled) {
					static constexpr uint adaptiveRefreshValues[] = { 70u, 72u, 80u, 90u };
					static const char* adaptiveRefreshRates[] = {
						"70 Hz | 35 FPS budget", "72 Hz | 36 FPS budget", "80 Hz | 40 FPS budget", "90 Hz | 45 FPS budget" };
					int refreshIndex = 2;
					for (int index = 0; index < IM_ARRAYSIZE(adaptiveRefreshValues); ++index)
						if (settings.neuralRenderingAdaptiveRefreshHz == adaptiveRefreshValues[index]) {
							refreshIndex = index;
							break;
						}

					bool customFpsEnabled = settings.neuralRenderingAdaptiveTargetFps != 0;
					if (ImGui::Checkbox(SettingsLabel("nr_adaptive_custom_fps"), &customFpsEnabled))
						settings.neuralRenderingAdaptiveTargetFps = customFpsEnabled ? 40u : 0u;
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_custom_fps"));
					int targetFps = customFpsEnabled ? static_cast<int>(std::clamp(settings.neuralRenderingAdaptiveTargetFps, 15u, 60u)) : 40;
					ImGui::BeginDisabled(!customFpsEnabled);
					if (ImGui::SliderInt(SettingsLabel("nr_adaptive_fps"), &targetFps, 15, 60, "%d FPS"))
						settings.neuralRenderingAdaptiveTargetFps = static_cast<uint>(targetFps);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_fps"));
					ImGui::EndDisabled();

					ImGui::TextDisabled("Headset refresh target");
					ImGui::BeginDisabled(customFpsEnabled);
					if (ImGui::BeginTable("##neural_rendering_adaptive_refresh", IM_ARRAYSIZE(adaptiveRefreshValues),
							ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings)) {
						for (int index = 0; index < IM_ARRAYSIZE(adaptiveRefreshValues); ++index) {
							ImGui::TableNextColumn();
							const bool selected = index == refreshIndex;
							if (selected)
								ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
							if (ImGui::Button(adaptiveRefreshRates[index], ImVec2(-1.0f, 32.0f * Util::GetUIScale())))
								settings.neuralRenderingAdaptiveRefreshHz = adaptiveRefreshValues[index];
							if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_refresh"));
							if (selected)
								ImGui::PopStyleColor();
						}
						ImGui::EndTable();
					}
					ImGui::EndDisabled();
					drawDisabledWrapped(customFpsEnabled ? "Custom FPS mode is active; headset buttons are disabled." : "Hz mode uses half the selected headset refresh.");

					static const char* adaptiveMinimums[] = {
						"100% | 100% model area", "95% | 90% model area", "90% | 81% model area", "85% | 72% model area",
						"80% | 64% model area", "75% | 56% model area", "70% | 49% model area" };
					static constexpr uint adaptiveMinimumValues[] = { 100u, 95u, 90u, 85u, 80u, 75u, 70u };
					int minimumIndex = 6;
					for (int index = 0; index < IM_ARRAYSIZE(adaptiveMinimumValues); ++index)
						if (settings.neuralRenderingAdaptiveMinimumResolution == adaptiveMinimumValues[index]) {
							minimumIndex = index;
							break;
						}
					if (ImGui::Combo(SettingsLabel("nr_adaptive_min"), &minimumIndex, adaptiveMinimums, IM_ARRAYSIZE(adaptiveMinimums)))
						settings.neuralRenderingAdaptiveMinimumResolution = adaptiveMinimumValues[minimumIndex];
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_min"));

					int downshiftFrames = static_cast<int>(settings.neuralRenderingAdaptiveDownshiftFrames);
					if (ImGui::SliderInt(SettingsLabel("nr_adaptive_down"), &downshiftFrames, 1, 16, "%d budget frames"))
						settings.neuralRenderingAdaptiveDownshiftFrames = static_cast<uint>(downshiftFrames);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_down"));
					int upshiftFrames = static_cast<int>(settings.neuralRenderingAdaptiveUpshiftFrames);
					if (ImGui::SliderInt(SettingsLabel("nr_adaptive_up"), &upshiftFrames, 4, 64, "%d frames"))
						settings.neuralRenderingAdaptiveUpshiftFrames = static_cast<uint>(upshiftFrames);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_up"));
					int minimumDwellFrames = static_cast<int>(settings.neuralRenderingAdaptiveMinimumDwellFrames);
					if (ImGui::SliderInt(SettingsLabel("nr_adaptive_dwell"), &minimumDwellFrames, 4, 240, "%d frames"))
						settings.neuralRenderingAdaptiveMinimumDwellFrames = static_cast<uint>(minimumDwellFrames);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_dwell"));
					ImGui::SliderFloat(SettingsLabel("nr_adaptive_headroom"), &settings.neuralRenderingAdaptiveGuardTimeMs,
						0.0f, 5.0f, "%.1f ms");
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_headroom"));
					ImGui::TextDisabled("Target %.0f FPS | NR tier %u%% -> %u%% | frame %.2f / %.2f ms",
						adaptiveController.ApplicationTargetFps(),
						adaptiveController.ActiveResolution(), adaptiveController.TargetResolution(),
						adaptiveController.HandoffAlpha(), adaptiveController.SmoothedFrameTimeMs(),
						adaptiveController.ApplicationDeadlineMs());
					drawDisabledWrapped("Order: crop → NR → crop. If crop is unavailable, NR adapts alone.");
					drawWarningWrapped("Resource setup may hitch once.");
				}

				ImGui::SeparatorText("Adaptive Foveated Crop");
				const bool adaptiveCropParentEnabled = settings.neuralRenderingAdaptiveEnabled;
				if (!adaptiveCropParentEnabled)
					ImGui::BeginDisabled();
				ImGui::Checkbox(SettingsLabel("nr_adaptive_crop"), &settings.neuralRenderingAdaptiveCropEnabled);
				if (!adaptiveCropParentEnabled)
					ImGui::EndDisabled();
				if (auto _tt = Util::HoverTooltipWrapper())
					drawWrapped(SettingsTooltip("nr_adaptive_crop"));
				if (!adaptiveCropParentEnabled)
					ImGui::TextDisabled("Enable Adaptive Neural Rendering before enabling its crop companion.");
				if (settings.neuralRenderingAdaptiveCropEnabled && adaptiveCropParentEnabled) {
					static const char* adaptiveCropMaximums[] = { "85%", "80%", "75%", "70%", "65%", "60%" };
					static constexpr uint adaptiveCropMaximumValues[] = { 85u, 80u, 75u, 70u, 65u, 60u };
					int cropMaximumIndex = 0;
					for (int index = 0; index < IM_ARRAYSIZE(adaptiveCropMaximumValues); ++index)
						if (settings.neuralRenderingAdaptiveCropMaximumCoverage == adaptiveCropMaximumValues[index]) {
							cropMaximumIndex = index;
							break;
						}
					if (ImGui::Combo(SettingsLabel("nr_adaptive_crop_max"), &cropMaximumIndex,
						adaptiveCropMaximums, IM_ARRAYSIZE(adaptiveCropMaximums)))
						settings.neuralRenderingAdaptiveCropMaximumCoverage = adaptiveCropMaximumValues[cropMaximumIndex];
				if (auto _tt = Util::HoverTooltipWrapper())
					drawWrapped(SettingsTooltip("nr_adaptive_crop_max"));

					static const char* adaptiveCropMinimums[] = { "80%", "75%", "70%", "65%", "60%" };
					static constexpr uint adaptiveCropMinimumValues[] = { 80u, 75u, 70u, 65u, 60u };
					int cropMinimumIndex = IM_ARRAYSIZE(adaptiveCropMinimumValues) - 1;
					for (int index = 0; index < IM_ARRAYSIZE(adaptiveCropMinimumValues); ++index)
						if (settings.neuralRenderingAdaptiveCropMinimumCoverage == adaptiveCropMinimumValues[index]) {
							cropMinimumIndex = index;
							break;
						}
					if (ImGui::Combo(SettingsLabel("nr_adaptive_crop_min"), &cropMinimumIndex,
						adaptiveCropMinimums, IM_ARRAYSIZE(adaptiveCropMinimums)))
						settings.neuralRenderingAdaptiveCropMinimumCoverage = adaptiveCropMinimumValues[cropMinimumIndex];
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_crop_min"));

					int cropDownshiftFrames = static_cast<int>(settings.neuralRenderingAdaptiveCropDownshiftFrames);
					if (ImGui::SliderInt(SettingsLabel("nr_adaptive_crop_down"), &cropDownshiftFrames, 1, 16, "%d frames"))
						settings.neuralRenderingAdaptiveCropDownshiftFrames = static_cast<uint>(cropDownshiftFrames);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_crop_down"));
					int cropUpshiftFrames = static_cast<int>(settings.neuralRenderingAdaptiveCropUpshiftFrames);
					if (ImGui::SliderInt(SettingsLabel("nr_adaptive_crop_up"), &cropUpshiftFrames, 8, 240, "%d frames"))
						settings.neuralRenderingAdaptiveCropUpshiftFrames = static_cast<uint>(cropUpshiftFrames);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_crop_up"));
					int cropMinimumDwellFrames = static_cast<int>(settings.neuralRenderingAdaptiveCropMinimumDwellFrames);
					if (ImGui::SliderInt(SettingsLabel("nr_adaptive_crop_dwell"), &cropMinimumDwellFrames, 8, 600, "%d frames"))
						settings.neuralRenderingAdaptiveCropMinimumDwellFrames = static_cast<uint>(cropMinimumDwellFrames);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_crop_dwell"));
					int cropTransitionFrames = static_cast<int>(settings.neuralRenderingAdaptiveCropTransitionFrames);
					if (ImGui::SliderInt(SettingsLabel("nr_adaptive_crop_transition"), &cropTransitionFrames, 2, 24, "%d frames"))
						settings.neuralRenderingAdaptiveCropTransitionFrames = static_cast<uint>(cropTransitionFrames);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_crop_transition"));
					ImGui::TextDisabled("Crop: %u%% -> %u%% | max %u%% | handoff %.2f",
						adaptiveCropController.ActiveCoverage(), adaptiveCropController.TargetCoverage(),
						settings.neuralRenderingAdaptiveCropMaximumCoverage,
						adaptiveCropController.HandoffAlpha());
				}
				if (IsEyeTrackedFoveationEnabled())
					drawWarningWrapped("Adaptive crop is disabled while eye-tracked foveation owns the crop.");
				if (FoveatedRenderImpl::Core::vrSubrectFixedEnvelopeRejected ||
					FoveatedRenderImpl::Core::vrSubrectNeuralFixedEnvelopeRejected)
					drawWarningWrapped("Crop held: resource contract failed. Restart required to retry.");

				if (settings.neuralRenderingAdaptiveDiagnostics) {
					ImGui::SeparatorText("Handoff Diagnostics");
					const char* resourceMode = FoveatedRenderImpl::Core::vrSubrectResourceMode ==
						FoveatedRenderImpl::Core::SubrectResourceMode::FixedEnvelope ? "fixed envelope" : "exact cache";
					ImGui::TextDisabled("Resources: %s | valid %ux%u | envelope %ux%u",
						resourceMode, FoveatedRenderImpl::Core::vrSubrectValidOutW,
						FoveatedRenderImpl::Core::vrSubrectValidOutH, FoveatedRenderImpl::Core::vrSubrectOutW,
						FoveatedRenderImpl::Core::vrSubrectOutH);
					ImGui::TextDisabled("Creates %llu | reuses %llu | frees %llu",
						static_cast<unsigned long long>(FoveatedRenderImpl::Core::vrSubrectResourceCreates),
						static_cast<unsigned long long>(FoveatedRenderImpl::Core::vrSubrectResourceReuses),
						static_cast<unsigned long long>(FoveatedRenderImpl::Core::vrSubrectResourceFrees));
					ImGui::TextDisabled("Fallback entries %llu | evictions %llu | envelope checks %llu",
						static_cast<unsigned long long>(FoveatedRenderImpl::Core::vrSubrectFallbackEntries),
						static_cast<unsigned long long>(FoveatedRenderImpl::Core::vrSubrectFallbackEvictions),
						static_cast<unsigned long long>(FoveatedRenderImpl::Core::vrSubrectEnvelopeValidations));
					ImGui::TextDisabled("Crop reset: %s | generation %llu",
						NeuralRendering::AdaptiveCropController::ResetReasonName(adaptiveCropController.LastResetReason()),
						static_cast<unsigned long long>(adaptiveCropController.Generation()));
				}

			}
		}
		if (!supportedRoute && settings.neuralRenderingEnabled)
			ImGui::EndDisabled();
			if (ImGui::CollapsingHeader(SettingsLabel("nr_diagnostics"), ImGuiTreeNodeFlags_Leaf)) {
			ImGui::TextWrapped("%s", FindSettingsText("nr_diagnostics").purpose);







				{
					auto& upscalingSettings = globals::features::upscaling.settings;
					if (ImGui::Button(SettingsLabel("nr_reset")))
						ResetDiagnosticsSettings(settings, upscalingSettings);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_reset"));
					bool visualizeRegions = settings.debugVisualize != 0;
					if (ImGui::Checkbox(SettingsLabel("nr_overlay"), &visualizeRegions))
						settings.debugVisualize = visualizeRegions ? 1u : 0u;
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_overlay"));
					ImGui::Checkbox(SettingsLabel("nr_mapprobe"), &upscalingSettings.mapProbeDiagnostics);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_mapprobe"));
					bool adaptiveDiagnostics = settings.neuralRenderingAdaptiveDiagnostics;
					if (ImGui::Checkbox(SettingsLabel("nr_adaptive_diagnostics"), &adaptiveDiagnostics))
						settings.neuralRenderingAdaptiveDiagnostics = adaptiveDiagnostics;
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_adaptive_diagnostics"));
					ImGui::Checkbox(SettingsLabel("nr_parallel_eye_queue"), &settings.neuralRenderingParallelEyeQueue);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_parallel_eye_queue"));
					ImGui::Checkbox(SettingsLabel("nr_flush_after_signal"), &settings.neuralRenderingFlushAfterSignal);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_flush_after_signal"));
					ImGui::Checkbox(SettingsLabel("nr_prewarm"), &settings.neuralRenderingPrewarm);
					if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_prewarm"));
				}
				auto& neuralRenderer = NeuralRendering::Renderer::Instance();
				if (adaptiveController.MemoryCeiling() < 100)
					ImGui::TextDisabled("VRAM-limited NR ceiling: %u%%. Reset retries higher tiers.", adaptiveController.MemoryCeiling());
				if (neuralRenderer.IsFailureLatched() || neuralRenderer.IsRecoveryLimited() ||
					adaptiveController.MemoryCeiling() < 100 ||
					FoveatedRenderImpl::Core::vrSubrectFixedEnvelopeRejected ||
					FoveatedRenderImpl::Core::vrSubrectNeuralFixedEnvelopeRejected) {
					drawWarningWrapped("Neural rendering recovery or adaptive crop is limited after a failure. Reset to retry.");
				if (ImGui::Button(SettingsLabel("nr_reset_failure")))
					NeuralRendering::RequestReset();
				if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_reset_failure"));
			}
			if (globals::state && globals::state->IsDeveloperMode()) {
				ImGui::TextDisabled("Status: %s | NGX: 0x%08X | Evaluations: %llu",
					neuralRenderer.StatusText(), neuralRenderer.NgxResult(),
					static_cast<unsigned long long>(neuralRenderer.SuccessfulFrames()));
			}
		}
	}


	if (!vrControlsFirst)
		drawVrControls();

			if (ImGui::CollapsingHeader("Eye-tracked Foveation", ImGuiTreeNodeFlags_Leaf)) {
		bool eyeTrackedFoveation = settings.neuralRenderingEyeTrackedFoveation;
		if (ImGui::Checkbox(SettingsLabel("nr_gaze"), &eyeTrackedFoveation))
			settings.neuralRenderingEyeTrackedFoveation = eyeTrackedFoveation;
		if (auto _tt = Util::HoverTooltipWrapper())
			drawWrapped(SettingsTooltip("nr_gaze"));
		if (settings.neuralRenderingEyeTrackedFoveation) {
			ImGui::SliderFloat(SettingsLabel("nr_gaze_smoothing"), &settings.neuralRenderingEyeTrackedSmoothingMs,
				0.0f, 250.0f, "%.0f ms");
			if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_gaze_smoothing"));
			int quantizationPixels = static_cast<int>(std::min(settings.neuralRenderingEyeTrackedQuantizationPixels, 64u));
			if (ImGui::SliderInt(SettingsLabel("nr_gaze_quantization"), &quantizationPixels, 0, 64,
				quantizationPixels == 0 ? "Off" : "%d input px"))
				settings.neuralRenderingEyeTrackedQuantizationPixels = static_cast<uint>(std::clamp(quantizationPixels, 0, 64));
			if (auto _tt = Util::HoverTooltipWrapper()) drawWrapped(SettingsTooltip("nr_gaze_quantization"));
			if (!globals::game::isVR)
				drawWarningWrapped("Native OpenVR gaze is available only in VR.");
			else if (GetDlssMode() != DlssMode::kDefault)
				drawWarningWrapped("Native OpenVR gaze requires Foveated Default mode.");
			else if (subrectController.GetUV().IsFullEye() && subrectController.GetRightEyeUV().IsFullEye())
				drawWarningWrapped("Select a cropped eye region before enabling moving gaze.");
			if (settings.neuralRenderingTemporalReuseCadence != 0)
				drawWarningWrapped("Temporal residual reuse is disabled while this crop moves; use a fixed crop for the N2 test.");

			const auto gaze = FoveatedRenderImpl::NativeOpenVRGaze::GetDiagnostics();
			ImGui::TextDisabled("Provider: %s | API: %s | Focus: %s | Native sample: %s",
				FoveatedRenderImpl::NativeOpenVRGaze::StatusName(gaze.status), gaze.interfaceVersion.c_str(),
				gaze.focused ? "yes" : "no", gaze.nativeQueryValid ? "valid" : "invalid");
			ImGui::TextDisabled("Crop: %s | Fallback: %s | History reset: %s | Last valid query: %.1f ms",
				gaze.dynamic ? "dynamic" : "static", gaze.usingFallback ? "yes" : "no",
				gaze.historyReset ? "yes" : "no", gaze.sampleAgeMs);
			ImGui::TextDisabled("Query: %.3f ms | Crop changes: %llu | Resets: %llu",
				gaze.providerQueryMs, static_cast<unsigned long long>(gaze.cropChangeCount),
				static_cast<unsigned long long>(gaze.historyResetCount));
			ImGui::TextDisabled("Filtered gaze L=(%.3f, %.3f) R=(%.3f, %.3f) | sequence=%llu",
				gaze.filteredLeftUV[0], gaze.filteredLeftUV[1], gaze.filteredRightUV[0], gaze.filteredRightUV[1],
				static_cast<unsigned long long>(gaze.sampleSequence));
		}
	}
}

#undef I18N_KEY_PREFIX
