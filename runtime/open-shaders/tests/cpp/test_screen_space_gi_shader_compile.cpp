#include <catch2/catch_test_macros.hpp>

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	using Microsoft::WRL::ComPtr;

	class FeatureInclude final : public ID3DInclude
	{
	public:
		explicit FeatureInclude(std::filesystem::path a_source)
			: source(std::move(a_source)), roots{ source.parent_path().parent_path(),
				std::filesystem::path(OPENNR_SHADER_SOURCE_DIR) / "package/Shaders" }
		{}

		HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR a_filename, LPCVOID a_parentData,
			LPCVOID* a_data, UINT* a_bytes) override
		{
			std::vector<std::filesystem::path> candidates;
			if (a_parentData) {
				if (const auto parent = paths.find(a_parentData); parent != paths.end())
					candidates.push_back(parent->second.parent_path() / a_filename);
			}
			else {
				candidates.push_back(source.parent_path() / a_filename);
			}
			for (const auto& root : roots)
				candidates.push_back(root / a_filename);

			for (const auto& path : candidates) {
				std::ifstream file(path, std::ios::binary | std::ios::ate);
				if (!file)
					continue;
				const auto size = static_cast<std::streamoff>(file.tellg());
				if (size < 0 || static_cast<std::uint64_t>(size) > std::numeric_limits<UINT>::max())
					return E_FAIL;
				auto bytes = std::make_unique<char[]>(static_cast<std::size_t>(size));
				file.seekg(0, std::ios::beg);
				if (size > 0 && !file.read(bytes.get(), static_cast<std::streamsize>(size)))
					return E_FAIL;
				*a_bytes = static_cast<UINT>(size);
				*a_data = bytes.get();
				paths.emplace(*a_data, path);
				buffers.emplace(*a_data, std::move(bytes));
				return S_OK;
			}
			return E_FAIL;
		}

		HRESULT STDMETHODCALLTYPE Close(LPCVOID a_data) override
		{
			paths.erase(a_data);
			buffers.erase(a_data);
			return S_OK;
		}

	private:
		std::filesystem::path source;
		std::array<std::filesystem::path, 2> roots;
		std::unordered_map<const void*, std::filesystem::path> paths;
		std::unordered_map<const void*, std::unique_ptr<char[]>> buffers;
	};

	struct ShaderPermutation
	{
		const char* name;
		std::vector<D3D_SHADER_MACRO> defines;
	};

	ShaderPermutation MakePermutation(const char* a_name, std::initializer_list<const char*> a_defines)
	{
		ShaderPermutation result{ a_name, {} };
		result.defines.push_back({ "COMPUTESHADER", "" });
		result.defines.push_back({ "WINPC", "" });
		result.defines.push_back({ "DX11", "" });
		for (const auto* define : a_defines)
			result.defines.push_back({ define, "" });
		result.defines.push_back({ nullptr, nullptr });
		return result;
	}

	std::string Compile(const std::filesystem::path& a_source, const ShaderPermutation& a_permutation)
	{
		FeatureInclude includes(a_source);
		ComPtr<ID3DBlob> bytecode;
		ComPtr<ID3DBlob> diagnostics;
		const auto result = D3DCompileFromFile(a_source.c_str(), a_permutation.defines.data(), &includes,
			"main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			&bytecode, &diagnostics);
		if (SUCCEEDED(result))
			return {};
		if (diagnostics && diagnostics->GetBufferPointer())
			return std::string(static_cast<const char*>(diagnostics->GetBufferPointer()), diagnostics->GetBufferSize());
		return "D3DCompileFromFile failed with HRESULT " + std::to_string(static_cast<unsigned long>(result));
	}
}

TEST_CASE("Screen Space GI disocclusion shader compiles its mask-output permutations", "[ssgi][shader-compile]")
{
	const auto source = std::filesystem::path(OPENNR_SHADER_SOURCE_DIR) /
		"features/Screen Space GI/Shaders/ScreenSpaceGI/radianceDisocc.cs.hlsl";
	REQUIRE(std::filesystem::exists(source));

	const std::array permutations{
		MakePermutation("VR AO without temporal denoising", { "VR" }),
		MakePermutation("VR AO with temporal denoising", { "VR", "TEMPORAL_DENOISER" }),
		MakePermutation("VR GI without specular or temporal denoising", { "VR", "GI" }),
		MakePermutation("VR GI with temporal denoising", { "VR", "GI", "TEMPORAL_DENOISER" }),
		MakePermutation("VR GI and specular without temporal denoising", { "VR", "GI", "GI_SPECULAR" }),
		MakePermutation("VR GI and specular with temporal denoising", { "VR", "GI", "GI_SPECULAR", "TEMPORAL_DENOISER" }),
	};

	for (const auto& permutation : permutations) {
		DYNAMIC_SECTION(permutation.name) {
			const auto diagnostics = Compile(source, permutation);
			INFO((diagnostics.empty() ? "No compiler diagnostics" : diagnostics));
			CHECK(diagnostics.empty());
		}
	}
}

TEST_CASE("Screen Space GI first-person early return writes every UAV", "[ssgi][shader-source]")
{
	const auto source = std::filesystem::path(OPENNR_SHADER_SOURCE_DIR) /
		"features/Screen Space GI/Shaders/ScreenSpaceGI/radianceDisocc.cs.hlsl";
	std::ifstream file(source, std::ios::binary);
	REQUIRE(file.good());
	const std::string shader{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	const auto branchStart = shader.find("if (curr_depth < FP_Z) {");
	REQUIRE(branchStart != std::string::npos);
	const auto branchEnd = shader.find("return;", branchStart);
	REQUIRE(branchEnd != std::string::npos);
	const auto earlyReturn = shader.substr(branchStart, branchEnd - branchStart);

	for (const auto* output : { "outRadianceDisocc", "outAccumFrames", "outRemappedAo",
			 "outRemappedIlY", "outRemappedIlCoCg", "outRemappedPrevGISpecular" })
		CHECK(earlyReturn.find(std::string(output) + "[pixCoord] =") != std::string::npos);
}
