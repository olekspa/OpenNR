#include "Features/GrassOptimizations/InputLayoutCache.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using LayoutCache = GrassOptimizationLayouts::InputLayoutCache<std::shared_ptr<int>>;

TEST_CASE("failed grass input layouts are negatively cached until the cache resets", "[grass][cache]")
{
	LayoutCache cache;
	constexpr std::uint64_t descriptor = 0x8001B00000650487ull;
	unsigned createAttempts = 0;

	for (unsigned frame = 0; frame < 120; ++frame) {
		if (auto* cached = cache.Find(descriptor)) {
			REQUIRE_FALSE(*cached);
			continue;
		}
		++createAttempts;
		cache.CacheFailure(descriptor);
	}

	REQUIRE(createAttempts == 1);
	REQUIRE(cache.Find(descriptor) != nullptr);

	cache.Clear();
	REQUIRE(cache.Find(descriptor) == nullptr);
	++createAttempts;
	cache.CacheFailure(descriptor);
	REQUIRE(createAttempts == 2);
}

TEST_CASE("successful grass input layouts remain cached by descriptor", "[grass][cache]")
{
	LayoutCache cache;
	constexpr std::uint64_t descriptor = 0x8001B00000650487ull;
	auto layout = std::make_shared<int>(7);
	cache.CacheSuccess(descriptor, std::move(layout));

	auto* cached = cache.Find(descriptor);
	REQUIRE(cached != nullptr);
	REQUIRE(*cached != nullptr);
	REQUIRE(**cached == 7);
}
