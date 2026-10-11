#include "Features/Upscaling/NeuralRendering/RuntimePolicy.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace NeuralRendering;

namespace
{
	NeuralCoverageRect ProjectionRectWithOffsets(double centerX, double centerY, float eyeOffsetX, float eyeOffsetY,
		float sharedOffsetX, float sharedOffsetY)
	{
		const double offsetX = eyeOffsetX == 0.0f ? sharedOffsetX : eyeOffsetX;
		const double offsetY = eyeOffsetY == 0.0f ? sharedOffsetY : eyeOffsetY;
		return ComputeNeuralCoverageRect(1120, 2428, 1120, 2428, 55, centerX, centerY, offsetX, offsetY);
	}
}

TEST_CASE("Per-eye NR offsets preserve centered and zero-offset projection crops", "[nr][policy][stereo-offset]")
{
	const auto centeredLeft = ComputeNeuralCoverageRect(1120, 2428, 1120, 2428, 55);
	const auto centeredRight = ComputeNeuralCoverageRect(1120, 2428, 1120, 2428, 55);
	REQUIRE(centeredLeft.colorX == 252);
	REQUIRE(centeredLeft.colorY == 546);
	REQUIRE(centeredLeft.colorWidth == 616);
	REQUIRE(centeredLeft.colorHeight == 1336);
	REQUIRE(centeredRight.colorX == centeredLeft.colorX);
	REQUIRE(centeredRight.colorY == centeredLeft.colorY);

	const auto leftProjection = ProjectionRectWithOffsets(700.0, 1350.0, 0.0f, 0.0f, 0.0f, 0.0f);
	const auto rightProjection = ProjectionRectWithOffsets(420.0, 1350.0, 0.0f, 0.0f, 0.0f, 0.0f);
	const auto oldLeftProjection = ComputeNeuralCoverageRect(1120, 2428, 1120, 2428, 55, 700.0, 1350.0);
	const auto oldRightProjection = ComputeNeuralCoverageRect(1120, 2428, 1120, 2428, 55, 420.0, 1350.0);
	REQUIRE(leftProjection.colorX == oldLeftProjection.colorX);
	REQUIRE(leftProjection.colorY == oldLeftProjection.colorY);
	REQUIRE(leftProjection.colorWidth == oldLeftProjection.colorWidth);
	REQUIRE(leftProjection.colorHeight == oldLeftProjection.colorHeight);
	REQUIRE(rightProjection.colorX == oldRightProjection.colorX);
	REQUIRE(rightProjection.colorY == oldRightProjection.colorY);
	REQUIRE(rightProjection.colorWidth == oldRightProjection.colorWidth);
	REQUIRE(rightProjection.colorHeight == oldRightProjection.colorHeight);
	REQUIRE(leftProjection.colorX == 392);
	REQUIRE(leftProjection.colorY == 682);
	REQUIRE(rightProjection.colorX == 112);
	REQUIRE(rightProjection.colorY == 682);
}

TEST_CASE("Per-eye NR offsets move independently and zero preserves shared fallback", "[nr][policy][stereo-offset]")
{
	const auto baselineLeft = ProjectionRectWithOffsets(700.0, 1350.0, 0.0f, 0.0f, 0.0f, 0.0f);
	const auto baselineRight = ProjectionRectWithOffsets(420.0, 1350.0, 0.0f, 0.0f, 0.0f, 0.0f);
	const auto shiftedLeft = ProjectionRectWithOffsets(700.0, 1350.0, 24.0f, 0.0f, 0.0f, 0.0f);
	const auto unchangedRight = ProjectionRectWithOffsets(420.0, 1350.0, 0.0f, 0.0f, 0.0f, 0.0f);
	const auto shiftedRight = ProjectionRectWithOffsets(420.0, 1350.0, 24.0f, 0.0f, 0.0f, 0.0f);
	REQUIRE(shiftedLeft.colorX > baselineLeft.colorX);
	REQUIRE(shiftedRight.colorX > baselineRight.colorX);
	REQUIRE(unchangedRight.colorX == baselineRight.colorX);
	REQUIRE(unchangedRight.colorY == baselineRight.colorY);

	const auto legacyFallbackLeft = ProjectionRectWithOffsets(700.0, 1350.0, 0.0f, 0.0f, 24.0f, -12.0f);
	const auto legacyExplicitLeft = ComputeNeuralCoverageRect(1120, 2428, 1120, 2428, 55,
		700.0, 1350.0, 24.0, -12.0);
	REQUIRE(legacyFallbackLeft.colorX == legacyExplicitLeft.colorX);
	REQUIRE(legacyFallbackLeft.colorY == legacyExplicitLeft.colorY);
	REQUIRE(legacyFallbackLeft.colorWidth == legacyExplicitLeft.colorWidth);
	REQUIRE(legacyFallbackLeft.colorHeight == legacyExplicitLeft.colorHeight);
}

