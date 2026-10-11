#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace PerfModePolicy
{
	inline constexpr std::uint64_t kMapSupersamplePlaneByteCap = 64ull * 1024ull * 1024ull;
	inline constexpr std::uint64_t kMapSupersamplePairByteCap = 2ull * kMapSupersamplePlaneByteCap;
	inline constexpr std::uint32_t kMapSupersampleMaxDimension = 16384;

	struct MapSupersampleExtent
	{
		std::uint32_t width;
		std::uint32_t height;
		float scaleX;
		float scaleY;
		std::uint64_t colorBytes;
		std::uint64_t depthBytes;
		bool valid;
	};

	inline float ClampMapSupersampleScale(float a_scale)
	{
		if (!std::isfinite(a_scale))
			return 1.0f;
		return a_scale < 1.0f ? 1.0f : (a_scale > 2.0f ? 2.0f : a_scale);
	}

	constexpr bool ShouldRestoreMapSupersample(bool a_active, bool a_mapOpen,
		bool a_perfModeActive, bool a_isVR, bool a_settingEnabled)
	{
		return a_active && (!a_mapOpen || !a_perfModeActive || !a_isVR || !a_settingEnabled);
	}

	inline float GetMapDisplayCoverageScale(std::uint32_t a_nativeWidth, std::uint32_t a_nativeHeight,
		std::uint32_t a_displayEyeWidth, std::uint32_t a_displayEyeHeight)
	{
		if (!a_nativeWidth || !a_nativeHeight)
			return 1.0f;
		return std::clamp(std::max(static_cast<float>(a_displayEyeWidth) / a_nativeWidth,
			static_cast<float>(a_displayEyeHeight) / a_nativeHeight), 1.0f, 2.0f);
	}

	inline MapSupersampleExtent GetMapSupersampleExtent(std::uint32_t a_nativeWidth,
		std::uint32_t a_nativeHeight, float a_scale, std::uint32_t a_colorBytesPerPixel,
		std::uint32_t a_depthBytesPerPixel)
	{
		MapSupersampleExtent result{};
		if (!a_nativeWidth || !a_nativeHeight ||
			a_nativeWidth > kMapSupersampleMaxDimension || a_nativeHeight > kMapSupersampleMaxDimension ||
			!a_colorBytesPerPixel || !a_depthBytesPerPixel)
			return result;

		const auto scale = ClampMapSupersampleScale(a_scale);
		auto scaledAligned = [scale](std::uint32_t value) {
			const auto rounded = static_cast<std::uint64_t>(std::ceil(static_cast<double>(value) * scale));
			return static_cast<std::uint32_t>((rounded + 7ull) & ~7ull);
		};
		result.width = scaledAligned(a_nativeWidth);
		result.height = scaledAligned(a_nativeHeight);
		result.scaleX = static_cast<float>(result.width) / static_cast<float>(a_nativeWidth);
		result.scaleY = static_cast<float>(result.height) / static_cast<float>(a_nativeHeight);
		const auto pixels = static_cast<std::uint64_t>(result.width) * result.height;
		result.colorBytes = pixels * a_colorBytesPerPixel;
		result.depthBytes = pixels * a_depthBytesPerPixel;
		const auto pairBytes = result.colorBytes + result.depthBytes;
		result.valid = result.width > a_nativeWidth && result.height > a_nativeHeight &&
			result.width <= kMapSupersampleMaxDimension && result.height <= kMapSupersampleMaxDimension &&
			result.colorBytes <= kMapSupersamplePlaneByteCap &&
			result.depthBytes <= kMapSupersamplePlaneByteCap &&
			pairBytes <= kMapSupersamplePairByteCap;
		return result;
	}
}
