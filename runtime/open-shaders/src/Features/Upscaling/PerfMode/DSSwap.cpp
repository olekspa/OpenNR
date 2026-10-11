#include "../PerfMode.h"
#include "../../../Menu.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "../../../State.h"
#include "../../Upscaling.h"

// Quality mode -> render-scale resolution is supplied by the FFX SDK helper
// (same one Upscaling.cpp uses), avoiding a duplicate scale table here.
#include <FidelityFX/host/ffx_fsr3.h>

// ============================================================================
// PlayerViewRender_Hook: clear postChainDone at PlayerView end
// ============================================================================
// PlayerView covers the entire VR pipeline (World→Post→UI→Submit).
// After func() returns, clear postChainDone so the Present-前 UI chain
// and the next frame use normal VP compression.

void PerfMode::PlayerViewRender_Hook::thunk(void* a1, bool a2, bool a3)
{
	func(a1, a2, a3);

	globals::features::upscaling.perfMode.ClearPostChainDone();
}

// ============================================================================
// BSGraphics_SetDirtyStates_Hook
// ============================================================================
// Wraps DS swap around the engine's RT/DS flush so enlarged-RT draws don't
// rasterizer-clip to the smaller kMAIN DS bounds.
void PerfMode::BSGraphics_SetDirtyStates_Hook::thunk(bool isCompute)
{
	bool swapped = false;
	if (!isCompute)
		swapped = globals::features::upscaling.perfMode.MaybeSwapDSForEnlargedRT();
	func(isCompute);
	if (swapped)
		globals::features::upscaling.perfMode.RestoreSwappedDS();
}

// ============================================================================
// BSGraphics_Renderer_UpdateViewPort_Hook
// ============================================================================
// Post-corrects the engine viewport when the bound RT and the requested VP
// don't agree about render-vs-display extent. Was originally in Hooks.cpp.
void PerfMode::BSGraphics_Renderer_UpdateViewPort_Hook::thunk(RE::BSGraphics::Renderer* a_this, uint32_t a_width, uint32_t a_height, bool a_forceMatchRT)
{
	func(a_this, a_width, a_height, a_forceMatchRT);

	auto& perfMode = globals::features::upscaling.perfMode;
	if (perfMode.IsMapMenuDisplayResolutionActive()) {
		perfMode.ApplyMapMenuDisplayRasterState();
		return;
	}
	if (!perfMode.IsHookActive())
		return;

	// During Post intercept enlarged kTEMP/kTOTAL already get the right VP
	// from func() because of their inflated RT dims — don't second-guess it.
	if (perfMode.IsPostInterceptActive())
		return;

	auto* ss = globals::game::shadowState;
	if (!ss)
		return;
	auto& vp = ss->GetVRRuntimeData().viewPort;
	const uint32_t displayW = perfMode.GetDisplayEyeWidth() * 2;
	const uint32_t displayH = perfMode.GetDisplayEyeHeight();
	const uint32_t renderW = perfMode.GetRenderEyeWidth() * 2;
	const uint32_t renderH = perfMode.GetRenderEyeHeight();

	// After the Post chain, UI / submit-prep draws target enlarged kTOTAL
	// at displayRes — expand any renderRes VP the engine sets back up.
	// The fade Draw(30) bypasses this path entirely (direct D3D RSSet-
	// Viewports) and is handled by the Draw vfunc hook in Globals.cpp.
	if (perfMode.IsPostChainDone()) {
		if (static_cast<uint32_t>(vp.width) == renderW &&
			static_cast<uint32_t>(vp.height) == renderH) {
			vp.width = static_cast<float>(displayW);
			vp.height = static_cast<float>(displayH);
		}
		return;
	}

	// Honor forceMatchRT for displayRes-enlarged RTs — shrinking VP there
	// leaves menu content in a renderRes corner of kTOTAL.
	if (a_forceMatchRT)
		return;

	// Same risk on the non-forceMatchRT path: the menu compositor calls
	// UpdateViewPort(displayW, displayH, false) directly with screen-space
	// dims, and compressing those would clip the BG.
	{
		const uint32_t rtIdx = static_cast<uint32_t>(ss->GetVRRuntimeData().renderTargets[0]);
		if (rtIdx == RE::RENDER_TARGETS::kTOTAL ||
			rtIdx == RE::RENDER_TARGETS::kMENUBG ||
			rtIdx == RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY)
			return;
	}

	// Normal world/depth path: compress displayRes → renderRes so draws
	// stay inside the renderRes-sized kMAIN family.
	if (static_cast<uint32_t>(vp.width) == displayW &&
		static_cast<uint32_t>(vp.height) == displayH) {
		vp.width = static_cast<float>(renderW);
		vp.height = static_cast<float>(renderH);
	}
}

