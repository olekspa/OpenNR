#include <atomic>
#include <chrono>
#include <future>
#include <latch>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "../../src/Features/LightLimitFix/ShadowWorkGate.h"

using namespace std::chrono_literals;
using ShadowCasterManager::ShadowWorkGate;

TEST_CASE("Shadow work gate drains active and nested readers before teardown", "[scm][teardown]")
{
	ShadowWorkGate gate;
	REQUIRE(gate.TryEnter());
	REQUIRE(gate.TryEnter());
	REQUIRE(gate.ReaderCount() == 2);
	REQUIRE(gate.IsCurrentThreadReader());

	gate.CloseAdmission();
	REQUIRE_FALSE(gate.TryEnter());
	gate.Leave();
	REQUIRE(gate.ReaderCount() == 1);
	gate.Leave();
	REQUIRE(gate.ReaderCount() == 0);
	REQUIRE_FALSE(gate.IsCurrentThreadReader());
	gate.WaitForDrain();
	gate.OpenAdmission();
	REQUIRE(gate.TryEnter());
	gate.Leave();
}

TEST_CASE("Shadow work gate keeps teardown blocked past the former timeout", "[scm][teardown][thread]")
{
	ShadowWorkGate gate;
	std::latch readerEntered(1);
	std::latch releaseReader(1);
	std::latch teardownStarted(1);
	std::atomic<bool> teardownPassedDrain{ false };
	std::promise<void> passedDrainPromise;
	auto passedDrain = passedDrainPromise.get_future();

	std::thread reader([&] {
		if (!gate.TryEnter())
			return;
		readerEntered.count_down();
		releaseReader.wait();
		gate.Leave();
	});
	readerEntered.wait();
	std::thread teardown([&] {
		gate.CloseAdmission();
		teardownStarted.count_down();
		gate.WaitForDrain();
		teardownPassedDrain.store(true, std::memory_order_release);
		passedDrainPromise.set_value();
		gate.OpenAdmission();
	});
	teardownStarted.wait();
	REQUIRE_FALSE(gate.TryEnter());
	REQUIRE(teardownPassedDrain.load(std::memory_order_acquire) == false);
	REQUIRE(passedDrain.wait_for(150ms) == std::future_status::timeout);
	REQUIRE_FALSE(teardownPassedDrain.load(std::memory_order_acquire));
	releaseReader.count_down();
	reader.join();
	teardown.join();
	REQUIRE(teardownPassedDrain.load(std::memory_order_acquire));
}

TEST_CASE("Shadow work gate admits no reader after close wins the increment race", "[scm][teardown][thread]")
{
	ShadowWorkGate gate;
	std::latch holdReader(1);
	std::latch readerEntered(1);
	std::thread reader([&] {
		if (gate.TryEnter()) {
			readerEntered.count_down();
			holdReader.wait();
			gate.Leave();
		}
	});
	readerEntered.wait();
	gate.CloseAdmission();
	REQUIRE_FALSE(gate.TryEnter());
	REQUIRE(gate.ReaderCount() == 1);
	holdReader.count_down();
	reader.join();
	gate.WaitForDrain();
	REQUIRE(gate.ReaderCount() == 0);
}
