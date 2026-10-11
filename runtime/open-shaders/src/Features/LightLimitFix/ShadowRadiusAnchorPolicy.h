#pragma once

#include <cstdint>

namespace ShadowCasterManager::RadiusAnchorPolicy
{
	struct Anchor
	{
		float radius = 0.0f;
		int32_t frame = 0;
	};

	inline void Advance(Anchor& anchor, float liveRadius, int32_t frame) noexcept
	{
		// Consumers can visit a light multiple times in one scheduler frame.
		// Advance once so the EMA is independent of which consumer runs first.
		if (anchor.frame == frame)
			return;
		anchor.frame = frame;
		anchor.radius += 0.15f * (liveRadius - anchor.radius);
	}

	inline float Select(bool enabled, float liveRadius, float anchoredRadius) noexcept
	{
		return enabled ? anchoredRadius : liveRadius;
	}
}
