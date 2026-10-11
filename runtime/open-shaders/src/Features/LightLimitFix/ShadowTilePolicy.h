#pragma once

#include <cstdint>

namespace ShadowCasterManager
{
	// Mirrors the ShadowParam.y contract consumed by LightLimitFix.hlsli:
	// positive samples a shadow footprint, zero is fail-open/lit, negative is dark.
	constexpr float ApplySpotMissingTilePolicy(float a_shadowParamY, uint32_t a_shadowType,
		bool a_suppressed, bool a_failOpenMissingSpot)
	{
		if (a_suppressed)
			return -1.0f;
		if (a_shadowParamY != 0.0f || a_shadowType != 0 || a_failOpenMissingSpot)
			return a_shadowParamY;
		return -1.0f;
	}
}
