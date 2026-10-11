#pragma once

#include <cstdint>

namespace PerfModePolicy
{
	struct MapDisplayExtent
	{
		uint32_t eyeWidth;
		uint32_t eyeHeight;
		uint32_t compositeWidth;
		uint32_t compositeHeight;
	};

	constexpr MapDisplayExtent GetMapDisplayExtent(uint32_t a_displayEyeWidth, uint32_t a_displayEyeHeight)
	{
		return { a_displayEyeWidth, a_displayEyeHeight, a_displayEyeWidth * 2, a_displayEyeHeight };
	}

	constexpr bool IsMapCopyEffect(bool a_isCopy, bool a_isCopyCustomViewport,
		bool a_isCopyDynamicFetchDisabled)
	{
		return a_isCopy || a_isCopyCustomViewport || a_isCopyDynamicFetchDisabled;
	}

	constexpr bool AreMapRoutePrerequisitesReady(bool a_hookActive, bool a_fakeDSV,
		bool a_renderer, bool a_shadowState, bool a_stateUpdateFlags)
	{
		return a_hookActive && a_fakeDSV && a_renderer && a_shadowState && a_stateUpdateFlags;
	}

	constexpr bool CanRouteMapCopyWithDepthState(bool a_isMainDepth, bool a_isMainCopyDepth,
		bool a_depthSelectionPending)
	{
		return a_isMainDepth || a_isMainCopyDepth || a_depthSelectionPending;
	}

	constexpr bool ShouldSwapMapCopyDepthState(bool a_isMainDepth, bool a_isMainCopyDepth)
	{
		return a_isMainDepth || a_isMainCopyDepth;
	}

	// Keep the testable decision separate from D3D resource state. The caller
	// additionally verifies that kMENUBG and the fake depth surface are ready.
	constexpr bool ShouldRouteMapAtDisplayResolution(bool a_enabled, bool a_isVR,
		bool a_perfModeActive, bool a_mapMenuOpen, bool a_resourcesReady)
	{
		return a_enabled && a_isVR && a_perfModeActive && a_mapMenuOpen && a_resourcesReady;
	}

	// The imagespace Render hook runs before the original renderer binds the
	// copy destination. Scope routing to a copy effect in the open VR map; do
	// not use the output-merger RTV at hook entry as a destination predicate.
	constexpr bool ShouldRouteMapCopyAtDisplayResolution(bool a_isMapCopy,
		bool a_isVR, bool a_mapMenuOpen, bool a_routeReady)
	{
		return a_isMapCopy && a_isVR && a_mapMenuOpen && a_routeReady;
	}
}
