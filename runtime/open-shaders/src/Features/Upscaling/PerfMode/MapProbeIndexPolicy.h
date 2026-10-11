#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace MapProbeIndexPolicy
{
	template <class Range>
	[[nodiscard]] constexpr auto TryIndex(Range& range, std::uint64_t index) noexcept(noexcept(range[std::size_t{}]))
		-> decltype(&range[std::size_t{}])
	{
		if (index >= static_cast<std::uint64_t>(std::size(range)))
			return nullptr;
		return &range[static_cast<std::size_t>(index)];
	}
}
