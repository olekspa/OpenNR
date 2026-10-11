#include <catch2/catch_test_macros.hpp>
#include <limits>

#include "Features/Upscaling/PerfMode/MapQualityPolicy.h"

TEST_CASE("Map DLSS preset policy keeps unchanged and clamps J through M", "[perfmode][map][quality]")
{
	using PerfModePolicy::ClampMapDlssPreset;
	REQUIRE(ClampMapDlssPreset(0.0f) == 0);
	REQUIRE(ClampMapDlssPreset(2.0f) == 2);
	REQUIRE(ClampMapDlssPreset(9.0f) == 4);
	REQUIRE(ClampMapDlssPreset(std::numeric_limits<uint32_t>::max()) == 4);
}

TEST_CASE("Map sharpness and mip-bias offset clamp safely", "[perfmode][map][quality]")
{
	using PerfModePolicy::ClampMapMipBiasOffset;
	using PerfModePolicy::ClampMapSharpness;
	REQUIRE(ClampMapSharpness(-1.0f) == 0.0f);
	REQUIRE(ClampMapSharpness(0.4f) == 0.4f);
	REQUIRE(ClampMapSharpness(2.0f) == 1.0f);
	REQUIRE(ClampMapSharpness(std::numeric_limits<float>::infinity()) == 0.0f);
	REQUIRE(ClampMapMipBiasOffset(-4.0f) == -3.0f);
	REQUIRE(ClampMapMipBiasOffset(-1.25f) == -1.25f);
	REQUIRE(ClampMapMipBiasOffset(1.0f) == 0.0f);
	REQUIRE(ClampMapMipBiasOffset(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
}

TEST_CASE("Map DLSS overrides require the VR PerfMode map backdrop", "[perfmode][map][quality]")
{
	using PerfModePolicy::ShouldUseMapQualityOverride;
	REQUIRE(ShouldUseMapQualityOverride(true, true, true, true));
	REQUIRE_FALSE(ShouldUseMapQualityOverride(false, true, true, true));
	REQUIRE_FALSE(ShouldUseMapQualityOverride(true, false, true, true));
	REQUIRE_FALSE(ShouldUseMapQualityOverride(true, true, false, true));
	REQUIRE_FALSE(ShouldUseMapQualityOverride(true, true, true, false));
}

TEST_CASE("Map mip-bias offset is additive and neutral outside an open VR map", "[perfmode][map][quality]")
{
	using PerfModePolicy::ApplyMapMipBiasOffset;
	REQUIRE(ApplyMapMipBiasOffset(-1.5f, -1.0f, true, true) == -2.5f);
	REQUIRE(ApplyMapMipBiasOffset(-1.5f, -1.0f, true, false) == -1.5f);
	REQUIRE(ApplyMapMipBiasOffset(-1.5f, -1.0f, false, true) == -1.5f);
}

TEST_CASE("Map quality log emits once until the map closes", "[perfmode][map][quality]")
{
	using PerfModePolicy::ShouldLogMapQuality;
	REQUIRE(ShouldLogMapQuality(true, true, false));
	REQUIRE_FALSE(ShouldLogMapQuality(true, true, true));
	REQUIRE_FALSE(ShouldLogMapQuality(false, true, false));
	REQUIRE_FALSE(ShouldLogMapQuality(true, false, false));
}
