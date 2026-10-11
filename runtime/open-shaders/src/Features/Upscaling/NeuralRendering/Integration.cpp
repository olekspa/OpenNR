#include "Integration.h"
#include "IntegrationPolicy.h"

#include "Renderer.h"
#include "RuntimePolicy.h"
#include "ShaderInitLatch.h"
#include "Features/HDRDisplay.h"
#include "Features/Upscaling.h"
#include "Features/Upscaling/FoveatedRender.h"
#include "Features/Upscaling/FoveatedRender/Bridge.h"
#include "Features/Upscaling/FoveatedRender/Core.h"
#include "Features/Upscaling/FoveatedRender/Ops.h"
#include "Features/Upscaling/NativeOpenVRGaze.h"
#include "Features/Upscaling/PerfMode.h"
#include "Globals.h"
#include "GpuPass.h"
#include "State.h"
#include "Utils/LazyShader.h"

#include "RE/C/Console.h"

#include <array>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

namespace NeuralRendering
{
	namespace
	{
		std::mutex routeStatusMutex;
		RouteStatus routeStatus;
	}

	RouteStatus GetRouteStatus()
	{
		std::lock_guard lock(routeStatusMutex);
		return routeStatus;
	}

	void PublishRouteStatus(RouteStatus status)
	{
		std::lock_guard lock(routeStatusMutex);
		if (routeStatus == status)
			return;
		routeStatus = std::move(status);
	}

	namespace
	{
		eastl::unique_ptr<Texture2D> stereoBlendTarget;
		eastl::unique_ptr<Texture2D> peripheryRaw;
		eastl::unique_ptr<Texture2D> peripheryOutput;
		eastl::unique_ptr<Texture2D> centreOutput;
		eastl::unique_ptr<Texture2D> peripheryCoarse;
		eastl::unique_ptr<Texture2D> peripheryLowResResidual;
		eastl::unique_ptr<Texture2D> centreCoarse;
		winrt::com_ptr<ID3D11ComputeShader> peripheryHybridCS;
		ShaderInitLatch peripheryHybridShaderInit;
		winrt::com_ptr<ID3D11Buffer> peripheryHybridCB;
		winrt::com_ptr<ID3D11SamplerState> peripheryHybridSampler;
		std::uint32_t colorWidth = 0;
		std::uint32_t colorHeight = 0;
		DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN;
		std::uint32_t stereoBlendWidth = 0;
		std::uint32_t stereoBlendHeight = 0;
		DXGI_FORMAT stereoBlendFormat = DXGI_FORMAT_UNKNOWN;
		std::uint32_t lastAppliedFrame = UINT32_MAX;
		bool writebackLogged = false;
		bool flatRouteWasActive = false;
		bool flatFrameGenerationBlockLogged = false;
		bool flatHdrBlockLogged = false;
		bool blendFallbackLogged = false;
		std::atomic_bool historyResetRequested{ false };
		std::atomic_bool fullResetRequested{ false };
		bool temporalSuppressed = false;
		bool menuStateObserved = false;
		bool menuWasOpen = false;
		std::uint32_t preUpscaleAppliedFrame = UINT32_MAX;
		std::uint32_t preUpscaleSkippedFrame = UINT32_MAX;
		bool preUpscaleModeObserved = false;
		bool preUpscaleMode = false;
		bool preUpscaleBlockLogged = false;
		bool preUpscaleSuccessLogged = false;
		bool preUpscaleExecutionFailed = false;
		bool startupPrewarmAttempted = false;
		bool startupPrewarmSlotsReady = false;
		bool startupPrewarmReuseLogged = false;
		StartupPrewarmStage startupPrewarmStage = StartupPrewarmStage::PreparePrimary;
		std::string lastPrewarmSkipReason;
		std::uint32_t lastPrewarmStepFrame = UINT32_MAX;
		bool adaptiveCropHandoffDisabledLogged = false;
		bool coverageUnavailableLogged = false;
		bool projectionCropLogged = false;
		bool projectionCropUnavailableLogged = false;
		NeuralCoverageHistoryGeometry coverageHistoryGeometry{};
		bool coverageHistoryGeometryObserved = false;
		bool coverageCropWasActive = false;
		std::uint32_t coverageActivePercent = 100;
		bool peripheryRouteWasActive = false;
		bool peripheryStatusWasActive = false;
		bool peripheryFailureLatched = false;
		bool peripheryFailureLogged = false;
		std::uint32_t peripheryFailureCount = 0;
		std::chrono::steady_clock::time_point peripheryRetryAt{};
		bool staleGuidesLogged = false;
		bool missingGuidesLogged = false;
		bool uvMismatchLogged = false;
		const char* lastPeripheryInactiveReason = nullptr;
		struct PeripheryShaderSettingsKey
		{
			bool enabled = false;
			std::uint32_t resolution = 0;
			std::uint32_t coverage = 0;
			bool stagger = false;
			float gain = 0.0f;
			float feather = 0.0f;
			bool projectionCentered = false;
			float centerOffsetX = 0.0f;
			float centerOffsetY = 0.0f;
			float leftCenterOffsetX = 0.0f;
			float leftCenterOffsetY = 0.0f;
			float rightCenterOffsetX = 0.0f;
			float rightCenterOffsetY = 0.0f;
			bool operator==(const PeripheryShaderSettingsKey&) const = default;
		};
		PeripheryShaderSettingsKey peripheryShaderSettingsKey{};
		bool peripheryShaderSettingsObserved = false;
		std::uint32_t lastPeripheryAppliedFrame = UINT32_MAX;
		std::uint32_t activePeripheryResolution = 0;
		bool activePeripheryStagger = true;
		std::uint32_t peripheryResourceWidth = 0;
		std::uint32_t peripheryResourceHeight = 0;
		std::uint32_t peripheryResourceResolution = 0;
		DXGI_FORMAT peripheryResourceFormat = DXGI_FORMAT_UNKNOWN;

		struct alignas(16) PeripheryHybridConstants
		{
			std::uint32_t width = 0, height = 0, eyeWidth = 0, mode = 0;
			std::uint32_t leftCropX = 0, leftCropY = 0, rightCropX = 0, rightCropY = 0;
			std::uint32_t cropWidth = 0, cropHeight = 0;
			float gain = 0.83f, feather = 80.0f;
		};
		static_assert(sizeof(PeripheryHybridConstants) == 48);
		struct ProjectionCropCenter
		{
			FoveatedRenderImpl::NativeOpenVRGaze::ProjectionTangents tangents{};
			ProjectionPixelCenter center{};
			const char* source = "";
		};

		struct CoverageGeometry
		{
			std::array<NeuralCoverageRect, 2> rects{};
			std::array<ProjectionCropCenter, 2> projectionCenters{};
			std::array<double, 2> offsetXPx{};
			std::array<double, 2> offsetYPx{};
			bool projectionRequested = false;
			bool projectionResolved = false;
			bool matchingSizes = false;
			bool stereoAlignmentFailed = false;
		};

		bool ResolveProjectionCropCenters(std::uint32_t eyeWidth, std::uint32_t eyeHeight,
			std::array<ProjectionCropCenter, 2>& centers)
		{
			std::array<FoveatedRenderImpl::NativeOpenVRGaze::ProjectionTangents, 2> rawTangents{};
			const bool haveRaw = FoveatedRenderImpl::NativeOpenVRGaze::GetProjectionRawTangents(rawTangents);
			for (std::uint32_t eye = 0; eye < centers.size(); ++eye) {
				auto& resolved = centers[eye];
				bool valid = false;
				if (haveRaw) {
					resolved.tangents = rawTangents[eye];
					valid = TryProjectionPixelCenterFromTangents(
						{ resolved.tangents.left, resolved.tangents.right, resolved.tangents.top, resolved.tangents.bottom },
						eyeWidth, eyeHeight, resolved.center);
					if (valid)
						resolved.source = "OpenVR GetProjectionRaw";
				}
				if (!valid) {
					// The mapped per-frame cbuffer is the transposed GPU-upload matrix
					// (see DynamicNearClip.cpp). For its row-vector projection, _13/_23
					// hold the forward-ray NDC offsets and _43 is the forward W scale.
					const auto& projection = globals::game::frameBufferCached.GetCameraProjUnjittered(eye);
					const double homogeneousW = static_cast<double>(projection._43);
					if (!std::isfinite(homogeneousW) || std::abs(homogeneousW) < 1.0e-8)
						return false;
					const double xScale = static_cast<double>(projection._11) / homogeneousW;
					const double yScale = static_cast<double>(projection._22) / homogeneousW;
					const double xOffset = static_cast<double>(projection._13) / homogeneousW;
					const double yOffset = static_cast<double>(projection._23) / homogeneousW;
					NeuralRendering::ProjectionTangents matrixTangents{};
					if (!TryProjectionTangentsFromD3DProjectionMatrixScaleOffset(
						xScale, yScale, xOffset, yOffset, matrixTangents))
						return false;
					resolved.tangents = { static_cast<float>(matrixTangents.left), static_cast<float>(matrixTangents.right),
						static_cast<float>(matrixTangents.top), static_cast<float>(matrixTangents.bottom) };
					if (!TryProjectionPixelCenterFromTangents(matrixTangents, eyeWidth, eyeHeight, resolved.center))
						return false;
					resolved.source = "game eye projection matrix";
				}
			}
			return true;
		}

		CoverageGeometry ComputeCoverageGeometry(const FoveatedRender& foveated,
			std::uint32_t eyeWidth, std::uint32_t eyeHeight,
			std::uint32_t guideWidth, std::uint32_t guideHeight, bool fullEye)
		{
			CoverageGeometry result{};
			const auto coverage = NormalizeNeuralCoverage(foveated.settings.neuralRenderingCoverage);
			result.projectionRequested = foveated.settings.neuralRenderingProjectionCenteredCrop && fullEye && coverage < 100;
			if (result.projectionRequested)
				result.projectionResolved = ResolveProjectionCropCenters(eyeWidth, eyeHeight, result.projectionCenters);

			for (std::uint32_t eye = 0; eye < result.rects.size(); ++eye) {
				if (!fullEye)
					break;
				if (result.projectionResolved) {
					const bool leftEye = eye == 0;
					const float eyeOffsetX = leftEye ? foveated.settings.neuralRenderingProjectionCenterOffsetLeftXPx :
						foveated.settings.neuralRenderingProjectionCenterOffsetRightXPx;
					const float eyeOffsetY = leftEye ? foveated.settings.neuralRenderingProjectionCenterOffsetLeftYPx :
						foveated.settings.neuralRenderingProjectionCenterOffsetRightYPx;
					result.offsetXPx[eye] = eyeOffsetX == 0.0f ? foveated.settings.neuralRenderingProjectionCenterOffsetXPx : eyeOffsetX;
					result.offsetYPx[eye] = eyeOffsetY == 0.0f ? foveated.settings.neuralRenderingProjectionCenterOffsetYPx : eyeOffsetY;
					result.rects[eye] = ComputeNeuralCoverageRect(eyeWidth, eyeHeight, guideWidth, guideHeight, coverage,
						result.projectionCenters[eye].center.x, result.projectionCenters[eye].center.y,
						result.offsetXPx[eye], result.offsetYPx[eye]);
				} else {
					result.rects[eye] = ComputeNeuralCoverageRect(eyeWidth, eyeHeight, guideWidth, guideHeight, coverage);
				}
			}

			auto hasMatchingSizes = [&]() {
				return result.rects[0].colorWidth == result.rects[1].colorWidth &&
					result.rects[0].colorHeight == result.rects[1].colorHeight &&
					result.rects[0].guideWidth == result.rects[1].guideWidth &&
					result.rects[0].guideHeight == result.rects[1].guideHeight;
			};
			result.matchingSizes = hasMatchingSizes();
			if (result.projectionResolved && !result.matchingSizes) {
				const bool aligned = AlignNeuralCoverageRectsForStereo(eyeWidth, eyeHeight, guideWidth, guideHeight,
					result.rects[0], result.rects[1]);
				result.matchingSizes = aligned && hasMatchingSizes();
			}
			if (result.projectionResolved && !result.matchingSizes) {
				result.stereoAlignmentFailed = true;
				result.projectionResolved = false;
				for (auto& rect : result.rects)
					rect = ComputeNeuralCoverageRect(eyeWidth, eyeHeight, guideWidth, guideHeight, coverage);
				result.matchingSizes = hasMatchingSizes();
			}
			return result;
		}