TEST_CASE("Guide-pixel motion converts to display pixels for color-space passes", "[nr][policy]")
{
	// Post-upscale VR: UV motion scaled by the 1468 px guide must land in 2496 px color space.
	const float guideScale = 1468.0f;
	const float colorScale = GuideToColorMotionScale(guideScale, 2496, 1468);
	REQUIRE(colorScale == Catch::Approx(2496.0f));
	// A 0.01 UV step moves 24.96 display pixels, not the 14.68 the old shaders applied.
	REQUIRE(0.01f * colorScale == Catch::Approx(24.96f));
	// Equal extents (flat/pre-upscale) are unchanged; a zero guide extent is passed through.
	REQUIRE(GuideToColorMotionScale(1920.0f, 1920, 1920) == Catch::Approx(1920.0f));
	REQUIRE(GuideToColorMotionScale(3.0f, 100, 0) == Catch::Approx(3.0f));
}

TEST_CASE("NR coverage presets normalize to the supported set", "[nr][policy]")
{
	for (const auto preset : kNeuralCoveragePresets)
		REQUIRE(NormalizeNeuralCoverage(preset) == preset);
	REQUIRE(NormalizeNeuralCoverage(0) == 100);
	REQUIRE(NormalizeNeuralCoverage(83) == 100);
	REQUIRE(NormalizeNeuralCoverage(200) == 100);
}

TEST_CASE("Periphery NR presets, feather ramp and hybrid math follow the contract", "[nr][periphery]")
{
	REQUIRE(NormalizePeripheryResolution(50) == 50);
	REQUIRE(NormalizePeripheryResolution(58) == 58);
	REQUIRE(NormalizePeripheryResolution(66) == 66);
	REQUIRE(NormalizePeripheryResolution(75) == 75);
	REQUIRE(NormalizePeripheryResolution(60) == 50);
	REQUIRE(CentreFeatherWeight(0.0f, 80.0f) == Catch::Approx(0.0f));
	REQUIRE(CentreFeatherWeight(40.0f, 80.0f) == Catch::Approx(0.5f));
	REQUIRE(CentreFeatherWeight(80.0f, 80.0f) == Catch::Approx(1.0f));
	// CPU reference for raw + g*cP + w*(effC-cC) + (1-w)*(effP-cP).
	const float result = PeripheryComposeChannel(0.20f, 0.50f, 0.70f, 0.10f, 0.20f, 0.50f, 0.83f);
	REQUIRE(result == Catch::Approx(0.533f).margin(1.0f / 255.0f));
	REQUIRE(PeripheryComposeChannel(0.2f, 0.5f, 0.7f, 0.1f, 0.2f, 0.0f, 0.83f) ==
		Catch::Approx(0.483f));
	REQUIRE(PeripheryComposeChannel(0.2f, 0.5f, 0.7f, 0.1f, 0.2f, 1.0f, 0.83f) ==
		Catch::Approx(0.583f));
}

TEST_CASE("Full coverage is the whole eye", "[nr][policy]")
{
	const auto rect = ComputeNeuralCoverageRect(2496, 2688, 1468, 1580, 100);
	REQUIRE(rect.IsValid());
	REQUIRE(rect.guideX == 0);
	REQUIRE(rect.guideY == 0);
	REQUIRE(rect.guideWidth == 1468);
	REQUIRE(rect.guideHeight == 1580);
	REQUIRE(rect.colorX == 0);
	REQUIRE(rect.colorY == 0);
	REQUIRE(rect.colorWidth == 2496);
	REQUIRE(rect.colorHeight == 2688);
}

