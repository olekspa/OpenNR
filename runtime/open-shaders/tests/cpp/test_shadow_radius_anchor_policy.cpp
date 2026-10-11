#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "Features/LightLimitFix/ShadowRadiusAnchorPolicy.h"

TEST_CASE("shadow radius anchor advances once per frame and can be bypassed")
{
	using namespace ShadowCasterManager::RadiusAnchorPolicy;

	SECTION("same-frame consumers read one stable radius")
	{
		Anchor anchor{ 100.0f, 12 };
		Advance(anchor, 300.0f, 12);
		REQUIRE(anchor.radius == 100.0f);
	}

	SECTION("next frame advances the 0.15 EMA once")
	{
		Anchor anchor{ 100.0f, 12 };
		Advance(anchor, 200.0f, 13);
		REQUIRE(std::abs(anchor.radius - 115.0f) < 0.0001f);
		Advance(anchor, 300.0f, 13);
		REQUIRE(std::abs(anchor.radius - 115.0f) < 0.0001f);
	}

	SECTION("toggle off selects the live 2.20.5 decision input")
	{
		REQUIRE(Select(false, 275.0f, 120.0f) == 275.0f);
		REQUIRE(Select(true, 275.0f, 120.0f) == 120.0f);
	}
}