		eastl::unique_ptr<Texture2D> CreatePeripherySignedTexture(ID3D11Device* device,
			std::uint32_t width, std::uint32_t height, const char* name)
		{
			if (!device || width == 0 || height == 0)
				return nullptr;
			UINT formatSupport = 0;
			if (FAILED(device->CheckFormatSupport(DXGI_FORMAT_R16G16B16A16_FLOAT, &formatSupport)) ||
				!(formatSupport & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) ||
				!State::SupportsTypedUAVLoad(DXGI_FORMAT_R16G16B16A16_FLOAT))
				return nullptr;
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			eastl::unique_ptr<Texture2D> texture;
			try {
				texture = eastl::make_unique<Texture2D>(desc, name);
				D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
				srv.Format = desc.Format;
				srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srv.Texture2D.MipLevels = 1;
				texture->CreateSRV(srv);
				D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
				uav.Format = desc.Format;
				uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				texture->CreateUAV(uav);
			} catch (...) {
				return nullptr;
			}
			return texture;
		}

		bool EnsurePeripheryResources(ID3D11Resource* source, const D3D11_TEXTURE2D_DESC& desc, std::uint32_t resolution)
		{
			if (peripheryRaw && peripheryOutput && centreOutput && peripheryCoarse && centreCoarse && peripheryLowResResidual &&
				peripheryResourceWidth == desc.Width && peripheryResourceHeight == desc.Height &&
				peripheryResourceFormat == desc.Format && peripheryResourceResolution == resolution)
				return true;
			peripheryRaw.reset(); peripheryOutput.reset(); centreOutput.reset();
			peripheryCoarse.reset(); centreCoarse.reset(); peripheryLowResResidual.reset();
			const auto coarseWidth = std::max(1u, (desc.Width + 7u) / 8u);
			const auto coarseHeight = std::max(1u, (desc.Height + 7u) / 8u);
			const auto modelEyeWidth = std::max(1u, static_cast<std::uint32_t>((static_cast<std::uint64_t>(desc.Width / 2u) * resolution + 50u) / 100u));
			const auto modelHeight = std::max(1u, static_cast<std::uint32_t>((static_cast<std::uint64_t>(desc.Height) * resolution + 50u) / 100u));
			peripheryRaw = Upscaling::CreateTextureFromSource(source, desc.Width, desc.Height, false, true, true, "NeuralRendering::PeripheryRaw");
			peripheryOutput = Upscaling::CreateTextureFromSource(source, desc.Width, desc.Height, false, true, true, "NeuralRendering::PeripheryOutput");
			centreOutput = Upscaling::CreateTextureFromSource(source, desc.Width, desc.Height, false, true, true, "NeuralRendering::CentreOutput");
			peripheryCoarse = CreatePeripherySignedTexture(globals::d3d::device, coarseWidth, coarseHeight, "NeuralRendering::PeripheryCoarse8");
			centreCoarse = CreatePeripherySignedTexture(globals::d3d::device, coarseWidth, coarseHeight, "NeuralRendering::CentreCoarse8");
			peripheryLowResResidual = CreatePeripherySignedTexture(globals::d3d::device, modelEyeWidth * 2u, modelHeight, "NeuralRendering::PeripheryLowResResidual");
			if (!peripheryRaw || !peripheryOutput || !centreOutput || !peripheryCoarse || !centreCoarse || !peripheryLowResResidual)
				return false;
			peripheryResourceWidth = desc.Width;
			peripheryResourceHeight = desc.Height;
			peripheryResourceFormat = desc.Format;
			peripheryResourceResolution = resolution;
			return true;
		}