bool PerfMode::MaybeSwapDSForEnlargedRT()
{
	if (!hookActive || postInterceptActive || mapMenuDisplayResolutionActive)
		return false;
	if (autoSwapDSIdx != UINT32_MAX)
		return false;  // re-entry guard

	auto* ss = globals::game::shadowState;
	if (!ss)
		return false;
	auto& srd = ss->GetVRRuntimeData();

	// Only the three RTs PerfMode_MaybeEnlargeRT inflates to displayRes.
	const uint32_t rtIdx = static_cast<uint32_t>(srd.renderTargets[0]);
	if (rtIdx != RE::RENDER_TARGETS::kTOTAL &&
		rtIdx != RE::RENDER_TARGETS::kMENUBG &&
		rtIdx != RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY)
		return false;

	const uint32_t dsIdx = static_cast<uint32_t>(srd.depthStencil);
	if (dsIdx != RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN &&
		dsIdx != RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY)
		return false;

	auto renderer = globals::game::renderer;
	auto& dsData = renderer->GetDepthStencilData();
	auto& bound = dsData.depthStencils[dsIdx];

	// Unbind DS entirely rather than rebind to fakeDSV. fakeDSV is cleared
	// once at init with stencil=0; ISHDRTonemapBlendCinematic (the menu's
	// kMAIN→kTOTAL bridge at event 931 in the baseline capture) reads
	// stencil to mask sky vs. world, so a wrong stencil value discards every
	// pixel and the BG never reaches kTOTAL. Fullscreen IS shaders and the
	// menu UI quad don't actually depth-test, so nullptr DS is safe and
	// sidesteps the stencil-content mismatch. Swap pattern matches UIPass-
	// Dispatch_Hook (all 8 view slots).
	for (int i = 0; i < 8; ++i) {
		autoSwapSavedViews[i] = Util::AsReal(bound.views[i]);
		if (bound.views[i])
			bound.views[i] = nullptr;
	}
	for (int i = 0; i < 8; ++i) {
		autoSwapSavedReadOnlyViews[i] = Util::AsReal(bound.readOnlyViews[i]);
		if (bound.readOnlyViews[i])
			bound.readOnlyViews[i] = nullptr;
	}
	autoSwapDSIdx = dsIdx;
	return true;
}

void PerfMode::OnMapMenuStateChanged(bool a_isOpen)
{
	if (!a_isOpen) {
		mapMenuFakeDepthRetryAttempted = false;
		const bool wasActive = mapSymbolSupersampleActive;
		ReleaseMapSymbolSupersample("map-closed");
		if (!wasActive)
			logger::info("[PerfMode][MapSS] restored=true reason=map-closed (native target remained installed)");
		mapSymbolSupersampleOpenLogged = false;
		mapQualityOpenLogged = false;
		mapSymbolSupersampleFailureLatched = false;
		mapSymbolSupersampleFailureScale = 1.0f;
		mapSymbolHudNativeWidth = mapSymbolHudNativeHeight = 0;
		mapSymbolHudWidth = mapSymbolHudHeight = 0;
		return;
	}

	mapMenuFakeDepthRetryAttempted = false;
	mapQualityOpenLogged = false;
	mapSymbolHudNativeWidth = mapSymbolHudNativeHeight = 0;
	mapSymbolHudWidth = mapSymbolHudHeight = 0;
	mapSymbolSupersampleFailureLatched = false;
	mapSymbolSupersampleFailureScale = 1.0f;
	if (!globals::game::isVR || !globals::features::upscaling.settings.vrMapMenuDisplayResolution)
		return;
	auto* renderer = globals::game::renderer;
	auto* shadowState = globals::game::shadowState;
	auto* stateUpdateFlags = globals::game::stateUpdateFlags;
	logger::info(
		"[PerfMode][Map] open readiness hookActive={} fakeDSV={} renderer={} shadowState={} stateUpdateFlags={}",
		hookActive, static_cast<bool>(fakeDSV), renderer != nullptr, shadowState != nullptr, stateUpdateFlags != nullptr);
}

void PerfMode::LogMapQualityState(bool a_rcasApplied)
{
	if (!PerfModePolicy::ShouldLogMapQuality(globals::game::isVR,
		globals::state && globals::state->isMapMenuOpen, mapQualityOpenLogged))
		return;
	mapQualityOpenLogged = true;

	auto& upscaling = globals::features::upscaling;
	const auto* state = globals::state;
	const bool staticBackdrop = state->IsStaticMenuBackdropOpen(globals::game::ui);
	const bool active = PerfModePolicy::ShouldUseMapQualityOverride(globals::game::isVR,
		hookActive, true, staticBackdrop);
	const auto mapPreset = active ? PerfModePolicy::ClampMapDlssPreset(upscaling.settings.vrMapDlssPreset) : 0u;
	const char* presetName = mapPreset == 0 ? "unchanged" : mapPreset == 1 ? "J" : mapPreset == 2 ? "K" : mapPreset == 3 ? "L" : "M";
	const float sharpness = active ? PerfModePolicy::ClampMapSharpness(upscaling.settings.vrMapSharpness) : 0.0f;
	const float mipOffset = PerfModePolicy::ShouldApplyMapMipBias(globals::game::isVR, true) ?
		PerfModePolicy::ClampMapMipBiasOffset(upscaling.settings.vrMapMipBiasOffset) : 0.0f;
	auto sizeFor = [](const auto& a_entry) {
		uint32_t width = 0, height = 0;
		if (auto* texture = Util::AsReal(a_entry.texture)) {
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			width = desc.Width;
			height = desc.Height;
		}
		return std::pair{ width, height };
	};
	std::pair<uint32_t, uint32_t> mainSize{}, menuBGSize{}, totalSize{};
	if (globals::game::renderer) {
		auto& targets = globals::game::renderer->GetRuntimeData().renderTargets;
		mainSize = sizeFor(targets[RE::RENDER_TARGETS::kMAIN]);
		menuBGSize = sizeFor(targets[RE::RENDER_TARGETS::kMENUBG]);
		totalSize = sizeFor(targets[RE::RENDER_TARGETS::kTOTAL]);
	}
	const auto& stream = upscaling.streamline;
	const bool dlssResetKnown = stream.lastDLSSResetFrame == state->frameCount;
	const bool dlssReset = dlssResetKnown && stream.lastDLSSReset;
	const bool constantsAccepted = stream.lastDLSSConstantsFrame == state->frameCount && stream.lastDLSSConstantsAccepted;
	const bool upscaleObserved = stream.lastDLSSConstantsFrame == state->frameCount;
	logger::info("[PerfMode][MapQuality] dlssPreset={} globalPreset={} sharpness={:.2f} rcasApplied={} mipBiasOffset={:.2f} gameMipBias={:.3f} dlssReset={} resetKnown={} constantsAccepted={} upscaleObserved={} menuMVsValid={} kMAIN={}x{} kMENUBG={}x{} kTOTAL={}x{}",
		presetName, upscaling.settings.presetDLSS, sharpness, a_rcasApplied ? "true" : "false", mipOffset, state->lastGameMipBias,
		dlssReset ? "true" : "false", dlssResetKnown ? "true" : "false", constantsAccepted ? "true" : "false",
		upscaleObserved ? "true" : "false",
		upscaling.menuCameraMVsValid ? "true" : "false",
		mainSize.first, mainSize.second, menuBGSize.first, menuBGSize.second, totalSize.first, totalSize.second);
}

