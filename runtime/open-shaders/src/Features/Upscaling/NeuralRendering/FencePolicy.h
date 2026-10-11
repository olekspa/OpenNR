#pragma once

#include <cstdint>
#include <limits>

namespace NeuralRendering
{
	struct ParallelQueueFencePlan
	{
		bool waitForInput = false;
		bool signalParallelCompletion = false;
		bool mainQueueWaitForParallel = false;
		bool signalFrameCompletion = true;
	};

	/** @brief Enables the compute route only for eye 1 of an explicitly enabled stereo evaluation. */
	[[nodiscard]] constexpr bool ShouldUseParallelEyeQueue(
		bool enabled, std::uint32_t eyeIndex, std::uint32_t eyeCount)
	{
		return enabled && eyeCount == 2 && eyeIndex == 1;
	}

	/**
	 * @brief Fence operations required to preserve D3D11 input/output dependencies.
	 *
	 * Both queues wait on the D3D11 input signal. If eye 1 was recorded on the compute
	 * queue, that queue signals completion and the direct queue waits for it before its
	 * final signal, which is the value D3D11 waits on.
	 */
	[[nodiscard]] constexpr ParallelQueueFencePlan GetParallelQueueFencePlan(
		bool parallelQueueAvailable, bool parallelListRecorded)
	{
		const bool parallel = parallelQueueAvailable && parallelListRecorded;
		return { parallel, parallel, parallel, true };
	}

	enum class FenceResourceDisposition
	{
		RetainUntilCompletion,
		ReleaseAfterCompletion,
		ReleaseAfterDeviceRemoval
	};

	[[nodiscard]] constexpr FenceResourceDisposition GetFenceResourceDisposition(
		std::uint64_t completedValue, std::uint64_t targetValue)
	{
		if (completedValue == std::numeric_limits<std::uint64_t>::max())
			return FenceResourceDisposition::ReleaseAfterDeviceRemoval;
		if (targetValue == 0 || completedValue >= targetValue)
			return FenceResourceDisposition::ReleaseAfterCompletion;
		return FenceResourceDisposition::RetainUntilCompletion;
	}

	// ID3D12Fence::GetCompletedValue uses UINT64_MAX to report device removal.
	// Keep this policy independent of D3D so its edge cases can be tested on CPU.
	[[nodiscard]] constexpr bool IsFenceValueComplete(std::uint64_t completedValue, std::uint64_t targetValue)
	{
		return GetFenceResourceDisposition(completedValue, targetValue) ==
			FenceResourceDisposition::ReleaseAfterCompletion;
	}

	[[nodiscard]] constexpr bool IsFenceDeviceRemoved(std::uint64_t completedValue)
	{
		return GetFenceResourceDisposition(completedValue, 0) ==
			FenceResourceDisposition::ReleaseAfterDeviceRemoval;
	}
}
