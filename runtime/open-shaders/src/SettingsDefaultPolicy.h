#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace SettingsDefaultPolicy
{
	using json = nlohmann::json;

	/** Reject a package default layer from another version. */
	inline std::optional<std::string> GetStaleVersion(const json& a_settings, std::string_view a_currentVersion)
	{
		std::string foundVersion = "missing";
		if (const auto it = a_settings.find("Version"); it != a_settings.end() && it->is_string())
			foundVersion = it->get<std::string>();

		if (foundVersion == a_currentVersion)
			return std::nullopt;
		return foundVersion;
	}

	inline std::optional<std::string> ApplyVersionGate(json& a_settings, std::string_view a_currentVersion)
	{
		auto staleVersion = GetStaleVersion(a_settings, a_currentVersion);
		if (staleVersion)
			a_settings = json::object();
		return staleVersion;
	}
}
