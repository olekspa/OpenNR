#include <catch2/catch_test_macros.hpp>
#include <limits>

#include "Features/Upscaling/PerfMode/MapSupersamplePolicy.h"

TEST_CASE("Map symbol supersample scale clamps and one disables", "[perfmode][map][supersample]")
{
	using PerfModePolicy::ClampMapSupersampleScale;
	REQUIRE(ClampMapSupersampleScale(0.2f) == 1.0f);
	REQUIRE(ClampMapSupersampleScale(1.0f) == 1.0f);
	REQUIRE(ClampMapSupersampleScale(1.5f) == 1.5f);
	REQUIRE(ClampMapSupersampleScale(2.8f) == 2.0f);
	REQUIRE(ClampMapSupersampleScale(std::numeric_limits<float>::quiet_NaN()) == 1.0f);
}

TEST_CASE("Map symbol supersample aligns dimensions and bounds both planes", "[perfmode][map][supersample]")
{
	const auto twoX = PerfModePolicy::GetMapSupersampleExtent(2048, 2048, 2.0f, 4, 4);
	REQUIRE(twoX.valid);
	REQUIRE(twoX.width == 4096);
	REQUIRE(twoX.height == 4096);
	REQUIRE(twoX.colorBytes == 64ull * 1024ull * 1024ull);
	REQUIRE(twoX.depthBytes == 64ull * 1024ull * 1024ull);
	REQUIRE(twoX.colorBytes + twoX.depthBytes == PerfModePolicy::kMapSupersamplePairByteCap);
	REQUIRE_FALSE(PerfModePolicy::GetMapSupersampleExtent(2048, 2048, 2.0f, 4, 8).valid);
	const auto aligned = PerfModePolicy::GetMapSupersampleExtent(2049, 2047, 1.5f, 4, 4);
	REQUIRE(aligned.valid);
	REQUIRE(aligned.width % 8 == 0);
	REQUIRE(aligned.height % 8 == 0);
	REQUIRE_FALSE(PerfModePolicy::GetMapSupersampleExtent(4096, 4096, 2.0f, 4, 4).valid);
	REQUIRE_FALSE(PerfModePolicy::GetMapSupersampleExtent(16384, 8, 2.0f, 1, 1).valid);
	REQUIRE_FALSE(PerfModePolicy::GetMapSupersampleExtent(0, 2048, 2.0f, 4, 4).valid);
	REQUIRE(PerfModePolicy::GetMapDisplayCoverageScale(2048, 2048, 2496, 2688) == 1.3125f);
	REQUIRE(PerfModePolicy::GetMapDisplayCoverageScale(2048, 2048, 4992, 2688) == 2.0f);
}

TEST_CASE("Map symbol target restores on close, VR exit, or PerfMode latch loss", "[perfmode][map][supersample]")
{
	using PerfModePolicy::ShouldRestoreMapSupersample;
	REQUIRE_FALSE(ShouldRestoreMapSupersample(false, true, true, true, true));
	REQUIRE_FALSE(ShouldRestoreMapSupersample(true, true, true, true, true));
	REQUIRE(ShouldRestoreMapSupersample(true, false, true, true, true));
	REQUIRE(ShouldRestoreMapSupersample(true, true, false, true, true));
	REQUIRE(ShouldRestoreMapSupersample(true, true, true, false, true));
	REQUIRE(ShouldRestoreMapSupersample(true, true, true, true, false));
}