namespace
{
	std::uint32_t MapTextureBytesPerPixel(DXGI_FORMAT a_format)
	{
		switch (a_format) {
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_SINT:
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UINT:
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SNORM:
		case DXGI_FORMAT_R16G16_SINT:
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
			return 4;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SNORM:
		case DXGI_FORMAT_R16G16B16A16_SINT:
		case DXGI_FORMAT_R32G32_TYPELESS:
		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
		case DXGI_FORMAT_R32G32_SINT:
			return 8;
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			return 4;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return 8;
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_FLOAT:
			return 2;
		default:
			return 0;
		}
	}

	template <class T>
	ID3D11Texture2D* RealTexture(T a_texture)
	{
		return Util::AsReal(a_texture);
	}
}

bool PerfMode::EnsureMapSymbolSupersample()
{
	if (mapSymbolSupersampleActive)
		return true;
	if (!hookActive || !globals::game::isVR || !globals::state || !globals::state->isMapMenuOpen ||
		!globals::game::renderer || !globals::d3d::device || !globals::d3d::context ||
		!globals::game::stateUpdateFlags)
		return false;

	auto& renderer = *globals::game::renderer;
	auto& colorEntry = renderer.GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kHUDMENU];
	auto& depthEntry = renderer.GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kHUDMENU];
	auto* colorSource = RealTexture(colorEntry.texture);
	auto* depthSource = RealTexture(depthEntry.texture);
	if (!colorSource || !colorEntry.RTV || !colorEntry.SRV || colorEntry.textureCopy || colorEntry.UAV || !depthSource)
		return false;

	D3D11_TEXTURE2D_DESC colorDesc{};
	D3D11_TEXTURE2D_DESC depthDesc{};
	colorSource->GetDesc(&colorDesc);
	depthSource->GetDesc(&depthDesc);
	if (colorDesc.SampleDesc.Count != 1 || depthDesc.SampleDesc.Count != 1 ||
		colorDesc.ArraySize != 1 || depthDesc.ArraySize != 1 ||
		colorDesc.MipLevels != 1 || depthDesc.MipLevels != 1 ||
		colorDesc.Width != depthDesc.Width || colorDesc.Height != depthDesc.Height)
		return false;

	const float displayCoverageScale = PerfModePolicy::GetMapDisplayCoverageScale(
		colorDesc.Width, colorDesc.Height, displayEyeWidth, displayEyeHeight);
	const float requestedScale = PerfModePolicy::ClampMapSupersampleScale(
		globals::features::upscaling.settings.vrMapSymbolSupersample);
	const auto extent = PerfModePolicy::GetMapSupersampleExtent(colorDesc.Width, colorDesc.Height,
		requestedScale <= 1.0f ? 1.0f : std::max(requestedScale, displayCoverageScale),
		MapTextureBytesPerPixel(colorDesc.Format), MapTextureBytesPerPixel(depthDesc.Format));
	if (!extent.valid)
		return false;
	mapSymbolHudNativeWidth = colorDesc.Width;
	mapSymbolHudNativeHeight = colorDesc.Height;
	mapSymbolHudWidth = extent.width;
	mapSymbolHudHeight = extent.height;

	D3D11_TEXTURE2D_DESC newColorDesc = colorDesc;
	newColorDesc.Width = extent.width;
	newColorDesc.Height = extent.height;
	D3D11_TEXTURE2D_DESC newDepthDesc = depthDesc;
	newDepthDesc.Width = extent.width;
	newDepthDesc.Height = extent.height;

	winrt::com_ptr<ID3D11Texture2D> newColor;
	winrt::com_ptr<ID3D11RenderTargetView> newRTV;
	winrt::com_ptr<ID3D11ShaderResourceView> newSRV;
	winrt::com_ptr<ID3D11Texture2D> newDepth;
	winrt::com_ptr<ID3D11DepthStencilView> newDepthViews[8];
	winrt::com_ptr<ID3D11DepthStencilView> newReadOnlyDepthViews[8];
	winrt::com_ptr<ID3D11ShaderResourceView> newDepthSRV;
	winrt::com_ptr<ID3D11ShaderResourceView> newStencilSRV;
	if (FAILED(globals::d3d::device->CreateTexture2D(&newColorDesc, nullptr, newColor.put())))
		return false;
	D3D11_RENDER_TARGET_VIEW_DESC colorRTVDesc{};
	Util::AsReal(colorEntry.RTV)->GetDesc(&colorRTVDesc);
	if (FAILED(globals::d3d::device->CreateRenderTargetView(newColor.get(), &colorRTVDesc, newRTV.put())))
		return false;
	D3D11_SHADER_RESOURCE_VIEW_DESC colorSRVDesc{};
	Util::AsReal(colorEntry.SRV)->GetDesc(&colorSRVDesc);
	if (FAILED(globals::d3d::device->CreateShaderResourceView(newColor.get(), &colorSRVDesc, newSRV.put())))
		return false;
	if (FAILED(globals::d3d::device->CreateTexture2D(&newDepthDesc, nullptr, newDepth.put())))
		return false;
	auto createDepthView = [&](ID3D11DepthStencilView* a_source, auto& a_destination) {
		if (!a_source)
			return true;
		D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
		a_source->GetDesc(&viewDesc);
		return SUCCEEDED(globals::d3d::device->CreateDepthStencilView(newDepth.get(), &viewDesc, a_destination.put()));
	};
	for (std::size_t i = 0; i < 8; ++i) {
		if (!createDepthView(Util::AsReal(depthEntry.views[i]), newDepthViews[i]) ||
			!createDepthView(Util::AsReal(depthEntry.readOnlyViews[i]), newReadOnlyDepthViews[i]))
			return false;
	}
	auto createDepthSRV = [&](ID3D11ShaderResourceView* a_source, auto& a_destination) {
		if (!a_source)
			return true;
		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
		a_source->GetDesc(&viewDesc);
		return SUCCEEDED(globals::d3d::device->CreateShaderResourceView(newDepth.get(), &viewDesc, a_destination.put()));
	};
	if (!createDepthSRV(Util::AsReal(depthEntry.depthSRV), newDepthSRV) ||
		!createDepthSRV(Util::AsReal(depthEntry.stencilSRV), newStencilSRV))
		return false;

	mapSymbolSavedColorTexture = colorSource;
	mapSymbolSavedColorTextureCopy = RealTexture(colorEntry.textureCopy);
	mapSymbolSavedColorRTV = Util::AsReal(colorEntry.RTV);
	mapSymbolSavedColorSRV = Util::AsReal(colorEntry.SRV);
	mapSymbolSavedColorSRVCopy = Util::AsReal(colorEntry.SRVCopy);
	mapSymbolSavedColorUAV = Util::AsReal(colorEntry.UAV);
	mapSymbolSavedDepthTexture = depthSource;
	for (std::size_t i = 0; i < 8; ++i) {
		mapSymbolSavedDepthViews[i] = Util::AsReal(depthEntry.views[i]);
		mapSymbolSavedReadOnlyDepthViews[i] = Util::AsReal(depthEntry.readOnlyViews[i]);
	}
	mapSymbolSavedDepthSRV = Util::AsReal(depthEntry.depthSRV);
	mapSymbolSavedStencilSRV = Util::AsReal(depthEntry.stencilSRV);
	mapSymbolHudColor = std::move(newColor);
	mapSymbolHudRTV = std::move(newRTV);
	mapSymbolHudSRV = std::move(newSRV);
	mapSymbolHudDepth = std::move(newDepth);
	for (std::size_t i = 0; i < 8; ++i) {
		mapSymbolHudDepthViews[i] = std::move(newDepthViews[i]);
		mapSymbolHudReadOnlyDepthViews[i] = std::move(newReadOnlyDepthViews[i]);
	}
	mapSymbolHudDepthSRV = std::move(newDepthSRV);
	mapSymbolHudStencilSRV = std::move(newStencilSRV);
	mapSymbolHudNativeWidth = colorDesc.Width;
	mapSymbolHudNativeHeight = colorDesc.Height;
	mapSymbolHudWidth = extent.width;
	mapSymbolHudHeight = extent.height;
	mapSymbolSupersampleActiveScale = std::max(extent.scaleX, extent.scaleY);
	colorEntry.texture = Util::AsW32(mapSymbolHudColor.get());
	colorEntry.textureCopy = nullptr;
	colorEntry.RTV = Util::AsW32(mapSymbolHudRTV.get());
	colorEntry.SRV = Util::AsW32(mapSymbolHudSRV.get());
	colorEntry.SRVCopy = nullptr;
	colorEntry.UAV = nullptr;
	depthEntry.texture = Util::AsW32(mapSymbolHudDepth.get());
	for (std::size_t i = 0; i < 8; ++i) {
		depthEntry.views[i] = Util::AsW32(mapSymbolHudDepthViews[i].get());
		depthEntry.readOnlyViews[i] = Util::AsW32(mapSymbolHudReadOnlyDepthViews[i].get());
	}
	depthEntry.depthSRV = Util::AsW32(mapSymbolHudDepthSRV.get());
	depthEntry.stencilSRV = Util::AsW32(mapSymbolHudStencilSRV.get());
	static constexpr float transparent[4]{};
	globals::d3d::context->ClearRenderTargetView(mapSymbolHudRTV.get(), transparent);
	for (const auto& depthView : mapSymbolHudDepthViews) {
		if (depthView) {
			D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
			depthView->GetDesc(&viewDesc);
			const bool hasStencil = viewDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
				viewDesc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
			const UINT clearFlags = D3D11_CLEAR_DEPTH | (hasStencil ? D3D11_CLEAR_STENCIL : 0u);
			globals::d3d::context->ClearDepthStencilView(depthView.get(), clearFlags, 1.0f, 0);
			break;
		}
	}
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_VIEWPORT);
	mapSymbolSupersampleActive = true;
	return true;
}

