#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "ShadowWorkGate.h"

namespace ShadowCasterManager
{
	template <class OwnerHold, class Args>
	class ShadowTeardownCoordinator
	{
	public:
		using InvokeOriginal = std::function<void(const OwnerHold&, const Args&)>;

		ShadowTeardownCoordinator(ShadowWorkGate& gate, std::atomic<bool>& resetPending,
			std::function<void()> deferredNotice = {}) :
			_gate(gate), _resetPending(resetPending), _deferredNotice(std::move(deferredNotice))
		{}

		void Clear(const void* a_key, OwnerHold a_ownerHold, Args a_args, InvokeOriginal a_invoke)
		{
			const auto owner = std::this_thread::get_id();
			if (_gate.IsCurrentThreadReader()) {
				std::lock_guard lock(_mutex);
				_requestClose();
				_enqueue(a_key, std::move(a_ownerHold), std::move(a_args), owner, std::move(a_invoke));
				return;
			}

			{
				std::unique_lock lock(_mutex);
				if (_active && _activeOwner == owner) {
					_requestClose();
					_enqueue(a_key, std::move(a_ownerHold), std::move(a_args), owner, std::move(a_invoke));
					return;
				}
				++_pendingExternal;
				_requestClose();
				_changed.wait(lock, [this] { return !_active; });
				_active = true;
				_activeKey = a_key;
				_activeArgs = a_args;
				_activeOwner = owner;
				--_pendingExternal;
				_requestClose();
				_eraseMatching(a_key, a_args, owner);
			}

			_gate.WaitForDrain();
			a_invoke(a_ownerHold, a_args);
			_finishActive();
			CompleteDeferredForCurrentThread();
		}

		void CompleteDeferredForCurrentThread()
		{
			const auto owner = std::this_thread::get_id();
			for (;;) {
				std::vector<Request> requests;
				{
					std::unique_lock lock(_mutex);
					if (!_hasOwnerRequest(owner))
						return;
					_changed.wait(lock, [this] { return !_active; });
					_takeNextOwnerRequest(owner, requests);
					if (requests.empty())
						continue;
					_active = true;
					_activeKey = requests.front().key;
					_activeArgs = requests.front().args;
					_activeOwner = owner;
				}

				_gate.WaitForDrain();
				for (auto& request : requests) {
					{
						std::lock_guard lock(_mutex);
						_activeKey = request.key;
						_activeArgs = request.args;
					}
					request.invoke(request.ownerHold, request.args);
				}
				_finishActive();
			}
		}

		std::size_t PendingRequestCount() const
		{
			std::lock_guard lock(_mutex);
			return _requests.size() + _pendingExternal;
		}

		bool IsClearActive() const
		{
			std::lock_guard lock(_mutex);
			return _active;
		}

	private:
		struct Request
		{
			const void* key{};
			OwnerHold ownerHold;
			Args args;
			std::thread::id owner;
			InvokeOriginal invoke;
		};

		void _requestClose() noexcept
		{
			_resetPending.store(true, std::memory_order_release);
			_gate.CloseAdmission();
		}

		void _enqueue(const void* a_key, OwnerHold a_ownerHold, Args a_args,
			std::thread::id a_owner, InvokeOriginal a_invoke)
		{
			if (_active && _activeOwner == a_owner && _activeKey == a_key && _activeArgs == a_args)
				return;
			if (std::none_of(_requests.begin(), _requests.end(), [a_key, &a_args, a_owner](const auto& request) {
					return request.key == a_key && request.args == a_args && request.owner == a_owner;
				})) {
				_requests.push_back({ a_key, std::move(a_ownerHold), std::move(a_args), a_owner, std::move(a_invoke) });
				if (!_deferredNoticeSent && _deferredNotice) {
					_deferredNoticeSent = true;
					_deferredNotice();
				}
			}
		}

		void _eraseMatching(const void* a_key, const Args& a_args, std::thread::id a_owner)
		{
			std::erase_if(_requests, [a_key, &a_args, a_owner](const auto& request) {
				return request.key == a_key && request.args == a_args && request.owner == a_owner;
			});
		}

		bool _hasOwnerRequest(std::thread::id a_owner) const
		{
			return std::any_of(_requests.begin(), _requests.end(), [a_owner](const auto& request) { return request.owner == a_owner; });
		}

		void _takeNextOwnerRequest(std::thread::id a_owner, std::vector<Request>& a_out)
		{
			for (auto it = _requests.begin(); it != _requests.end();) {
				if (it->owner == a_owner) {
					a_out.push_back(std::move(*it));
					it = _requests.erase(it);
					break;
				} else {
					++it;
				}
			}
		}

		void _finishActive()
		{
			{
				std::lock_guard lock(_mutex);
				_resetPending.store(true, std::memory_order_release);
				_active = false;
				_activeKey = nullptr;
				_activeArgs = Args{};
				_activeOwner = {};
				if (_requests.empty() && _pendingExternal == 0)
					_gate.OpenAdmission();
			}
			_changed.notify_all();
		}

		ShadowWorkGate& _gate;
		std::atomic<bool>& _resetPending;
		mutable std::mutex _mutex;
		std::condition_variable _changed;
		std::vector<Request> _requests;
		bool _active = false;
		const void* _activeKey = nullptr;
		Args _activeArgs{};
		std::thread::id _activeOwner;
		std::size_t _pendingExternal = 0;
		std::function<void()> _deferredNotice;
		bool _deferredNoticeSent = false;
	};
}
