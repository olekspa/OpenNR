#include "Features/Upscaling/FoveatedRender/RouteHistory.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

TEST_CASE("foveated periphery history is reseeded across route gaps", "[upscaling][foveation][history]")
{
	using FoveatedRenderImpl::NeedsPeripheryHistoryReset;
	using FoveatedRenderImpl::kNoCompletedRouteFrame;

	SECTION("first route after startup or resource clear reseeds")
	{
		REQUIRE(NeedsPeripheryHistoryReset(kNoCompletedRouteFrame, 0));
		REQUIRE(NeedsPeripheryHistoryReset(kNoCompletedRouteFrame, 42));
	}

	SECTION("a completed adjacent frame keeps history")
	{
		REQUIRE_FALSE(NeedsPeripheryHistoryReset(41, 42));
	}

	SECTION("skips and repeated dispatches are gaps")
	{
		REQUIRE(NeedsPeripheryHistoryReset(41, 43));
		REQUIRE(NeedsPeripheryHistoryReset(41, 41));
	}

	SECTION("frame wrap does not reuse the sentinel as valid history")
	{
		constexpr auto maxFrame = std::numeric_limits<std::uint32_t>::max();
		REQUIRE_FALSE(NeedsPeripheryHistoryReset(maxFrame - 1, maxFrame));
		REQUIRE(NeedsPeripheryHistoryReset(maxFrame, 0));
	}
}
