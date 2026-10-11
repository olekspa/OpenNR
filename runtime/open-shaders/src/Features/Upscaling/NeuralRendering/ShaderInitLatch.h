#pragma once

#include <utility>

namespace NeuralRendering
{
	/**
	 * @brief Caches the first result of route-specific shader/resource initialization.
	 * A failure stays disabled until Reset() is called after a settings change or reload.
	 */
	class ShaderInitLatch
	{
	public:
		template <typename Initialize, typename OnFailure>
		bool Ensure(Initialize&& initialize, OnFailure&& onFailure)
		{
			if (state == State::kFailed)
				return false;
			if (state == State::kReady)
				return true;

			if (std::forward<Initialize>(initialize)()) {
				state = State::kReady;
				return true;
			}
			state = State::kFailed;
			std::forward<OnFailure>(onFailure)();
			return false;
		}

		void Reset() noexcept { state = State::kUninitialized; }
		[[nodiscard]] bool Failed() const noexcept { return state == State::kFailed; }

	private:
		enum class State { kUninitialized, kReady, kFailed };
		State state = State::kUninitialized;
	};
}
