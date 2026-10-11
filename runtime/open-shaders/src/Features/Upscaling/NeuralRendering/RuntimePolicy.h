#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace NeuralRendering
{
	/**
	 * @brief Reduced model tiers remain locked off for the normal/detail route. The
	 * explicitly opt-in Periphery NR pass is the only reduced-resolution exception.
	 */
	inline constexpr bool kFullResolutionNeuralRenderingOnly = true;

	/** @brief Centered NR-only coverage presets (linear per axis). 100 = the full eye. */
	inline constexpr std::array<std::uint32_t, 11> kNeuralCoveragePresets{ 100, 95, 90, 85, 80, 75, 70, 60, 55, 50, 45 };
	inline constexpr std::array<std::uint32_t, 4> kPeripheryResolutionPresets{ 50, 58, 66, 75 };

	[[nodiscard]] inline std::uint32_t NormalizePeripheryResolution(std::uint32_t percent)
	{
		return std::find(kPeripheryResolutionPresets.begin(), kPeripheryResolutionPresets.end(), percent) !=
			       kPeripheryResolutionPresets.end() ? percent : 50u;
	}

	[[nodiscard]] inline float CentreFeatherWeight(float edgeDistancePx, float featherWidthPx)
	{
		const float t = std::clamp(edgeDistancePx / std::max(featherWidthPx, 1.0f), 0.0f, 1.0f);
		return 0.5f - 0.5f * std::cos(3.14159265358979323846f * t);
	}

	[[nodiscard]] inline float PeripheryComposeChannel(float raw, float peripheryOutput, float centreOutput,
		float coarsePeriphery, float coarseCentre, float centreWeight, float lightingGain)
	{
		const float effP = peripheryOutput - raw;
		const float effC = centreOutput - raw;
		const float w = std::clamp(centreWeight, 0.0f, 1.0f);
		return raw + std::clamp(lightingGain, 0.0f, 1.5f) * coarsePeriphery +
			w * (effC - coarseCentre) + (1.0f - w) * (effP - coarsePeriphery);
	}

	[[nodiscard]] inline std::uint32_t NormalizeNeuralCoverage(std::uint32_t percent)
	{
		return std::find(kNeuralCoveragePresets.begin(), kNeuralCoveragePresets.end(), percent) !=
		               kNeuralCoveragePresets.end() ?
		           percent :
		           100u;
	}

	/**
	 * @brief Feature 18 receives motion-vector scale in guide pixels (UV * guide extent).
	 * Display-space passes that add motion to color-pixel positions must convert it; the
	 * post-upscale VR route has guides at render resolution and color at display resolution.
	 */
	[[nodiscard]] inline float GuideToColorMotionScale(float guidePixelScale, std::uint32_t colorExtent,
		std::uint32_t guideExtent)
	{
		return guideExtent == 0 ? guidePixelScale :
		                          guidePixelScale * static_cast<float>(colorExtent) / static_cast<float>(guideExtent);
	}

	/** @brief One eye's centered NR-only region in local eye coordinates, plus its matching guide rect. */
	struct NeuralCoverageRect
	{
		std::uint32_t colorX = 0;
		std::uint32_t colorY = 0;
		std::uint32_t colorWidth = 0;
		std::uint32_t colorHeight = 0;
		std::uint32_t guideX = 0;
		std::uint32_t guideY = 0;
		std::uint32_t guideWidth = 0;
		std::uint32_t guideHeight = 0;

		[[nodiscard]] bool IsValid() const { return colorWidth && colorHeight && guideWidth && guideHeight; }
		bool operator==(const NeuralCoverageRect&) const = default;
	};

	/** @brief Stable stereo crop geometry associated with stateful Feature 18 histories. */
	struct NeuralCoverageHistoryGeometry
	{
		bool stable = false;
		std::uint32_t sourceColorWidth = 0;
		std::uint32_t sourceColorHeight = 0;
		std::uint32_t sourceGuideWidth = 0;
		std::uint32_t sourceGuideHeight = 0;
		std::array<NeuralCoverageRect, 2> eyes{};
		bool operator==(const NeuralCoverageHistoryGeometry&) const = default;
	};

	/**
	 * @brief A changed stable eye rect must reseed both stereo Feature 18 histories.
	 * Dynamic crops are ignored frame-to-frame, but crossing between a stable and
	 * dynamic layout still invalidates the previous stable history once.
	 */
	[[nodiscard]] inline bool NeuralCoverageHistoryGeometryChanged(
		const NeuralCoverageHistoryGeometry& previous, const NeuralCoverageHistoryGeometry& current)
	{
		const bool sourceExtentsChanged = previous.sourceColorWidth != current.sourceColorWidth ||
			previous.sourceColorHeight != current.sourceColorHeight ||
			previous.sourceGuideWidth != current.sourceGuideWidth ||
			previous.sourceGuideHeight != current.sourceGuideHeight;
		if (sourceExtentsChanged)
			return true;

		// Dynamic crop origins can move every frame; only compare crop-local
		// rectangles while either side represents a stable route/layout.
		return (previous.stable || current.stable) && previous != current;
	}

	/** @brief Raw off-axis tangent bounds in the OpenVR convention (top is negative-up). */
	struct ProjectionTangents
	{
		double left = 0.0;
		double right = 0.0;
		double top = 0.0;
		double bottom = 0.0;
	};

	/** @brief Pixel location of the forward direction in one eye's render buffer. */
	struct ProjectionPixelCenter
	{
		double x = 0.0;
		double y = 0.0;
	};

	/**
	 * @brief Converts row-vector D3D off-axis projection scale/offset terms to tangent bounds.
	 * NDC is x' = xScale*x + xOffset and y' = yScale*y + yOffset for a point on the
	 * forward axis. The D3D viewport maps +Y NDC to the top of the image; the vertical
	 * tangent fields follow OpenVR's top-negative/bottom-positive ordering.
	 */
	[[nodiscard]] inline bool TryProjectionTangentsFromD3DProjectionMatrixScaleOffset(double xScale,
		double yScale, double xOffset, double yOffset, ProjectionTangents& tangents)
	{
		if (!std::isfinite(xScale) || !std::isfinite(yScale) || !std::isfinite(xOffset) || !std::isfinite(yOffset) ||
		    xScale <= 0.0 || yScale <= 0.0)
			return false;
		const ProjectionTangents candidate{
			(-1.0 - xOffset) / xScale,
			(1.0 - xOffset) / xScale,
			(yOffset - 1.0) / yScale,
			(yOffset + 1.0) / yScale,
		};
		if (!std::isfinite(candidate.left) || !std::isfinite(candidate.right) ||
		    !std::isfinite(candidate.top) || !std::isfinite(candidate.bottom) ||
		    !(candidate.left < candidate.right) || !(candidate.top < candidate.bottom))
			return false;
		tangents = candidate;
		return true;
	}

	/** @brief Converts validated tangent bounds to a forward-direction pixel center, clamped to the eye. */
	[[nodiscard]] inline bool TryProjectionPixelCenterFromTangents(const ProjectionTangents& tangents,
		std::uint32_t eyeWidth, std::uint32_t eyeHeight, ProjectionPixelCenter& center)
	{
		if (!eyeWidth || !eyeHeight || !std::isfinite(tangents.left) || !std::isfinite(tangents.right) ||
		    !std::isfinite(tangents.top) || !std::isfinite(tangents.bottom) ||
		    !(tangents.left < tangents.right) || !(tangents.top < tangents.bottom))
			return false;
		const double x = (-tangents.left) / (tangents.right - tangents.left) * eyeWidth;
		const double y = (-tangents.top) / (tangents.bottom - tangents.top) * eyeHeight;
		if (!std::isfinite(x) || !std::isfinite(y))
			return false;
		center.x = std::clamp(x, 0.0, static_cast<double>(eyeWidth));
		center.y = std::clamp(y, 0.0, static_cast<double>(eyeHeight));
		return true;
	}

	/**
	 * @brief Picks the guide rectangle first in whole guide pixels, then maps its edges to
	 * display pixels, so color/guide misalignment stays below half a display pixel.
	 */
	[[nodiscard]] inline NeuralCoverageRect ComputeNeuralCoverageRect(std::uint32_t eyeWidth, std::uint32_t eyeHeight,
		std::uint32_t guideWidth, std::uint32_t guideHeight, std::uint32_t coveragePercent)
	{
		NeuralCoverageRect rect{};
		if (!eyeWidth || !eyeHeight || guideWidth < 2 || guideHeight < 2)
			return rect;
		coveragePercent = NormalizeNeuralCoverage(coveragePercent);
		auto guideSpan = [&](std::uint32_t extent) {
			const auto span = static_cast<std::uint32_t>((static_cast<std::uint64_t>(extent) * coveragePercent + 50) / 100);
			// Keep the span parity equal to the extent so the rectangle is exactly centered.
			const auto centered = span + ((extent - span) & 1u);
			return std::clamp<std::uint32_t>(centered, 2u, extent);
		};
		rect.guideWidth = guideSpan(guideWidth);
		rect.guideHeight = guideSpan(guideHeight);
		rect.guideX = (guideWidth - rect.guideWidth) / 2;
		rect.guideY = (guideHeight - rect.guideHeight) / 2;
		auto mapEdge = [](std::uint32_t edge, std::uint32_t colorExtent, std::uint32_t guideExtent) {
			return static_cast<std::uint32_t>((static_cast<std::uint64_t>(edge) * colorExtent + guideExtent / 2) / guideExtent);
		};
		const auto left = mapEdge(rect.guideX, eyeWidth, guideWidth);
		const auto right = mapEdge(rect.guideX + rect.guideWidth, eyeWidth, guideWidth);
		const auto top = mapEdge(rect.guideY, eyeHeight, guideHeight);
		const auto bottom = mapEdge(rect.guideY + rect.guideHeight, eyeHeight, guideHeight);
		rect.colorX = left;
		rect.colorY = top;
		rect.colorWidth = right > left ? right - left : 0;
		rect.colorHeight = bottom > top ? bottom - top : 0;
		return rect;
	}

	/**
	 * @brief Computes the same guide-grid-sized coverage rect around a per-eye projection
	 * center. Offsets are display pixels. Invalid centers fall back to the eye center and
	 * non-finite offsets are ignored; the resulting center is always clamped inside the eye.
	 * The five-argument overload above remains the legacy byte-identical centered path.
	 */
	[[nodiscard]] inline NeuralCoverageRect ComputeNeuralCoverageRect(std::uint32_t eyeWidth, std::uint32_t eyeHeight,
		std::uint32_t guideWidth, std::uint32_t guideHeight, std::uint32_t coveragePercent,
		double projectionCenterX, double projectionCenterY, double offsetX = 0.0, double offsetY = 0.0)
	{
		NeuralCoverageRect rect{};
		if (!eyeWidth || !eyeHeight || guideWidth < 2 || guideHeight < 2)
			return rect;
		coveragePercent = NormalizeNeuralCoverage(coveragePercent);
		auto guideSpan = [&](std::uint32_t extent) {
			const auto span = static_cast<std::uint32_t>((static_cast<std::uint64_t>(extent) * coveragePercent + 50) / 100);
			const auto centered = span + ((extent - span) & 1u);
			return std::clamp<std::uint32_t>(centered, 2u, extent);
		};
		rect.guideWidth = guideSpan(guideWidth);
		rect.guideHeight = guideSpan(guideHeight);
		auto snappedStart = [](double center, double offset, std::uint32_t colorExtent, std::uint32_t guideExtent,
			std::uint32_t span) {
			const double safeCenter = std::isfinite(center) ? center : static_cast<double>(colorExtent) * 0.5;
			const double safeOffset = std::isfinite(offset) ? offset : 0.0;
			const double colorCenter = std::clamp(safeCenter + safeOffset, 0.0, static_cast<double>(colorExtent));
			const double guideCenter = colorCenter * static_cast<double>(guideExtent) / colorExtent;
			const auto maxStart = guideExtent - span;
			const auto rounded = static_cast<std::int64_t>(std::floor(guideCenter - static_cast<double>(span) * 0.5 + 0.5));
			return static_cast<std::uint32_t>(std::clamp<std::int64_t>(rounded, 0, maxStart));
		};
		rect.guideX = snappedStart(projectionCenterX, offsetX, eyeWidth, guideWidth, rect.guideWidth);
		rect.guideY = snappedStart(projectionCenterY, offsetY, eyeHeight, guideHeight, rect.guideHeight);
		auto mapEdge = [](std::uint32_t edge, std::uint32_t colorExtent, std::uint32_t guideExtent) {
			return static_cast<std::uint32_t>((static_cast<std::uint64_t>(edge) * colorExtent + guideExtent / 2) / guideExtent);
		};
		const auto left = mapEdge(rect.guideX, eyeWidth, guideWidth);
		const auto right = mapEdge(rect.guideX + rect.guideWidth, eyeWidth, guideWidth);
		const auto top = mapEdge(rect.guideY, eyeHeight, guideHeight);
		const auto bottom = mapEdge(rect.guideY + rect.guideHeight, eyeHeight, guideHeight);
		rect.colorX = left;
		rect.colorY = top;
		rect.colorWidth = right > left ? right - left : 0;
		rect.colorHeight = bottom > top ? bottom - top : 0;
		return rect;
	}

	/**
	 * @brief Aligns two projection-centered crops to one stereo model size.
	 *
	 * The guide grid is authoritative, so color edges are mapped with the exact same
	 * edge rounding as ComputeNeuralCoverageRect. With a non-integer color/guide ratio,
	 * the mapped color span can differ by one pixel at different guide origins. Search
	 * at most eight guide pixels around each already-snapped origin for a shared span;
	 * this keeps each crop close to its requested projection center without changing its
	 * guide dimensions or introducing color/guide edge drift.
	 */
	[[nodiscard]] inline bool AlignNeuralCoverageRectsForStereo(std::uint32_t eyeWidth, std::uint32_t eyeHeight,
		std::uint32_t guideWidth, std::uint32_t guideHeight, NeuralCoverageRect& left, NeuralCoverageRect& right,
		std::uint32_t maxGuideShift = 8u)
	{
		if (!eyeWidth || !eyeHeight || guideWidth < 2u || guideHeight < 2u ||
			!left.IsValid() || !right.IsValid() ||
			left.guideWidth != right.guideWidth || left.guideHeight != right.guideHeight)
			return false;
		maxGuideShift = std::min(maxGuideShift, 8u);

		auto leftAligned = left;
		auto rightAligned = right;
		auto alignAxis = [&](std::uint32_t colorExtent, std::uint32_t guideExtent, std::uint32_t span,
			std::uint32_t& leftStart, std::uint32_t& rightStart, std::uint32_t& leftColorStart,
			std::uint32_t& rightColorStart, std::uint32_t& commonColorSpan) {
			if (span > guideExtent)
				return false;
			const std::uint32_t maxStart = guideExtent - span;
			const std::uint32_t preferredLeft = std::min(leftStart, maxStart);
			const std::uint32_t preferredRight = std::min(rightStart, maxStart);
			const auto mapEdge = [&](std::uint32_t edge) {
				return static_cast<std::uint32_t>((static_cast<std::uint64_t>(edge) * colorExtent + guideExtent / 2u) / guideExtent);
			};
			auto makeStarts = [&](std::uint32_t preferred, std::array<std::uint32_t, 17>& starts) {
				std::uint32_t count = 1u;
				starts[0] = preferred;
				for (std::uint32_t delta = 1u; delta <= maxGuideShift; ++delta) {
					if (preferred >= delta)
						starts[count++] = preferred - delta;
					if (preferred <= maxStart - std::min(delta, maxStart)) {
						const auto plus = preferred + delta;
						if (plus <= maxStart)
							starts[count++] = plus;
					}
				}
				return count;
			};
			std::array<std::uint32_t, 17> leftStarts{};
			std::array<std::uint32_t, 17> rightStarts{};
			const auto leftCount = makeStarts(preferredLeft, leftStarts);
			const auto rightCount = makeStarts(preferredRight, rightStarts);
			std::uint32_t bestCost = UINT32_MAX;
			std::uint32_t bestLeft = preferredLeft;
			std::uint32_t bestRight = preferredRight;
			std::uint32_t bestLeftColor = 0u;
			std::uint32_t bestRightColor = 0u;
			std::uint32_t bestSpan = 0u;
			for (std::uint32_t li = 0; li < leftCount; ++li) {
				const auto leftColorStartCandidate = mapEdge(leftStarts[li]);
				const auto leftColorEndCandidate = mapEdge(leftStarts[li] + span);
				if (leftColorEndCandidate <= leftColorStartCandidate)
					continue;
				for (std::uint32_t ri = 0; ri < rightCount; ++ri) {
					const auto rightColorStartCandidate = mapEdge(rightStarts[ri]);
					const auto rightColorEndCandidate = mapEdge(rightStarts[ri] + span);
					if (rightColorEndCandidate <= rightColorStartCandidate ||
						leftColorEndCandidate - leftColorStartCandidate != rightColorEndCandidate - rightColorStartCandidate)
						continue;
					const auto leftShift = leftStarts[li] > preferredLeft ? leftStarts[li] - preferredLeft : preferredLeft - leftStarts[li];
					const auto rightShift = rightStarts[ri] > preferredRight ? rightStarts[ri] - preferredRight : preferredRight - rightStarts[ri];
					const auto cost = leftShift + rightShift;
					if (cost < bestCost) {
						bestCost = cost;
						bestLeft = leftStarts[li];
						bestRight = rightStarts[ri];
						bestLeftColor = leftColorStartCandidate;
						bestRightColor = rightColorStartCandidate;
						bestSpan = leftColorEndCandidate - leftColorStartCandidate;
					}
				}
			}
			if (bestCost == UINT32_MAX)
				return false;
			leftStart = bestLeft;
			rightStart = bestRight;
			leftColorStart = bestLeftColor;
			rightColorStart = bestRightColor;
			commonColorSpan = bestSpan;
			return true;
		};

		if (!alignAxis(eyeWidth, guideWidth, left.guideWidth,
			leftAligned.guideX, rightAligned.guideX, leftAligned.colorX, rightAligned.colorX, leftAligned.colorWidth))
			return false;
		rightAligned.colorWidth = leftAligned.colorWidth;
		if (!alignAxis(eyeHeight, guideHeight, left.guideHeight,
			leftAligned.guideY, rightAligned.guideY, leftAligned.colorY, rightAligned.colorY, leftAligned.colorHeight))
			return false;
		rightAligned.colorHeight = leftAligned.colorHeight;
		left = leftAligned;
		right = rightAligned;
		return left.IsValid() && right.IsValid();
	}

	/**
	 * @brief Eye-staggered N2 residual reuse: one eye receives native Feature 18 per frame
	 * while the other reuses its previous-frame residual, keeping per-frame NR cost flat.
	 */
	[[nodiscard]] inline std::uint32_t StaggeredNativeEye(std::uint64_t temporalFrameIndex)
	{
		return static_cast<std::uint32_t>(temporalFrameIndex & 1u);
	}

	/** @brief Separates a model-tier change from changes that invalidate display-space handoff history. */
	struct TemporalHistoryConfig
	{
		bool adaptive = false;
		std::uint32_t modelResolution = 100;
		std::uint32_t cadence = 0;
		float depthThreshold = 0.05f;
		float colorTolerance = 0.08f;
		std::uint32_t passes = 1;

		bool operator==(const TemporalHistoryConfig&) const = default;

		[[nodiscard]] bool PreservesHandoff(const TemporalHistoryConfig& next) const
		{
			auto sameTier = next;
			sameTier.modelResolution = modelResolution;
			return adaptive && next.adaptive && *this == sameTier;
		}
	};

	/** @brief Tracks the active/previous tier; the renderer may add its two immediate neighbors for bounded prewarm. */
	struct TierResidency
	{
		std::uint32_t current = UINT32_MAX;
		std::uint32_t previous = UINT32_MAX;

		bool Select(std::uint32_t tier, bool retainPrevious)
		{
			const bool changed = current != tier;
			if (changed) {
				previous = current;
				current = tier;
			}
			if (!retainPrevious)
				previous = UINT32_MAX;
			return changed;
		}

		[[nodiscard]] bool Contains(std::uint32_t tier) const
		{
			return tier == current || tier == previous;
		}
	};
}
