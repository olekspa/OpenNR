#include "D3D12Interop.h"
#include "FencePolicy.h"
#include "IntegrationPolicy.h"

#include "Globals.h"
#include "GpuPass.h"
#include "Profiler.h"
#include "Utils/D3D.h"

#include <algorithm>
#include <utility>

#include <Windows.h>

namespace NeuralRendering
{
	D3D12Interop::~D3D12Interop() { Shutdown(); }

	bool D3D12Interop::RecordFailure(HRESULT result)
	{
		lastError_ = result;
		return false;
	}

	bool D3D12Interop::Initialize(IDXGIAdapter* adapter, ID3D11Device* device, ID3D11DeviceContext* context,
		ID3D12Device* sharedDevice)
	{
		if (!Shutdown())
			return false;
		if ((!adapter && !sharedDevice) || !device || !context)
			return RecordFailure(E_INVALIDARG);

		HRESULT result = device->QueryInterface(IID_PPV_ARGS(&device11_));
		if (FAILED(result)) return RecordFailure(result);
		result = context->QueryInterface(IID_PPV_ARGS(&context11_));
		if (FAILED(result)) return RecordFailure(result);
		if (sharedDevice) {
			device12_ = sharedDevice;
		} else {
			result = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device12_));
			if (FAILED(result)) return RecordFailure(result);
		}

		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		result = device12_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue12_));
		if (FAILED(result)) return RecordFailure(result);
		for (auto& commandContext : commandContexts_) {
			result = device12_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(&commandContext.allocator));
			if (FAILED(result)) return RecordFailure(result);
			result = device12_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
				commandContext.allocator.Get(), nullptr, IID_PPV_ARGS(&commandContext.commandList));
			if (FAILED(result)) return RecordFailure(result);
			result = commandContext.commandList->Close();
			if (FAILED(result)) return RecordFailure(result);
		}

		result = device12_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12_));
		if (FAILED(result)) return RecordFailure(result);
		HANDLE sharedFence = nullptr;
		result = device12_->CreateSharedHandle(fence12_.Get(), nullptr, GENERIC_ALL, nullptr, &sharedFence);
		if (FAILED(result)) return RecordFailure(result);
		result = device11_->OpenSharedFence(sharedFence, IID_PPV_ARGS(&fence11_));
		CloseHandle(sharedFence);
		if (FAILED(result)) return RecordFailure(result);

		fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!fenceEvent_) return RecordFailure(HRESULT_FROM_WIN32(GetLastError()));
		initialized_ = true;
		lastError_ = S_OK;
		logger::info("[DLSSNR] D3D12 interop initialized commandContexts={} cpuFenceWait=backpressure-only",
			kCommandContextCount);
		return true;
	}

	bool D3D12Interop::PrepareShutdown()
	{
		if (!initialized_)
			return true;
		if (WaitForIdle())
			return true;
		if (DeviceWasRemoved())
			return true;
		return false;
	}

	bool D3D12Interop::Shutdown()
	{
		if (!PrepareShutdown()) {
			logger::error("[DLSSNR] D3D12 interop shutdown deferred; retaining resources until GPU work completes hr=0x{:08X}",
				static_cast<std::uint32_t>(lastError_));
			return false;
		}
		if (fenceEvent_) CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
		recording_ = false;
		recordingContext_ = kCommandContextCount;
		commandContextCursor_ = 0;
		backpressureLogged_ = false;
		consecutiveFenceTimeouts_ = 0;
		initialized_ = false;
		fenceValue_ = 0;
		untrackedDrainFenceValue_ = 0;
		parallelDrainFenceValue_ = 0;
		parallelDrainWaitQueued_ = false;
		submissionUntracked_ = false;
		parallelQueueFailed_ = false;
		parallelQueueRequested_ = false;
		parallelListRecording_ = false;
		parallelSubmissionUntracked_ = false;
		fence11_.Reset();
		fence12_.Reset();
		commandContexts_ = {};
		parallelCommandContexts_ = {};
		parallelQueue12_.Reset();
		queue12_.Reset();
		device12_.Reset();
		context11_.Reset();
		device11_.Reset();
		return true;
	}

	bool D3D12Interop::DeviceWasRemoved() const
	{
		if (!device12_)
			return false;
		if (fence12_ && IsFenceDeviceRemoved(fence12_->GetCompletedValue()))
			return true;
		return FAILED(device12_->GetDeviceRemovedReason());
	}

	bool D3D12Interop::CanReleaseResourcesNow() const
	{
		if (!initialized_)
			return true;
		if (recording_)
			return false;
		if (DeviceWasRemoved())
			return true;
		if (!fence12_)
			return false;
		if (parallelSubmissionUntracked_) {
			return parallelDrainFenceValue_ != 0 &&
				IsFenceValueComplete(fence12_->GetCompletedValue(), parallelDrainFenceValue_);
		}
		if (submissionUntracked_) {
			return untrackedDrainFenceValue_ != 0 &&
				IsFenceValueComplete(fence12_->GetCompletedValue(), untrackedDrainFenceValue_);
		}
		std::uint64_t lastSubmittedValue = 0;
		for (const auto& commandContext : commandContexts_)
			lastSubmittedValue = std::max(lastSubmittedValue, commandContext.fenceValue);
		return IsFenceValueComplete(fence12_->GetCompletedValue(), lastSubmittedValue);
	}

	bool D3D12Interop::CreateSharedTexture(const D3D11_TEXTURE2D_DESC& sourceDesc, SharedTexture& texture, const char* name)
	{
		if (!initialized_ || sourceDesc.Width == 0 || sourceDesc.Height == 0 ||
			sourceDesc.ArraySize == 0 || sourceDesc.ArraySize > UINT16_MAX || sourceDesc.MipLevels > UINT16_MAX)
			return RecordFailure(E_INVALIDARG);

		D3D11_TEXTURE2D_DESC desc = sourceDesc;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		if ((desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) == 0) {
			lastOperation_ = "CreateSharedTextureRequiresUAV";
			return RecordFailure(E_INVALIDARG);
		}

		SharedTexture replacement;
		desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		HRESULT result = device11_->CreateTexture2D(&desc, nullptr, &replacement.resource11);
		if (FAILED(result)) {
			lastOperation_ = "D3D11CreateTexture2D";
			return RecordFailure(result);
		}
		Util::SetResourceName(replacement.resource11.Get(), name);
		result = device11_->CreateShaderResourceView(replacement.resource11.Get(), nullptr, &replacement.srv11);
		if (FAILED(result)) {
			lastOperation_ = "D3D11CreateShaderResourceView";
			return RecordFailure(result);
		}
		Util::SetResourceName(replacement.srv11.Get(), "%s SRV", name);
		result = device11_->CreateUnorderedAccessView(replacement.resource11.Get(), nullptr, &replacement.uav11);
		if (FAILED(result)) {
			lastOperation_ = "D3D11CreateUnorderedAccessView";
			return RecordFailure(result);
		}
		Util::SetResourceName(replacement.uav11.Get(), "%s UAV", name);

		Microsoft::WRL::ComPtr<IDXGIResource1> dxgiResource;
		result = replacement.resource11.As(&dxgiResource);
		if (FAILED(result)) return RecordFailure(result);
		HANDLE sharedTexture = nullptr;
		result = dxgiResource->CreateSharedHandle(nullptr,
			DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &sharedTexture);
		if (FAILED(result)) return RecordFailure(result);
		result = device12_->OpenSharedHandle(sharedTexture, IID_PPV_ARGS(&replacement.resource12));
		CloseHandle(sharedTexture);
		if (FAILED(result)) return RecordFailure(result);

		lastResourceFlags_ = static_cast<std::uint32_t>(replacement.resource12->GetDesc().Flags);
		replacement.desc = desc;
		texture = std::move(replacement);
		lastOperation_ = "CreateSharedTexture(D3D11First)";
		lastError_ = S_OK;
		return true;
	}

	bool D3D12Interop::BeginD3D12(ID3D12GraphicsCommandList** commandList, bool enableParallelEyeQueue,
		bool flushAfterSignal)
	{
		if (!initialized_ || recording_ || !commandList) return RecordFailure(E_UNEXPECTED);
		if ((submissionUntracked_ || parallelSubmissionUntracked_) && !WaitForIdle())
			return false;

		const std::uint64_t completedValue = fence12_->GetCompletedValue();
		if (IsFenceDeviceRemoved(completedValue)) {
			const HRESULT removedReason = device12_->GetDeviceRemovedReason();
			return RecordFailure(FAILED(removedReason) ? removedReason : DXGI_ERROR_DEVICE_REMOVED);
		}
		std::size_t contextIndex = kCommandContextCount;
		for (std::size_t offset = 0; offset < kCommandContextCount; ++offset) {
			const std::size_t candidate = (commandContextCursor_ + offset) % kCommandContextCount;
			const auto pendingValue = commandContexts_[candidate].fenceValue;
			if (pendingValue == 0 || completedValue >= pendingValue) {
				contextIndex = candidate;
				break;
			}
		}
		if (contextIndex == kCommandContextCount) {
			contextIndex = commandContextCursor_;
			if (!backpressureLogged_) {
				logger::warn("[DLSSNR] D3D12 command contexts saturated; applying CPU backpressure");
				backpressureLogged_ = true;
			}
			if (!WaitForFence(commandContexts_[contextIndex].fenceValue))
				return false;
		}

		auto& commandContext = commandContexts_[contextIndex];
		commandContext.fenceValue = 0;
		const std::uint64_t readyValue = ++fenceValue_;
		HRESULT result = S_OK;
		{
			CS_GPU_PASS("NeuralRendering::Interop::D3D11FenceSignal");
			result = context11_->Signal(fence11_.Get(), readyValue);
		}
		if (FAILED(result)) return RecordFailure(result);
		if (flushAfterSignal)
			context11_->Flush();
		{
			CS_GPU_PASS("NeuralRendering::Interop::D3D12QueueWait");
			result = queue12_->Wait(fence12_.Get(), readyValue);
		}
		if (FAILED(result)) return RecordFailure(result);
		result = commandContext.allocator->Reset();
		if (FAILED(result)) return RecordFailure(result);
		result = commandContext.commandList->Reset(commandContext.allocator.Get(), nullptr);
		if (FAILED(result)) return RecordFailure(result);
		recording_ = true;
		recordingContext_ = contextIndex;
		parallelQueueRequested_ = enableParallelEyeQueue && !parallelQueueFailed_;
		parallelListRecording_ = false;
		commandContextCursor_ = (contextIndex + 1) % kCommandContextCount;
		*commandList = commandContext.commandList.Get();
		return true;
	}

	ID3D12GraphicsCommandList* D3D12Interop::ParallelEyeCommandList()
	{
		if (!initialized_ || !recording_ || !parallelQueueRequested_ || parallelListRecording_)
			return nullptr;
		if (!parallelQueue12_) {
			D3D12_COMMAND_QUEUE_DESC queueDesc{};
			queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
			HRESULT result = device12_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&parallelQueue12_));
			if (SUCCEEDED(result)) {
				result = parallelQueue12_->SetName(L"NeuralRendering::ParallelEyeQueue");
			}
			for (auto& commandContext : parallelCommandContexts_) {
				if (FAILED(result))
					break;
				result = device12_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
					IID_PPV_ARGS(&commandContext.allocator));
				if (SUCCEEDED(result))
					result = device12_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
						commandContext.allocator.Get(), nullptr, IID_PPV_ARGS(&commandContext.commandList));
				if (SUCCEEDED(result))
					result = commandContext.commandList->SetName(L"NeuralRendering::ParallelEyeCommands");
				if (SUCCEEDED(result))
					result = commandContext.commandList->Close();
			}
			if (FAILED(result)) {
				logger::warn("[DLSSNR] Parallel eye compute queue unavailable (hr=0x{:08X}); using the direct queue",
					static_cast<std::uint32_t>(result));
				parallelCommandContexts_ = {};
				parallelQueue12_.Reset();
				parallelQueueFailed_ = true;
				parallelQueueRequested_ = false;
				return nullptr;
			}
		}

		auto& commandContext = parallelCommandContexts_[recordingContext_];
		// The corresponding direct context is only reused after its final fence, which
		// follows the compute completion fence; its allocator is therefore retired too.
		HRESULT result = commandContext.allocator->Reset();
		if (FAILED(result)) {
			parallelQueueFailed_ = true;
			parallelQueueRequested_ = false;
			lastOperation_ = "ParallelEyeAllocatorReset";
			lastError_ = result;
			logger::warn("[DLSSNR] Parallel eye allocator reset failed (hr=0x{:08X}); using the direct queue",
				static_cast<std::uint32_t>(result));
			return nullptr;
		}
		result = commandContext.commandList->Reset(commandContext.allocator.Get(), nullptr);
		if (FAILED(result)) {
			parallelQueueFailed_ = true;
			parallelQueueRequested_ = false;
			lastOperation_ = "ParallelEyeCommandListReset";
			lastError_ = result;
			logger::warn("[DLSSNR] Parallel eye command-list reset failed (hr=0x{:08X}); using the direct queue",
				static_cast<std::uint32_t>(result));
			return nullptr;
		}
		// The D3D11 Signal already queued by BeginD3D12 is the shared input fence
		// observed by both queues, so neither queue can read inputs prematurely.
		const auto fencePlan = GetParallelQueueFencePlan(parallelQueue12_.Get() != nullptr, true);
		if (fencePlan.waitForInput) {
			result = parallelQueue12_->Wait(fence12_.Get(), fenceValue_);
			if (FAILED(result)) {
				commandContext.commandList->Close();
				parallelQueueFailed_ = true;
				parallelQueueRequested_ = false;
				lastOperation_ = "ParallelEyeInputFenceWait";
				lastError_ = result;
				logger::warn("[DLSSNR] Parallel eye input wait failed (hr=0x{:08X}); using the direct queue",
					static_cast<std::uint32_t>(result));
				return nullptr;
			}
		}
		parallelListRecording_ = true;
		lastError_ = S_OK;
		return commandContext.commandList.Get();
	}

	bool D3D12Interop::EndD3D12()
	{
		if (!initialized_ || !recording_ || recordingContext_ >= kCommandContextCount)
			return RecordFailure(E_UNEXPECTED);
		recording_ = false;
		auto& commandContext = commandContexts_[recordingContext_];
		auto& parallelCommandContext = parallelCommandContexts_[recordingContext_];
		const auto fencePlan = GetParallelQueueFencePlan(
			parallelQueueRequested_ && parallelQueue12_.Get() != nullptr, parallelListRecording_);
		recordingContext_ = kCommandContextCount;
		HRESULT result = commandContext.commandList->Close();
		if (FAILED(result)) return RecordFailure(result);
		if (fencePlan.signalParallelCompletion) {
			result = parallelCommandContext.commandList->Close();
			if (FAILED(result)) return RecordFailure(result);
		}
		ID3D12CommandList* lists[] = { commandContext.commandList.Get() };
		// Once ExecuteCommandLists runs, allocator/list/resources cannot be released
		// until a queue fence is known to follow that work or the device is removed.
		submissionUntracked_ = true;
		{
			CS_GPU_PASS("NeuralRendering::Interop::D3D12CommandListSubmit");
			queue12_->ExecuteCommandLists(1, lists);
		}
		if (fencePlan.signalParallelCompletion) {
			ID3D12CommandList* parallelLists[] = { parallelCommandContext.commandList.Get() };
			parallelSubmissionUntracked_ = true;
			parallelQueue12_->ExecuteCommandLists(1, parallelLists);
			const std::uint64_t parallelCompleteValue = ++fenceValue_;
			result = parallelQueue12_->Signal(fence12_.Get(), parallelCompleteValue);
			if (FAILED(result)) return RecordFailure(result);
			parallelCommandContext.fenceValue = parallelCompleteValue;
			parallelSubmissionUntracked_ = false;
			result = queue12_->Wait(fence12_.Get(), parallelCompleteValue);
			if (FAILED(result)) {
				parallelSubmissionUntracked_ = true;
				parallelDrainFenceValue_ = parallelCompleteValue;
				parallelDrainWaitQueued_ = false;
				return RecordFailure(result);
			}
		}
		const std::uint64_t completeValue = ++fenceValue_;
		{
			CS_GPU_PASS("NeuralRendering::Interop::D3D12FenceSignal");
			result = queue12_->Signal(fence12_.Get(), completeValue);
		}
		if (FAILED(result)) return RecordFailure(result);
		commandContext.fenceValue = completeValue;
		submissionUntracked_ = false;
		parallelQueueRequested_ = false;
		parallelListRecording_ = false;
		untrackedDrainFenceValue_ = 0;

		// This queues a GPU-side dependency. Subsequent D3D11 output copies wait
		// for Feature 18 without stalling the render thread on the CPU.
		{
			CS_GPU_PASS("NeuralRendering::Interop::D3D11QueueWait");
			result = context11_->Wait(fence11_.Get(), completeValue);
		}
		if (FAILED(result)) return RecordFailure(result);
		lastError_ = S_OK;
		return true;
	}

	bool D3D12Interop::WaitForFence(std::uint64_t value)
	{
		if (!initialized_ || !fence12_ || !device12_ || !fenceEvent_)
			return RecordFailure(E_UNEXPECTED);
		const std::uint64_t completedValue = fence12_->GetCompletedValue();
		if (IsFenceDeviceRemoved(completedValue)) {
			const HRESULT removedReason = device12_->GetDeviceRemovedReason();
			return RecordFailure(FAILED(removedReason) ? removedReason : DXGI_ERROR_DEVICE_REMOVED);
		}
		if (IsFenceValueComplete(completedValue, value)) {
			consecutiveFenceTimeouts_ = 0;
			lastError_ = S_OK;
			return true;
		}
		const HRESULT result = fence12_->SetEventOnCompletion(value, fenceEvent_);
		if (FAILED(result)) return RecordFailure(result);
		CS_PROFILE_CPU_SCOPE(globals::profiler, "NeuralRendering::Interop::BlockingFenceWaitCPU");
		const DWORD waitResult = WaitForSingleObject(fenceEvent_, 250);
		if (waitResult != WAIT_OBJECT_0) {
			if (waitResult == WAIT_TIMEOUT) {
				++consecutiveFenceTimeouts_;
				const HRESULT timeout = ShouldDeferTimeoutFailure(consecutiveFenceTimeouts_) ?
					HRESULT_FROM_WIN32(ERROR_RETRY) : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
				return RecordFailure(timeout);
			}
			return RecordFailure(HRESULT_FROM_WIN32(GetLastError()));
		}
		const std::uint64_t completedAfterWait = fence12_->GetCompletedValue();
		if (IsFenceDeviceRemoved(completedAfterWait)) {
			const HRESULT removedReason = device12_->GetDeviceRemovedReason();
			return RecordFailure(FAILED(removedReason) ? removedReason : DXGI_ERROR_DEVICE_REMOVED);
		}
		if (completedAfterWait < value) {
			++consecutiveFenceTimeouts_;
			return RecordFailure(ShouldDeferTimeoutFailure(consecutiveFenceTimeouts_) ?
				HRESULT_FROM_WIN32(ERROR_RETRY) : HRESULT_FROM_WIN32(ERROR_TIMEOUT));
		}
		lastError_ = S_OK;
		consecutiveFenceTimeouts_ = 0;
		return true;
	}

	bool D3D12Interop::WaitForIdle()
	{
		if (!initialized_)
			return true;
		if (recording_)
			return RecordFailure(E_UNEXPECTED);
		if (parallelSubmissionUntracked_) {
			// A compute submission may be live without a successful compute signal.
			// Queue one retryable marker on that queue, then make the direct queue
			// wait for it so the existing direct-queue drain marker covers both lists.
			if (parallelDrainFenceValue_ == 0) {
				parallelDrainFenceValue_ = ++fenceValue_;
				const HRESULT signalResult = parallelQueue12_->Signal(fence12_.Get(), parallelDrainFenceValue_);
				if (FAILED(signalResult)) {
					parallelDrainFenceValue_ = 0;
					return RecordFailure(signalResult);
				}
			}
			if (!parallelDrainWaitQueued_) {
				const HRESULT waitResult = queue12_->Wait(fence12_.Get(), parallelDrainFenceValue_);
				if (FAILED(waitResult))
					return RecordFailure(waitResult);
				parallelDrainWaitQueued_ = true;
			}
			parallelSubmissionUntracked_ = false;
			submissionUntracked_ = true;
		}
		if (submissionUntracked_) {
			// A prior queue Signal failed after work was submitted. Queue a fresh
			// marker behind that work; successful completion of this marker proves
			// the older submission is drained. Keep retrying the same marker on
			// timeout rather than creating an unbounded series of queue signals.
			if (untrackedDrainFenceValue_ == 0) {
				untrackedDrainFenceValue_ = ++fenceValue_;
				const HRESULT signalResult = queue12_->Signal(fence12_.Get(), untrackedDrainFenceValue_);
				if (FAILED(signalResult)) {
					untrackedDrainFenceValue_ = 0;
					return RecordFailure(signalResult);
				}
			}
			if (!WaitForFence(untrackedDrainFenceValue_))
				return false;
			for (auto& commandContext : commandContexts_)
				commandContext.fenceValue = std::max(commandContext.fenceValue, untrackedDrainFenceValue_);
			submissionUntracked_ = false;
			untrackedDrainFenceValue_ = 0;
			parallelDrainFenceValue_ = 0;
			parallelDrainWaitQueued_ = false;
			lastError_ = S_OK;
			return true;
		}
		std::uint64_t lastSubmittedValue = 0;
		for (const auto& commandContext : commandContexts_)
			lastSubmittedValue = std::max(lastSubmittedValue, commandContext.fenceValue);
		return WaitForFence(lastSubmittedValue);
	}
}
