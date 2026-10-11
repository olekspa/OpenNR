#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace PerfModePolicy
{
	// 0 leaves the user's normal DLSS preset untouched; 1..4 are J/K/L/M.
	constexpr uint32_t ClampMapDlssPreset(uint32_t a_preset)
	{
		return std::min(a_preset, 4u);
	}

	inline float ClampMapSharpness(float a_sharpness)
	{
		return std::isfinite(a_sharpness) ? std::clamp(a_sharpness, 0.0f, 1.0f) : 0.0f;
	}

	inline float ClampMapMipBiasOffset(float a_offset)
	{
		return std::isfinite(a_offset) ? std::clamp(a_offset, -3.0f, 0.0f) : 0.0f;
	}

	constexpr bool ShouldUseMapQualityOverride(bool a_isVR, bool a_perfModeActive,
		bool a_mapOpen, bool a_staticBackdropOpen)
	{
		return a_isVR && a_perfModeActive && a_mapOpen && a_staticBackdropOpen;
	}

	constexpr bool ShouldApplyMapMipBias(bool a_isVR, bool a_mapOpen)
	{
		return a_isVR && a_mapOpen;
	}

	constexpr float ApplyMapMipBiasOffset(float a_gameBias, float a_mapOffset,
		bool a_isVR, bool a_mapOpen)
	{
		return a_gameBias + ((a_isVR && a_mapOpen) ? a_mapOffset : 0.0f);
	}

	constexpr bool ShouldLogMapQuality(bool a_isVR, bool a_mapOpen, bool a_alreadyLogged)
	{
		return a_isVR && a_mapOpen && !a_alreadyLogged;
	}
}
