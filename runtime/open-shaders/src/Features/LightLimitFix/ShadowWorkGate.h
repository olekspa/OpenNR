#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace ShadowCasterManager
{
	class ShadowWorkGate
	{
	public:
		static constexpr std::uint64_t kClosed = std::uint64_t{ 1 } << 63;
		static constexpr std::uint64_t kReaderMask = ~kClosed;

		bool TryEnter() noexcept
		{
			auto state = _state.load(std::memory_order_acquire);
			for (;;) {
				if (state & kClosed)
					return false;
				if ((state & kReaderMask) == kReaderMask)
					return false;
				if (_state.compare_exchange_weak(state, state + 1,
						std::memory_order_acq_rel, std::memory_order_acquire)) {
					++_threadReadDepth;
					return true;
				}
			}
		}

		bool Leave() noexcept
		{
			if (_threadReadDepth > 0)
				--_threadReadDepth;
			_state.fetch_sub(1, std::memory_order_acq_rel);
			_state.notify_all();
			return _threadReadDepth == 0;
		}

		bool IsCurrentThreadReader() const noexcept { return _threadReadDepth != 0; }
		bool IsClosing() const noexcept { return (_state.load(std::memory_order_acquire) & kClosed) != 0; }
		std::uint64_t ReaderCount() const noexcept { return _state.load(std::memory_order_acquire) & kReaderMask; }

		void CloseAdmission() noexcept
		{
			_state.fetch_or(kClosed, std::memory_order_acq_rel);
			_state.notify_all();
		}

		void WaitForDrain() const noexcept
		{
			auto state = _state.load(std::memory_order_acquire);
			while ((state & kReaderMask) != 0) {
				_state.wait(state, std::memory_order_acquire);
				state = _state.load(std::memory_order_acquire);
			}
		}

		void OpenAdmission() noexcept
		{
			_state.store(0, std::memory_order_release);
			_state.notify_all();
		}

	private:
		// A single word makes reader admission atomic with teardown closure.
		std::atomic<std::uint64_t> _state{ 0 };
		inline static thread_local std::uint32_t _threadReadDepth = 0;
	};
}
