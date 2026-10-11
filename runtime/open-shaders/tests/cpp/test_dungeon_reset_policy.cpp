#include <catch2/catch_test_macros.hpp>
#include "Features/LightLimitFix/SceneHistoryResetPolicy.h"

TEST_CASE("scene identity history reset policy tracks interior and worldspace transitions")
{
	using LightLimitFixPolicy::SceneIdentity;
	using LightLimitFixPolicy::SceneIdentityChanged;
	REQUIRE_FALSE(SceneIdentityChanged({false, 1, 10}, {false, 2, 10})); // exterior cell streaming is ignored
	REQUIRE(SceneIdentityChanged({false, 0, 10}, {true, 20, 10}));
	REQUIRE(SceneIdentityChanged({true, 20, 10}, {true, 21, 10}));
	REQUIRE(SceneIdentityChanged({true, 20, 10}, {false, 0, 10}));
	REQUIRE(SceneIdentityChanged({false, 0, 10}, {false, 0, 11}));
}

TEST_CASE("bounded reset drain bypasses the legacy VR shadow skip only at a safe boundary")
{
	using LightLimitFixPolicy::ShouldDrainPendingResetBeforeVR;
	REQUIRE(ShouldDrainPendingResetBeforeVR(true, true, true, true, false));
	REQUIRE_FALSE(ShouldDrainPendingResetBeforeVR(false, true, true, true, false)); // 2.20.4 ordering
	REQUIRE_FALSE(ShouldDrainPendingResetBeforeVR(true, false, true, true, false));
	REQUIRE_FALSE(ShouldDrainPendingResetBeforeVR(true, true, false, true, false));
	REQUIRE_FALSE(ShouldDrainPendingResetBeforeVR(true, true, true, false, false));
	REQUIRE_FALSE(ShouldDrainPendingResetBeforeVR(true, true, true, true, true));
}
