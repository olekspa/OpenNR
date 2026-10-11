#include <catch2/catch_test_macros.hpp>
#include "Features/LightLimitFix/ShadowTilePolicy.h"

TEST_CASE("missing spotlight tile policy preserves valid and suppressed states")
{
	using ShadowCasterManager::ApplySpotMissingTilePolicy;
	SECTION("valid spot tile is unchanged")
		REQUIRE(ApplySpotMissingTilePolicy(0.75f, 0, false, true) == 0.75f);
	SECTION("missing unsuppressed spot fails open by default")
		REQUIRE(ApplySpotMissingTilePolicy(0.0f, 0, false, true) == 0.0f);
	SECTION("missing unsuppressed spot can retain legacy fail dark")
		REQUIRE(ApplySpotMissingTilePolicy(0.0f, 0, false, false) == -1.0f);
	SECTION("explicit suppression always remains dark")
		REQUIRE(ApplySpotMissingTilePolicy(-1.0f, 0, true, true) == -1.0f);
	SECTION("non-spot safe-lit sentinel is unchanged")
		REQUIRE(ApplySpotMissingTilePolicy(0.0f, 2, false, false) == 0.0f);
}