		bool EnsurePeripheryHybridShader(ID3D11Device* device)
		{
			if (!device)
				return false;
			return peripheryHybridShaderInit.Ensure([&] {
				peripheryHybridCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
					L"Data\\Shaders\\Upscaling\\NeuralRendering\\PeripheryHybridCS.hlsl", {}, "cs_5_0")));
				if (!peripheryHybridCS)
					return false;
				D3D11_BUFFER_DESC desc{};
				desc.ByteWidth = sizeof(PeripheryHybridConstants);
				desc.Usage = D3D11_USAGE_DYNAMIC;
				desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				if (FAILED(device->CreateBuffer(&desc, nullptr, peripheryHybridCB.put())))
					return false;
				D3D11_SAMPLER_DESC samplerDesc{};
				samplerDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
				samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
				if (FAILED(device->CreateSamplerState(&samplerDesc, peripheryHybridSampler.put())))
					return false;
				return true;
			}, [] {
				logger::error("[DLSSNR] Periphery hybrid shader/resource initialization failed; route disabled until settings change or shader reload");
			});
		}

		void ResetPeripheryHybridShaderState()
		{
			peripheryHybridShaderInit.Reset();
			peripheryHybridCS = nullptr;
			peripheryHybridCB = nullptr;
			peripheryHybridSampler = nullptr;
		}

		bool DispatchPeripheryHybrid(ID3D11Device* device, ID3D11DeviceContext* context,
			ID3D11UnorderedAccessView* destinationUAV, std::uint32_t width, std::uint32_t height,
			std::uint32_t eyeWidth, const NeuralCoverageRect& leftCrop, const NeuralCoverageRect& rightCrop,
			float gain, float feather)
		{
			if (!EnsurePeripheryHybridShader(device) || !context || !destinationUAV)
				return false;
			const auto coarseWidth = std::max(1u, (width + 7u) / 8u);
			const auto coarseHeight = std::max(1u, (height + 7u) / 8u);
			auto dispatch = [&](std::uint32_t mode, std::uint32_t dispatchWidth, std::uint32_t dispatchHeight,
				ID3D11UnorderedAccessView** uavs) {
				D3D11_MAPPED_SUBRESOURCE mapped{};
				if (FAILED(context->Map(peripheryHybridCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
					return false;
				*static_cast<PeripheryHybridConstants*>(mapped.pData) = {
					.width = width, .height = height, .eyeWidth = eyeWidth, .mode = mode,
					.leftCropX = leftCrop.colorX, .leftCropY = leftCrop.colorY,
					.rightCropX = rightCrop.colorX, .rightCropY = rightCrop.colorY,
					.cropWidth = leftCrop.colorWidth, .cropHeight = leftCrop.colorHeight,
					.gain = gain, .feather = feather,
				};
				context->Unmap(peripheryHybridCB.get(), 0);
				ID3D11Buffer* cb = peripheryHybridCB.get();
				ID3D11SamplerState* sampler = peripheryHybridSampler.get();
				ID3D11ShaderResourceView* srvs[6]{ peripheryRaw->srv.get(), peripheryOutput->srv.get(),
					centreOutput->srv.get(), mode == 0 ? nullptr : peripheryCoarse->srv.get(),
					mode == 0 ? nullptr : centreCoarse->srv.get(),
					mode == 0 ? peripheryLowResResidual->srv.get() : nullptr };
				context->CSSetShader(peripheryHybridCS.get(), nullptr, 0);
				context->CSSetConstantBuffers(0, 1, &cb);
				context->CSSetShaderResources(0, 6, srvs);
				context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
				context->CSSetSamplers(0, 1, &sampler);
				context->Dispatch((dispatchWidth + 7u) / 8u, (dispatchHeight + 7u) / 8u, 1);
				ID3D11ShaderResourceView* nullSRVs[6]{};
				ID3D11UnorderedAccessView* nullUAVs[3]{};
				context->CSSetShaderResources(0, 6, nullSRVs);
				context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
				context->CSSetShader(nullptr, nullptr, 0);
				return true;
			};
			ID3D11UnorderedAccessView* coarseUAVs[3]{ peripheryCoarse->uav.get(), centreCoarse->uav.get(), nullptr };
			if (!dispatch(0, coarseWidth, coarseHeight, coarseUAVs))
				return false;
			ID3D11UnorderedAccessView* outputUAVs[3]{ nullptr, nullptr, destinationUAV };
			return dispatch(1, width, height, outputUAVs);
		}

		bool IsGameMenuOpen()
		{
			auto* state = globals::state;
			return state && state->IsPausedOrMenuOpen(globals::game::ui);
		}

		bool IsTemporalOverlayOpen()
		{
			auto* state = globals::state;
			auto* ui = globals::game::ui;
			const bool consoleOpen = ui && ui->IsMenuOpen(RE::Console::MENU_NAME);
			// Ordinary pause/map/stats menus have a guarded camera-MV path below and
			// are safe to keep on NR. Loading and console overlays still have no
			// stable scene contract, so they remain fail-closed and reset history.
			return consoleOpen || (state && state->isLoadingMenuOpen);
		}

		ID3D11Texture2D* ResolveRenderTargetTexture(
			const RE::BSGraphics::RenderTargetData& target,
			winrt::com_ptr<ID3D11Texture2D>& holder)
		{
			if (target.texture)
				return Util::AsReal(target.texture);
			auto resolveView = [&](ID3D11View* view) -> ID3D11Texture2D* {
				if (!view)
					return nullptr;
				winrt::com_ptr<ID3D11Resource> resource;
				view->GetResource(resource.put());
				if (!resource || FAILED(resource->QueryInterface(holder.put())))
					return nullptr;
				return holder.get();
			};
			if (auto* texture = resolveView(Util::AsReal(target.SRV)))
				return texture;
			return resolveView(Util::AsReal(target.RTV));
		}

		bool EnsureStereoBlendTarget(ID3D11Resource* source, std::uint32_t width, std::uint32_t height)
		{
			if (!source || width == 0 || height == 0)
				return false;

			winrt::com_ptr<ID3D11Texture2D> sourceTexture;
			if (FAILED(source->QueryInterface(sourceTexture.put())))
				return false;
			D3D11_TEXTURE2D_DESC sourceDesc{};
			sourceTexture->GetDesc(&sourceDesc);
			if (stereoBlendTarget && stereoBlendWidth == width && stereoBlendHeight == height &&
				stereoBlendFormat == sourceDesc.Format && stereoBlendTarget->uav)
				return true;

			stereoBlendTarget = Upscaling::CreateTextureFromSource(source, width, height, false, true, true,
				"NeuralRendering::FoveatedBlendTarget");
			if (!stereoBlendTarget || !stereoBlendTarget->uav)
				return false;
			stereoBlendWidth = width;
			stereoBlendHeight = height;
			stereoBlendFormat = sourceDesc.Format;
			return true;
		}

		Tuning GetTuning(const FoveatedRender& foveated, bool adaptiveEligible)
		{
			const auto& settings = foveated.settings;
			static bool flushStateObserved = false;
			static bool lastFlushState = false;
			if (!flushStateObserved || lastFlushState != settings.neuralRenderingFlushAfterSignal) {
				logger::info("[DLSSNR] flush-after-signal: {}", settings.neuralRenderingFlushAfterSignal ? "enabled" : "disabled");
				flushStateObserved = true;
				lastFlushState = settings.neuralRenderingFlushAfterSignal;
			}
			const bool adaptive = adaptiveEligible && settings.neuralRenderingAdaptiveEnabled &&
				foveated.adaptiveController.IsEnabled();
			const bool adaptiveCrop = adaptive && foveated.IsAdaptiveCropRuntimeActive();
			const auto& controller = foveated.adaptiveController;
			const int prewarmDirection = !adaptive || foveated.IsAdaptiveCropTransitioning() ? 0 :
				controller.LastSampleOverBudget() && !controller.IsAtMinimum() ? -1 :
				controller.LastSampleHadHeadroom() && !controller.IsAtMaximum() ? 1 : 0;
Tuning tuning{
				settings.neuralRenderingIntensity,
				settings.neuralRenderingLocalTone,
				settings.neuralRenderingLocalStructure,
				settings.neuralRenderingSkinStructure,
				settings.neuralRenderingStyle,
				settings.neuralRenderingAutoMask,
				settings.neuralRenderingUICorrection,
				adaptive ? foveated.adaptiveController.ActiveResolution() : settings.neuralRenderingModelResolution,
				settings.neuralRenderingResolveMode,
				// Keep the two experimental stage-order features mutually exclusive:
				// pre-upscale already adds a second NR route before the normal DLSS pass.
				settings.neuralRenderingPreUpscale == 0 ? std::min(settings.neuralRenderingMultiPass, 2u) : 0u,
				// Adaptive NR owns the per-frame model tier and its display-space
				// handoff. Keep native residual reuse disabled while tiers are moving.
				adaptive ? 0u : settings.neuralRenderingTemporalReuseCadence,
				settings.neuralRenderingTemporalDepthThreshold,
				settings.neuralRenderingTemporalColorTolerance,
				settings.neuralRenderingTemporalReuseResetAfterSkip,
				adaptive,
				!adaptiveCrop,
				adaptive ? foveated.adaptiveController.HandoffAlpha() : 1.0f,
				adaptive ? settings.neuralRenderingTemporalDepthThreshold : 0.05f,
				prewarmDirection,
				adaptive ? controller.MemoryCeiling() : 100u,
				settings.neuralRenderingResultShapingEnabled,
				settings.neuralRenderingResultEditStrength,
				settings.neuralRenderingResultBrightening,
				settings.neuralRenderingResultDarkening,
				settings.neuralRenderingResultColor,
				settings.neuralRenderingResultHueShiftStrength,
				settings.neuralRenderingResultShadows,
				settings.neuralRenderingResultMidtones,
				settings.neuralRenderingResultHighlights,
				settings.neuralRenderingResultMaxBrighteningStops,
				settings.neuralRenderingResultMaxDarkeningStops,
				settings.neuralRenderingResultMaxColorChangeStops,
				settings.neuralRenderingResultLargeScaleTone,
				settings.neuralRenderingResultFineDetail,
				settings.neuralRenderingResultDetailRadius,
				settings.neuralRenderingResultHaloSuppression,
				settings.neuralRenderingStabilizeMode,
				settings.neuralRenderingStabilizeTimeMs,
				settings.neuralRenderingStabilizeDetail,
				settings.neuralRenderingStabilizeDepthThreshold,
				settings.neuralRenderingStabilizeColorTolerance,
				settings.neuralRenderingTemporalReuseStaggerEyes,
			};
			tuning.parallelEyeQueue = settings.neuralRenderingParallelEyeQueue;
			tuning.flushAfterSignal = settings.neuralRenderingFlushAfterSignal;
			if constexpr (kFullResolutionNeuralRenderingOnly) {
				// Enforce the full-resolution contract at the runtime boundary too, so a
				// remote/DevBench settings write cannot select a reduced NR model area.
				tuning.modelResolutionPercent = 100;
				tuning.adaptiveResolution = false;
				tuning.adaptiveHandoff = true;
				tuning.adaptiveHandoffAlpha = 1.0f;
				tuning.adaptivePrewarmDirection = 0;
				tuning.adaptiveMemoryCeiling = 100;
			}
			return tuning;
		}

		Tuning MakeHybridCentreTuning(Tuning tuning)
		{
			tuning.temporalReuseCadence = 0;
			tuning.temporalReuseStaggerEyes = false;
			tuning.parallelEyeQueueRoute = "centre";
			return tuning;
		}

		Tuning MakeHybridPeripheryTuning(Tuning tuning, const FoveatedRender& foveated)
		{
			tuning.modelResolutionPercent = NormalizePeripheryResolution(
				foveated.settings.neuralRenderingPeripheryResolution);
			tuning.modelResolveMode = 4;
			tuning.multiPass = 0;
			tuning.temporalReuseCadence = foveated.settings.neuralRenderingPeripheryStagger ? 2u : 0u;
			tuning.temporalReuseStaggerEyes = foveated.settings.neuralRenderingPeripheryStagger;
			tuning.parallelEyeQueueRoute = "periphery";
			tuning.temporalReuseResetAfterSkip = false;
			tuning.adaptiveResolution = false;
			tuning.adaptiveHandoff = true;
			tuning.resultShapingEnabled = false;
			tuning.stabilizeMode = 0;
			return tuning;
		}

		void LogPreUpscaleBlocked(const char* reason)
		{
			if (!preUpscaleBlockLogged) {
				logger::warn("[DLSSNR] experimental pre-upscale route unavailable ({}); falling back to post-upscale NR", reason);
				preUpscaleBlockLogged = true;
			}
		}

		bool IsFullEyeStereo(const FoveatedRender& foveated)
		{
			return foveated.subrectController.GetUV().IsFullEye() &&
				foveated.subrectController.GetRightEyeUV().IsFullEye();
		}

		void LogStartupPrewarmReuse(const char* route)
		{
			if (startupPrewarmSlotsReady && !startupPrewarmReuseLogged) {
				logger::info("[DLSSNR][PREWARM] reused route={}", route);
				startupPrewarmReuseLogged = true;
			}
		}

		void TryStartupPrewarm(Upscaling& upscaling, FoveatedRender& foveated)
		{
			if (startupPrewarmAttempted)
				return;
			const auto coverage = NormalizeNeuralCoverage(foveated.settings.neuralRenderingCoverage);
			const bool hybrid = foveated.settings.neuralRenderingPeripheryEnabled && coverage < 100;
			const StartupPrewarmGateInput gate{
				.enabled = foveated.settings.neuralRenderingPrewarm,
				.vr = globals::game::isVR && globals::state,
				.menuOrLoading = globals::state && (IsGameMenuOpen() || globals::state->isLoadingMenuOpen),
				.nrEnabled = foveated.settings.neuralRenderingEnabled && !upscaling.IsFrameGenerationActive() &&
					foveated.settings.neuralRenderingPreUpscale == 0 &&
					!foveated.settings.neuralRenderingAdaptiveEnabled && !foveated.settings.neuralRenderingEyeTrackedFoveation,
				.routeReady = FoveatedRenderImpl::Bridge::IsRouteActive() &&
					upscaling.GetUpscaleMethod() == Upscaling::UpscaleMethod::kDLSS,
				.dlssDefault = foveated.GetDlssMode() == FoveatedRender::DlssMode::kDefault,
				.perfModeReady = upscaling.perfMode.IsHookActive(),
				.stableFullEye = IsFullEyeStereo(foveated),
				.supportedLayout = coverage == 100 || hybrid,
			};
			if (const auto* reason = StartupPrewarmGateReason(gate)) {
				if (lastPrewarmSkipReason != reason) {
					logger::info("[DLSSNR][PREWARM] skipped reason={}", reason);
					lastPrewarmSkipReason = reason;
				}
				return;
			}
			const auto frame = globals::state->frameCount;
			if (lastPrewarmStepFrame == frame)
				return;

			auto* context = globals::d3d::context;
			auto* device = globals::d3d::device;
			if (!context || !device) {
				if (lastPrewarmSkipReason != "d3d-not-ready") logger::info("[DLSSNR][PREWARM] skipped reason=d3d-not-ready");
				lastPrewarmSkipReason = "d3d-not-ready";
				return;
			}
			const auto displayEyeWidth = upscaling.perfMode.GetDisplayEyeWidth();
			const auto displayEyeHeight = upscaling.perfMode.GetDisplayEyeHeight();
			const auto guideWidth = upscaling.perfMode.GetRenderEyeWidth();
			const auto guideHeight = upscaling.perfMode.GetRenderEyeHeight();
			if (displayEyeWidth == 0 || displayEyeHeight == 0 || guideWidth == 0 || guideHeight == 0 ||
				(displayEyeWidth % 8u) != 0) {
				if (lastPrewarmSkipReason != "perfmode-dimensions") logger::info("[DLSSNR][PREWARM] skipped reason=perfmode-dimensions");
				lastPrewarmSkipReason = "perfmode-dimensions";
				return;
			}
			const auto geometry = ComputeCoverageGeometry(foveated, displayEyeWidth, displayEyeHeight,
				guideWidth, guideHeight, true);
			if (!geometry.rects[0].IsValid() || !geometry.rects[1].IsValid() || !geometry.matchingSizes) {
				if (lastPrewarmSkipReason != "coverage-geometry") logger::info("[DLSSNR][PREWARM] skipped reason=coverage-geometry");
				lastPrewarmSkipReason = "coverage-geometry";
				return;
			}
			lastPrewarmSkipReason.clear();

			// The same route geometry/tuning helpers as the lazy path are used here.
			// PerfMode's render dimensions provide valid guide extents on menu frames,
			// before the first world-frame intermediate guide textures exist.
			const Tuning primaryTuning = hybrid ? MakeHybridCentreTuning(GetTuning(foveated, false)) : GetTuning(foveated, false);
			const Tuning peripheryTuning = MakeHybridPeripheryTuning(GetTuning(foveated, false), foveated);
			const auto& rect = geometry.rects[0];
			bool succeeded = false;
			switch (startupPrewarmStage) {
			case StartupPrewarmStage::PreparePrimary:
				succeeded = Renderer::Instance().PreparePrewarm(device, context);
				break;
			case StartupPrewarmStage::PrimaryEye0:
			case StartupPrewarmStage::PrimaryEye1: {
				const auto eye = startupPrewarmStage == StartupPrewarmStage::PrimaryEye0 ? 0u : 1u;
				const auto colorW = hybrid ? rect.colorWidth : displayEyeWidth;
				const auto colorH = hybrid ? rect.colorHeight : displayEyeHeight;
				const auto guideW = hybrid ? rect.guideWidth : guideWidth;
				const auto guideH = hybrid ? rect.guideHeight : guideHeight;
				succeeded = Renderer::Instance().PrewarmStereoEye(device, context, guideW, guideH,
					colorW, colorH, eye, primaryTuning);
				break;
			}
			case StartupPrewarmStage::PreparePeriphery:
				succeeded = Renderer::PeripheryInstance().PreparePrewarm(device, context, true);
				break;
			case StartupPrewarmStage::PeripheryEye0:
			case StartupPrewarmStage::PeripheryEye1: {
				const auto eye = startupPrewarmStage == StartupPrewarmStage::PeripheryEye0 ? 0u : 1u;
				succeeded = Renderer::PeripheryInstance().PrewarmStereoEye(device, context, guideWidth, guideHeight,
					displayEyeWidth, displayEyeHeight, eye, peripheryTuning, true);
				break;
			}
			case StartupPrewarmStage::Complete:
				startupPrewarmAttempted = true;
				startupPrewarmSlotsReady = true;
				return;
			}
			lastPrewarmStepFrame = frame;
			if (!succeeded) {
				startupPrewarmAttempted = true;
				logger::warn("[DLSSNR][PREWARM] skipped reason=step-failed stage={}", static_cast<unsigned>(startupPrewarmStage));
				return;
			}
			startupPrewarmStage = NextStartupPrewarmStage(startupPrewarmStage, hybrid);
		if (startupPrewarmStage == StartupPrewarmStage::Complete) {
			startupPrewarmAttempted = true;
			startupPrewarmSlotsReady = true;
			logger::info("[DLSSNR][PREWARM] slots ready route={} projection={} display={}x{} guides={}x{}; first NR evaluation should reuse them",
					hybrid ? "hybrid" : "full-eye", geometry.projectionResolved ? "resolved" :
					(geometry.projectionRequested ? "centered-fallback" : "centered-configured"),
					displayEyeWidth, displayEyeHeight, guideWidth, guideHeight);
			}
		}

		void RestoreRenderTargets(ID3D11DeviceContext* context,
			ID3D11RenderTargetView* (&savedRTVs)[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT],
			ID3D11DepthStencilView* savedDSV)
		{
			context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, savedDSV);
			for (auto*& rtv : savedRTVs)
				if (rtv)
					rtv->Release();
			if (savedDSV)
				savedDSV->Release();
		}

		bool ApplyFlatLdr(Upscaling& upscaling, FoveatedRender& foveated)
		{
			const bool frameGenerationConfigured = upscaling.IsFrameGenerationConfiguredForSession();
			const bool hdrConfigured = globals::features::hdrDisplay.loaded &&
				globals::features::hdrDisplay.settings.enableHDR;
			auto* renderer = globals::game::renderer;
			winrt::com_ptr<ID3D11Texture2D> framebufferHolder;
			ID3D11Texture2D* framebuffer = nullptr;
			if (renderer) {
				auto& target = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
				framebuffer = ResolveRenderTargetTexture(target, framebufferHolder);
			}
			const bool routeActive = upscaling.GetUpscaleMethod() == Upscaling::UpscaleMethod::kDLSS &&
				foveated.settings.neuralRenderingEnabled && !frameGenerationConfigured && !hdrConfigured;
			if (!routeActive) {
				if (foveated.settings.neuralRenderingEnabled && frameGenerationConfigured && !flatFrameGenerationBlockLogged) {
					logger::warn("[DLSSNR] Flat route blocked: disable Frame Generation and restart the game");
					flatFrameGenerationBlockLogged = true;
				}
				if (foveated.settings.neuralRenderingEnabled && hdrConfigured && !flatHdrBlockLogged) {
					logger::warn("[DLSSNR] Flat route blocked: HDR Display is not supported by the LDR integration");
					flatHdrBlockLogged = true;
				}
				if (flatRouteWasActive)
					Reset();
				return false;
			}
			flatRouteWasActive = true;

			const std::uint32_t frame = globals::state ? globals::state->frameCount : 0;
			if (preUpscaleAppliedFrame == frame || preUpscaleSkippedFrame == frame)
				return false;
			if (lastAppliedFrame == frame)
				return true;
			auto* context = globals::d3d::context;
			if (!renderer || !context || !globals::d3d::device || !upscaling.motionVectorCopyTexture)
				return false;

			auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			if (!framebuffer || !depth.texture || !depth.depthSRV || !upscaling.motionVectorCopyTexture->resource)
				return false;

			D3D11_TEXTURE2D_DESC totalDesc{};
			D3D11_TEXTURE2D_DESC motionDesc{};
			framebuffer->GetDesc(&totalDesc);
			upscaling.motionVectorCopyTexture->resource->GetDesc(&motionDesc);
			CS_GPU_PASS("NeuralRendering::FlatLdrBeforeUI");
			ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
			ID3D11DepthStencilView* savedDSV = nullptr;
			context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);
			context->OMSetRenderTargets(0, nullptr, nullptr);
			const bool succeeded = Renderer::Instance().Apply(globals::d3d::device, context, 0,
				framebuffer, Util::AsReal(depth.texture), Util::AsReal(depth.depthSRV),
				upscaling.motionVectorCopyTexture->resource.get(), motionDesc.Width, motionDesc.Height,
				totalDesc.Width, totalDesc.Height, static_cast<float>(motionDesc.Width),
				static_cast<float>(motionDesc.Height), GetTuning(foveated, false));
			if (succeeded) {
				lastAppliedFrame = frame;
				if (!writebackLogged) {
					logger::info("[DLSSNR] Flat LDR kFRAMEBUFFER output written before UI guides={}x{} color={}x{}",
						motionDesc.Width, motionDesc.Height, totalDesc.Width, totalDesc.Height);
					writebackLogged = true;
				}
			}

			context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, savedDSV);
			for (auto*& rtv : savedRTVs)
				if (rtv) rtv->Release();
			if (savedDSV) savedDSV->Release();
			return succeeded;
		}
	}

	void ResetHistory()
	{
		Renderer::Instance().ResetHistory();
		Renderer::PeripheryInstance().ResetHistory();
		coverageHistoryGeometryObserved = false;
		lastAppliedFrame = UINT32_MAX;
		preUpscaleAppliedFrame = UINT32_MAX;
		preUpscaleSkippedFrame = UINT32_MAX;
	}

	void RequestHistoryReset()
	{
		historyResetRequested.store(true, std::memory_order_release);
	}

	void UpdateFrameState()
	{
		static std::uint32_t resetCheckedFrame = UINT32_MAX;
		const auto currentFrame = globals::state ? globals::state->frameCount : 0;
		const bool firstUpdate = resetCheckedFrame != currentFrame;
		resetCheckedFrame = currentFrame;
		if (firstUpdate && fullResetRequested.exchange(false, std::memory_order_acq_rel)) {
			Reset();
			if (ShouldRearmManualReset(Renderer::Instance().IsFailureLatched())) {
				fullResetRequested.store(true, std::memory_order_release);
				return;
			}
			auto& streamline = globals::features::upscaling.streamline;
			streamline.DestroyDLSSResources();
			streamline.lastDLSSFailureFrame = UINT32_MAX;
			streamline.lastVRAMPressureFrame = UINT32_MAX;
			FoveatedRenderImpl::Core::vrSubrectFixedEnvelopeRejected = false;
			FoveatedRenderImpl::Core::vrSubrectNeuralFixedEnvelopeRejected = false;
			FoveatedRenderImpl::Core::activeSubrectUVHash = 0;
			FoveatedRenderImpl::Core::InvalidateTemporalState();
			logger::info("[DLSSNR] Manual reset completed on render thread; DLSS and adaptive crop rearmed");
		}
		const bool menuOpen = IsGameMenuOpen();
		const bool overlayOpen = IsTemporalOverlayOpen();
		const bool requested = historyResetRequested.exchange(false, std::memory_order_acq_rel);
		const bool requestedPreUpscale = globals::features::upscaling.foveatedRender.settings.neuralRenderingPreUpscale != 0;
		bool stageChanged = false;
		bool menuChanged = false;
		if (!menuStateObserved) {
			menuStateObserved = true;
			menuWasOpen = menuOpen;
		} else if (menuWasOpen != menuOpen) {
			menuWasOpen = menuOpen;
			menuChanged = true;
		}
		if (!preUpscaleModeObserved) {
			preUpscaleModeObserved = true;
			preUpscaleMode = requestedPreUpscale;
		} else if (preUpscaleMode != requestedPreUpscale) {
			preUpscaleMode = requestedPreUpscale;
			stageChanged = true;
		}
		if (stageChanged) {
			preUpscaleBlockLogged = false;
			preUpscaleExecutionFailed = false;
		}
		if (!requested && overlayOpen == temporalSuppressed && !stageChanged && !menuChanged)
			return;

		temporalSuppressed = overlayOpen;
		if (menuChanged) {
			// Menu entry/exit changes the motion-vector contract. Invalidate both
			// the neural history and the foveated periphery history, but do not
			// suppress the ordinary menu route itself.
			FoveatedRenderImpl::Core::InvalidateTemporalState();
		} else {
			ResetHistory();
		}
		const char* resetReason = stageChanged ? "NR stage changed" :
			menuChanged ? (menuOpen ? "menu open" : "menu closed") :
			requested ? "event" : (overlayOpen ? "overlay open" : "overlay closed");
		logger::info("[DLSSNR] Temporal history reset ({})",
			resetReason);
	}

	bool ApplyPreUpscale()
	{
		auto& upscaling = globals::features::upscaling;
		auto& foveated = upscaling.foveatedRender;
		if (!foveated.settings.neuralRenderingEnabled || foveated.settings.neuralRenderingPreUpscale == 0)
			return false;
		UpdateFrameState();
		// The pre-upscale hook runs before Upscale() has installed the guarded
		// menu motion-vector fallback. Keep this experimental stage out of menus;
		// post-upscale NR is allowed once those inputs have been prepared.
		if (temporalSuppressed || IsGameMenuOpen())
			return false;

		// Pre-upscale NR evaluates at render resolution, below the display eye.
		if constexpr (kFullResolutionNeuralRenderingOnly)
			return false;
		if (preUpscaleExecutionFailed) {
			LogPreUpscaleBlocked("the previous pre-NR execution failed; toggle the option to retry");
			return false;
		}

		const std::uint32_t frame = globals::state ? globals::state->frameCount : 0;
		if (preUpscaleAppliedFrame == frame)
			return true;
		if (preUpscaleSkippedFrame == frame)
			return false;

		const bool frameGenerationConfigured = upscaling.IsFrameGenerationConfiguredForSession();
		const bool hdrConfigured = globals::features::hdrDisplay.loaded &&
			globals::features::hdrDisplay.settings.enableHDR;
		if (upscaling.GetUpscaleMethod() != Upscaling::UpscaleMethod::kDLSS) {
			LogPreUpscaleBlocked("DLSS is not the selected upscaler");
			return false;
		}
		if (frameGenerationConfigured || upscaling.IsFrameGenerationActive()) {
			LogPreUpscaleBlocked("Frame Generation is enabled");
			return false;
		}
		if (hdrConfigured) {
			LogPreUpscaleBlocked("HDR Display uses the unsupported HDR integration");
			return false;
		}

		auto* renderer = globals::game::renderer;
		auto* context = globals::d3d::context;
		if (!renderer || !context || !globals::d3d::device) {
			LogPreUpscaleBlocked("D3D or renderer resources are not ready");
			return false;
		}

		auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		if (!main.texture || !motionVector.texture || !depth.texture || !depth.depthSRV) {
			LogPreUpscaleBlocked("native color/depth/motion guide resources are missing");
			return false;
		}

		D3D11_TEXTURE2D_DESC colorDesc{};
		D3D11_TEXTURE2D_DESC motionDesc{};
		main.texture->GetDesc(Util::AsW32(&colorDesc));
		motionVector.texture->GetDesc(Util::AsW32(&motionDesc));
		if (colorDesc.Width == 0 || colorDesc.Height == 0 ||
			(globals::game::isVR && (colorDesc.Width < 2 || (colorDesc.Width & 1u) != 0)) ||
			motionDesc.Width == 0 || motionDesc.Height == 0) {
			LogPreUpscaleBlocked("native stereo dimensions are invalid");
			return false;
		}
		if (Renderer::Instance().IsFailureLatched()) {
			LogPreUpscaleBlocked("the shared NR renderer has an existing failure latch");
			return false;
		}

		ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		ID3D11DepthStencilView* savedDSV = nullptr;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);
		context->OMSetRenderTargets(0, nullptr, nullptr);

		// Pre-upscale and the normal before-UI route share the renderer object but
		// are different Feature 18 stages. Keep the temporal state on the normal
		// route until the state is made stage-aware; otherwise equal-sized stages
		// could reuse a residual from the wrong point in the frame.
		Tuning tuning = GetTuning(foveated, false);
		tuning.temporalReuseCadence = 0;
		bool succeeded = false;
		if (!globals::game::isVR) {
			CS_GPU_PASS("NeuralRendering::FlatPreUpscale");
			succeeded = Renderer::Instance().Apply(globals::d3d::device, context, 0,
				Util::AsReal(main.texture), Util::AsReal(depth.texture), Util::AsReal(depth.depthSRV), Util::AsReal(motionVector.texture),
				motionDesc.Width, motionDesc.Height, colorDesc.Width, colorDesc.Height,
				static_cast<float>(motionDesc.Width), static_cast<float>(motionDesc.Height), tuning);
		} else {
			if (!FoveatedRenderImpl::Bridge::IsRouteActive()) {
				LogPreUpscaleBlocked("VR foveated route is not active");
			} else if (foveated.GetDlssMode() != FoveatedRender::DlssMode::kDefault) {
				LogPreUpscaleBlocked("VR Faster mode does not provide the isolated pre-NR guide contract");
			} else if (!IsFullEyeStereo(foveated)) {
				LogPreUpscaleBlocked("VR pre-NR is currently limited to Full Eye");
			} else {
				const std::uint32_t eyeWidth = colorDesc.Width / 2;
				const std::uint32_t eyeHeight = colorDesc.Height;
				std::uint32_t outputEyeWidth = eyeWidth;
				std::uint32_t outputEyeHeight = eyeHeight;
				if (upscaling.perfMode.IsHookActive() && upscaling.perfMode.GetTestTexture()) {
					outputEyeWidth = upscaling.perfMode.GetDisplayEyeWidth();
					outputEyeHeight = upscaling.perfMode.GetDisplayEyeHeight();
				} else {
					auto& total = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTOTAL];
					if (total.texture) {
						D3D11_TEXTURE2D_DESC totalDesc{};
						Util::AsReal(total.texture)->GetDesc(&totalDesc);
						if (totalDesc.Width >= 2 && totalDesc.Height > 0) {
							outputEyeWidth = totalDesc.Width / 2;
							outputEyeHeight = totalDesc.Height;
						}
					}
				}

				if (outputEyeWidth == 0 || outputEyeHeight == 0) {
					LogPreUpscaleBlocked("display-resolution stereo dimensions are invalid");
				} else if (!FoveatedRenderImpl::Core::PrepareVRPerEyeInputs(
						Util::AsReal(main.texture), Util::AsReal(depth.texture), Util::AsReal(motionVector.texture), nullptr, nullptr,
						eyeWidth, eyeHeight, outputEyeWidth, outputEyeHeight)) {
					LogPreUpscaleBlocked("per-eye pre-NR guide preparation failed");
				} else {
					std::array<Renderer::StereoEyeInput, 2> inputs{};
					for (std::uint32_t eye = 0; eye < 2; ++eye) {
						auto& depthGuide = FoveatedRenderImpl::Core::vrIntermediateDepth[eye];
						auto& motionGuide = FoveatedRenderImpl::Core::vrIntermediateMotionVectors[eye];
						if (!depthGuide || !motionGuide)
							continue;
						inputs[eye] = {
							.depth = depthGuide->resource.get(),
							.depthSRV = depthGuide->srv.get(),
							.motionVectors = motionGuide->resource.get(),
							.sourceX = eye * eyeWidth,
							.sourceY = 0,
							// Motion vectors are UV-unit; Feature 18 expects guide pixels
							// (the post-upscale and flat routes scale the same way).
							.motionVectorScaleX = static_cast<float>(eyeWidth),
							.motionVectorScaleY = static_cast<float>(eyeHeight),
						};
					}
					if (!inputs[0].depth || !inputs[1].depth || !inputs[0].motionVectors || !inputs[1].motionVectors) {
						LogPreUpscaleBlocked("per-eye pre-NR guides were not created");
					} else {
						CS_GPU_PASS("NeuralRendering::StereoPreUpscale");
						succeeded = Renderer::Instance().ApplyStereo(globals::d3d::device, context,
						Util::AsReal(main.texture), inputs, eyeWidth, eyeHeight, eyeWidth, eyeHeight, tuning,
						nullptr, nullptr, false, {}, false, nullptr, tuning.parallelEyeQueue, tuning.flushAfterSignal);
					}
				}
			}
		}

		RestoreRenderTargets(context, savedRTVs, savedDSV);
		if (!succeeded) {
			if (!ShouldResetAfterApplyFailure(Renderer::Instance().WasLastApplyTransientSkip())) {
				// A fence backpressure skip leaves the renderer and its live NGX
				// handles intact. Retry on the next frame; do not turn a transient
				// timeout into the multi-second cold initialization path.
				preUpscaleSkippedFrame = frame;
				logger::warn("[DLSSNR] pre-upscale skipped after transient fence backpressure; preserving renderer for retry");
				return false;
			}
			// The experimental stage must not poison the established post-upscale
			// route. Reset the shared renderer after a pre-stage execution failure;
			// the later UI-composite hook can initialize it again for fallback.
			FoveatedRenderImpl::Core::ClearBlendSourceSRVCache();
			Renderer::Instance().Reset();
			Renderer::PeripheryInstance().Reset();
			preUpscaleExecutionFailed = true;
			LogPreUpscaleBlocked("pre-NR execution failed; renderer reset for fallback");
			return false;
		}

		preUpscaleAppliedFrame = frame;
		preUpscaleBlockLogged = false;
		preUpscaleExecutionFailed = false;
		if (!preUpscaleSuccessLogged) {
			logger::info("[DLSSNR] experimental pre-upscale route active; stage=before-dlss vr={} model={} resolve={}",
				globals::game::isVR, foveated.settings.neuralRenderingModelResolution,
				foveated.settings.neuralRenderingResolveMode == 1 ? "matched-residual" : "classic");
			preUpscaleSuccessLogged = true;
		}
		return true;
	}

	bool ApplyFoveatedLdr()
	{
		UpdateFrameState();
		auto& upscaling = globals::features::upscaling;
		auto& foveated = upscaling.foveatedRender;
		TryStartupPrewarm(upscaling, foveated);
		if (temporalSuppressed)
			return false;
		if (!globals::game::isVR)
			return ApplyFlatLdr(upscaling, foveated);
		if (!foveated.settings.neuralRenderingEnabled) {
			RouteStatus status{};
			status.route = RouteKind::Off;
			PublishRouteStatus(std::move(status));
		} else if (!FoveatedRenderImpl::Bridge::IsRouteActive() ||
			upscaling.GetUpscaleMethod() != Upscaling::UpscaleMethod::kDLSS ||
			foveated.GetDlssMode() != FoveatedRender::DlssMode::kDefault ||
			upscaling.IsFrameGenerationActive()) {
			RouteStatus status{};
			status.route = RouteKind::Refused;
			status.refusalTerm = !FoveatedRenderImpl::Bridge::IsRouteActive() ? "foveatedRoute" :
				upscaling.GetUpscaleMethod() != Upscaling::UpscaleMethod::kDLSS ? "upscaler" :
				foveated.GetDlssMode() != FoveatedRender::DlssMode::kDefault ? "dlssMode" : "frameGeneration";
			PublishRouteStatus(std::move(status));
		}
		if (!globals::game::isVR || !FoveatedRenderImpl::Bridge::IsRouteActive() ||
			upscaling.GetUpscaleMethod() != Upscaling::UpscaleMethod::kDLSS ||
			foveated.GetDlssMode() != FoveatedRender::DlssMode::kDefault ||
			!foveated.settings.neuralRenderingEnabled || upscaling.IsFrameGenerationActive())
			return false;

		const std::uint32_t frame = globals::state ? globals::state->frameCount : 0;
		if (preUpscaleAppliedFrame == frame)
			return false;
		const std::uint32_t guideFrame = FoveatedRenderImpl::Core::neuralGuidesFrame;
		if (lastAppliedFrame == frame || (guideFrame != frame && !(frame > 0 && guideFrame == frame - 1)))
			return false;
		if (HasFrameGap(lastAppliedFrame, frame)) {
			Renderer::Instance().ResetHistory();
			logger::info("[DLSSNR] Primary Feature 18 history reset after frame gap previous={} current={}",
				lastAppliedFrame, frame);
		}
		// The controller samples the host-frame interval here, immediately before
		// choosing the native tier used by this frame. Calling it again from the
		// foveated hook is harmless because it is frame-idempotent.
		foveated.UpdateAdaptiveState(frame, true);

		auto* renderer = globals::game::renderer;
		auto* context = globals::d3d::context;
		if (!renderer || !context || !globals::d3d::device)
			return false;
		auto& total = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTOTAL];
		if (!total.texture)
			return false;
		D3D11_TEXTURE2D_DESC totalDesc{};
		 total.texture->GetDesc(Util::AsW32(&totalDesc));

		const FoveatedRenderImpl::NativeOpenVRGaze::Config gazeConfig{
			.enabled = foveated.settings.neuralRenderingEyeTrackedFoveation,
			.smoothingMs = foveated.settings.neuralRenderingEyeTrackedSmoothingMs,
			.quantizationPixels = foveated.settings.neuralRenderingEyeTrackedQuantizationPixels,
		};
		const bool gazeRequested = gazeConfig.enabled && foveated.settings.neuralRenderingEnabled &&
			foveated.GetDlssMode() == FoveatedRender::DlssMode::kDefault &&
			upscaling.GetUpscaleMethod() == Upscaling::UpscaleMethod::kDLSS;
		const auto staticLeftUV = foveated.GetEffectiveLeftUV();
		const auto staticRightUV = foveated.GetEffectiveRightUV();
		const auto gaze = FoveatedRenderImpl::NativeOpenVRGaze::ResolveForFrame(
			gazeConfig, staticLeftUV, staticRightUV, totalDesc.Width / 2, totalDesc.Height, frame,
			gazeRequested && FoveatedRenderImpl::NativeOpenVRGaze::IsDynamicGazeAllowed());
		// If the DLSS route did not publish this frame's guides yet, a moving crop
		// would make the previous frame's guides address the wrong source region.
		// Keep the established one-frame static tolerance, but fail closed for the
		// dynamic experiment until the current guides are available.
		if ((gaze.dynamic || foveated.IsAdaptiveCropRuntimeActive()) && guideFrame != frame) {
			if (!staleGuidesLogged) {
				logger::warn("[DLSSNR] NR route refused: stale or missing motion/depth guides");
				staleGuidesLogged = true;
			}
			RouteStatus status{};
			status.route = RouteKind::Refused;
			status.refusalTerm = "staleGuides";
			PublishRouteStatus(std::move(status));
			return false;
		}
		if (guideFrame == frame)
			staleGuidesLogged = false;
		const auto& leftUV = gaze.leftUV;
		const auto& rightUV = gaze.rightUV;
		const bool fullEye = leftUV.IsFullEye() && rightUV.IsFullEye();
		const auto* depthLeft = fullEye ? FoveatedRenderImpl::Core::vrIntermediateDepth[0].get() : FoveatedRenderImpl::Core::vrSubrectDepth[0].get();
		const auto* depthRight = fullEye ? FoveatedRenderImpl::Core::vrIntermediateDepth[1].get() : FoveatedRenderImpl::Core::vrSubrectDepth[1].get();
		const auto* motionLeft = fullEye ? FoveatedRenderImpl::Core::vrIntermediateMotionVectors[0].get() : FoveatedRenderImpl::Core::vrSubrectMotionVectors[0].get();
		const auto* motionRight = fullEye ? FoveatedRenderImpl::Core::vrIntermediateMotionVectors[1].get() : FoveatedRenderImpl::Core::vrSubrectMotionVectors[1].get();
		if (!depthLeft || !depthRight || !motionLeft || !motionRight) {
			if (!missingGuidesLogged) {
				logger::warn("[DLSSNR] NR route refused: depth or motion-vector guide resource missing");
				missingGuidesLogged = true;
			}
			RouteStatus status{};
			status.route = RouteKind::Refused;
			status.refusalTerm = "missingGuides";
			PublishRouteStatus(std::move(status));
			return false;
		}
		missingGuidesLogged = false;
		if (!NearlyEqualUVExtent(leftUV.w, rightUV.w) || !NearlyEqualUVExtent(leftUV.h, rightUV.h)) {
			if (!uvMismatchLogged) {
				logger::warn("[DLSSNR] NR route refused: left/right guide UV extents differ");
				uvMismatchLogged = true;
			}
			RouteStatus status{};
			status.route = RouteKind::Refused;
			status.refusalTerm = "uvMismatch";
			PublishRouteStatus(std::move(status));
			return false;
		}
		uvMismatchLogged = false;
		const std::uint32_t eyeWidth = totalDesc.Width / 2;
		std::uint32_t outWidth = FullEyeExtent(eyeWidth, leftUV.w, fullEye);
		std::uint32_t outHeight = FullEyeExtent(totalDesc.Height, leftUV.h, fullEye);
			// The fixed crop envelope is a backing-resource contract only. Feature
			// 18 still receives the current valid native guide extent so a smaller
			// crop never exposes stale tail data from the envelope.
			const std::uint32_t fullGuideWidth = fullEye ? FoveatedRenderImpl::Core::vrIntermediateDepth[0]->desc.Width : FoveatedRenderImpl::Core::vrSubrectValidInW;
			const std::uint32_t fullGuideHeight = fullEye ? FoveatedRenderImpl::Core::vrIntermediateDepth[0]->desc.Height : FoveatedRenderImpl::Core::vrSubrectValidInH;
			if (fullGuideWidth == 0 || fullGuideHeight == 0) {
				if (!missingGuidesLogged) {
					logger::warn("[DLSSNR] NR route refused: guide extent is zero");
					missingGuidesLogged = true;
				}
				RouteStatus status{};
				status.route = RouteKind::Refused;
				status.refusalTerm = "missingGuides";
				PublishRouteStatus(std::move(status));
				return false;
			}
			std::uint32_t guideWidth = fullGuideWidth;
			std::uint32_t guideHeight = fullGuideHeight;

		// NR-only coverage: DLSS stays full eye and every NR pixel stays at 100%
		// model resolution. Feature 18 runs on a projection-centered region cropped from the
		// full-eye guides, and the result is edge-blended over the DLSS image, so the
		// periphery keeps full DLSS quality instead of the stretched foveated path.
		const std::uint32_t coverage = NormalizeNeuralCoverage(foveated.settings.neuralRenderingCoverage);
		const auto coverageGeometry = ComputeCoverageGeometry(foveated, eyeWidth, totalDesc.Height,
			fullGuideWidth, fullGuideHeight, fullEye);
		auto& coverageRects = coverageGeometry.rects;
		auto& projectionCenters = coverageGeometry.projectionCenters;
		auto& projectionCropOffsetXPx = coverageGeometry.offsetXPx;
		auto& projectionCropOffsetYPx = coverageGeometry.offsetYPx;
		bool projectionCropResolved = coverageGeometry.projectionResolved;
		if (coverageGeometry.projectionRequested && !projectionCropResolved) {
			if (!projectionCropUnavailableLogged) {
				logger::warn("[DLSSNR] Projection-centred NR crop could not resolve both eye projections; using the centered crop");
				projectionCropUnavailableLogged = true;
			}
		}
		const auto& leftCoverageRect = coverageRects[0];
		const auto& rightCoverageRect = coverageRects[1];
		const bool matchingCoverageRects = coverageGeometry.matchingSizes;
		if (coverageGeometry.stereoAlignmentFailed && !projectionCropUnavailableLogged) {
			logger::warn("[DLSSNR] Projection-centred NR crops do not share a stereo size; using the centered crop");
			projectionCropUnavailableLogged = true;
		}
		if (projectionCropResolved && !projectionCropLogged) {
			constexpr double radiansToDegrees = 180.0 / 3.14159265358979323846;
			for (std::uint32_t eye = 0; eye < projectionCenters.size(); ++eye) {
				const auto& resolved = projectionCenters[eye];
				const double fovX = (std::atan(resolved.tangents.right) - std::atan(resolved.tangents.left)) * radiansToDegrees;
				const double fovY = (std::atan(resolved.tangents.bottom) - std::atan(resolved.tangents.top)) * radiansToDegrees;
				const auto& rect = coverageRects[eye];
				const double requestedCenterX = std::clamp(resolved.center.x + projectionCropOffsetXPx[eye],
					0.0, static_cast<double>(eyeWidth));
				const double requestedCenterY = std::clamp(resolved.center.y + projectionCropOffsetYPx[eye],
					0.0, static_cast<double>(totalDesc.Height));
				const double effectiveCropCenterX = rect.colorX + rect.colorWidth * 0.5;
				const double effectiveCropCenterY = rect.colorY + rect.colorHeight * 0.5;
				logger::info("[DLSSNR] Projection-centred NR eye={} source={} tangents L={} R={} T={} B={} forward_px=({:.2f},{:.2f}) requested_center_px=({:.2f},{:.2f}) effective_crop_center_px=({:.2f},{:.2f}) fov_deg=({:.2f}x{:.2f}) render={}x{}",
					eye, resolved.source, resolved.tangents.left, resolved.tangents.right,
					resolved.tangents.top, resolved.tangents.bottom, resolved.center.x, resolved.center.y,
					requestedCenterX, requestedCenterY, effectiveCropCenterX, effectiveCropCenterY,
					fovX, fovY, eyeWidth, totalDesc.Height);
			}
			projectionCropLogged = true;
		}
		const std::uint32_t fullEyeColorWidth = outWidth;
		const std::uint32_t fullEyeColorHeight = outHeight;
		// Use the guide selected for this route. A cropped route can have valid
		// subrect depth while the full-eye intermediate depth is not allocated.
		const DXGI_FORMAT coverageDepthFormat = depthLeft->desc.Format;
		const bool peripheryEnabled = foveated.settings.neuralRenderingPeripheryEnabled;
		const bool adaptiveCropActive = foveated.IsAdaptiveCropRuntimeActive();
		const bool coverageRectsValid = leftCoverageRect.IsValid() && rightCoverageRect.IsValid() && matchingCoverageRects;
		const bool coverageDepthSupported = coverageDepthFormat == DXGI_FORMAT_R32_TYPELESS || coverageDepthFormat == DXGI_FORMAT_R32_FLOAT;
		const bool eyeWidthAligned = (eyeWidth % 8u) == 0;
		const bool coverageUseful = coverage < 100;
		const bool hybridEligible = peripheryEnabled && fullEye && !gaze.dynamic && !adaptiveCropActive &&
			coverageUseful && coverageRectsValid && coverageDepthSupported && eyeWidthAligned;
		const bool inPeripheryBackoff = peripheryFailureLatched && std::chrono::steady_clock::now() < peripheryRetryAt;
		bool hybridRequested = hybridEligible && !inPeripheryBackoff;
		const char* peripheryInactiveReason = !peripheryEnabled ? "disabled" :
			!fullEye ? "fullEye" : gaze.dynamic ? "gaze" : adaptiveCropActive ? "adaptiveCrop" :
			!coverageUseful ? "coverage100" : !coverageRectsValid ? "coverageRects" : !coverageDepthSupported ? "depthFormat" :
			inPeripheryBackoff ? "hybridBackoff" :
			!eyeWidthAligned ? "eyeWidth%8" : nullptr;
		if (!hybridRequested) {
			if (peripheryStatusWasActive || lastPeripheryInactiveReason != peripheryInactiveReason) {
				logger::info("[DLSSNR] Periphery NR inactive: {}", peripheryInactiveReason);
				RouteStatus status{};
				const bool centreCropActive = fullEye && coverage < 100 && !gaze.dynamic && !adaptiveCropActive &&
					coverageRectsValid && coverageDepthSupported;
				status.route = centreCropActive ? RouteKind::NrOnlyCrop : RouteKind::FullEye;
				status.refusalTerm = peripheryInactiveReason;
				status.displayWidth = totalDesc.Width;
				status.displayHeight = totalDesc.Height;
				status.renderWidth = totalDesc.Width;
				status.renderHeight = totalDesc.Height;
				status.coveragePercent = coverage;
				status.peripheryResolutionPercent = foveated.settings.neuralRenderingPeripheryResolution;
				PublishRouteStatus(std::move(status));
			}
			peripheryStatusWasActive = false;
			lastPeripheryInactiveReason = peripheryInactiveReason;
		}
		const std::uint32_t requestedPeripheryResolution = hybridRequested ?
			NormalizePeripheryResolution(foveated.settings.neuralRenderingPeripheryResolution) : 0u;
		const PeripheryShaderSettingsKey shaderSettingsKey{
			.enabled = foveated.settings.neuralRenderingPeripheryEnabled,
			.resolution = foveated.settings.neuralRenderingPeripheryResolution,
			.coverage = coverage,
			.stagger = foveated.settings.neuralRenderingPeripheryStagger,
			.gain = foveated.settings.neuralRenderingPeripheryLightingGain,
			.feather = foveated.settings.neuralRenderingPeripheryFeatherWidth,
			.projectionCentered = foveated.settings.neuralRenderingProjectionCenteredCrop,
			.centerOffsetX = foveated.settings.neuralRenderingProjectionCenterOffsetXPx,
			.centerOffsetY = foveated.settings.neuralRenderingProjectionCenterOffsetYPx,
			.leftCenterOffsetX = foveated.settings.neuralRenderingProjectionCenterOffsetLeftXPx,
			.leftCenterOffsetY = foveated.settings.neuralRenderingProjectionCenterOffsetLeftYPx,
			.rightCenterOffsetX = foveated.settings.neuralRenderingProjectionCenterOffsetRightXPx,
			.rightCenterOffsetY = foveated.settings.neuralRenderingProjectionCenterOffsetRightYPx,
		};
		// A settings change should reopen a failed route, but a successfully compiled
		// shader is independent of these runtime parameters. Keep it alive while sliders
		// or A/B presets change; the texture and renderer paths resize/reset as needed.
		const bool peripherySettingsChanged = peripheryShaderSettingsObserved && shaderSettingsKey != peripheryShaderSettingsKey;
		if (peripherySettingsChanged &&
			peripheryHybridShaderInit.Failed())
			ResetPeripheryHybridShaderState();
		if (peripheryFailureLatched && peripherySettingsChanged) {
			peripheryFailureLatched = false;
			peripheryFailureCount = 0;
			peripheryFailureLogged = false;
		}
		peripheryShaderSettingsKey = shaderSettingsKey;
		peripheryShaderSettingsObserved = true;
		if (peripheryFailureLatched && !inPeripheryBackoff && peripheryHybridShaderInit.Failed())
			ResetPeripheryHybridShaderState();
		// Compile and initialize the hybrid route before either Feature 18 evaluation.
		// A broken shader therefore falls back to centre-only without paying for a
		// periphery evaluation on every subsequent frame.
		const bool hybridShaderReady = hybridRequested && EnsurePeripheryHybridShader(globals::d3d::device);
		const bool hybridResourcesReady = hybridRequested && hybridShaderReady &&
			EnsurePeripheryResources(Util::AsReal(total.texture), totalDesc, requestedPeripheryResolution);
		bool coverageCrop = fullEye && coverage < 100 && !gaze.dynamic && !foveated.IsAdaptiveCropRuntimeActive();
		if (coverageCrop) {
			const auto depthFormat = depthLeft->desc.Format;
			coverageCrop = leftCoverageRect.IsValid() && rightCoverageRect.IsValid() && matchingCoverageRects &&
				(depthFormat == DXGI_FORMAT_R32_TYPELESS || depthFormat == DXGI_FORMAT_R32_FLOAT);
			if (!coverageCrop && !coverageUnavailableLogged) {
				logger::warn("[DLSSNR] NR coverage {}% unavailable for this guide contract; using the full eye", coverage);
				coverageUnavailableLogged = true;
			}
		}
		if (coverageCrop) {
			outWidth = leftCoverageRect.colorWidth;
			outHeight = leftCoverageRect.colorHeight;
			guideWidth = leftCoverageRect.guideWidth;
			guideHeight = leftCoverageRect.guideHeight;
		}
		if (coverageCrop != coverageCropWasActive || (coverageCrop && coverage != coverageActivePercent)) {
			if (coverageCrop)
				logger::info("[DLSSNR] NR-only coverage {}% active color={}x{} guides={}x{} (DLSS full eye, model 100%)",
					coverage, outWidth, outHeight, guideWidth, guideHeight);
			else
				logger::info("[DLSSNR] NR-only coverage off; full-eye NR");
			RouteStatus status{};
			status.route = coverageCrop ? RouteKind::NrOnlyCrop : RouteKind::FullEye;
			if (peripheryInactiveReason)
				status.refusalTerm = peripheryInactiveReason;
			status.displayWidth = totalDesc.Width;
			status.displayHeight = totalDesc.Height;
			status.renderWidth = coverageCrop ? outWidth * 2u : totalDesc.Width;
			status.renderHeight = coverageCrop ? outHeight : totalDesc.Height;
			status.coveragePercent = coverage;
			status.peripheryResolutionPercent = foveated.settings.neuralRenderingPeripheryResolution;
			PublishRouteStatus(std::move(status));
			coverageCropWasActive = coverageCrop;
			coverageActivePercent = coverage;
		}
			Renderer::StereoResourceEnvelope resourceEnvelope{};
			if (!fullEye && !gaze.dynamic &&
				foveated.IsAdaptiveCropRuntimeActive() &&
				FoveatedRenderImpl::Core::vrSubrectResourceMode == FoveatedRenderImpl::Core::SubrectResourceMode::FixedEnvelope &&
				!FoveatedRenderImpl::Core::vrSubrectFixedEnvelopeRejected &&
				!FoveatedRenderImpl::Core::vrSubrectNeuralFixedEnvelopeRejected &&
				FoveatedRenderImpl::Core::vrSubrectInW >= guideWidth &&
				FoveatedRenderImpl::Core::vrSubrectInH >= guideHeight &&
				FoveatedRenderImpl::Core::vrSubrectOutW >= outWidth &&
				FoveatedRenderImpl::Core::vrSubrectOutH >= outHeight) {
				resourceEnvelope = {
					.enabled = true,
					.guideWidth = FoveatedRenderImpl::Core::vrSubrectInW,
					.guideHeight = FoveatedRenderImpl::Core::vrSubrectInH,
					.colorWidth = FoveatedRenderImpl::Core::vrSubrectOutW,
					.colorHeight = FoveatedRenderImpl::Core::vrSubrectOutH,
				};
			}

		// The normal foveated route blends the cropped upscaler result over the
		// stretched/background image. NR used to bypass that step and hard-copy
		// the crop, which made its rectangle visible even when Edge Blend was set
		// to Feather or Dither.
		const auto blendMode = foveated.GetSubrectBlendMode();
		const bool wantsEdgeBlend = hybridRequested || coverageCrop || (!fullEye &&
			(foveated.IsAdaptiveCropRuntimeActive() || blendMode != FoveatedRender::SubrectBlendMode::kHardCopy));
		ID3D11Resource* destination = Util::AsReal(total.texture);
		ID3D11UnorderedAccessView* destinationUAV = Util::AsReal(total.UAV);
		CS_GPU_PASS("NeuralRendering::FoveatedLdrBeforeUI");
		bool stagedBlendTarget = false;
		if (wantsEdgeBlend && !destinationUAV) {
			// kTOTAL is not guaranteed to have D3D11_BIND_UNORDERED_ACCESS. Keep
			// its original crop background in a private UAV-capable copy, blend there,
			// then copy the changed crop boxes back. The hybrid periphery compositor
			// writes outside the centre crop too and retains its full-frame copy path.
			if (EnsureStereoBlendTarget(Util::AsReal(total.texture), totalDesc.Width, totalDesc.Height)) {
				destination = stereoBlendTarget->resource.get();
				destinationUAV = stereoBlendTarget->uav.get();
				stagedBlendTarget = true;
			} else if (!blendFallbackLogged) {
				logger::warn("[DLSSNR] Edge Blend requested but kTOTAL has no usable UAV and staging allocation failed; using hard copy");
				blendFallbackLogged = true;
			}
		}

		ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		ID3D11DepthStencilView* savedDSV = nullptr;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);
		context->OMSetRenderTargets(0, nullptr, nullptr);

		const Util::Subrect::UVRegion* eyeUVs[2]{ &leftUV, &rightUV };
		std::array<Renderer::StereoEyeInput, 2> inputs{};
		for (std::uint32_t eye = 0; eye < 2; ++eye) {
			const auto& uv = *eyeUVs[eye];
			const std::uint32_t x = (eye ? eyeWidth : 0) + static_cast<std::uint32_t>(eyeWidth * uv.x);
			const std::uint32_t y = static_cast<std::uint32_t>(totalDesc.Height * uv.y);
			float motionScaleX = 1.0f;
			float motionScaleY = 1.0f;
			FoveatedRenderImpl::Bridge::ComputeMvecScale(eye, motionScaleX, motionScaleY);
			inputs[eye] = {
				.depth = (fullEye ? FoveatedRenderImpl::Core::vrIntermediateDepth[eye] : FoveatedRenderImpl::Core::vrSubrectDepth[eye])->resource.get(),
				.depthSRV = (fullEye ? FoveatedRenderImpl::Core::vrIntermediateDepth[eye] : FoveatedRenderImpl::Core::vrSubrectDepth[eye])->srv.get(),
				.motionVectors = (fullEye ? FoveatedRenderImpl::Core::vrIntermediateMotionVectors[eye] : FoveatedRenderImpl::Core::vrSubrectMotionVectors[eye])->resource.get(),
				.sourceX = x + (coverageCrop ? coverageRects[eye].colorX : 0u),
				.sourceY = y + (coverageCrop ? coverageRects[eye].colorY : 0u),
				// UV-unit vectors -> guide pixels of the full source eye; a guide crop
				// does not change the pixel displacement.
				.motionVectorScaleX = motionScaleX * fullGuideWidth,
				.motionVectorScaleY = motionScaleY * fullGuideHeight,
				.guideSourceX = coverageCrop ? coverageRects[eye].guideX : 0u,
				.guideSourceY = coverageCrop ? coverageRects[eye].guideY : 0u,
			};
		}
		if (stagedBlendTarget) {
			CS_GPU_PASS("NeuralRendering::FoveatedStagedTargetCopyIn");
			if (hybridRequested) {
				// The hybrid compositor writes the whole eye rectangle from its own raw
				// snapshot when its resources are ready; only the fallback path needs the
				// original SBS frame as the staging source.
				if (!hybridResourcesReady)
					context->CopyResource(destination, Util::AsReal(total.texture));
			} else {
				for (const auto& input : inputs) {
					const D3D11_BOX cropBox{
						input.sourceX, input.sourceY, 0,
						input.sourceX + outWidth, input.sourceY + outHeight, 1
					};
					context->CopySubresourceRegion(destination, 0, input.sourceX, input.sourceY, 0,
						Util::AsReal(total.texture), 0, &cropBox);
				}
			}
		}
		// Feature 18 centre history is crop-local; periphery history consumes full-eye
		// inputs. Quantize crop-origin observation to its 8-pixel sampling block so a
		// slider drag does not reset the centre renderer for every sub-block motion.
		NeuralCoverageHistoryGeometry currentCoverageHistoryGeometry{};
		currentCoverageHistoryGeometry.stable = fullEye && !gaze.dynamic &&
			!foveated.IsAdaptiveCropRuntimeActive();
		currentCoverageHistoryGeometry.sourceColorWidth = eyeWidth;
		currentCoverageHistoryGeometry.sourceColorHeight = totalDesc.Height;
		currentCoverageHistoryGeometry.sourceGuideWidth = fullGuideWidth;
		currentCoverageHistoryGeometry.sourceGuideHeight = fullGuideHeight;
		for (std::uint32_t eye = 0; eye < inputs.size(); ++eye) {
			currentCoverageHistoryGeometry.eyes[eye] = {
				.colorX = inputs[eye].sourceX & ~7u,
				.colorY = inputs[eye].sourceY & ~7u,
				.colorWidth = outWidth,
				.colorHeight = outHeight,
				.guideX = inputs[eye].guideSourceX & ~7u,
				.guideY = inputs[eye].guideSourceY & ~7u,
				.guideWidth = guideWidth,
				.guideHeight = guideHeight,
			};
		}
		const bool cropGeometryChanged = coverageHistoryGeometryObserved &&
			NeuralCoverageHistoryGeometryChanged(coverageHistoryGeometry, currentCoverageHistoryGeometry);
		if (cropGeometryChanged) {
			logger::info("[DLSSNR] Stable NR crop geometry changed; resetting centre Feature 18 history");
			Renderer::Instance().ResetHistory();
			peripheryFailureLatched = false;
			peripheryFailureCount = 0;
			peripheryFailureLogged = false;
		}
		coverageHistoryGeometry = currentCoverageHistoryGeometry;
		coverageHistoryGeometryObserved = true;
		Tuning tuning = GetTuning(foveated, true);
		if (!fullEye && tuning.adaptiveResolution && !tuning.adaptiveHandoff && !adaptiveCropHandoffDisabledLogged) {
			logger::info("[DLSSNR] adaptive NR history handoff disabled while adaptive crop is active; using current-frame crop feathering");
			adaptiveCropHandoffDisabledLogged = true;
		}
		if (!fullEye || coverageCrop)
			// The cascade relies on full-eye dimensions and isolated stage history;
			// keep cropped/foveated regions on the established single-pass route.
		{
			tuning.multiPass = 0;
			// A fixed crop now owns crop-local temporal history in the renderer. Do
			// not attempt reuse while an eye-tracked crop is moving: its local origin
			// changes every frame and must first be handled by an explicit crop-origin
			// transform. Adaptive crop already supplies cadence zero through GetTuning.
			if (gaze.dynamic)
				tuning.temporalReuseCadence = 0;
		}
			if (hybridRequested != peripheryRouteWasActive ||
				(hybridRequested && requestedPeripheryResolution != activePeripheryResolution) ||
				(hybridRequested && foveated.settings.neuralRenderingPeripheryStagger != activePeripheryStagger) ||
				(hybridRequested && lastPeripheryAppliedFrame != UINT32_MAX &&
					static_cast<std::uint32_t>(frame - lastPeripheryAppliedFrame) > 2u)) {
				if (peripheryFailureLatched && hybridRequested)
					Renderer::PeripheryInstance().ResetHistory();
				else
					ResetHistory();
			}
			if (hybridRequested && peripheryFailureLatched) {
				peripheryFailureLatched = false;
				peripheryFailureLogged = false;
			}
			if (hybridRequested && peripheryStatusWasActive &&
				(requestedPeripheryResolution != activePeripheryResolution ||
					foveated.settings.neuralRenderingPeripheryStagger != activePeripheryStagger)) {
				RouteStatus status{};
				status.route = RouteKind::PeripheryHybrid;
				status.displayWidth = totalDesc.Width;
				status.displayHeight = totalDesc.Height;
				status.renderWidth = totalDesc.Width;
				status.renderHeight = totalDesc.Height;
				status.coveragePercent = coverage;
				status.peripheryResolutionPercent = requestedPeripheryResolution;
				PublishRouteStatus(std::move(status));
			}
			peripheryRouteWasActive = hybridRequested;
			activePeripheryResolution = requestedPeripheryResolution;
			activePeripheryStagger = foveated.settings.neuralRenderingPeripheryStagger;
			if (!hybridRequested)
				lastPeripheryAppliedFrame = UINT32_MAX;

			bool succeeded = false;
			bool hybridSucceeded = false;
			bool rawSnapshotReady = false;
			bool centrePassSucceeded = false;
			if (hybridResourcesReady) {
				CS_GPU_PASS("NeuralRendering::HybridInputSnapshotCopies");
				context->CopyResource(peripheryRaw->resource.get(), Util::AsReal(total.texture));
				rawSnapshotReady = true;
				auto peripheryInputs = inputs;
				for (std::uint32_t eye = 0; eye < peripheryInputs.size(); ++eye) {
					peripheryInputs[eye].sourceX = eye * eyeWidth;
					peripheryInputs[eye].sourceY = 0;
					peripheryInputs[eye].guideSourceX = 0;
					peripheryInputs[eye].guideSourceY = 0;
				}
				Tuning peripheryTuning = MakeHybridPeripheryTuning(tuning, foveated);
			// The skipped eye's fresh effect is one host frame old while the current
			// motion vectors cover one frame. E29 accepts that age for low-resolution
			// periphery reuse; do not reseed NGX on every scheduled alternate turn.
			// Teleports, setting/resolution changes, and gaps over two frames still
			// reseed both renderers through ResetHistory above.
			bool peripheryOk = false;
			{
				LogStartupPrewarmReuse("hybrid");
				CS_GPU_PASS("NeuralRendering::PeripheryInstance::ApplyStereo");
				peripheryOk = Renderer::PeripheryInstance().ApplyStereo(globals::d3d::device, context,
					peripheryRaw->resource.get(), peripheryInputs, fullGuideWidth, fullGuideHeight,
					fullEyeColorWidth, fullEyeColorHeight, peripheryTuning,
					peripheryOutput->resource.get(), nullptr, false, {}, true,
					peripheryLowResResidual->uav.get(), peripheryTuning.parallelEyeQueue, peripheryTuning.flushAfterSignal);
			}
			if (peripheryOk) {
				// Preserve the complete raw frame for a correct centre-only fallback if
				// a later hybrid dispatch fails. ApplyStereo writes only the crop.
				context->CopyResource(centreOutput->resource.get(), peripheryRaw->resource.get());
				Tuning centreTuning = MakeHybridCentreTuning(tuning);
				{
					CS_GPU_PASS("NeuralRendering::CentreInstance::ApplyStereo");
					peripheryOk = Renderer::Instance().ApplyStereo(globals::d3d::device, context,
						peripheryRaw->resource.get(), inputs, guideWidth, guideHeight,
						outWidth, outHeight, centreTuning, centreOutput->resource.get(), nullptr, false, resourceEnvelope, false,
						nullptr, centreTuning.parallelEyeQueue, centreTuning.flushAfterSignal);
				}
				centrePassSucceeded = peripheryOk;
			}
			if (peripheryOk) {
				CS_GPU_PASS("NeuralRendering::PeripheryHybridDispatchChain");
				peripheryOk = DispatchPeripheryHybrid(globals::d3d::device, context, destinationUAV,
					totalDesc.Width, totalDesc.Height, eyeWidth, leftCoverageRect, rightCoverageRect,
					foveated.settings.neuralRenderingPeripheryLightingGain,
					foveated.settings.neuralRenderingPeripheryFeatherWidth);
			}
			if (peripheryOk) {
				lastPeripheryAppliedFrame = frame;
				hybridSucceeded = succeeded = true;
				if (!peripheryStatusWasActive) {
					D3D11_TEXTURE2D_DESC rawDesc{}, outputDesc{}, centreDesc{}, coarseDesc{}, centreCoarseDesc{}, lowResidualDesc{};
					peripheryRaw->resource->GetDesc(&rawDesc);
					peripheryOutput->resource->GetDesc(&outputDesc);
					centreOutput->resource->GetDesc(&centreDesc);
					peripheryCoarse->resource->GetDesc(&coarseDesc);
					centreCoarse->resource->GetDesc(&centreCoarseDesc);
					peripheryLowResResidual->resource->GetDesc(&lowResidualDesc);
					logger::info("[DLSSNR] Periphery NR active: display={}x{} fmt={} rawFmt={} outputFmt={} centreFmt={} coarseFmt={} centreCoarseFmt={} lowResidualFmt={} path={} coverage={}% res={}% gain={} feather={} stagger={}",
						totalDesc.Width, totalDesc.Height, static_cast<std::uint32_t>(totalDesc.Format),
						static_cast<std::uint32_t>(rawDesc.Format), static_cast<std::uint32_t>(outputDesc.Format),
						static_cast<std::uint32_t>(centreDesc.Format), static_cast<std::uint32_t>(coarseDesc.Format),
						static_cast<std::uint32_t>(centreCoarseDesc.Format), static_cast<std::uint32_t>(lowResidualDesc.Format),
						stagedBlendTarget ? "staged" : (destinationUAV ? "direct-uav" : "no-uav"), coverage, requestedPeripheryResolution,
						foveated.settings.neuralRenderingPeripheryLightingGain,
						foveated.settings.neuralRenderingPeripheryFeatherWidth,
						foveated.settings.neuralRenderingPeripheryStagger);
					RouteStatus status{};
					status.route = RouteKind::PeripheryHybrid;
					status.displayWidth = totalDesc.Width;
					status.displayHeight = totalDesc.Height;
					status.renderWidth = totalDesc.Width;
					status.renderHeight = totalDesc.Height;
					status.coveragePercent = coverage;
					status.peripheryResolutionPercent = requestedPeripheryResolution;
					PublishRouteStatus(std::move(status));
				}
				peripheryStatusWasActive = true;
				peripheryFailureCount = 0;
				peripheryFailureLogged = false;
				lastPeripheryInactiveReason = nullptr;
				if (stagedBlendTarget) {
					CS_GPU_PASS("NeuralRendering::PeripheryStagedWriteback");
					context->CopyResource(Util::AsReal(total.texture), destination);
				}
			} else if (hybridRequested) {
				// Fail closed. If the centre pass already completed, its raw-initialized
				// output is the exact centre-only fallback; do not evaluate Feature 18 again.
				if (rawSnapshotReady) {
					CS_GPU_PASS("NeuralRendering::HybridFailureRawRestore");
					context->CopyResource(destination, centrePassSucceeded ? centreOutput->resource.get() : peripheryRaw->resource.get());
					succeeded = centrePassSucceeded;
				}
				peripheryRouteWasActive = false;
				peripheryStatusWasActive = false;
				peripheryFailureLatched = true;
				const auto backoffSeconds = std::min(60u, 1u << std::min(peripheryFailureCount, 6u));
				++peripheryFailureCount;
				peripheryRetryAt = std::chrono::steady_clock::now() + std::chrono::seconds(backoffSeconds);
				const char* failureReason = !hybridShaderReady ? "shader" :
					!hybridResourcesReady ? "resources" : !peripheryOk ? "hybridPass" : "unknown";
				if (!peripheryFailureLogged) {
					logger::warn("[DLSSNR] Periphery NR refused ({}, retry in {}s); using centre-only route",
						failureReason, backoffSeconds);
					peripheryFailureLogged = true;
				}
				RouteStatus status{};
				status.route = coverageCrop ? RouteKind::NrOnlyCrop : RouteKind::FullEye;
				status.refusalTerm = failureReason;
				status.displayWidth = totalDesc.Width;
				status.displayHeight = totalDesc.Height;
				status.renderWidth = coverageCrop ? outWidth * 2u : totalDesc.Width;
				status.renderHeight = coverageCrop ? outHeight : totalDesc.Height;
				status.coveragePercent = coverage;
				status.peripheryResolutionPercent = requestedPeripheryResolution;
				PublishRouteStatus(std::move(status));
				if (stagedBlendTarget && succeeded) {
					CS_GPU_PASS("NeuralRendering::PeripheryStagedWriteback");
					context->CopyResource(Util::AsReal(total.texture), destination);
				}
			}
		}
		if (!hybridSucceeded && !succeeded) {
			// A hard-copy edge mode would expose the NR-only rectangle; upgrade it to
			// feathering for this composite only.
			FoveatedRenderImpl::Ops::forceFeatherBlend = coverageCrop;
			{
				LogStartupPrewarmReuse("full-eye-or-crop");
				CS_GPU_PASS("NeuralRendering::CentreInstance::FallbackApplyStereo");
				succeeded = Renderer::Instance().ApplyStereo(globals::d3d::device, context,
					Util::AsReal(total.texture), inputs, guideWidth, guideHeight,
					outWidth, outHeight, tuning, destination, destinationUAV, wantsEdgeBlend && destinationUAV != nullptr,
					resourceEnvelope, false, nullptr, tuning.parallelEyeQueue, tuning.flushAfterSignal);
			}
		}
			if (!succeeded && resourceEnvelope.IsValid() &&
				Renderer::Instance().IsFailureRecoverable()) {
				// A native Feature 18 failure is not allowed to leave the runtime
				// latched on the experimental envelope. Drop only the NR renderer's
				// state, remember the rejection for this route, and retry with exact
				// current extents. The native depth/motion guide inputs stay unchanged.
				logger::warn("[DLSSNR] fixed resource envelope rejected; retrying exact extents frame={} result=0x{:08X} fallbackEntries={}",
					frame, Renderer::Instance().NgxResult(), FoveatedRenderImpl::Core::vrSubrectNeuralFallbackEntries + 1);
				FoveatedRenderImpl::Core::ClearBlendSourceSRVCache();
				if (Renderer::Instance().Reset()) {
					Renderer::PeripheryInstance().Reset();
					FoveatedRenderImpl::Core::vrSubrectNeuralFixedEnvelopeRejected = true;
					++FoveatedRenderImpl::Core::vrSubrectNeuralFallbackEntries;
					FoveatedRenderImpl::Core::InvalidateTemporalState();
					resourceEnvelope.enabled = false;
					{
						CS_GPU_PASS("NeuralRendering::CentreInstance::EnvelopeRetryApplyStereo");
						succeeded = Renderer::Instance().ApplyStereo(globals::d3d::device, context,
							Util::AsReal(total.texture), inputs, guideWidth, guideHeight,
							outWidth, outHeight, tuning, destination, destinationUAV, wantsEdgeBlend && destinationUAV != nullptr,
							resourceEnvelope, false, nullptr, tuning.parallelEyeQueue, tuning.flushAfterSignal);
					}
				} else {
					logger::error("[DLSSNR] fixed resource envelope fallback aborted because the GPU fence could not be drained");
				}
			}
		FoveatedRenderImpl::Ops::forceFeatherBlend = false;
		if (succeeded) {
			if (stagedBlendTarget) {
				CS_GPU_PASS("NeuralRendering::FoveatedStagedWriteback");
				if (hybridRequested) {
					context->CopyResource(Util::AsReal(total.texture), destination);
				} else {
					for (const auto& input : inputs) {
						const D3D11_BOX cropBox{
							input.sourceX, input.sourceY, 0,
							input.sourceX + outWidth, input.sourceY + outHeight, 1
						};
						context->CopySubresourceRegion(Util::AsReal(total.texture), 0,
							input.sourceX, input.sourceY, 0, destination, 0, &cropBox);
					}
				}
			}
			// Crop handoffs move the current-frame compositing mask; no prior SBS image is reused.
			lastAppliedFrame = frame;
			if (!writebackLogged) {
				const char* path = stagedBlendTarget ? "staged-uav" :
					(destinationUAV ? "direct-uav" : "direct-copy");
				logger::info("[DLSSNR] LDR output written before UI composite size={}x{} edgeBlend={} mode={} path={} gazeCrop={} batchedAsync=true",
					outWidth, outHeight, wantsEdgeBlend && destinationUAV != nullptr,
					FoveatedRender::SubrectBlendModeName(blendMode), path, gaze.dynamic);
				writebackLogged = true;
			}
		}

		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, savedDSV);
		for (auto*& rtv : savedRTVs)
			if (rtv) rtv->Release();
		if (savedDSV) savedDSV->Release();
		return succeeded;
	}

	void RequestReset()
	{
		fullResetRequested.store(true, std::memory_order_release);
	}

	void Reset()
	{
		FoveatedRenderImpl::Core::ClearBlendSourceSRVCache();
		Renderer::Instance().Reset();
		Renderer::PeripheryInstance().Reset();
		globals::features::upscaling.foveatedRender.ResetAdaptiveState();
		FoveatedRenderImpl::NativeOpenVRGaze::Reset();
		historyResetRequested.store(false, std::memory_order_release);
		temporalSuppressed = false;
		stereoBlendTarget.reset();
		peripheryRaw.reset();
		peripheryOutput.reset();
		centreOutput.reset();
		peripheryCoarse.reset();
		centreCoarse.reset();
		peripheryLowResResidual.reset();
		ResetPeripheryHybridShaderState();
		peripheryShaderSettingsObserved = false;
		peripheryResourceWidth = peripheryResourceHeight = 0;
		peripheryResourceFormat = DXGI_FORMAT_UNKNOWN;
		peripheryRouteWasActive = false;
		peripheryStatusWasActive = false;
		peripheryFailureLatched = false;
		peripheryFailureLogged = false;
		peripheryFailureCount = 0;
		staleGuidesLogged = false;
		missingGuidesLogged = false;
		uvMismatchLogged = false;
		lastPeripheryInactiveReason = nullptr;
		lastPeripheryAppliedFrame = UINT32_MAX;
		activePeripheryResolution = 0;
		activePeripheryStagger = true;
		peripheryResourceResolution = 0;
		stereoBlendWidth = stereoBlendHeight = 0;
		stereoBlendFormat = DXGI_FORMAT_UNKNOWN;
		lastAppliedFrame = UINT32_MAX;
		preUpscaleAppliedFrame = UINT32_MAX;
		preUpscaleSkippedFrame = UINT32_MAX;
		startupPrewarmAttempted = false;
		startupPrewarmSlotsReady = false;
		startupPrewarmReuseLogged = false;
		startupPrewarmStage = StartupPrewarmStage::PreparePrimary;
		lastPrewarmSkipReason.clear();
		lastPrewarmStepFrame = UINT32_MAX;
		menuStateObserved = false;
		menuWasOpen = false;
		preUpscaleModeObserved = false;
		preUpscaleMode = false;
		preUpscaleBlockLogged = false;
		preUpscaleSuccessLogged = false;
		preUpscaleExecutionFailed = false;
		adaptiveCropHandoffDisabledLogged = false;
		coverageUnavailableLogged = false;
		coverageCropWasActive = false;
		coverageActivePercent = 100;
		writebackLogged = false;
		flatRouteWasActive = false;
		flatFrameGenerationBlockLogged = false;
		flatHdrBlockLogged = false;
		blendFallbackLogged = false;
	}
}
