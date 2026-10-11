#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NeuralRendering
{
	enum class StartupPrewarmStage : std::uint8_t
	{
		PreparePrimary,
		PrimaryEye0,
		PrimaryEye1,
		PreparePeriphery,
		PeripheryEye0,
		PeripheryEye1,
		Complete,
	};

	struct StartupPrewarmGateInput
	{
		bool enabled = false;
		bool vr = false;
		bool menuOrLoading = false;
		bool nrEnabled = false;
		bool routeReady = false;
		bool dlssDefault = false;
		bool perfModeReady = false;
		bool stableFullEye = false;
		bool supportedLayout = false;
	};

	[[nodiscard]] constexpr const char* StartupPrewarmGateReason(const StartupPrewarmGateInput& input)
	{
		if (!input.enabled) return "disabled";
		if (!input.vr) return "not-vr";
		if (!input.menuOrLoading) return "not-menu-loading";
		if (!input.nrEnabled) return "nr-disabled";
		if (!input.routeReady) return "route-inactive";
		if (!input.dlssDefault) return "dlss-mode";
		if (!input.perfModeReady) return "perfmode-not-ready";
		if (!input.stableFullEye) return "not-full-eye";
		if (!input.supportedLayout) return "unsupported-layout";
		return nullptr;
	}

	[[nodiscard]] constexpr StartupPrewarmStage NextStartupPrewarmStage(
		StartupPrewarmStage stage, bool hybrid)
	{
		switch (stage) {
		case StartupPrewarmStage::PreparePrimary: return StartupPrewarmStage::PrimaryEye0;
		case StartupPrewarmStage::PrimaryEye0: return StartupPrewarmStage::PrimaryEye1;
		case StartupPrewarmStage::PrimaryEye1: return hybrid ? StartupPrewarmStage::PreparePeriphery : StartupPrewarmStage::Complete;
		case StartupPrewarmStage::PreparePeriphery: return StartupPrewarmStage::PeripheryEye0;
		case StartupPrewarmStage::PeripheryEye0: return StartupPrewarmStage::PeripheryEye1;
		case StartupPrewarmStage::PeripheryEye1: return StartupPrewarmStage::Complete;
		case StartupPrewarmStage::Complete: return StartupPrewarmStage::Complete;
		}
		return StartupPrewarmStage::Complete;
	}

	[[nodiscard]] constexpr bool HasFrameGap(std::uint32_t lastFrame, std::uint32_t currentFrame)
	{
		return lastFrame != UINT32_MAX && currentFrame - lastFrame > 1u;
	}

	[[nodiscard]] inline bool NearlyEqualUVExtent(float left, float right, float tolerance = 1.0e-5f)
	{
		return std::isfinite(left) && std::isfinite(right) && std::abs(left - right) <= tolerance;
	}

	[[nodiscard]] constexpr std::uint32_t FullEyeExtent(std::uint32_t extent, float uvExtent, bool fullEye)
	{
		return fullEye ? extent : std::max(1u, static_cast<std::uint32_t>(extent * uvExtent));
	}

	[[nodiscard]] constexpr bool ShouldDeferTimeoutFailure(std::uint32_t consecutiveTimeouts,
		std::uint32_t retryableTimeouts = 2u)
	{
		return consecutiveTimeouts <= retryableTimeouts;
	}

	[[nodiscard]] constexpr bool ShouldRearmManualReset(bool resetDeferred)
	{
		return resetDeferred;
	}

	[[nodiscard]] constexpr bool ShouldResetAfterApplyFailure(bool retryableTransientFailure)
	{
		return !retryableTransientFailure;
	}
}
