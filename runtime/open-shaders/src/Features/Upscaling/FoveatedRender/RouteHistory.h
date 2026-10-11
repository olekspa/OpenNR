#pragma once

#include <cstdint>
#include <limits>

namespace FoveatedRenderImpl
{
	inline constexpr std::uint32_t kNoCompletedRouteFrame = std::numeric_limits<std::uint32_t>::max();

	constexpr bool NeedsPeripheryHistoryReset(std::uint32_t lastCompletedFrame, std::uint32_t currentFrame) noexcept
	{
		return lastCompletedFrame == kNoCompletedRouteFrame || currentFrame != lastCompletedFrame + 1u;
	}
}
