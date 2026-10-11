#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>

/** @brief Caches both valid vertex layouts and null failure entries by descriptor. */
namespace GrassOptimizationLayouts
{
	template <class Resource>
	class InputLayoutCache
	{
	public:
		[[nodiscard]] Resource* Find(std::uint64_t descriptor) noexcept
		{
			auto it = entries.find(descriptor);
			return it == entries.end() ? nullptr : &it->second;
		}

		void CacheFailure(std::uint64_t descriptor)
		{
			entries.try_emplace(descriptor, Resource{});
		}

		void CacheSuccess(std::uint64_t descriptor, Resource&& resource)
		{
			entries.insert_or_assign(descriptor, std::move(resource));
		}

		void Clear() noexcept { entries.clear(); }

	private:
		std::unordered_map<std::uint64_t, Resource> entries;
	};
}
