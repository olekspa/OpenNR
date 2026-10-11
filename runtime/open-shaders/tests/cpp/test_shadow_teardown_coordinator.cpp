#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <latch>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../../src/Features/LightLimitFix/ShadowTeardownCoordinator.h"

using namespace std::chrono_literals;
using ShadowCasterManager::ShadowTeardownCoordinator;
using ShadowCasterManager::ShadowWorkGate;

namespace
{
	using OwnerHold = std::shared_ptr<int>;
	using Coordinator = ShadowTeardownCoordinator<OwnerHold, int>;

	void WaitForPendingRequest(const Coordinator& coordinator)
	{
		const auto deadline = std::chrono::steady_clock::now() + 2s;
		while (coordinator.PendingRequestCount() == 0 && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
	}
}

TEST_CASE("teardown coordinator defers a reader request to its outermost owner-thread exit", "[scm][teardown][thread]")
{
	ShadowWorkGate gate;
	std::atomic<bool> resetPending{ false };
	Coordinator coordinator(gate, resetPending);
	auto owner = std::make_shared<int>(17);
	std::weak_ptr<int> weakOwner = owner;
	const auto ownerThread = std::this_thread::get_id();
	int originalCalls = 0;
	std::thread::id originalThread;
	bool ownerRetainedDuringCall = false;

	REQUIRE(gate.TryEnter());
	REQUIRE(gate.TryEnter());
	coordinator.Clear(owner.get(), owner, 4, [&](const OwnerHold& held, const int&) {
		++originalCalls;
		originalThread = std::this_thread::get_id();
		ownerRetainedDuringCall = held.get() == weakOwner.lock().get();
	});
	owner.reset();
	REQUIRE(originalCalls == 0);
	REQUIRE(resetPending.load(std::memory_order_acquire));
	REQUIRE(gate.IsClosing());
	REQUIRE_FALSE(gate.TryEnter());

	if (gate.Leave())
		coordinator.CompleteDeferredForCurrentThread();
	REQUIRE(originalCalls == 0);
	REQUIRE(gate.ReaderCount() == 1);
	if (gate.Leave())
		coordinator.CompleteDeferredForCurrentThread();

	REQUIRE(originalCalls == 1);
	REQUIRE(originalThread == ownerThread);
	REQUIRE(ownerRetainedDuringCall);
	REQUIRE_FALSE(gate.IsClosing());
	REQUIRE(resetPending.load(std::memory_order_acquire));
	REQUIRE(weakOwner.expired());
}

TEST_CASE("teardown coordinator coalesces same-node recursion and runs other nodes on the owner thread", "[scm][teardown]")
{
	ShadowWorkGate gate;
	std::atomic<bool> resetPending{ false };
	Coordinator coordinator(gate, resetPending);
	auto firstOwner = std::make_shared<int>(1);
	auto secondOwner = std::make_shared<int>(2);
	int firstCalls = 0;
	int duplicateCalls = 0;
	int differentArgsCalls = 0;
	int secondCalls = 0;
	std::vector<int> order;
	std::vector<std::thread::id> callbackThreads;

	coordinator.Clear(firstOwner.get(), firstOwner, 0, [&](const OwnerHold& held, const int&) {
		++firstCalls;
		order.push_back(*held);
		callbackThreads.push_back(std::this_thread::get_id());
		coordinator.Clear(firstOwner.get(), firstOwner, 0, [&](const OwnerHold&, const int&) { ++duplicateCalls; });
		coordinator.Clear(firstOwner.get(), firstOwner, 3, [&](const OwnerHold& deferred, const int&) {
			++differentArgsCalls;
			order.push_back(*deferred + 2);
		});
		coordinator.Clear(secondOwner.get(), secondOwner, 2, [&](const OwnerHold& deferred, const int&) {
			++secondCalls;
			order.push_back(*deferred);
			callbackThreads.push_back(std::this_thread::get_id());
		});
	});

	REQUIRE(firstCalls == 1);
	REQUIRE(duplicateCalls == 0);
	REQUIRE(differentArgsCalls == 1);
	REQUIRE(secondCalls == 1);
	REQUIRE((order == std::vector<int>{ 1, 3, 2 }));
	REQUIRE(callbackThreads.size() == 2);
	REQUIRE(callbackThreads[0] == std::this_thread::get_id());
	REQUIRE(callbackThreads[1] == std::this_thread::get_id());
	REQUIRE_FALSE(gate.IsClosing());
}

TEST_CASE("deferred callback coalesces a request already waiting later in the owner queue", "[scm][teardown]")
{
	ShadowWorkGate gate;
	std::atomic<bool> resetPending{ false };
	Coordinator coordinator(gate, resetPending);
	auto ownerA = std::make_shared<int>(10);
	auto ownerB = std::make_shared<int>(11);
	int callA = 0;
	int callB = 0;
	int duplicateB = 0;
	bool gateClosedDuringA = false;
	bool gateClosedDuringB = false;

	REQUIRE(gate.TryEnter());
	coordinator.Clear(ownerA.get(), ownerA, 0, [&](const OwnerHold&, const int&) {
		++callA;
		gateClosedDuringA = gate.IsClosing();
		coordinator.Clear(ownerB.get(), ownerB, 3, [&](const OwnerHold&, const int&) { ++duplicateB; });
	});
	coordinator.Clear(ownerB.get(), ownerB, 3, [&](const OwnerHold&, const int&) {
		++callB;
		gateClosedDuringB = gate.IsClosing();
	});
	if (gate.Leave())
		coordinator.CompleteDeferredForCurrentThread();

	REQUIRE(callA == 1);
	REQUIRE(callB == 1);
	REQUIRE(duplicateB == 0);
	REQUIRE(gateClosedDuringA);
	REQUIRE(gateClosedDuringB);
	REQUIRE_FALSE(gate.IsClosing());
}

TEST_CASE("teardown coordinator serializes concurrent clear callers without reopening admission", "[scm][teardown][thread]")
{
	ShadowWorkGate gate;
	std::atomic<bool> resetPending{ false };
	Coordinator coordinator(gate, resetPending);
	auto firstOwner = std::make_shared<int>(1);
	auto secondOwner = std::make_shared<int>(2);
	std::latch firstOriginalEntered(1);
	std::latch releaseFirstOriginal(1);
	std::latch secondRequestStarted(1);
	std::atomic<int> inOriginal{ 0 };
	std::atomic<int> maxInOriginal{ 0 };
	std::atomic<int> originalCalls{ 0 };
	std::atomic<bool> secondSawClosedGate{ false };
	auto recordEntry = [&] {
		const int now = inOriginal.fetch_add(1, std::memory_order_acq_rel) + 1;
		int max = maxInOriginal.load(std::memory_order_relaxed);
		while (now > max && !maxInOriginal.compare_exchange_weak(max, now, std::memory_order_relaxed)) {}
		originalCalls.fetch_add(1, std::memory_order_relaxed);
	};

	std::thread first([&] {
		coordinator.Clear(firstOwner.get(), firstOwner, 1, [&](const OwnerHold&, const int&) {
			recordEntry();
			firstOriginalEntered.count_down();
			releaseFirstOriginal.wait();
			inOriginal.fetch_sub(1, std::memory_order_acq_rel);
		});
	});
	firstOriginalEntered.wait();
	std::thread second([&] {
		secondRequestStarted.count_down();
		coordinator.Clear(secondOwner.get(), secondOwner, 2, [&](const OwnerHold&, const int&) {
			secondSawClosedGate.store(gate.IsClosing(), std::memory_order_release);
			recordEntry();
			inOriginal.fetch_sub(1, std::memory_order_acq_rel);
		});
	});
	secondRequestStarted.wait();
	WaitForPendingRequest(coordinator);
	REQUIRE(coordinator.PendingRequestCount() == 1);
	REQUIRE(gate.IsClosing());
	releaseFirstOriginal.count_down();
	first.join();
	second.join();

	REQUIRE(originalCalls.load() == 2);
	REQUIRE(maxInOriginal.load() == 1);
	REQUIRE(secondSawClosedGate.load());
	REQUIRE_FALSE(gate.IsClosing());
}

TEST_CASE("teardown coordinator retains same-key requests for each admitted owner thread", "[scm][teardown][thread]")
{
	ShadowWorkGate gate;
	std::atomic<bool> resetPending{ false };
	Coordinator coordinator(gate, resetPending);
	auto owner = std::make_shared<int>(8);
	std::latch bothEntered(2);
	std::latch bothQueued(2);
	std::atomic<int> originalCalls{ 0 };
	std::mutex resultMutex;
	std::vector<std::thread::id> callbackThreads;
	std::array<std::thread::id, 2> readerThreads{};

	auto reader = [&](std::size_t index) {
		if (!gate.TryEnter()) {
			bothEntered.count_down();
			bothEntered.wait();
			return;
		}
		readerThreads[index] = std::this_thread::get_id();
		bothEntered.count_down();
		bothEntered.wait();
		coordinator.Clear(owner.get(), owner, 5, [&](const OwnerHold&, const int&) {
			originalCalls.fetch_add(1, std::memory_order_relaxed);
			std::lock_guard lock(resultMutex);
			callbackThreads.push_back(std::this_thread::get_id());
		});
		bothQueued.count_down();
		bothQueued.wait();
		if (gate.Leave())
			coordinator.CompleteDeferredForCurrentThread();
	};
	std::thread first(reader, 0);
	std::thread second(reader, 1);
	first.join();
	second.join();

	REQUIRE(originalCalls.load() == 2);
	REQUIRE(callbackThreads.size() == 2);
	REQUIRE(std::find(callbackThreads.begin(), callbackThreads.end(), readerThreads[0]) != callbackThreads.end());
	REQUIRE(std::find(callbackThreads.begin(), callbackThreads.end(), readerThreads[1]) != callbackThreads.end());
	REQUIRE_FALSE(gate.IsClosing());
}

TEST_CASE("external clear preserves a queued same-key request from another owner", "[scm][teardown][thread]")
{
	ShadowWorkGate gate;
	std::atomic<bool> resetPending{ false };
	Coordinator coordinator(gate, resetPending);
	auto owner = std::make_shared<int>(9);
	const auto readerThread = std::this_thread::get_id();
	std::latch externalStarted(1);
	std::atomic<int> readerOriginalCalls{ 0 };
	std::atomic<int> externalOriginalCalls{ 0 };
	std::thread::id readerCallbackThread;
	std::thread::id externalCallbackThread;
	std::thread::id externalThreadId;

	REQUIRE(gate.TryEnter());
	coordinator.Clear(owner.get(), owner, 7, [&](const OwnerHold&, const int&) {
		readerOriginalCalls.fetch_add(1, std::memory_order_relaxed);
		readerCallbackThread = std::this_thread::get_id();
	});
	std::thread external([&] {
		externalThreadId = std::this_thread::get_id();
		externalStarted.count_down();
		coordinator.Clear(owner.get(), owner, 7, [&](const OwnerHold&, const int&) {
			externalOriginalCalls.fetch_add(1, std::memory_order_relaxed);
			externalCallbackThread = std::this_thread::get_id();
		});
	});
	externalStarted.wait();
	const auto deadline = std::chrono::steady_clock::now() + 2s;
	while (!coordinator.IsClearActive() && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	const bool externalClearActive = coordinator.IsClearActive();
	const auto pendingBeforeLeave = coordinator.PendingRequestCount();
	if (gate.Leave())
		coordinator.CompleteDeferredForCurrentThread();
	external.join();

	REQUIRE(externalClearActive);
	REQUIRE(pendingBeforeLeave == 1);
	REQUIRE(readerOriginalCalls.load() == 1);
	REQUIRE(externalOriginalCalls.load() == 1);
	REQUIRE(readerCallbackThread == readerThread);
	REQUIRE(externalCallbackThread == externalThreadId);
	REQUIRE_FALSE(gate.IsClosing());
}

TEST_CASE("teardown completion republishes reset after a reader consumed the pending flag", "[scm][teardown][thread]")
{
	ShadowWorkGate gate;
	std::atomic<bool> resetPending{ false };
	Coordinator coordinator(gate, resetPending);
	auto owner = std::make_shared<int>(3);
	std::atomic<int> originalCalls{ 0 };
	REQUIRE(gate.TryEnter());

	std::thread clear([&] {
		coordinator.Clear(owner.get(), owner, 0, [&](const OwnerHold&, const int&) {
			originalCalls.fetch_add(1, std::memory_order_relaxed);
		});
	});
	// Closing admission proves the clear thread has reached its drain wait.
	const auto deadline = std::chrono::steady_clock::now() + 2s;
	while (!gate.IsClosing() && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	REQUIRE(gate.IsClosing());
	REQUIRE(resetPending.exchange(false, std::memory_order_acq_rel));
	REQUIRE(originalCalls.load() == 0);
	gate.Leave();
	clear.join();
	REQUIRE(originalCalls.load() == 1);
	REQUIRE(resetPending.load(std::memory_order_acquire));
	REQUIRE_FALSE(gate.IsClosing());
}