void PerfMode::ReleaseMapSymbolSupersample(const char* a_reason)
{
	if (!mapSymbolSupersampleActive)
		return;
	if (globals::game::renderer) {
		auto& colorEntry = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kHUDMENU];
		auto& depthEntry = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kHUDMENU];
		if (RealTexture(colorEntry.texture) == mapSymbolHudColor.get()) {
			colorEntry.texture = Util::AsW32(mapSymbolSavedColorTexture);
			colorEntry.textureCopy = Util::AsW32(mapSymbolSavedColorTextureCopy);
			colorEntry.RTV = Util::AsW32(mapSymbolSavedColorRTV);
			colorEntry.SRV = Util::AsW32(mapSymbolSavedColorSRV);
			colorEntry.SRVCopy = Util::AsW32(mapSymbolSavedColorSRVCopy);
			colorEntry.UAV = Util::AsW32(mapSymbolSavedColorUAV);
		}
		if (RealTexture(depthEntry.texture) == mapSymbolHudDepth.get()) {
			depthEntry.texture = Util::AsW32(mapSymbolSavedDepthTexture);
			for (std::size_t i = 0; i < 8; ++i) {
				depthEntry.views[i] = Util::AsW32(mapSymbolSavedDepthViews[i]);
				depthEntry.readOnlyViews[i] = Util::AsW32(mapSymbolSavedReadOnlyDepthViews[i]);
			}
			depthEntry.depthSRV = Util::AsW32(mapSymbolSavedDepthSRV);
			depthEntry.stencilSRV = Util::AsW32(mapSymbolSavedStencilSRV);
		}
		if (globals::game::stateUpdateFlags) {
			globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
			globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_VIEWPORT);
		}
	}
	logger::info("[PerfMode][MapSS] restored=true reason={}", a_reason ? a_reason : "unspecified");
	mapSymbolSupersampleActive = false;
	mapSymbolHudColor = nullptr;
	mapSymbolHudRTV = nullptr;
	mapSymbolHudSRV = nullptr;
	mapSymbolHudDepth = nullptr;
	mapSymbolHudDepthSRV = nullptr;
	mapSymbolHudStencilSRV = nullptr;
	for (std::size_t i = 0; i < 8; ++i) {
		mapSymbolHudDepthViews[i] = nullptr;
		mapSymbolHudReadOnlyDepthViews[i] = nullptr;
		mapSymbolSavedDepthViews[i] = nullptr;
		mapSymbolSavedReadOnlyDepthViews[i] = nullptr;
	}
	mapSymbolSavedColorTexture = nullptr;
	mapSymbolSavedColorTextureCopy = nullptr;
	mapSymbolSavedColorRTV = nullptr;
	mapSymbolSavedColorSRV = nullptr;
	mapSymbolSavedColorSRVCopy = nullptr;
	mapSymbolSavedColorUAV = nullptr;
	mapSymbolSavedDepthTexture = nullptr;
	mapSymbolSavedDepthSRV = nullptr;
	mapSymbolSavedStencilSRV = nullptr;
}

