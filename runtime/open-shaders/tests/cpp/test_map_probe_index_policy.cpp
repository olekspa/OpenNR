#include <catch2/catch_test_macros.hpp>

#include "Features/Upscaling/PerfMode/MapProbeIndexPolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{
	struct CountingRange
	{
		std::array<int, 3> values{ 11, 22, 33 };
		std::size_t accesses = 0;

		constexpr std::size_t size() const noexcept { return values.size(); }
		int& operator[](std::size_t index) noexcept
		{
			++accesses;
			return values[index];
		}
	};
}

TEST_CASE("MapProbe rejects an out-of-range target/depth index before access", "[map][probe][bounds]")
{
	CountingRange range;
	constexpr std::uint64_t invalidRuntimeIndex = 0x5FFFFFFFAull;

	REQUIRE(MapProbeIndexPolicy::TryIndex(range, invalidRuntimeIndex) == nullptr);
	REQUIRE(range.accesses == 0);

	auto* valid = MapProbeIndexPolicy::TryIndex(range, 2);
	REQUIRE(valid == &range.values[2]);
	REQUIRE(*valid == 33);
	REQUIRE(range.accesses == 1);
}
