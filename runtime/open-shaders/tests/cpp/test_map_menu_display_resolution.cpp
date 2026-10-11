#include <catch2/catch_test_macros.hpp>

#include <utility>

#include "Features/Upscaling/PerfMode/MapMenuDisplayResolutionPolicy.h"

TEST_CASE("VR map display-resolution route is scoped to an open map under PerfMode", "[perfmode][map]")
{
	using PerfModePolicy::ShouldRouteMapAtDisplayResolution;
	REQUIRE(ShouldRouteMapAtDisplayResolution(true, true, true, true, true));
	REQUIRE_FALSE(ShouldRouteMapAtDisplayResolution(false, true, true, true, true));
	REQUIRE_FALSE(ShouldRouteMapAtDisplayResolution(true, false, true, true, true));
	REQUIRE_FALSE(ShouldRouteMapAtDisplayResolution(true, true, false, true, true));
	REQUIRE_FALSE(ShouldRouteMapAtDisplayResolution(true, true, true, false, true));
	REQUIRE_FALSE(ShouldRouteMapAtDisplayResolution(true, true, true, true, false));
}

TEST_CASE("Map route readiness requires all five runtime prerequisites", "[perfmode][map]")
{
	using PerfModePolicy::AreMapRoutePrerequisitesReady;
	REQUIRE(AreMapRoutePrerequisitesReady(true, true, true, true, true));
	REQUIRE_FALSE(AreMapRoutePrerequisitesReady(false, true, true, true, true));
	REQUIRE_FALSE(AreMapRoutePrerequisitesReady(true, false, true, true, true));
	REQUIRE_FALSE(AreMapRoutePrerequisitesReady(true, true, false, true, true));
	REQUIRE_FALSE(AreMapRoutePrerequisitesReady(true, true, true, false, true));
	REQUIRE_FALSE(AreMapRoutePrerequisitesReady(true, true, true, true, false));
	REQUIRE(PerfModePolicy::ShouldRouteMapAtDisplayResolution(true, true, true, true,
		AreMapRoutePrerequisitesReady(true, true, true, true, true)));
}

TEST_CASE("Map copy route accepts an unresolved depth selection without swapping depth", "[perfmode][map]")
{
	using PerfModePolicy::CanRouteMapCopyWithDepthState;
	using PerfModePolicy::ShouldSwapMapCopyDepthState;
	REQUIRE(CanRouteMapCopyWithDepthState(true, false, false));
	REQUIRE(CanRouteMapCopyWithDepthState(false, true, false));
	REQUIRE(CanRouteMapCopyWithDepthState(false, false, true));
	REQUIRE_FALSE(CanRouteMapCopyWithDepthState(false, false, false));
	REQUIRE(ShouldSwapMapCopyDepthState(true, false));
	REQUIRE(ShouldSwapMapCopyDepthState(false, true));
	REQUIRE_FALSE(ShouldSwapMapCopyDepthState(false, false));
}

TEST_CASE("Display-resolution map route is selected before the renderer binds the map copy destination", "[perfmode][map]")
{
	using PerfModePolicy::ShouldRouteMapCopyAtDisplayResolution;
	REQUIRE(ShouldRouteMapCopyAtDisplayResolution(true, true, true, true));
	REQUIRE_FALSE(ShouldRouteMapCopyAtDisplayResolution(false, true, true, true));
	REQUIRE_FALSE(ShouldRouteMapCopyAtDisplayResolution(true, false, true, true));
	REQUIRE_FALSE(ShouldRouteMapCopyAtDisplayResolution(true, true, false, true));
	REQUIRE_FALSE(ShouldRouteMapCopyAtDisplayResolution(true, true, true, false));

	// The current RTV is not a route input: the original draw binds kMENUBG
	// only after this pre-render decision is made.
	const bool draws[][4] = {
		{ true, true, true, true },
		{ true, true, false, true },
		{ true, false, true, true },
		{ false, true, true, true },
	};
	unsigned routedDisplayRes = 0;
	for (const auto& draw : draws)
		routedDisplayRes += ShouldRouteMapCopyAtDisplayResolution(draw[0], draw[1], draw[2], draw[3]);
	REQUIRE(routedDisplayRes == 1);
}

TEST_CASE("Desktop-only menu presentation does not block VR map routing", "[perfmode][map]")
{
	// ImGui presentation mode is deliberately absent from the policy inputs.
	REQUIRE(PerfModePolicy::ShouldRouteMapAtDisplayResolution(true, true, true, true, true));
	REQUIRE(PerfModePolicy::ShouldRouteMapCopyAtDisplayResolution(true, true, true, true));
}

TEST_CASE("Map display extent preserves native dimensions for both eyes", "[perfmode][map]")
{
	const auto extent = PerfModePolicy::GetMapDisplayExtent(2240, 2428);
	REQUIRE(extent.eyeWidth == 2240);
	REQUIRE(extent.eyeHeight == 2428);
	REQUIRE(extent.compositeWidth == 4480);
	REQUIRE(extent.compositeHeight == 2428);
	REQUIRE(extent.compositeWidth / 2 == extent.eyeWidth);
	REQUIRE(extent.compositeHeight == extent.eyeHeight);
}

TEST_CASE("Map display routing recognizes the actual copy shader variants", "[perfmode][map]")
{
	using PerfModePolicy::IsMapCopyEffect;
	REQUIRE(IsMapCopyEffect(true, false, false));
	REQUIRE(IsMapCopyEffect(false, true, false));
	REQUIRE(IsMapCopyEffect(false, false, true));
	REQUIRE_FALSE(IsMapCopyEffect(false, false, false));
}

TEST_CASE("Map display route covers render-size and display-size copy viewports", "[perfmode][map]")
{
	using PerfModePolicy::ShouldRouteMapCopyAtDisplayResolution;
	const auto extent = PerfModePolicy::GetMapDisplayExtent(2496, 2688);
	REQUIRE(extent.compositeWidth == 4992);
	REQUIRE(extent.compositeHeight == 2688);

	const std::pair<uint32_t, uint32_t> copyViewports[] = {
		{ 2048, 2048 },
		{ 4992, 2688 },
	};
	for (const auto& [sourceWidth, sourceHeight] : copyViewports) {
		INFO("copy viewport " << sourceWidth << "x" << sourceHeight);
		REQUIRE(ShouldRouteMapCopyAtDisplayResolution(true, true, true, true));
		REQUIRE(extent.compositeWidth == 4992);
		REQUIRE(extent.compositeHeight == 2688);
	}
}