void PerfMode::MapMenuPostDisplay_Hook::thunk(RE::MapMenu* a_menu)
{
	auto& perfMode = globals::features::upscaling.perfMode;
	const float setting = PerfModePolicy::ClampMapSupersampleScale(globals::features::upscaling.settings.vrMapSymbolSupersample);
	const bool mapOpen = globals::state && globals::state->isMapMenuOpen;
	if (setting <= 1.0f)
		perfMode.mapSymbolSupersampleFailureLatched = false;
	if (PerfModePolicy::ShouldRestoreMapSupersample(perfMode.mapSymbolSupersampleActive, mapOpen,
		perfMode.hookActive, globals::game::isVR, setting > 1.0f))
		perfMode.ReleaseMapSymbolSupersample("runtime-gate-lost");
	const bool eligible = a_menu && globals::game::isVR && globals::state && globals::state->isMapMenuOpen &&
		perfMode.hookActive && setting > 1.0f;
	bool viewportScaled = false;
	const char* failOpen = "none";
	if (eligible && !perfMode.mapSymbolSupersampleActive &&
		(!perfMode.mapSymbolSupersampleFailureLatched || setting != perfMode.mapSymbolSupersampleFailureScale)) {
		perfMode.mapSymbolSupersampleFailureLatched = false;
		if (!perfMode.EnsureMapSymbolSupersample()) {
			perfMode.mapSymbolSupersampleFailureLatched = true;
			perfMode.mapSymbolSupersampleFailureScale = setting;
			failOpen = "allocation-or-view-contract";
		}
	} else if (eligible && perfMode.mapSymbolSupersampleFailureLatched) {
		failOpen = "allocation-or-view-contract";
	} else if (!eligible) {
		failOpen = setting <= 1.0f ? "setting-disabled" : !globals::game::isVR ? "not-vr" : "perfmode-inactive";
	}

	RE::GViewport nativeViewport{};
	const SKSE::stl::scope_exit restoreViewport([&]() noexcept {
		if (viewportScaled && a_menu && a_menu->uiMovie)
			a_menu->uiMovie->SetViewport(nativeViewport);
	});
	if (eligible && perfMode.mapSymbolSupersampleActive && a_menu->uiMovie) {
		a_menu->uiMovie->GetViewport(&nativeViewport);
		if (nativeViewport.bufferWidth == static_cast<std::int32_t>(perfMode.mapSymbolHudNativeWidth) &&
			nativeViewport.bufferHeight == static_cast<std::int32_t>(perfMode.mapSymbolHudNativeHeight) &&
			nativeViewport.width > 0 && nativeViewport.height > 0) {
			auto scaled = nativeViewport;
			auto scale = [](std::int32_t value, float factor) { return static_cast<std::int32_t>(std::lround(static_cast<double>(value) * factor)); };
			const float scaleX = static_cast<float>(perfMode.mapSymbolHudWidth) / perfMode.mapSymbolHudNativeWidth;
			const float scaleY = static_cast<float>(perfMode.mapSymbolHudHeight) / perfMode.mapSymbolHudNativeHeight;
			scaled.bufferWidth = scale(nativeViewport.bufferWidth, scaleX);
			scaled.bufferHeight = scale(nativeViewport.bufferHeight, scaleY);
			scaled.left = scale(nativeViewport.left, scaleX);
			scaled.top = scale(nativeViewport.top, scaleY);
			scaled.width = scale(nativeViewport.width, scaleX);
			scaled.height = scale(nativeViewport.height, scaleY);
			scaled.scissorLeft = scale(nativeViewport.scissorLeft, scaleX);
			scaled.scissorTop = scale(nativeViewport.scissorTop, scaleY);
			scaled.scissorWidth = scale(nativeViewport.scissorWidth, scaleX);
			scaled.scissorHeight = scale(nativeViewport.scissorHeight, scaleY);
			a_menu->uiMovie->SetViewport(scaled);
			viewportScaled = true;
		} else {
			failOpen = "scaleform-buffer-mismatch";
			perfMode.ReleaseMapSymbolSupersample("scaleform-buffer-mismatch");
		}
	} else if (eligible && perfMode.mapSymbolSupersampleActive) {
		failOpen = "scaleform-movie-unavailable";
		perfMode.ReleaseMapSymbolSupersample("scaleform-movie-unavailable");
	}
	if (!eligible || !viewportScaled) {
		if (globals::game::renderer) {
			auto* hud = Util::AsReal(globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kHUDMENU].texture);
			if (hud) {
				D3D11_TEXTURE2D_DESC desc{};
				hud->GetDesc(&desc);
				perfMode.mapSymbolHudNativeWidth = desc.Width;
				perfMode.mapSymbolHudNativeHeight = desc.Height;
				perfMode.mapSymbolHudWidth = desc.Width;
				perfMode.mapSymbolHudHeight = desc.Height;
			}
		}
	}
	if (!perfMode.mapSymbolSupersampleOpenLogged && globals::state && globals::state->isMapMenuOpen) {
		const float reportedScale = perfMode.mapSymbolSupersampleActive ? perfMode.mapSymbolSupersampleActiveScale : setting;
		logger::info("[PerfMode][MapSS] scale={:.2f} hud={}x{}->{}x{} viewportScaled={} failOpen={}",
			reportedScale, perfMode.mapSymbolHudNativeWidth, perfMode.mapSymbolHudNativeHeight,
			perfMode.mapSymbolHudWidth, perfMode.mapSymbolHudHeight, viewportScaled, failOpen);
		perfMode.mapSymbolSupersampleOpenLogged = true;
	}
	func(a_menu);
	// Guarantee a one-per-open quality diagnostic even if the one-shot menu
	// upscale exits before it can record the post-evaluation snapshot.
	if (mapOpen)
		perfMode.LogMapQualityState(false);
}

