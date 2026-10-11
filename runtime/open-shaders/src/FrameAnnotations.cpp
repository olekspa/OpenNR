#include "FrameAnnotations.h"

#include "GpuPass.h"
#include "State.h"
#include "Util.h"
#include "Features/Upscaling.h"
#include "Features/Upscaling/PerfMode/MapProbeIndexPolicy.h"
#include "Features/Upscaling/PerfMode/MapMenuDisplayResolutionPolicy.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <Windows.h>

#pragma comment(lib, "dxguid.lib")

namespace FrameAnnotations
{
	namespace
	{
		struct MapProbeSession
		{
			bool active = false;
			std::uint32_t draws = 0;
			std::uint32_t loggedImageSpaceDraws = 0;
			std::uint32_t targetBinds = 0;
			std::uint32_t isMapDraws = 0;
			std::uint32_t isWorldMapDraws = 0;
			std::uint32_t isWorldMapNoSkyBlurDraws = 0;
			std::uint32_t isCopyDraws = 0;
			std::uint32_t routedDisplayResDraws = 0;
			bool invalidRtLogged = false;
			bool invalidDsLogged = false;
			bool drawFaultLogged = false;
			bool bindFaultLogged = false;
		};
		MapProbeSession mapProbe;
		bool mapResolutionLogged = false;
		bool mapRouteDenialLogged = false;

		std::pair<std::uint32_t, std::uint32_t> GetMapRenderTargetSize(std::uint32_t a_index)
		{
			auto* renderer = globals::game::renderer;
			if (!renderer || a_index >= std::size(renderer->GetRuntimeData().renderTargets))
				return {};
			auto& target = renderer->GetRuntimeData().renderTargets[a_index];
			if (!target.texture)
				return {};
			D3D11_TEXTURE2D_DESC desc{};
			Util::AsReal(target.texture)->GetDesc(&desc);
			return { desc.Width, desc.Height };
		}

		std::pair<std::uint32_t, std::uint32_t> GetMapFinalCompositeSize()
		{
			auto* renderer = globals::game::renderer;
			if (!renderer)
				return {};

			if (globals::game::isVR) {
				if (RE::RENDER_TARGETS::kVR_FRAMEBUFFER >= static_cast<std::uint32_t>(Util::GetRenderTargetCount()))
					return {};
				// The VR runtime appends its presented SBS target after the flat
				// render-target array; follow the same slot used by screenshot capture.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warray-bounds"
				auto& target = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kVR_FRAMEBUFFER];
#pragma clang diagnostic pop
				if (!target.texture)
					return {};
				D3D11_TEXTURE2D_DESC desc{};
				Util::AsReal(target.texture)->GetDesc(&desc);
				return { desc.Width, desc.Height };
			}

			return GetMapRenderTargetSize(RE::RENDER_TARGETS::kTOTAL);
		}

		std::pair<std::uint32_t, std::uint32_t> GetMapCopySourceSize()
		{
			if (!globals::d3d::context)
				return {};
			ID3D11ShaderResourceView* sourceView = nullptr;
			globals::d3d::context->PSGetShaderResources(0, 1, &sourceView);
			if (!sourceView)
				return {};
			ID3D11Resource* sourceResource = nullptr;
			sourceView->GetResource(&sourceResource);
			sourceView->Release();
			if (!sourceResource)
				return {};
			winrt::com_ptr<ID3D11Texture2D> sourceTexture;
			const HRESULT queryResult = sourceResource->QueryInterface(__uuidof(ID3D11Texture2D), sourceTexture.put_void());
			sourceResource->Release();
			if (FAILED(queryResult))
				return {};
			D3D11_TEXTURE2D_DESC desc{};
			sourceTexture->GetDesc(&desc);
			return { desc.Width, desc.Height };
		}

		void LogMapResolutionOnce(bool a_displayRouteApplied)
		{
			if (mapResolutionLogged)
				return;
			mapResolutionLogged = true;
			const auto mapSize = GetMapRenderTargetSize(RE::RENDER_TARGETS::kMENUBG);
			const auto sourceSize = GetMapCopySourceSize();
			const auto destinationSize = GetMapRenderTargetSize(RE::RENDER_TARGETS::kMENUBG);
			const auto compositeSize = GetMapFinalCompositeSize();
			const auto compositeTarget = globals::game::isVR ? "kVR_FRAMEBUFFER" : "kTOTAL";
			logger::info("[PerfMode][Map] size-chain mapTarget={}x{} ISCopySource={}x{} ISCopyDestination={}x{} displayRouteApplied={} finalCompositeTarget={} finalComposite={}x{}",
				mapSize.first, mapSize.second, sourceSize.first, sourceSize.second,
				destinationSize.first, destinationSize.second, a_displayRouteApplied,
				compositeTarget,
				compositeSize.first, compositeSize.second);
			spdlog::default_logger()->flush();
		}

		struct MapProbeRasterState
		{
			UINT viewportCount = 0;
			D3D11_VIEWPORT viewport{};
			UINT scissorCount = 0;
			D3D11_RECT scissor{};
		};

		MapProbeRasterState CaptureMapProbeRasterState()
		{
			MapProbeRasterState state{};
			if (!globals::d3d::context)
				return state;

			state.viewportCount = 1;
			globals::d3d::context->RSGetViewports(&state.viewportCount, &state.viewport);
			state.scissorCount = 1;
			globals::d3d::context->RSGetScissorRects(&state.scissorCount, &state.scissor);
			return state;
		}

		void FlushMapProbeLine()
		{
			spdlog::default_logger()->flush();
		}

		void LogMapProbeOpen(std::uint32_t frame)
		{
			const auto& upscaling = globals::features::upscaling;
			const auto& perfMode = upscaling.perfMode;
			logger::info("[MapProbe] open frame={} perfMode={} renderEye={}x{} displayEye={}x{} renderAtUpscaleRes={}",
				frame, perfMode.IsHookActive(), perfMode.GetRenderEyeWidth(), perfMode.GetRenderEyeHeight(),
				perfMode.GetDisplayEyeWidth(), perfMode.GetDisplayEyeHeight(), upscaling.settings.renderAtUpscaleRes);
			FlushMapProbeLine();
		}

		void LogInvalidMapProbeIndexOnce(bool& logged, const char* kind, std::uint64_t index, std::size_t bound)
		{
			if (logged)
				return;
			logged = true;
			logger::info("[MapProbe] invalid index kind={} value=0x{:X} bound={}; probe record skipped", kind, index, bound);
		}

		void LogMapProbeFaultOnce(bool& logged, const char* kind)
		{
			if (logged)
				return;
			logged = true;
			logger::error("[MapProbe] caught structured exception in {} probe; record skipped", kind);
			FlushMapProbeLine();
		}