TEST_CASE("Centered coverage keeps color and guide rectangles aligned and inside the eye", "[nr][policy]")
{
	constexpr std::uint32_t eyeWidth = 2496, eyeHeight = 2688, guideWidth = 1468, guideHeight = 1580;
	for (const auto coverage : kNeuralCoveragePresets) {
		const auto rect = ComputeNeuralCoverageRect(eyeWidth, eyeHeight, guideWidth, guideHeight, coverage);
		REQUIRE(rect.IsValid());
		// Exactly centered in guide space.
		REQUIRE(rect.guideX * 2 + rect.guideWidth == guideWidth);
		REQUIRE(rect.guideY * 2 + rect.guideHeight == guideHeight);
		REQUIRE(rect.colorX + rect.colorWidth <= eyeWidth);
		REQUIRE(rect.colorY + rect.colorHeight <= eyeHeight);
		// Both edges of the color rect map to the guide rect within half a display pixel.
		const double ratioX = static_cast<double>(eyeWidth) / guideWidth;
		const double ratioY = static_cast<double>(eyeHeight) / guideHeight;
		REQUIRE(std::abs(rect.colorX - rect.guideX * ratioX) <= 0.5);
		REQUIRE(std::abs(rect.colorX + rect.colorWidth - (rect.guideX + rect.guideWidth) * ratioX) <= 0.5);
		REQUIRE(std::abs(rect.colorY - rect.guideY * ratioY) <= 0.5);
		REQUIRE(std::abs(rect.colorY + rect.colorHeight - (rect.guideY + rect.guideHeight) * ratioY) <= 0.5);
		// The linear coverage matches the preset within one guide pixel.
		REQUIRE(std::abs(static_cast<double>(rect.guideWidth) - guideWidth * coverage / 100.0) <= 1.5);
	}
}

TEST_CASE("Coverage 80% keeps about 64% of the NR area", "[nr][policy]")
{
	const auto rect = ComputeNeuralCoverageRect(2496, 2688, 1468, 1580, 80);
	const double area = static_cast<double>(rect.colorWidth) * rect.colorHeight / (2496.0 * 2688.0);
	REQUIRE(area == Catch::Approx(0.64).margin(0.01));
}

TEST_CASE("Degenerate coverage inputs are rejected", "[nr][policy]")
{
	REQUIRE_FALSE(ComputeNeuralCoverageRect(0, 2688, 1468, 1580, 80).IsValid());
	REQUIRE_FALSE(ComputeNeuralCoverageRect(2496, 2688, 1, 1580, 80).IsValid());
}

TEST_CASE("Projection-centered symmetric crop exactly matches legacy coverage", "[nr][policy]")
{
	for (const auto coverage : kNeuralCoveragePresets) {
		const auto legacy = ComputeNeuralCoverageRect(2496, 2688, 1468, 1580, coverage);
		const auto symmetric = ComputeNeuralCoverageRect(2496, 2688, 1468, 1580, coverage,
			1248.0, 1344.0);
		REQUIRE(symmetric.colorX == legacy.colorX);
		REQUIRE(symmetric.colorY == legacy.colorY);
		REQUIRE(symmetric.colorWidth == legacy.colorWidth);
		REQUIRE(symmetric.colorHeight == legacy.colorHeight);
		REQUIRE(symmetric.guideX == legacy.guideX);
		REQUIRE(symmetric.guideY == legacy.guideY);
		REQUIRE(symmetric.guideWidth == legacy.guideWidth);
		REQUIRE(symmetric.guideHeight == legacy.guideHeight);
	}
}

TEST_CASE("Asymmetric projection shifts crop on the guide grid without changing its size", "[nr][policy]")
{
	const ProjectionTangents tangents{ -1.2, 0.8, -0.9, 1.1 };
	ProjectionPixelCenter center{};
	REQUIRE(TryProjectionPixelCenterFromTangents(tangents, 1000, 800, center));
	REQUIRE(center.x == Catch::Approx(600.0));
	REQUIRE(center.y == Catch::Approx(360.0));

	const auto leftEye = ComputeNeuralCoverageRect(1000, 800, 800, 600, 80, center.x, center.y);
	const auto rightEye = ComputeNeuralCoverageRect(1000, 800, 800, 600, 80, 500.0, 400.0);
	REQUIRE(leftEye.guideWidth == rightEye.guideWidth);
	REQUIRE(leftEye.guideHeight == rightEye.guideHeight);
	REQUIRE(leftEye.colorWidth == rightEye.colorWidth);
	REQUIRE(leftEye.colorHeight == rightEye.colorHeight);
	REQUIRE(leftEye.guideX == 160);
	REQUIRE(leftEye.guideY == 30);
	REQUIRE(leftEye.guideX != rightEye.guideX);
	REQUIRE(leftEye.guideY != rightEye.guideY);
	const double sx = 1000.0 / 800.0, sy = 800.0 / 600.0;
	REQUIRE(std::abs(leftEye.colorX - leftEye.guideX * sx) <= 0.5);
	REQUIRE(std::abs(leftEye.colorX + leftEye.colorWidth - (leftEye.guideX + leftEye.guideWidth) * sx) <= 0.5);
	REQUIRE(std::abs(leftEye.colorY - leftEye.guideY * sy) <= 0.5);
	REQUIRE(std::abs(leftEye.colorY + leftEye.colorHeight - (leftEye.guideY + leftEye.guideHeight) * sy) <= 0.5);
}