bool PerfMode::BeginMapMenuDisplayResolution(uint32_t* a_sourceRT)
{
	if (mapMenuDisplayResolutionActive || !globals::state)
		return false;
	if (a_sourceRT && globals::game::shadowState)
		*a_sourceRT = static_cast<uint32_t>(globals::game::shadowState->GetVRRuntimeData().renderTargets[0]);
	auto& upscaling = globals::features::upscaling;

	// SetupResources may run before kMAIN's depth texture is available. Recheck
	// once at the first map draw, then keep the normal prerequisite gate below.
	if (upscaling.settings.vrMapMenuDisplayResolution && globals::game::isVR && !mapMenuFakeDepthRetryAttempted) {
		mapMenuFakeDepthRetryAttempted = true;
		if (hookActive && !fakeDSV) {
			const bool created = EnsureFakeDSV();
			logger::info("[PerfMode][Map] first-frame fakeDSV retry attempted=true succeeded={}", created);
		}
	}

	const bool resourcesReady = PerfModePolicy::AreMapRoutePrerequisitesReady(
		hookActive, static_cast<bool>(fakeDSV), globals::game::renderer != nullptr,
		globals::game::shadowState != nullptr, globals::game::stateUpdateFlags != nullptr);
	if (!PerfModePolicy::ShouldRouteMapAtDisplayResolution(
		upscaling.settings.vrMapMenuDisplayResolution,
		globals::game::isVR,
		hookActive,
		globals::state->isMapMenuOpen,
		resourcesReady))
		return false;

	auto* shadowState = globals::game::shadowState;
	auto* renderer = globals::game::renderer;
	auto& runtime = shadowState->GetVRRuntimeData();
	const uint32_t sourceRT = static_cast<uint32_t>(runtime.renderTargets[0]);
	const uint32_t sourceDS = static_cast<uint32_t>(runtime.depthStencil);
	if (a_sourceRT)
		*a_sourceRT = sourceRT;

	const bool sourceDSIsMain = sourceDS == RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN;
	const bool sourceDSIsMainCopy = sourceDS == RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY;
	const bool depthSelectionPending = sourceDS == UINT32_MAX;
	if (!PerfModePolicy::CanRouteMapCopyWithDepthState(
		sourceDSIsMain, sourceDSIsMainCopy, depthSelectionPending)) {
		logger::info("[PerfMode][Map] route skipped unsupported depth index={}", sourceDS);
		return false;
	}
	auto& menuTarget = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMENUBG];
	if (!menuTarget.texture || !menuTarget.RTV)
		return false;

	mapMenuSavedDSValid = PerfModePolicy::ShouldSwapMapCopyDepthState(sourceDSIsMain, sourceDSIsMainCopy);
	mapMenuSavedRTValid = sourceRT != UINT32_MAX;
	if (mapMenuSavedDSValid) {
		auto& dsEntry = renderer->GetDepthStencilData().depthStencils[sourceDS];
		for (int i = 0; i < 8; ++i) {
			mapMenuSavedViews[i] = Util::AsReal(dsEntry.views[i]);
			mapMenuSavedReadOnlyViews[i] = Util::AsReal(dsEntry.readOnlyViews[i]);
		}
	}
	// If the copy render has not selected its shadow-state entries yet,
	// retain kMENUBG as the expected destination after the copy. The engine
	// selects its depth target later in the wrapped render; this fullscreen
	// color copy does not need depth testing while that selection is pending.
	mapMenuSavedRT = mapMenuSavedRTValid ? sourceRT : static_cast<uint32_t>(RE::RENDER_TARGETS::kMENUBG);
	mapMenuSavedDS = sourceDS;
	mapMenuSavedViewport = runtime.viewPort;
	if (globals::d3d::context) {
		mapMenuSavedViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		globals::d3d::context->RSGetViewports(&mapMenuSavedViewportCount, mapMenuSavedRasterViewports);
		mapMenuSavedScissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		globals::d3d::context->RSGetScissorRects(&mapMenuSavedScissorCount, mapMenuSavedScissors);
	} else {
		mapMenuSavedViewportCount = 0;
		mapMenuSavedScissorCount = 0;
	}

	if (mapMenuSavedDSValid) {
		auto& dsEntry = renderer->GetDepthStencilData().depthStencils[sourceDS];
		for (int i = 0; i < 8; ++i) {
			if (dsEntry.views[i])
				dsEntry.views[i] = Util::AsW32(fakeDSV.get());
			if (dsEntry.readOnlyViews[i])
				dsEntry.readOnlyViews[i] = Util::AsW32(fakeDSV.get());
		}
	}
	runtime.renderTargets[0] = RE::RENDER_TARGETS::kMENUBG;
	runtime.viewPort.topLeftX = 0.0f;
	runtime.viewPort.topLeftY = 0.0f;
	const auto displayExtent = PerfModePolicy::GetMapDisplayExtent(displayEyeWidth, displayEyeHeight);
	runtime.viewPort.width = static_cast<float>(displayExtent.compositeWidth);
	runtime.viewPort.height = static_cast<float>(displayExtent.compositeHeight);
	runtime.viewPort.minDepth = 0.0f;
	runtime.viewPort.maxDepth = 1.0f;
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_VIEWPORT);
	mapMenuDisplayResolutionActive = true;
	ApplyMapMenuDisplayRasterState();
	return true;
}