		void LogMapProbeImageSpaceDrawUnsafe(std::uint16_t rawEffect, std::uint32_t frame, bool routedDisplayRes)
		{
			if (!mapProbe.active || !globals::state || !globals::state->isMapMenuOpen)
				return;

			++mapProbe.draws;
			if (mapProbe.loggedImageSpaceDraws >= 20)
				return;
			++mapProbe.loggedImageSpaceDraws;
			const auto effect = static_cast<RE::ImageSpaceManager::ImageSpaceEffectEnum>(rawEffect);
			const auto shaderName = RE::ImageSpaceManager::GetImageSpaceEffectName(effect);
			const auto raster = CaptureMapProbeRasterState();
			logger::info("[MapProbe] imagespace draw #{} frame={} shader={} effect={}({}) routedDisplayRes={} viewportCount={} viewport=({:.1f},{:.1f},{:.1f},{:.1f}) scissorCount={} scissor=({},{},{},{})",
				mapProbe.loggedImageSpaceDraws, frame, shaderName, rawEffect, magic_enum::enum_name(effect), routedDisplayRes,
				raster.viewportCount, raster.viewport.TopLeftX, raster.viewport.TopLeftY,
				raster.viewport.Width, raster.viewport.Height,
				raster.scissorCount, raster.scissor.left, raster.scissor.top, raster.scissor.right, raster.scissor.bottom);
			FlushMapProbeLine();

			const auto* renderer = globals::game::renderer;
			const auto* shadowState = globals::game::shadowState;
			const auto& perfMode = globals::features::upscaling.perfMode;
			std::uint64_t rtIndex = UINT64_MAX;
			std::uint64_t dsIndex = UINT64_MAX;
			std::uint32_t rtWidth = 0, rtHeight = 0, dsWidth = 0, dsHeight = 0;
			if (shadowState) {
				const auto& runtime = shadowState->GetVRRuntimeData();
				rtIndex = static_cast<std::uint64_t>(runtime.renderTargets[0]);
				dsIndex = static_cast<std::uint64_t>(runtime.depthStencil);
			}
			if (!renderer || !shadowState)
				return;
			auto& renderTargets = renderer->GetRuntimeData().renderTargets;
			auto& depthStencils = renderer->GetDepthStencilData().depthStencils;
			auto* target = MapProbeIndexPolicy::TryIndex(renderTargets, rtIndex);
			if (!target) {
				LogInvalidMapProbeIndexOnce(mapProbe.invalidRtLogged, "render-target", rtIndex, std::size(renderTargets));
				return;
			}
			auto* depth = MapProbeIndexPolicy::TryIndex(depthStencils, dsIndex);
			if (!depth) {
				LogInvalidMapProbeIndexOnce(mapProbe.invalidDsLogged, "depth-stencil", dsIndex, std::size(depthStencils));
				return;
			}
			using RenderTarget = RE::RENDER_TARGETS::RENDER_TARGET;
			using DepthStencil = RE::RENDER_TARGETS_DEPTHSTENCIL::RENDER_TARGET_DEPTHSTENCIL;
			const auto rtEnum = static_cast<RenderTarget>(rtIndex);
			const auto dsEnum = static_cast<DepthStencil>(dsIndex);
			if (target->texture) {
				D3D11_TEXTURE2D_DESC desc{};
				Util::AsReal(target->texture)->GetDesc(&desc);
				rtWidth = desc.Width;
				rtHeight = desc.Height;
			}
			{
				ID3D11View* view = routedDisplayRes ? static_cast<ID3D11View*>(perfMode.GetFakeDSV()) :
					static_cast<ID3D11View*>(Util::AsReal(depth->depthSRV));
				if (view) {
					winrt::com_ptr<ID3D11Resource> resource;
					view->GetResource(resource.put());
					winrt::com_ptr<ID3D11Texture2D> texture;
					if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), texture.put_void()))) {
						D3D11_TEXTURE2D_DESC desc{};
						texture->GetDesc(&desc);
						dsWidth = desc.Width;
						dsHeight = desc.Height;
					}
				}
			}

			const auto rtName = magic_enum::enum_name(rtEnum);
			const auto dsName = magic_enum::enum_name(dsEnum);
			const bool inEnlargeSet = rtEnum == RE::RENDER_TARGETS::kTOTAL ||
				rtEnum == RE::RENDER_TARGETS::kMENUBG || rtEnum == RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY;
			logger::info("[MapProbe] imagespace resources #{} frame={} rtIdx={}({}) rtSize={}x{} dsIdx={}({}) dsSize={}x{} inPerfModeEnlargeSet={} swapActive={}",
				mapProbe.loggedImageSpaceDraws,
				frame, rtIndex, rtName, rtWidth, rtHeight, dsIndex, dsName, dsWidth, dsHeight,
				inEnlargeSet, perfMode.IsSwapActive());
			FlushMapProbeLine();
		}

		void LogMapProbeImageSpaceDraw(std::uint16_t rawEffect, std::uint32_t frame, bool routedDisplayRes)
		{
			__try {
				LogMapProbeImageSpaceDrawUnsafe(rawEffect, frame, routedDisplayRes);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				LogMapProbeFaultOnce(mapProbe.drawFaultLogged, "imagespace-draw");
			}
		}

		void LogMapProbeRenderTargetBindUnsafe()
		{
			if (!mapProbe.active || !globals::state || !globals::state->isMapMenuOpen)
				return;
			++mapProbe.targetBinds;
			if (mapProbe.targetBinds > 20)
				return;
			const auto* shadowState = globals::game::shadowState;
			const auto* renderer = globals::game::renderer;
			if (!shadowState || !renderer)
				return;
			const auto& runtime = shadowState->GetVRRuntimeData();
			const auto& perfMode = globals::features::upscaling.perfMode;
			using RenderTarget = RE::RENDER_TARGETS::RENDER_TARGET;
			using DepthStencil = RE::RENDER_TARGETS_DEPTHSTENCIL::RENDER_TARGET_DEPTHSTENCIL;
			const auto rtIndex = static_cast<std::uint64_t>(runtime.renderTargets[0]);
			auto& renderTargets = renderer->GetRuntimeData().renderTargets;
			auto* target = MapProbeIndexPolicy::TryIndex(renderTargets, rtIndex);
			if (!target) {
				LogInvalidMapProbeIndexOnce(mapProbe.invalidRtLogged, "render-target", rtIndex, std::size(renderTargets));
				return;
			}
			const auto rtEnum = static_cast<RenderTarget>(rtIndex);
			if (rtEnum != RE::RENDER_TARGETS::kMENUBG && rtEnum != RE::RENDER_TARGETS::kTOTAL &&
				rtEnum != RE::RENDER_TARGETS::kPROJECTEDMENU)
				return;
			const auto dsIndex = static_cast<std::uint64_t>(runtime.depthStencil);
			const auto dsEnum = static_cast<DepthStencil>(dsIndex);
			auto& depthStencils = renderer->GetDepthStencilData().depthStencils;
			auto* depth = MapProbeIndexPolicy::TryIndex(depthStencils, dsIndex);
			if (!depth) {
				LogInvalidMapProbeIndexOnce(mapProbe.invalidDsLogged, "depth-stencil", dsIndex, std::size(depthStencils));
				return;
			}
			std::uint32_t rtWidth = 0, rtHeight = 0, dsWidth = 0, dsHeight = 0;
			if (target->texture) {
				D3D11_TEXTURE2D_DESC desc{};
				Util::AsReal(target->texture)->GetDesc(&desc);
				rtWidth = desc.Width;
				rtHeight = desc.Height;
			}
			{
				ID3D11View* depthView = perfMode.IsMapMenuDisplayResolutionActive() ?
					static_cast<ID3D11View*>(perfMode.GetFakeDSV()) :
					static_cast<ID3D11View*>(Util::AsReal(depth->depthSRV));
				if (depthView) {
					winrt::com_ptr<ID3D11Resource> resource;
					depthView->GetResource(resource.put());
					winrt::com_ptr<ID3D11Texture2D> texture;
					if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), texture.put_void()))) {
						D3D11_TEXTURE2D_DESC desc{};
						texture->GetDesc(&desc);
						dsWidth = desc.Width;
						dsHeight = desc.Height;
					}
				}
			}
			const auto raster = CaptureMapProbeRasterState();
			logger::info("[MapProbe] target bind #{} rt={}({}) size={}x{} ds={}({}) size={}x{} viewportCount={} viewport=({:.1f},{:.1f},{:.1f},{:.1f}) scissorCount={} scissor=({},{},{},{}) inPerfModeEnlargeSet={} swapActive={}",
				mapProbe.targetBinds, rtIndex, magic_enum::enum_name(rtEnum), rtWidth, rtHeight,
				dsIndex, magic_enum::enum_name(dsEnum), dsWidth, dsHeight,
				raster.viewportCount, raster.viewport.TopLeftX, raster.viewport.TopLeftY,
				raster.viewport.Width, raster.viewport.Height,
				raster.scissorCount, raster.scissor.left, raster.scissor.top, raster.scissor.right, raster.scissor.bottom,
				rtEnum == RE::RENDER_TARGETS::kMENUBG || rtEnum == RE::RENDER_TARGETS::kTOTAL,
				perfMode.IsSwapActive());
		}

		void LogMapProbeRenderTargetBind()
		{
			__try {
				LogMapProbeRenderTargetBindUnsafe();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				LogMapProbeFaultOnce(mapProbe.bindFaultLogged, "target-bind");
			}
		}

		// Cache the shader-defines suffix (a rarely-changing global) so the per-draw
		// annotation doesn't re-derive it every draw. Render-thread only, plain static.
		static std::string_view CachedDefinesSuffix()
		{
			thread_local std::string s_src;
			thread_local std::string s_suffix;
			const std::string& src = globals::state->shaderDefinesString;
			if (src != s_src) {
				s_src = src;
				s_suffix = Util::GetShaderDefinesSuffix(src);
			}
			return s_suffix;
		}

		static std::string BuildEventName(RE::ImageSpaceManager::ImageSpaceEffectEnum EffectType)
		{
			auto enumName = RE::ImageSpaceManager::GetImageSpaceEffectName(EffectType);

			if (globals::state && globals::state->IsDeveloperMode()) {
				uint16_t packed = static_cast<uint16_t>(EffectType);
				uint16_t se = RE::ImageSpaceManager::GetSEIndex(EffectType);
				uint16_t vr = RE::ImageSpaceManager::GetVRIndex(EffectType);
				std::string packedString = std::format(" (packed: 0x{:X}, SE: {}, VR: {})", packed, se, vr);
				return enumName + packedString;
			} else {
				return enumName;
			}
		}
	}

	void OnMapMenuState(bool open, std::uint32_t frame)
	{
		mapResolutionLogged = false;
		mapRouteDenialLogged = false;
		if (!globals::features::upscaling.settings.mapProbeDiagnostics) {
			mapProbe = {};
			return;
		}
		if (open && !mapProbe.active) {
			mapProbe = {};
			mapProbe.active = true;
			LogMapProbeOpen(frame);
		} else if (!open && mapProbe.active) {
			logger::info("[MapProbe] close frame={} draws={}", frame, mapProbe.draws);
			logger::info("[MapProbe] effect count shader=ISMap raw={} draws={}",
				static_cast<std::uint16_t>(RE::ImageSpaceManager::ISMap), mapProbe.isMapDraws);
			logger::info("[MapProbe] effect count shader=ISWorldMap raw={} draws={}",
				static_cast<std::uint16_t>(RE::ImageSpaceManager::ISWorldMap), mapProbe.isWorldMapDraws);
			logger::info("[MapProbe] effect count shader=ISWorldMapNoSkyBlur raw={} draws={}",
				static_cast<std::uint16_t>(RE::ImageSpaceManager::ISWorldMapNoSkyBlur), mapProbe.isWorldMapNoSkyBlurDraws);
			logger::info("[MapProbe] effect count shader=ISCopy raw={} draws={} routedDisplayRes={}",
				static_cast<std::uint16_t>(RE::ImageSpaceManager::ISCopy), mapProbe.isCopyDraws,
				mapProbe.routedDisplayResDraws);
			logger::info("[MapProbe] target binds logged={} total={}", std::min(mapProbe.targetBinds, 20u), mapProbe.targetBinds);
			FlushMapProbeLine();
			mapProbe = {};
		}
	}

	void OnMapShaderDraw(std::uint16_t effect, std::uint32_t frame, bool routedDisplayRes)
	{
		if (mapProbe.active && globals::state && globals::state->isMapMenuOpen) {
			switch (static_cast<RE::ImageSpaceManager::ImageSpaceEffectEnum>(effect)) {
			case RE::ImageSpaceManager::ISMap:
				++mapProbe.isMapDraws;
				break;
			case RE::ImageSpaceManager::ISWorldMap:
				++mapProbe.isWorldMapDraws;
				break;
			case RE::ImageSpaceManager::ISWorldMapNoSkyBlur:
				++mapProbe.isWorldMapNoSkyBlurDraws;
				break;
			case RE::ImageSpaceManager::ISCopy:
			case RE::ImageSpaceManager::ISCopyCustomViewport:
			case RE::ImageSpaceManager::ISCopyDynamicFetchDisabled:
				++mapProbe.isCopyDraws;
				if (routedDisplayRes)
					++mapProbe.routedDisplayResDraws;
				break;
			default:
				break;
			}
		}
		LogMapProbeImageSpaceDraw(effect, frame, routedDisplayRes);
	}

	void OnMapRenderTargetBind()
	{
		LogMapProbeRenderTargetBind();
	}

	template <RE::BSShader::Type ShaderType>
	struct BSShader_SetupGeometry
	{
		static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
		{
			if (globals::state->frameAnnotations && globals::state->IsPerfCaptureActive()) {
				uint32_t descriptor = 0;
				if (globals::game::currentPixelShader && *globals::game::currentPixelShader) {
					descriptor = (*globals::game::currentPixelShader)->id;
				}
				// Same annotation content as before, but built into reused
				// buffers with the invariant defines suffix cached, so a heavy
				// draw load doesn't pay a std::format + string alloc per draw.
				thread_local std::string s_diskPath;
				thread_local std::string s_passName;
				s_diskPath.clear();
				std::format_to(std::back_inserter(s_diskPath), "Data/ShaderCache/{}/{:X}{}.pso",
					shader->fxpFilename, descriptor, CachedDefinesSuffix());
				s_passName.clear();
				std::format_to(std::back_inserter(s_passName), "[{}:{:X}] ({:X}) <{}> {} -> {}",
					magic_enum::enum_name(ShaderType), descriptor, pass->passEnum,
					pass->accumulationHint, pass->geometry->name.c_str(), s_diskPath);
				globals::state->BeginDrawEvent(s_passName);
			}

			func(shader, pass, renderFlags);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	template <RE::BSShader::Type ShaderType>
	struct BSShader_RestoreGeometry
	{
		static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
		{
			func(shader, pass, renderFlags);

			if (globals::state->frameAnnotations && globals::state->IsPerfCaptureActive()) {
				globals::state->EndDrawEvent();
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	template <RE::ImageSpaceManager::ImageSpaceEffectEnum EffectType>
	struct BSImagespaceShader_Render
	{
		static void thunk(void* imageSpaceShader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
		{
			bool mapDisplayRoute = false;
			bool ownsMapDisplayRoute = false;
			bool logMapResolution = false;
			uint32_t sourceRT = UINT32_MAX;
			if constexpr (PerfModePolicy::IsMapCopyEffect(
				EffectType == RE::ImageSpaceManager::ISCopy,
				EffectType == RE::ImageSpaceManager::ISCopyCustomViewport,
				EffectType == RE::ImageSpaceManager::ISCopyDynamicFetchDisabled)) {
				if (globals::game::isVR && globals::state && globals::state->isMapMenuOpen) {
					auto& perfMode = globals::features::upscaling.perfMode;
					if constexpr (EffectType == RE::ImageSpaceManager::ISCopy)
						logMapResolution = true;
					const bool mapCopy = true;
					const bool isVR = globals::game::isVR;
					const bool mapOpen = globals::state->isMapMenuOpen;
					const bool routeReady = perfMode.IsHookActive();
					const bool routeCandidate = PerfModePolicy::ShouldRouteMapCopyAtDisplayResolution(
						mapCopy, isVR, mapOpen, routeReady);
					if (routeCandidate) {
						mapDisplayRoute = perfMode.IsMapMenuDisplayResolutionActive();
						if (!mapDisplayRoute) {
							mapDisplayRoute = perfMode.BeginMapMenuDisplayResolution(&sourceRT);
							ownsMapDisplayRoute = mapDisplayRoute;
						}
					}
					if constexpr (EffectType == RE::ImageSpaceManager::ISCopy) {
						if (!mapDisplayRoute && !mapRouteDenialLogged) {
							mapRouteDenialLogged = true;
							const bool settingEnabled = globals::features::upscaling.settings.vrMapMenuDisplayResolution;
							const bool fakeDSV = perfMode.GetFakeDSV() != nullptr;
							const bool renderer = globals::game::renderer != nullptr;
							const bool shadowState = globals::game::shadowState != nullptr;
							const bool stateUpdateFlags = globals::game::stateUpdateFlags != nullptr;
							const bool resourcesReady = PerfModePolicy::AreMapRoutePrerequisitesReady(
								perfMode.IsHookActive(), fakeDSV, renderer, shadowState, stateUpdateFlags);
							const char* reason = !settingEnabled ? "setting-disabled" : !isVR ? "not-vr-game" :
								!mapOpen ? "map-closed" : !routeReady ? "hook-inactive" :
								!resourcesReady ? "resources-not-ready" : "begin-route-failed";
							logger::info("[PerfMode][Map] first ISCopy route denied reason={} enabled={} isVR={} mapOpen={} hookActive={} fakeDSV={} renderer={} shadowState={} stateUpdateFlags={} resourcesReady={} menuPresentationIgnored=true",
								reason, settingEnabled, isVR, mapOpen, perfMode.IsHookActive(), fakeDSV, renderer,
								shadowState, stateUpdateFlags, resourcesReady);
						}
					}
				}
			}
			if (globals::state && globals::state->isMapMenuOpen)
				OnMapShaderDraw(static_cast<std::uint16_t>(EffectType), globals::state->frameCount, mapDisplayRoute);
			const bool annotate = globals::state->frameAnnotations;
			if (annotate)
				globals::state->BeginPerfEvent("{} Draw", BuildEventName(EffectType));

			func(imageSpaceShader, shape, param);

			// The renderer binds the copy destination inside func(). Log after
			// it returns so the size-chain samples the actual copy draw state.
			if (logMapResolution)
				LogMapResolutionOnce(mapDisplayRoute);

			if (ownsMapDisplayRoute)
				globals::features::upscaling.perfMode.EndMapMenuDisplayResolution();

			if (annotate)
				globals::state->EndPerfEvent();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	template <RE::ImageSpaceManager::ImageSpaceEffectEnum EffectType>
	struct BSImagespaceShader_Dispatch
	{
		static void thunk(void* imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3)
		{
			const bool annotate = globals::state->frameAnnotations;
			if (annotate)
				globals::state->BeginPerfEvent("{} Dispatch", BuildEventName(EffectType));

			func(imageSpaceShader, a1, a2, a3);

			if (annotate)
				globals::state->EndPerfEvent();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShaderAccumulator_FinishAccumulatingDispatch
	{
		static void thunk(RE::BSGraphics::BSShaderAccumulator* shaderAccumulator, uint32_t renderFlags)
		{
			const bool frameAnnotations = globals::state->frameAnnotations;
			if (frameAnnotations) {
				globals::state->BeginPerfEvent("BSShaderAccumulator::FinishAccumulatingDispatch [{}] <{}>",
					static_cast<uint32_t>(shaderAccumulator->GetRuntimeData().renderMode), renderFlags);
			}

			func(shaderAccumulator, renderFlags);

			if (frameAnnotations) {
				globals::state->EndPerfEvent();
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSCubeMapCamera_RenderCubemap
	{
		static void thunk(RE::NiAVObject* camera, int a2, bool a3, bool a4, bool a5)
		{
			const bool annotate = globals::state->frameAnnotations;
			if (annotate)
				globals::state->BeginPerfEvent("Cubemap {}", camera->name.c_str());

			func(camera, a2, a3, a4, a5);

			if (annotate)
				globals::state->EndPerfEvent();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShadowDirectionalLight_RenderShadowmaps
	{
		static void thunk(RE::BSShadowLight* light, void* a2)
		{
			const bool annotate = globals::state->frameAnnotations;
			if (annotate)
				globals::state->BeginPerfEvent("Directional Light Shadowmaps");

			func(light, a2);

			if (annotate)
				globals::state->EndPerfEvent();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShadowFrustumLight_RenderShadowmaps
	{
		static void thunk(RE::BSShadowLight* light, void* a2)
		{
			const bool annotate = globals::state->frameAnnotations;
			if (annotate)
				globals::state->BeginPerfEvent("Spot Light Shadowmaps");

			func(light, a2);

			if (annotate)
				globals::state->EndPerfEvent();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShadowParabolicLight_RenderShadowmaps
	{
		static void thunk(RE::BSShadowLight* light, void* a2)
		{
			const bool annotate = globals::state->frameAnnotations;
			if (annotate)
				globals::state->BeginPerfEvent("Omnidirectional Light Shadowmaps");

			func(light, a2);

			if (annotate)
				globals::state->EndPerfEvent();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSBatchRenderer_RenderBatches
	{
		static bool thunk(void* renderer, uint32_t* currentPass, uint32_t* bucketIndex,
			void* passIndexList,
			uint32_t renderFlags)
		{
			const bool frameAnnotations = globals::state->frameAnnotations;
			if (frameAnnotations) {
				globals::state->BeginPerfEvent("BSBatchRenderer::RenderBatches ({:X})[{}] <{}>", *currentPass, *bucketIndex,
					renderFlags);
			}

			const bool result = func(renderer, currentPass, bucketIndex, passIndexList, renderFlags);

			if (frameAnnotations) {
				globals::state->EndPerfEvent();
			}

			return result;
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderDepth
	{
		static void thunk(bool a1, bool a2)
		{
			globals::state->BeginPerfEvent("Depth");

			func(a1, a2);

			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderShadowmasks
	{
		static void thunk(bool a1)
		{
			globals::state->BeginPerfEvent("Shadowmasks");

			func(a1);

			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct VR_RenderDepth_DownscaleDepthBuffer
	{
		static void thunk(RE::BSSceneGraph* a1)
		{
			globals::state->BeginPerfEvent("DownscaleDepthBuffer");
			func(a1);
			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct VR_RenderDepth_BSOBBOcclusionTestingShader
	{
		static void thunk(RE::BSImagespaceShader* a_this, RE::ImageSpaceEffectParam* a_param)
		{
			globals::state->BeginPerfEvent("BSOBBOcclusionTestingShader");
			func(a_this, a_param);
			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct VR_UpscaleDepthBuffer
	{
		static void thunk(RE::ImageSpaceManager* a_this, unsigned int a2, RE::RENDER_TARGET a_target, RE::RENDER_TARGET a_target2, __int64 a5, bool a6)
		{
			globals::state->BeginPerfEvent("UpscaleDepthBuffer");
			func(a_this, a2, a_target, a_target2, a5, a6);
			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderWorld
	{
		static void thunk(bool a1)
		{
			globals::state->BeginPerfEvent("World");

			func(a1);

			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderFirstPersonView
	{
		static void thunk(bool a1, bool a2)
		{
			globals::state->BeginPerfEvent("First Person View");

			func(a1, a2);

			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderWaterEffects
	{
		static void thunk()
		{
			CS_GPU_PASS("Water::RenderWaterEffects");
			func();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderPlayerView
	{
		static void thunk(void* a1, bool a2, bool a3)
		{
			globals::state->BeginPerfEvent("Player View");

			func(a1, a2, a3);

			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShaderAccumulator_RenderEffects
	{
		static void thunk(void* accumulator, uint32_t renderFlags)
		{
			CS_GPU_PASS("Effects");
			func(accumulator, renderFlags);
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShaderAccumulator_RenderBatches
	{
		static void thunk(void* shaderAccumulator, uint32_t firstPass, uint32_t lastPass, uint32_t renderFlags, int groupIndex)
		{
			const bool frameAnnotations = globals::state->frameAnnotations;
			if (frameAnnotations) {
				globals::state->BeginPerfEvent("BSShaderAccumulator::RenderBatches ({:X}:{:X})[{}] <{}>", firstPass, lastPass, groupIndex,
					renderFlags);
			}

			func(shaderAccumulator, firstPass, lastPass, renderFlags, groupIndex);

			if (frameAnnotations) {
				globals::state->EndPerfEvent();
			}
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShaderAccumulator_RenderPersistentPassList
	{
		static void thunk(void* passList, uint32_t renderFlags)
		{
			const bool frameAnnotations = globals::state->frameAnnotations;
			if (frameAnnotations) {
				globals::state->BeginPerfEvent("BSShaderAccumulator::RenderPersistentPassList <{}>", renderFlags);
			}

			func(passList, renderFlags);

			if (frameAnnotations) {
				globals::state->EndPerfEvent();
			}
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct VolumetricLightingDescriptor_Render
	{
		static void thunk(void* a1, void* a2, bool a3)
		{
			globals::state->BeginPerfEvent("Volumetric Lighting");

			func(a1, a2, a3);

			globals::state->EndPerfEvent();
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void OnPostPostLoad()
	{
		stl::detour_thunk<BSShaderAccumulator_RenderEffects>(REL::RelocationID(99940, 106585));
		bool isMapHookInstalled = false;
		bool isWorldMapHookInstalled = false;
		bool isWorldMapNoSkyBlurHookInstalled = false;
		bool isCopyHookInstalled = false;
		if (globals::game::isVR) {
			// The map image is copied by ISCopy into kMENUBG. Keep this hook and
			// the map census hooks available when Frame Annotations is disabled.
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopy>>(
				RE::VTABLE_BSImagespaceShaderCopy[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopyDynamicFetchDisabled>>(
				RE::VTABLE_BSImagespaceShaderCopyDynamicFetchDisabled[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopyCustomViewport>>(
				RE::VTABLE_BSImagespaceShaderCopyCustomViewport[3]);
			isCopyHookInstalled = true;
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISMap>>(
				RE::VTABLE_BSImagespaceShaderMap[3]);
			isMapHookInstalled = true;
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWorldMap>>(
				RE::VTABLE_BSImagespaceShaderWorldMap[3]);
			isWorldMapHookInstalled = true;
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWorldMapNoSkyBlur>>(
				RE::VTABLE_BSImagespaceShaderWorldMapNoSkyBlur[3]);
			isWorldMapNoSkyBlurHookInstalled = true;
		}
		const auto VfuncMatches = [](std::uintptr_t vtableAddress, std::size_t slot, std::uintptr_t expected) {
			return *reinterpret_cast<const std::uintptr_t*>(vtableAddress + slot * sizeof(std::uintptr_t)) == expected;
		};
		isMapHookInstalled = isMapHookInstalled && VfuncMatches(RE::VTABLE_BSImagespaceShaderMap[3].address(), 1,
			stl::unrestricted_cast<std::uintptr_t>(&BSImagespaceShader_Render<RE::ImageSpaceManager::ISMap>::thunk));
		isWorldMapHookInstalled = isWorldMapHookInstalled && VfuncMatches(RE::VTABLE_BSImagespaceShaderWorldMap[3].address(), 1,
			stl::unrestricted_cast<std::uintptr_t>(&BSImagespaceShader_Render<RE::ImageSpaceManager::ISWorldMap>::thunk));
		isWorldMapNoSkyBlurHookInstalled = isWorldMapNoSkyBlurHookInstalled && VfuncMatches(
			RE::VTABLE_BSImagespaceShaderWorldMapNoSkyBlur[3].address(), 1,
			stl::unrestricted_cast<std::uintptr_t>(&BSImagespaceShader_Render<RE::ImageSpaceManager::ISWorldMapNoSkyBlur>::thunk));
		isCopyHookInstalled = isCopyHookInstalled && VfuncMatches(RE::VTABLE_BSImagespaceShaderCopy[3].address(), 1,
			stl::unrestricted_cast<std::uintptr_t>(&BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopy>::thunk));
		if (globals::features::upscaling.settings.mapProbeDiagnostics) {
			logger::info("[MapProbe] hooks installed: ISCopy={} ISMap={} ISWorldMap={} ISWorldMapNoSkyBlur={} frameAnnotations={}",
				isCopyHookInstalled, isMapHookInstalled, isWorldMapHookInstalled, isWorldMapNoSkyBlurHookInstalled,
				globals::state->frameAnnotations);
			FlushMapProbeLine();
		}

		if (!globals::state->frameAnnotations)
			return;

		stl::detour_thunk<Main_RenderShadowmasks>(REL::RelocationID(100422, 107140));

		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Lighting>>(
			RE::VTABLE_BSLightingShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Effect>>(
			RE::VTABLE_BSEffectShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Water>>(
			RE::VTABLE_BSWaterShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Utility>>(
			RE::VTABLE_BSUtilityShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Particle>>(
			RE::VTABLE_BSParticleShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Grass>>(
			RE::VTABLE_BSGrassShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::DistantTree>>(
			RE::VTABLE_BSDistantTreeShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::BloodSplatter>>(
			RE::VTABLE_BSBloodSplatterShader[0]);
		stl::write_vfunc<0x6, BSShader_SetupGeometry<RE::BSShader::Type::Sky>>(
			RE::VTABLE_BSSkyShader[0]);

		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::Lighting>>(
			RE::VTABLE_BSLightingShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::Effect>>(
			RE::VTABLE_BSEffectShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::Water>>(
			RE::VTABLE_BSWaterShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::Utility>>(
			RE::VTABLE_BSUtilityShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::Particle>>(
			RE::VTABLE_BSParticleShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::Grass>>(
			RE::VTABLE_BSGrassShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::DistantTree>>(
			RE::VTABLE_BSDistantTreeShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::BloodSplatter>>(
			RE::VTABLE_BSBloodSplatterShader[0]);
		stl::write_vfunc<0x7, BSShader_RestoreGeometry<RE::BSShader::Type::Sky>>(
			RE::VTABLE_BSSkyShader[0]);

		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISFXAA>>(
			RE::VTABLE_BSImagespaceShaderFXAA[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopyScaleBias>>(
			RE::VTABLE_BSImagespaceShaderCopyScaleBias[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopyGrayScale>>(
			RE::VTABLE_BSImagespaceShaderGreyScale[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISRefraction>>(
			RE::VTABLE_BSImagespaceShaderRefraction[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDoubleVision>>(
			RE::VTABLE_BSImagespaceShaderDoubleVision[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopyTextureMask>>(
			RE::VTABLE_BSImagespaceShaderTextureMask[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDepthOfField>>(
			RE::VTABLE_BSImagespaceShaderDepthOfField[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDepthOfFieldFogged>>(
			RE::VTABLE_BSImagespaceShaderDepthOfFieldFogged[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISDepthOfFieldMaskedFogged>>(
			RE::VTABLE_BSImagespaceShaderDepthOfFieldMaskedFogged[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDistantBlur>>(
			RE::VTABLE_BSImagespaceShaderDistantBlur[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDistantBlurFogged>>(
			RE::VTABLE_BSImagespaceShaderDistantBlurFogged[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISDistantBlurMaskedFogged>>(
			RE::VTABLE_BSImagespaceShaderDistantBlurMaskedFogged[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISRadialBlur>>(
			RE::VTABLE_BSImagespaceShaderRadialBlur[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISRadialBlurMedium>>(
			RE::VTABLE_BSImagespaceShaderRadialBlurMedium[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISRadialBlurHigh>>(
			RE::VTABLE_BSImagespaceShaderRadialBlurHigh[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRTonemapBlendCinematic>>(
			RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematic[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRTonemapBlendCinematicFade>>(
			RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematicFade[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample16>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample4>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample16Lum>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16Lum[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample4RGB2Lum>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4RGB2Lum[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample4LumClamp>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4LumClamp[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample4LightAdapt>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4LightAdapt[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample16LumClamp>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16LumClamp[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISHDRDownSample16LightAdapt>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16LightAdapt[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur3>>(
			RE::VTABLE_BSImagespaceShaderBlur3[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur5>>(
			RE::VTABLE_BSImagespaceShaderBlur5[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur7>>(
			RE::VTABLE_BSImagespaceShaderBlur7[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur9>>(
			RE::VTABLE_BSImagespaceShaderBlur9[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur11>>(
			RE::VTABLE_BSImagespaceShaderBlur11[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur13>>(
			RE::VTABLE_BSImagespaceShaderBlur13[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur15>>(
			RE::VTABLE_BSImagespaceShaderBlur15[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNonHDRBlur3>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur3[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNonHDRBlur5>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur5[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNonHDRBlur7>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur7[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNonHDRBlur9>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur9[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNonHDRBlur11>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur11[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNonHDRBlur13>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur13[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNonHDRBlur15>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur15[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBrightPassBlur3>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur3[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBrightPassBlur5>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur5[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBrightPassBlur7>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur7[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBrightPassBlur9>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur9[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBrightPassBlur11>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur11[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBrightPassBlur13>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur13[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBrightPassBlur15>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur15[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterDisplacementClearSimulation>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementClearSimulation[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterDisplacementTexOffset>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementTexOffset[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterDisplacementWadingRipple>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementWadingRipple[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterDisplacementRainRipple>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementRainRipple[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterWadingHeightmap>>(
			RE::VTABLE_BSImagespaceShaderWaterWadingHeightmap[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterRainHeightmap>>(
			RE::VTABLE_BSImagespaceShaderWaterRainHeightmap[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterBlendHeightmaps>>(
			RE::VTABLE_BSImagespaceShaderWaterBlendHeightmaps[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterSmoothHeightmap>>(
			RE::VTABLE_BSImagespaceShaderWaterSmoothHeightmap[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterDisplacementNormals>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementNormals[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNoiseScrollAndBlend>>(
			RE::VTABLE_BSImagespaceShaderNoiseScrollAndBlend[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISNoiseNormalmap>>(
			RE::VTABLE_BSImagespaceShaderNoiseNormalmap[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderVolumetricLighting[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLocalMap>>(
			RE::VTABLE_BSImagespaceShaderLocalMap[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISAlphaBlend>>(
			RE::VTABLE_BSImagespaceShaderAlphaBlend[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLensFlare>>(
			RE::VTABLE_BSImagespaceShaderLensFlare[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLensFlareVisibility>>(
			RE::VTABLE_BSImagespaceShaderLensFlareVisibility[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISApplyReflections>>(
			RE::VTABLE_BSImagespaceShaderApplyReflections[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISApplyVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderISApplyVolumetricLighting[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBasicCopy>>(
			RE::VTABLE_BSImagespaceShaderISBasicCopy[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISBlur>>(
			RE::VTABLE_BSImagespaceShaderISBlur[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISVolumetricLightingBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISVolumetricLightingBlurHCS[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISVolumetricLightingBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISVolumetricLightingBlurVCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISReflectionBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderReflectionBlurHCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISReflectionBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderReflectionBlurVCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISParallaxMaskBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISParallaxMaskBlurHCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISParallaxMaskBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISParallaxMaskBlurVCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDepthOfFieldBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISDepthOfFieldBlurHCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDepthOfFieldBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISDepthOfFieldBlurVCS[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISCompositeVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderISCompositeVolumetricLighting[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCompositeLensFlare>>(
			RE::VTABLE_BSImagespaceShaderISCompositeLensFlare[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<
								  RE::ImageSpaceManager::ISCompositeLensFlareVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderISCompositeLensFlareVolumetricLighting[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopySubRegionCS>>(
			RE::VTABLE_BSImagespaceShaderISCopySubRegionCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDebugSnow>>(
			RE::VTABLE_BSImagespaceShaderISDebugSnow[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDownsample>>(
			RE::VTABLE_BSImagespaceShaderISDownsample[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISDownsampleIgnoreBrightest>>(
			RE::VTABLE_BSImagespaceShaderISDownsampleIgnoreBrightest[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDownsampleCS>>(
			RE::VTABLE_BSImagespaceShaderISDownsampleCS[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISDownsampleIgnoreBrightestCS>>(
			RE::VTABLE_BSImagespaceShaderISDownsampleIgnoreBrightestCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISExp>>(
			RE::VTABLE_BSImagespaceShaderISExp[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISIBLensFlares>>(
			RE::VTABLE_BSImagespaceShaderISIBLensFlares[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLightingComposite>>(
			RE::VTABLE_BSImagespaceShaderISLightingComposite[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<
								  RE::ImageSpaceManager::ISLightingCompositeNoDirectionalLight>>(
			RE::VTABLE_BSImagespaceShaderISLightingCompositeNoDirectionalLight[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLightingCompositeMenu>>(
			RE::VTABLE_BSImagespaceShaderISLightingCompositeMenu[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISPerlinNoiseCS>>(
			RE::VTABLE_BSImagespaceShaderISPerlinNoiseCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISPerlinNoise2DCS>>(
			RE::VTABLE_BSImagespaceShaderISPerlinNoise2DCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISReflectionsRayTracing>>(
			RE::VTABLE_BSImagespaceShaderReflectionsRayTracing[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISReflectionsDebugSpecMask>>(
			RE::VTABLE_BSImagespaceShaderReflectionsDebugSpecMask[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOBlurH>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurH[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOBlurV>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurV[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurHCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurVCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOCameraZ>>(
			RE::VTABLE_BSImagespaceShaderISSAOCameraZ[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOCameraZAndMipsCS>>(
			RE::VTABLE_BSImagespaceShaderISSAOCameraZAndMipsCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOCompositeSAO>>(
			RE::VTABLE_BSImagespaceShaderISSAOCompositeSAO[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOCompositeFog>>(
			RE::VTABLE_BSImagespaceShaderISSAOCompositeFog[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAOCompositeSAOFog>>(
			RE::VTABLE_BSImagespaceShaderISSAOCompositeSAOFog[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISMinify>>(
			RE::VTABLE_BSImagespaceShaderISMinify[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISMinifyContrast>>(
			RE::VTABLE_BSImagespaceShaderISMinifyContrast[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAORawAO>>(
			RE::VTABLE_BSImagespaceShaderISSAORawAO[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAORawAONoTemporal>>(
			RE::VTABLE_BSImagespaceShaderISSAORawAONoTemporal[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSAORawAOCS>>(
			RE::VTABLE_BSImagespaceShaderISSAORawAOCS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSILComposite>>(
			RE::VTABLE_BSImagespaceShaderISSILComposite[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSILRawInd>>(
			RE::VTABLE_BSImagespaceShaderISSILRawInd[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSimpleColor>>(
			RE::VTABLE_BSImagespaceShaderISSimpleColor[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDisplayDepth>>(
			RE::VTABLE_BSImagespaceShaderISDisplayDepth[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSnowSSS>>(
			RE::VTABLE_BSImagespaceShaderISSnowSSS[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISTemporalAA>>(
			RE::VTABLE_BSImagespaceShaderISTemporalAA[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISTemporalAA_UI>>(
			RE::VTABLE_BSImagespaceShaderISTemporalAA_UI[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISTemporalAA_Water>>(
			RE::VTABLE_BSImagespaceShaderISTemporalAA_Water[3]);
		stl::write_vfunc<0x1,
			BSImagespaceShader_Render<RE::ImageSpaceManager::ISUpsampleDynamicResolution>>(
			RE::VTABLE_BSImagespaceShaderISUpsampleDynamicResolution[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterBlend>>(
			RE::VTABLE_BSImagespaceShaderISWaterBlend[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISUnderwaterMask>>(
			RE::VTABLE_BSImagespaceShaderISUnderwaterMask[3]);
		stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISWaterFlow>>(
			RE::VTABLE_BSImagespaceShaderWaterFlow[3]);

		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISFXAA>>(
			RE::VTABLE_BSImagespaceShaderFXAA[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopy>>(
			RE::VTABLE_BSImagespaceShaderCopy[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopyDynamicFetchDisabled>>(
			RE::VTABLE_BSImagespaceShaderCopyDynamicFetchDisabled[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopyScaleBias>>(
			RE::VTABLE_BSImagespaceShaderCopyScaleBias[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopyCustomViewport>>(
			RE::VTABLE_BSImagespaceShaderCopyCustomViewport[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopyGrayScale>>(
			RE::VTABLE_BSImagespaceShaderGreyScale[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISRefraction>>(
			RE::VTABLE_BSImagespaceShaderRefraction[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDoubleVision>>(
			RE::VTABLE_BSImagespaceShaderDoubleVision[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopyTextureMask>>(
			RE::VTABLE_BSImagespaceShaderTextureMask[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISMap>>(
			RE::VTABLE_BSImagespaceShaderMap[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWorldMap>>(
			RE::VTABLE_BSImagespaceShaderWorldMap[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWorldMapNoSkyBlur>>(
			RE::VTABLE_BSImagespaceShaderWorldMapNoSkyBlur[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDepthOfField>>(
			RE::VTABLE_BSImagespaceShaderDepthOfField[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDepthOfFieldFogged>>(
			RE::VTABLE_BSImagespaceShaderDepthOfFieldFogged[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDepthOfFieldMaskedFogged>>(
			RE::VTABLE_BSImagespaceShaderDepthOfFieldMaskedFogged[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDistantBlur>>(
			RE::VTABLE_BSImagespaceShaderDistantBlur[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDistantBlurFogged>>(
			RE::VTABLE_BSImagespaceShaderDistantBlurFogged[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDistantBlurMaskedFogged>>(
			RE::VTABLE_BSImagespaceShaderDistantBlurMaskedFogged[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISRadialBlur>>(
			RE::VTABLE_BSImagespaceShaderRadialBlur[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISRadialBlurMedium>>(
			RE::VTABLE_BSImagespaceShaderRadialBlurMedium[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISRadialBlurHigh>>(
			RE::VTABLE_BSImagespaceShaderRadialBlurHigh[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRTonemapBlendCinematic>>(
			RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematic[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRTonemapBlendCinematicFade>>(
			RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematicFade[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample16>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample4>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample16Lum>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16Lum[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample4RGB2Lum>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4RGB2Lum[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample4LumClamp>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4LumClamp[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample4LightAdapt>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample4LightAdapt[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample16LumClamp>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16LumClamp[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISHDRDownSample16LightAdapt>>(
			RE::VTABLE_BSImagespaceShaderHDRDownSample16LightAdapt[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur3>>(
			RE::VTABLE_BSImagespaceShaderBlur3[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur5>>(
			RE::VTABLE_BSImagespaceShaderBlur5[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur7>>(
			RE::VTABLE_BSImagespaceShaderBlur7[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur9>>(
			RE::VTABLE_BSImagespaceShaderBlur9[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur11>>(
			RE::VTABLE_BSImagespaceShaderBlur11[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur13>>(
			RE::VTABLE_BSImagespaceShaderBlur13[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur15>>(
			RE::VTABLE_BSImagespaceShaderBlur15[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNonHDRBlur3>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur3[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNonHDRBlur5>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur5[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNonHDRBlur7>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur7[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNonHDRBlur9>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur9[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNonHDRBlur11>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur11[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNonHDRBlur13>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur13[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNonHDRBlur15>>(
			RE::VTABLE_BSImagespaceShaderNonHDRBlur15[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBrightPassBlur3>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur3[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBrightPassBlur5>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur5[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBrightPassBlur7>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur7[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBrightPassBlur9>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur9[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBrightPassBlur11>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur11[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBrightPassBlur13>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur13[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBrightPassBlur15>>(
			RE::VTABLE_BSImagespaceShaderBrightPassBlur15[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterDisplacementClearSimulation>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementClearSimulation[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterDisplacementTexOffset>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementTexOffset[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterDisplacementWadingRipple>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementWadingRipple[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterDisplacementRainRipple>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementRainRipple[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterWadingHeightmap>>(
			RE::VTABLE_BSImagespaceShaderWaterWadingHeightmap[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterRainHeightmap>>(
			RE::VTABLE_BSImagespaceShaderWaterRainHeightmap[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterBlendHeightmaps>>(
			RE::VTABLE_BSImagespaceShaderWaterBlendHeightmaps[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterSmoothHeightmap>>(
			RE::VTABLE_BSImagespaceShaderWaterSmoothHeightmap[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterDisplacementNormals>>(
			RE::VTABLE_BSImagespaceShaderWaterDisplacementNormals[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNoiseScrollAndBlend>>(
			RE::VTABLE_BSImagespaceShaderNoiseScrollAndBlend[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISNoiseNormalmap>>(
			RE::VTABLE_BSImagespaceShaderNoiseNormalmap[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderVolumetricLighting[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLocalMap>>(
			RE::VTABLE_BSImagespaceShaderLocalMap[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISAlphaBlend>>(
			RE::VTABLE_BSImagespaceShaderAlphaBlend[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLensFlare>>(
			RE::VTABLE_BSImagespaceShaderLensFlare[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLensFlareVisibility>>(
			RE::VTABLE_BSImagespaceShaderLensFlareVisibility[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISApplyReflections>>(
			RE::VTABLE_BSImagespaceShaderApplyReflections[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISApplyVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderISApplyVolumetricLighting[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBasicCopy>>(
			RE::VTABLE_BSImagespaceShaderISBasicCopy[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISBlur>>(
			RE::VTABLE_BSImagespaceShaderISBlur[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISVolumetricLightingBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISVolumetricLightingBlurHCS[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISVolumetricLightingBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISVolumetricLightingBlurVCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISReflectionBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderReflectionBlurHCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISReflectionBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderReflectionBlurVCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISParallaxMaskBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISParallaxMaskBlurHCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISParallaxMaskBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISParallaxMaskBlurVCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDepthOfFieldBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISDepthOfFieldBlurHCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDepthOfFieldBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISDepthOfFieldBlurVCS[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCompositeVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderISCompositeVolumetricLighting[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCompositeLensFlare>>(
			RE::VTABLE_BSImagespaceShaderISCompositeLensFlare[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<
								  RE::ImageSpaceManager::ISCompositeLensFlareVolumetricLighting>>(
			RE::VTABLE_BSImagespaceShaderISCompositeLensFlareVolumetricLighting[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopySubRegionCS>>(
			RE::VTABLE_BSImagespaceShaderISCopySubRegionCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDebugSnow>>(
			RE::VTABLE_BSImagespaceShaderISDebugSnow[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDownsample>>(
			RE::VTABLE_BSImagespaceShaderISDownsample[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDownsampleIgnoreBrightest>>(
			RE::VTABLE_BSImagespaceShaderISDownsampleIgnoreBrightest[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDownsampleCS>>(
			RE::VTABLE_BSImagespaceShaderISDownsampleCS[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDownsampleIgnoreBrightestCS>>(
			RE::VTABLE_BSImagespaceShaderISDownsampleIgnoreBrightestCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISExp>>(
			RE::VTABLE_BSImagespaceShaderISExp[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISIBLensFlares>>(
			RE::VTABLE_BSImagespaceShaderISIBLensFlares[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLightingComposite>>(
			RE::VTABLE_BSImagespaceShaderISLightingComposite[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<
								  RE::ImageSpaceManager::ISLightingCompositeNoDirectionalLight>>(
			RE::VTABLE_BSImagespaceShaderISLightingCompositeNoDirectionalLight[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLightingCompositeMenu>>(
			RE::VTABLE_BSImagespaceShaderISLightingCompositeMenu[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISPerlinNoiseCS>>(
			RE::VTABLE_BSImagespaceShaderISPerlinNoiseCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISPerlinNoise2DCS>>(
			RE::VTABLE_BSImagespaceShaderISPerlinNoise2DCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISReflectionsRayTracing>>(
			RE::VTABLE_BSImagespaceShaderReflectionsRayTracing[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISReflectionsDebugSpecMask>>(
			RE::VTABLE_BSImagespaceShaderReflectionsDebugSpecMask[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOBlurH>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurH[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOBlurV>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurV[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOBlurHCS>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurHCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOBlurVCS>>(
			RE::VTABLE_BSImagespaceShaderISSAOBlurVCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOCameraZ>>(
			RE::VTABLE_BSImagespaceShaderISSAOCameraZ[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOCameraZAndMipsCS>>(
			RE::VTABLE_BSImagespaceShaderISSAOCameraZAndMipsCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOCompositeSAO>>(
			RE::VTABLE_BSImagespaceShaderISSAOCompositeSAO[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOCompositeFog>>(
			RE::VTABLE_BSImagespaceShaderISSAOCompositeFog[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAOCompositeSAOFog>>(
			RE::VTABLE_BSImagespaceShaderISSAOCompositeSAOFog[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISMinify>>(
			RE::VTABLE_BSImagespaceShaderISMinify[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISMinifyContrast>>(
			RE::VTABLE_BSImagespaceShaderISMinifyContrast[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAORawAO>>(
			RE::VTABLE_BSImagespaceShaderISSAORawAO[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAORawAONoTemporal>>(
			RE::VTABLE_BSImagespaceShaderISSAORawAONoTemporal[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSAORawAOCS>>(
			RE::VTABLE_BSImagespaceShaderISSAORawAOCS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSILComposite>>(
			RE::VTABLE_BSImagespaceShaderISSILComposite[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSILRawInd>>(
			RE::VTABLE_BSImagespaceShaderISSILRawInd[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSimpleColor>>(
			RE::VTABLE_BSImagespaceShaderISSimpleColor[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDisplayDepth>>(
			RE::VTABLE_BSImagespaceShaderISDisplayDepth[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSnowSSS>>(
			RE::VTABLE_BSImagespaceShaderISSnowSSS[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISTemporalAA>>(
			RE::VTABLE_BSImagespaceShaderISTemporalAA[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISTemporalAA_UI>>(
			RE::VTABLE_BSImagespaceShaderISTemporalAA_UI[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISTemporalAA_Water>>(
			RE::VTABLE_BSImagespaceShaderISTemporalAA_Water[0]);
		stl::write_vfunc<0xC,
			BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISUpsampleDynamicResolution>>(
			RE::VTABLE_BSImagespaceShaderISUpsampleDynamicResolution[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterBlend>>(
			RE::VTABLE_BSImagespaceShaderISWaterBlend[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISUnderwaterMask>>(
			RE::VTABLE_BSImagespaceShaderISUnderwaterMask[0]);
		stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISWaterFlow>>(
			RE::VTABLE_BSImagespaceShaderWaterFlow[0]);
		// VR-only shaders
		if (globals::game::isVR) {
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopyDepthBuffer>>(
				RE::VTABLE_BSImagespaceShaderCopyDepthBuffer[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopyDepthBuffer_DR>>(
				RE::VTABLE_BSImagespaceShaderCopyDepthBuffer_DR[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISCopyDepthBufferTargetSize>>(
				RE::VTABLE_BSImagespaceShaderDepthBuffer4xDownscale[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDownsampleHierarchicalDepthBufferCS>>(
				RE::VTABLE_BSImagespaceShaderISDownsampleHierarchicalDepthBufferCS[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISDiffScaleDownsampleDepthBufferCS>>(
				RE::VTABLE_BSImagespaceShaderISDiffScaleDownsampleDepthBufferCS[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISFullScreenVR>>(
				RE::VTABLE_BSImagespaceShaderISFullScreenVR[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISTransformLvl7PreTest>>(
				RE::VTABLE_BSImagespaceShaderTransformLvl7PreTest[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLvl6PreTest>>(
				RE::VTABLE_BSImagespaceShaderLvl6PreTest[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLvl5PreTest>>(
				RE::VTABLE_BSImagespaceShaderLvl5PreTest[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLvl4PreTest>>(
				RE::VTABLE_BSImagespaceShaderLvl4PreTest[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLvl3PreTest>>(
				RE::VTABLE_BSImagespaceShaderLvl3PreTest[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLvl2PreTest>>(
				RE::VTABLE_BSImagespaceShaderLvl2PreTest[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLvl1PreTest>>(
				RE::VTABLE_BSImagespaceShaderLvl1PreTest[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLvl0PreTest>>(
				RE::VTABLE_BSImagespaceShaderLvl0PreTest[3]);
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISSetupPreTest>>(
				RE::VTABLE_BSImagespaceShaderSetupPreTest[3]);

			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopyDepthBuffer>>(
				RE::VTABLE_BSImagespaceShaderCopyDepthBuffer[0]);
			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopyDepthBuffer_DR>>(
				RE::VTABLE_BSImagespaceShaderCopyDepthBuffer_DR[0]);
			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISCopyDepthBufferTargetSize>>(
				RE::VTABLE_BSImagespaceShaderDepthBuffer4xDownscale[0]);
			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDownsampleHierarchicalDepthBufferCS>>(
				RE::VTABLE_BSImagespaceShaderISDownsampleHierarchicalDepthBufferCS[0]);
			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISDiffScaleDownsampleDepthBufferCS>>(
				RE::VTABLE_BSImagespaceShaderISDiffScaleDownsampleDepthBufferCS[0]);
			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISFullScreenVR>>(
				RE::VTABLE_BSImagespaceShaderISFullScreenVR[0]);
			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISTransformLvl7PreTest>>(
				RE::VTABLE_BSImagespaceShaderTransformLvl7PreTest[0]);
			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLvl6PreTest>>(
				RE::VTABLE_BSImagespaceShaderLvl6PreTest[0]);
			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLvl5PreTest>>(
				RE::VTABLE_BSImagespaceShaderLvl5PreTest[0]);
			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLvl4PreTest>>(
				RE::VTABLE_BSImagespaceShaderLvl4PreTest[0]);
			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLvl3PreTest>>(
				RE::VTABLE_BSImagespaceShaderLvl3PreTest[0]);
			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLvl2PreTest>>(
				RE::VTABLE_BSImagespaceShaderLvl2PreTest[0]);
			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLvl1PreTest>>(
				RE::VTABLE_BSImagespaceShaderLvl1PreTest[0]);
			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISLvl0PreTest>>(
				RE::VTABLE_BSImagespaceShaderLvl0PreTest[0]);
			stl::write_vfunc<0xC, BSImagespaceShader_Dispatch<RE::ImageSpaceManager::ISSetupPreTest>>(
				RE::VTABLE_BSImagespaceShaderSetupPreTest[0]);
			stl::write_thunk_call<VR_RenderDepth_DownscaleDepthBuffer>(REL::RelocationID(100421, 107139).address() + REL::Relocate(0x37f, 0));
			stl::write_thunk_call<VR_RenderDepth_BSOBBOcclusionTestingShader>(REL::RelocationID(100421, 107139).address() + REL::Relocate(0x3b1, 0));
			stl::write_thunk_call<VR_UpscaleDepthBuffer>(REL::Offset(0x13246AE).address());
		}

		stl::write_vfunc<0x2A, BSShaderAccumulator_FinishAccumulatingDispatch>(
			RE::VTABLE_BSShaderAccumulator[0]);

		stl::write_vfunc<0x35, BSCubeMapCamera_RenderCubemap>(RE::VTABLE_BSCubeMapCamera[0]);

		stl::write_vfunc<0xA, BSShadowDirectionalLight_RenderShadowmaps>(
			RE::VTABLE_BSShadowDirectionalLight[0]);
		stl::write_vfunc<0xA, BSShadowFrustumLight_RenderShadowmaps>(
			RE::VTABLE_BSShadowFrustumLight[0]);
		stl::write_vfunc<0xA, BSShadowParabolicLight_RenderShadowmaps>(
			RE::VTABLE_BSShadowParabolicLight[0]);

		stl::detour_thunk<BSBatchRenderer_RenderBatches>(REL::RelocationID(100852, 107642));
		stl::detour_thunk<Main_RenderDepth>(REL::RelocationID(100421, 107139));
		stl::detour_thunk<Main_RenderWorld>(REL::RelocationID(100424, 107142));
		stl::detour_thunk<Main_RenderFirstPersonView>(REL::RelocationID(100411, 107129));
		stl::detour_thunk<Main_RenderPlayerView>(REL::RelocationID(35560, 36559));
		stl::detour_thunk<Main_RenderWaterEffects>(REL::RelocationID(35561, 36560));
		stl::detour_thunk<BSShaderAccumulator_RenderBatches>(REL::RelocationID(99963, 106609));
		stl::detour_thunk<BSShaderAccumulator_RenderPersistentPassList>(REL::RelocationID(100840, 107630));
	}

	void OnDataLoaded()
	{
		if (!globals::state->frameAnnotations)
			return;

		auto renderer = globals::game::renderer;

		for (size_t renderTargetIndex = 0;
			renderTargetIndex < static_cast<size_t>(Util::GetRenderTargetCount()); ++renderTargetIndex) {
			const auto renderTargetName = magic_enum::enum_name(
				static_cast<RE::RENDER_TARGETS::RENDER_TARGET>(renderTargetIndex));
			if (auto texture = renderer->GetRuntimeData().renderTargets[renderTargetIndex].texture) {
				texture->SetPrivateData(*Util::AsW32(&WKPDID_D3DDebugObjectName),
					static_cast<UINT>(renderTargetName.size()), renderTargetName.data());
			}
		}

		for (size_t renderTargetIndex = 0;
			renderTargetIndex < RE::RENDER_TARGETS_CUBEMAP::kTOTAL;
			++renderTargetIndex) {
			const auto renderTargetName = magic_enum::enum_name(
				static_cast<RE::RENDER_TARGETS_CUBEMAP::RENDER_TARGET_CUBEMAP>(renderTargetIndex));
			if (auto texture = renderer->GetRendererData().cubemapRenderTargets[renderTargetIndex].texture) {
				texture->SetPrivateData(*Util::AsW32(&WKPDID_D3DDebugObjectName),
					static_cast<UINT>(renderTargetName.size()), renderTargetName.data());
			}
		}

		for (size_t renderTargetIndex = 0;
			renderTargetIndex < static_cast<size_t>(Util::GetDepthStencilCount());
			++renderTargetIndex) {
			const auto renderTargetName = magic_enum::enum_name(
				static_cast<RE::RENDER_TARGETS_DEPTHSTENCIL::RENDER_TARGET_DEPTHSTENCIL>(
					renderTargetIndex));
			if (auto texture = renderer->GetDepthStencilData().depthStencils[renderTargetIndex].texture) {
				texture->SetPrivateData(*Util::AsW32(&WKPDID_D3DDebugObjectName),
					static_cast<UINT>(renderTargetName.size()), renderTargetName.data());
			}
		}
	}
}
