#include <catch2/catch_test_macros.hpp>

#include "Features/Upscaling/NeuralRendering/IntegrationPolicy.h"

#include <limits>

using namespace NeuralRendering;

TEST_CASE("Primary NR history resets after a real frame gap", "[nr][history]")
{
	REQUIRE_FALSE(HasFrameGap(UINT32_MAX, 10));
	REQUIRE_FALSE(HasFrameGap(9, 10));
	REQUIRE(HasFrameGap(8, 10));
	REQUIRE(HasFrameGap(UINT32_MAX - 1u, 1u));
}

TEST_CASE("Stereo crop extents tolerate floating point noise and preserve full eye", "[nr][crop]")
{
	REQUIRE(NearlyEqualUVExtent(0.75f, 0.750001f));
	REQUIRE_FALSE(NearlyEqualUVExtent(0.75f, 0.7501f));
	REQUIRE_FALSE(NearlyEqualUVExtent(0.75f, std::numeric_limits<float>::quiet_NaN()));
	REQUIRE(FullEyeExtent(2496, 0.999f, true) == 2496);
	REQUIRE(FullEyeExtent(2496, 0.75f, false) == 1872);
}

TEST_CASE("Only the first two consecutive NR fence timeouts are retryable", "[nr][fence]")
{
	REQUIRE(ShouldDeferTimeoutFailure(1));
	REQUIRE(ShouldDeferTimeoutFailure(2));
	REQUIRE_FALSE(ShouldDeferTimeoutFailure(3));
}

TEST_CASE("A transient pre-upscale fence skip preserves the live renderer", "[nr][fence]")
{
	REQUIRE_FALSE(ShouldResetAfterApplyFailure(true));
	REQUIRE(ShouldResetAfterApplyFailure(false));
}

TEST_CASE("A deferred manual reset remains queued for a later frame", "[nr][reset]")
{
	REQUIRE(ShouldRearmManualReset(true));
	REQUIRE_FALSE(ShouldRearmManualReset(false));
}