void PerfMode::ApplyMapMenuDisplayRasterState()
{
	if (!mapMenuDisplayResolutionActive || !globals::d3d::context)
		return;

	const auto displayExtent = PerfModePolicy::GetMapDisplayExtent(displayEyeWidth, displayEyeHeight);
	const float displayWidth = static_cast<float>(displayExtent.compositeWidth);
	const float displayHeight = static_cast<float>(displayExtent.compositeHeight);
	D3D11_VIEWPORT viewport{};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = displayWidth;
	viewport.Height = displayHeight;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	globals::d3d::context->RSSetViewports(1, &viewport);

	const D3D11_RECT scissor{ 0, 0, static_cast<LONG>(displayExtent.compositeWidth), static_cast<LONG>(displayExtent.compositeHeight) };
	globals::d3d::context->RSSetScissorRects(1, &scissor);

	if (globals::game::shadowState) {
		auto& runtime = globals::game::shadowState->GetVRRuntimeData();
		runtime.viewPort.topLeftX = 0.0f;
		runtime.viewPort.topLeftY = 0.0f;
		runtime.viewPort.width = displayWidth;
		runtime.viewPort.height = displayHeight;
		runtime.viewPort.minDepth = 0.0f;
		runtime.viewPort.maxDepth = 1.0f;
	}
}

