#pragma once
#include <cstdint>

namespace LightLimitFixPolicy
{
	struct SceneIdentity
	{
		bool interior = false;
		uint32_t cell = 0;
		uint32_t worldspace = 0;
	};

	constexpr bool SceneIdentityChanged(const SceneIdentity& previous, const SceneIdentity& current)
	{
		return previous.interior != current.interior ||
			(previous.interior && current.interior && previous.cell != current.cell) ||
			previous.worldspace != current.worldspace;
	}

	constexpr bool ShouldDrainPendingResetBeforeVR(bool bounded, bool pending,
		bool sceneReady, bool cameraReady, bool portalTransitioning)
	{
		return bounded && pending && sceneReady && cameraReady && !portalTransitioning;
	}
}