TEST_CASE("Projection crops with non-integer color scaling retain a common stereo size", "[nr][policy]")
{
	constexpr std::uint32_t eyeWidth = 2496, eyeHeight = 2688, guideWidth = 1468, guideHeight = 1580;
	auto leftEye = ComputeNeuralCoverageRect(eyeWidth, eyeHeight, guideWidth, guideHeight, 75, 1000.0, 1500.0);
	auto rightEye = ComputeNeuralCoverageRect(eyeWidth, eyeHeight, guideWidth, guideHeight, 75, 1500.0, 1000.0);
	const auto originalLeft = leftEye;
	const auto originalRight = rightEye;
	REQUIRE(leftEye.colorWidth != rightEye.colorWidth);
	REQUIRE(leftEye.colorHeight != rightEye.colorHeight);
	REQUIRE(AlignNeuralCoverageRectsForStereo(eyeWidth, eyeHeight, guideWidth, guideHeight, leftEye, rightEye));
	REQUIRE(leftEye.colorWidth == rightEye.colorWidth);
	REQUIRE(leftEye.colorHeight == rightEye.colorHeight);
	REQUIRE(leftEye.guideWidth == rightEye.guideWidth);
	REQUIRE(leftEye.guideHeight == rightEye.guideHeight);
	REQUIRE(std::abs(static_cast<int>(leftEye.guideX) - static_cast<int>(originalLeft.guideX)) <= 8);
	REQUIRE(std::abs(static_cast<int>(rightEye.guideX) - static_cast<int>(originalRight.guideX)) <= 8);
	REQUIRE(std::abs(static_cast<int>(leftEye.guideY) - static_cast<int>(originalLeft.guideY)) <= 8);
	REQUIRE(std::abs(static_cast<int>(rightEye.guideY) - static_cast<int>(originalRight.guideY)) <= 8);
	const double sx = static_cast<double>(eyeWidth) / guideWidth;
	const double sy = static_cast<double>(eyeHeight) / guideHeight;
	REQUIRE(std::abs(leftEye.colorX - leftEye.guideX * sx) <= 0.5);
	REQUIRE(std::abs(leftEye.colorX + leftEye.colorWidth - (leftEye.guideX + leftEye.guideWidth) * sx) <= 0.5);
	REQUIRE(std::abs(rightEye.colorX - rightEye.guideX * sx) <= 0.5);
	REQUIRE(std::abs(rightEye.colorX + rightEye.colorWidth - (rightEye.guideX + rightEye.guideWidth) * sx) <= 0.5);
	REQUIRE(std::abs(leftEye.colorY - leftEye.guideY * sy) <= 0.5);
	REQUIRE(std::abs(leftEye.colorY + leftEye.colorHeight - (leftEye.guideY + leftEye.guideHeight) * sy) <= 0.5);
	REQUIRE(std::abs(rightEye.colorY - rightEye.guideY * sy) <= 0.5);
	REQUIRE(std::abs(rightEye.colorY + rightEye.colorHeight - (rightEye.guideY + rightEye.guideHeight) * sy) <= 0.5);
}

TEST_CASE("Projection crop clamps at every eye edge and ignores invalid centers", "[nr][policy]")
{
	constexpr std::uint32_t eyeWidth = 1000, eyeHeight = 800, guideWidth = 800, guideHeight = 600;
	const auto atTopLeft = ComputeNeuralCoverageRect(eyeWidth, eyeHeight, guideWidth, guideHeight, 80,
		-100.0, -200.0, -1000.0, -1000.0);
	const auto atBottomRight = ComputeNeuralCoverageRect(eyeWidth, eyeHeight, guideWidth, guideHeight, 80,
		1100.0, 1000.0, 1000.0, 1000.0);
	REQUIRE(atTopLeft.guideX == 0);
	REQUIRE(atTopLeft.guideY == 0);
	REQUIRE(atBottomRight.guideX + atBottomRight.guideWidth == guideWidth);
	REQUIRE(atBottomRight.guideY + atBottomRight.guideHeight == guideHeight);
	REQUIRE(atTopLeft.guideWidth == atBottomRight.guideWidth);
	REQUIRE(atTopLeft.guideHeight == atBottomRight.guideHeight);
	REQUIRE(atBottomRight.colorX + atBottomRight.colorWidth <= eyeWidth);
	REQUIRE(atBottomRight.colorY + atBottomRight.colorHeight <= eyeHeight);

	const auto invalidCenter = ComputeNeuralCoverageRect(eyeWidth, eyeHeight, guideWidth, guideHeight, 80,
		std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity());
	const auto centered = ComputeNeuralCoverageRect(eyeWidth, eyeHeight, guideWidth, guideHeight, 80,
		eyeWidth * 0.5, eyeHeight * 0.5);
	REQUIRE(invalidCenter.guideX == centered.guideX);
	REQUIRE(invalidCenter.guideY == centered.guideY);
}