void PerfMode::EndMapMenuDisplayResolution()
{
	if (!mapMenuDisplayResolutionActive)
		return;
	if (globals::d3d::context) {
		globals::d3d::context->RSSetViewports(mapMenuSavedViewportCount, mapMenuSavedRasterViewports);
		globals::d3d::context->RSSetScissorRects(mapMenuSavedScissorCount, mapMenuSavedScissors);
		mapMenuSavedViewportCount = 0;
		mapMenuSavedScissorCount = 0;
	}
	if (globals::game::renderer && globals::game::shadowState && globals::game::stateUpdateFlags) {
		if (mapMenuSavedDSValid) {
			auto& dsEntry = globals::game::renderer->GetDepthStencilData().depthStencils[mapMenuSavedDS];
			for (int i = 0; i < 8; ++i) {
				dsEntry.views[i] = Util::AsW32(mapMenuSavedViews[i]);
				dsEntry.readOnlyViews[i] = Util::AsW32(mapMenuSavedReadOnlyViews[i]);
				mapMenuSavedViews[i] = nullptr;
				mapMenuSavedReadOnlyViews[i] = nullptr;
			}
		}
		auto& runtime = globals::game::shadowState->GetVRRuntimeData();
		if (mapMenuSavedRTValid)
			runtime.renderTargets[0] = static_cast<RE::RENDER_TARGETS::RENDER_TARGET>(mapMenuSavedRT);
		else
			runtime.renderTargets[0] = RE::RENDER_TARGETS::kMENUBG;
		if (mapMenuSavedDSValid)
			runtime.depthStencil = static_cast<RE::RENDER_TARGETS_DEPTHSTENCIL::RENDER_TARGET_DEPTHSTENCIL>(mapMenuSavedDS);
		runtime.viewPort = mapMenuSavedViewport;
		globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
		globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_VIEWPORT);
	}
	mapMenuSavedRTValid = false;
	mapMenuSavedDSValid = false;
	mapMenuDisplayResolutionActive = false;
}

void PerfMode::RestoreSwappedDS()
{
	if (autoSwapDSIdx == UINT32_MAX)
		return;
	auto renderer = globals::game::renderer;
	auto& dsData = renderer->GetDepthStencilData();
	auto& bound = dsData.depthStencils[autoSwapDSIdx];
	for (int i = 0; i < 8; ++i)
		bound.views[i] = Util::AsW32(autoSwapSavedViews[i]);
	for (int i = 0; i < 8; ++i)
		bound.readOnlyViews[i] = Util::AsW32(autoSwapSavedReadOnlyViews[i]);
	autoSwapDSIdx = UINT32_MAX;
}

// ============================================================================
// CreateRenderTarget enlarge — install + per-site thunks
// ============================================================================
// Three specific call sites inside BSShaderRenderTargets::Create produce the
// displayRes-enlarged RTs (kMENUBG, kIMAGESPACE_TEMP_COPY, kTOTAL). Offsets
// identified in Ghidra (see CreateRT_k* labels inside the renamed
// BSShaderRenderTargets__Create function in SkyrimVR.exe). VR-only.

// ============================================================================
// ID3D11DeviceContext_Draw_Hook (vtable index 13)
// ============================================================================
// Engine fade-overlay Draw(30) fires after the Post chain and before Submit.
// Under PerfMode the draw's VP is computed at renderRes while the RT (kTOTAL)
// is displayRes — partial-screen "black stamp" without this swap. Gate on
// VertexCount==30 + isVR keeps the cost a single comparison on flat / non-
// fade draws.
void PerfMode::ID3D11DeviceContext_Draw_Hook::thunk(ID3D11DeviceContext* This, UINT VertexCount, UINT StartVertexLocation)
{
	if (VertexCount == 30 && globals::game::isVR) {
		auto& perfMode = globals::features::upscaling.perfMode;
		if (perfMode.IsHookActive() && perfMode.IsPostChainDone()) {
			UINT numVP = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
			D3D11_VIEWPORT savedVP[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
			This->RSGetViewports(&numVP, savedVP);

			D3D11_VIEWPORT vp{};
			vp.Width = static_cast<float>(perfMode.GetDisplayEyeWidth() * 2);
			vp.Height = static_cast<float>(perfMode.GetDisplayEyeHeight());
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			This->RSSetViewports(1, &vp);

			func(This, VertexCount, StartVertexLocation);

			if (numVP > 0)
				This->RSSetViewports(numVP, savedVP);
			return;
		}
	}
	func(This, VertexCount, StartVertexLocation);
}

void PerfMode::InstallFadeOverlayHook(ID3D11DeviceContext* context)
{
	if (!globals::game::isVR || !context)
		return;
	stl::detour_vfunc<13, ID3D11DeviceContext_Draw_Hook>(context);
}
