#include <catch2/catch_test_macros.hpp>

#include "Features/Upscaling/NeuralRendering/FencePolicy.h"
#include "Features/Upscaling/NeuralRendering/IntegrationPolicy.h"

#include <cstdint>
#include <cstring>
#include <limits>

using NeuralRendering::FenceResourceDisposition;
using NeuralRendering::GetFenceResourceDisposition;
using NeuralRendering::GetParallelQueueFencePlan;
using NeuralRendering::ShouldUseParallelEyeQueue;
using NeuralRendering::StartupPrewarmGateInput;
using NeuralRendering::StartupPrewarmStage;

TEST_CASE("D3D12 fence policy retains in-flight resources and releases completed work", "[nr][fence]")
{
	SECTION("a timeout or incomplete fence retains resources")
	{
		REQUIRE(GetFenceResourceDisposition(14, 15) == FenceResourceDisposition::RetainUntilCompletion);
	}

	SECTION("a signaled target permits resource release")
	{
		REQUIRE(GetFenceResourceDisposition(15, 15) == FenceResourceDisposition::ReleaseAfterCompletion);
		REQUIRE(GetFenceResourceDisposition(18, 15) == FenceResourceDisposition::ReleaseAfterCompletion);
	}

	SECTION("a zero target has no pending work")
	{
		REQUIRE(GetFenceResourceDisposition(0, 0) == FenceResourceDisposition::ReleaseAfterCompletion);
	}

	SECTION("the UINT64_MAX sentinel means device removal, never normal completion")
	{
		constexpr auto removed = std::numeric_limits<std::uint64_t>::max();
		REQUIRE(GetFenceResourceDisposition(removed, 15) == FenceResourceDisposition::ReleaseAfterDeviceRemoval);
		REQUIRE_FALSE(NeuralRendering::IsFenceValueComplete(removed, 0));
		REQUIRE(NeuralRendering::IsFenceDeviceRemoved(removed));
	}
}

TEST_CASE("startup prewarm gates distinguish menu readiness from first-frame guide availability", "[nr][prewarm]")
{
	StartupPrewarmGateInput ready{ true, true, true, true, true, true, true, true, true };
	REQUIRE(NeuralRendering::StartupPrewarmGateReason(ready) == nullptr);

	SECTION("prewarm is held until a menu or loading frame")
	{
		ready.menuOrLoading = false;
		REQUIRE(std::strcmp(NeuralRendering::StartupPrewarmGateReason(ready), "not-menu-loading") == 0);
	}
	SECTION("unsupported crop-only layouts fail closed")
	{
		ready.supportedLayout = false;
		REQUIRE(std::strcmp(NeuralRendering::StartupPrewarmGateReason(ready), "unsupported-layout") == 0);
	}
}

TEST_CASE("startup prewarm schedules at most one renderer phase per frame", "[nr][prewarm]")
{
	using NeuralRendering::NextStartupPrewarmStage;
	REQUIRE(NextStartupPrewarmStage(StartupPrewarmStage::PreparePrimary, true) == StartupPrewarmStage::PrimaryEye0);
	REQUIRE(NextStartupPrewarmStage(StartupPrewarmStage::PrimaryEye0, true) == StartupPrewarmStage::PrimaryEye1);
	REQUIRE(NextStartupPrewarmStage(StartupPrewarmStage::PrimaryEye1, true) == StartupPrewarmStage::PreparePeriphery);
	REQUIRE(NextStartupPrewarmStage(StartupPrewarmStage::PreparePeriphery, true) == StartupPrewarmStage::PeripheryEye0);
	REQUIRE(NextStartupPrewarmStage(StartupPrewarmStage::PeripheryEye0, true) == StartupPrewarmStage::PeripheryEye1);
	REQUIRE(NextStartupPrewarmStage(StartupPrewarmStage::PeripheryEye1, true) == StartupPrewarmStage::Complete);
	REQUIRE(NextStartupPrewarmStage(StartupPrewarmStage::PrimaryEye1, false) == StartupPrewarmStage::Complete);
	REQUIRE(NextStartupPrewarmStage(StartupPrewarmStage::Complete, true) == StartupPrewarmStage::Complete);
}

TEST_CASE("parallel eye queue is opt-in for stereo eye one and preserves fence order", "[nr][fence][parallel]")
{
	SECTION("disabled and non-stereo paths remain on one queue")
	{
		REQUIRE_FALSE(ShouldUseParallelEyeQueue(false, 1, 2));
		REQUIRE_FALSE(ShouldUseParallelEyeQueue(true, 0, 2));
		REQUIRE_FALSE(ShouldUseParallelEyeQueue(true, 1, 1));
		const auto plan = GetParallelQueueFencePlan(false, false);
		REQUIRE_FALSE(plan.waitForInput);
		REQUIRE_FALSE(plan.signalParallelCompletion);
		REQUIRE_FALSE(plan.mainQueueWaitForParallel);
		REQUIRE(plan.signalFrameCompletion);
	}

	SECTION("eye one waits for input and main completion follows compute completion")
	{
		REQUIRE(ShouldUseParallelEyeQueue(true, 1, 2));
		const auto plan = GetParallelQueueFencePlan(true, true);
		REQUIRE(plan.waitForInput);
		REQUIRE(plan.signalParallelCompletion);
		REQUIRE(plan.mainQueueWaitForParallel);
		REQUIRE(plan.signalFrameCompletion);
	}

	SECTION("requested queue without a recorded eye list does not add a cross-queue dependency")
	{
		const auto plan = GetParallelQueueFencePlan(true, false);
		REQUIRE_FALSE(plan.waitForInput);
		REQUIRE_FALSE(plan.signalParallelCompletion);
		REQUIRE_FALSE(plan.mainQueueWaitForParallel);
		REQUIRE(plan.signalFrameCompletion);
	}

	SECTION("queue creation failure falls back without adding cross-queue fence operations")
	{
		const auto plan = GetParallelQueueFencePlan(false, true);
		REQUIRE_FALSE(plan.waitForInput);
		REQUIRE_FALSE(plan.signalParallelCompletion);
		REQUIRE_FALSE(plan.mainQueueWaitForParallel);
		REQUIRE(plan.signalFrameCompletion);
	}
}