TEST_CASE("D3D off-axis projection scale and offset produce OpenVR tangent convention", "[nr][policy]")
{
	ProjectionTangents tangents{};
	REQUIRE(TryProjectionTangentsFromD3DProjectionMatrixScaleOffset(2.0, 4.0, 0.2, -0.1, tangents));
	REQUIRE(tangents.left == Catch::Approx(-0.6));
	REQUIRE(tangents.right == Catch::Approx(0.4));
	REQUIRE(tangents.top == Catch::Approx(-0.275));
	REQUIRE(tangents.bottom == Catch::Approx(0.225));
	ProjectionPixelCenter center{};
	REQUIRE(TryProjectionPixelCenterFromTangents(tangents, 1000, 800, center));
	REQUIRE(center.x == Catch::Approx(600.0));
	REQUIRE(center.y == Catch::Approx(440.0));

	const ProjectionTangents original{ 11.0, 12.0, 13.0, 14.0 };
	tangents = original;
	REQUIRE_FALSE(TryProjectionTangentsFromD3DProjectionMatrixScaleOffset(0.0, 1.0, 0.0, 0.0, tangents));
	REQUIRE(tangents.left == original.left);
	REQUIRE_FALSE(TryProjectionTangentsFromD3DProjectionMatrixScaleOffset(1.0, 1.0,
		std::numeric_limits<double>::quiet_NaN(), 0.0, tangents));
	REQUIRE_FALSE(TryProjectionPixelCenterFromTangents({ 1.0, -1.0, -1.0, 1.0 }, 1000, 800, center));
}

TEST_CASE("Eye stagger alternates the native eye every frame", "[nr][policy]")
{
	REQUIRE(StaggeredNativeEye(0) == 0);
	REQUIRE(StaggeredNativeEye(1) == 1);
	REQUIRE(StaggeredNativeEye(2) == 0);
	REQUIRE(StaggeredNativeEye(0xFFFFFFFFull) == 1);
}

TEST_CASE("This build keeps NR at full resolution", "[nr][policy]")
{
	STATIC_REQUIRE(kFullResolutionNeuralRenderingOnly);
}

TEST_CASE("Stable NR crop origin changes invalidate both stereo histories", "[nr][policy][history]")
{
	NeuralCoverageHistoryGeometry previous{};
	previous.stable = true;
	previous.sourceColorWidth = 2496;
	previous.sourceColorHeight = 2688;
	previous.sourceGuideWidth = 1468;
	previous.sourceGuideHeight = 1580;
	previous.eyes[0] = { 96, 80, 1344, 1568, 56, 48, 792, 928 };
	previous.eyes[1] = { 1128, 80, 1344, 1568, 664, 48, 792, 928 };

	auto sameSizeMoved = previous;
	sameSizeMoved.eyes[1].colorX += 32;
	sameSizeMoved.eyes[1].guideX += 19;
	REQUIRE(NeuralCoverageHistoryGeometryChanged(previous, sameSizeMoved));
	REQUIRE_FALSE(NeuralCoverageHistoryGeometryChanged(previous, previous));
	// Preserve the resolution reset contract even when crop rounding leaves the
	// effective NR input rectangles unchanged.
	auto sameCropDifferentSourceResolution = previous;
	sameCropDifferentSourceResolution.sourceColorWidth += 8;
	REQUIRE(NeuralCoverageHistoryGeometryChanged(previous, sameCropDifferentSourceResolution));

	// Dynamic crops can move every frame; only entering/leaving dynamic mode
	// invalidates the last stable geometry, avoiding per-frame NGX reseeds.
	auto dynamic = sameSizeMoved;
	dynamic.stable = false;
	REQUIRE(NeuralCoverageHistoryGeometryChanged(previous, dynamic));
	auto nextDynamic = dynamic;
	nextDynamic.eyes[1].colorX += 17;
	REQUIRE_FALSE(NeuralCoverageHistoryGeometryChanged(dynamic, nextDynamic));
	auto dynamicSourceResize = dynamic;
	dynamicSourceResize.sourceGuideHeight += 4;
	REQUIRE(NeuralCoverageHistoryGeometryChanged(dynamic, dynamicSourceResize));
}
