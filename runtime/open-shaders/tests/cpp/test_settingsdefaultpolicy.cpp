#include "SettingsDefaultPolicy.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <chrono>

namespace
{
	using Settings = SettingsDefaultPolicy::json;
	constexpr auto currentVersion = "2-22-0-0";

	std::filesystem::path MakeTempDirectory()
	{
		auto path = std::filesystem::temp_directory_path() /
		            ("opennr-settings-default-policy-test-" +
		             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(path);
		return path;
	}

	void WriteJson(const std::filesystem::path& a_path, const Settings& a_settings)
	{
		std::ofstream output(a_path);
		output << a_settings.dump();
	}

	Settings ReadJson(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path);
		Settings settings;
		input >> settings;
		return settings;
	}
}

TEST_CASE("SettingsDefault version gate ignores stale package defaults but user settings still merge", "[settingsdefault]")
{
	auto directory = MakeTempDirectory();
	auto stalePath = directory / "SettingsDefault.json";
	WriteJson(stalePath, {{"Version", "2-10-1-0"}, {"Global", {{"keepCurrentDefault", false}}}, {"FeatureA", {{"enabled", false}}}});

	auto settings = ReadJson(stalePath);
	REQUIRE(SettingsDefaultPolicy::ApplyVersionGate(settings, currentVersion) == "2-10-1-0");
	REQUIRE(settings.empty());

	const Settings userSettings{{"Global", {{"keepCurrentDefault", true}}}, {"FeatureA", {{"enabled", false}}}};
	for (const auto& [key, value] : userSettings.items())
		settings[key] = value;
	REQUIRE(settings["Global"]["keepCurrentDefault"] == true);
	REQUIRE(settings["FeatureA"]["enabled"] == false);
	std::filesystem::remove_all(directory);
}

TEST_CASE("SettingsDefault version gate preserves current-version base and feature defaults", "[settingsdefault]")
{
	auto directory = MakeTempDirectory();
	auto currentPath = directory / "SettingsDefault.json";
	const Settings current{{"Version", currentVersion}, {"Global", {{"keepCurrentDefault", false}}}, {"FeatureA", {{"enabled", true}}}};
	WriteJson(currentPath, current);

	auto settings = ReadJson(currentPath);
	REQUIRE_FALSE(SettingsDefaultPolicy::ApplyVersionGate(settings, currentVersion));
	REQUIRE(settings == current);
	std::filesystem::remove_all(directory);
}

TEST_CASE("Missing SettingsDefault uses generated current-version defaults", "[settingsdefault]")
{
	auto directory = MakeTempDirectory();
	const auto missingPath = directory / "SettingsDefault.json";
	REQUIRE_FALSE(std::filesystem::exists(missingPath));

	// State::Load creates the missing file through Save(DEFAULT), then reads it.
	const Settings generated{{"Version", currentVersion}, {"Global", {{"keepCurrentDefault", true}}}, {"FeatureA", {{"enabled", true}}}};
	WriteJson(missingPath, generated);
	auto settings = ReadJson(missingPath);
	SettingsDefaultPolicy::ApplyVersionGate(settings, currentVersion);
	REQUIRE(settings == generated);
	std::filesystem::remove_all(directory);
}
