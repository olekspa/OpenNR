#include "Utils/ShaderInclude.h"

#include <catch2/catch_test_macros.hpp>
#include <d3dcompiler.h>
#include <filesystem>
#include <string>
#include <vector>
#include <winrt/base.h>

namespace
{
	struct ShaderFixture
	{
		std::filesystem::path root = std::filesystem::temp_directory_path() /
		                             ("OpenNR-UpscalingShaderCompile-" + std::to_string(GetCurrentProcessId()));
		std::filesystem::path previousDirectory = std::filesystem::current_path();
		std::filesystem::path shaderPath = root / "Data/Shaders/Upscaling/EncodeTexturesCS.hlsl";

		ShaderFixture()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
			std::filesystem::create_directories(shaderPath.parent_path());
			std::filesystem::create_directories(root / "Data/Shaders/Common");
			std::filesystem::copy_file(
				std::filesystem::path(OPENNR_SHADER_SOURCE_DIR) / "features/Upscaling/Shaders/Upscaling/EncodeTexturesCS.hlsl",
				shaderPath);
			std::filesystem::copy(
				std::filesystem::path(OPENNR_SHADER_SOURCE_DIR) / "package/Shaders/Common",
				root / "Data/Shaders/Common",
				std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
			std::filesystem::current_path(root);
		}

		~ShaderFixture()
		{
			std::error_code error;
			std::filesystem::current_path(previousDirectory, error);
			std::filesystem::remove_all(root, error);
		}
	};

	winrt::com_ptr<ID3DBlob> CompileTypedDepthVariant(const char* a_upscaler,
		winrt::com_ptr<ID3DBlob>& a_errors, HRESULT& a_result)
	{
		std::vector<D3D_SHADER_MACRO> defines = {
			{ a_upscaler, "" },
			{ "DEPTH_OUTPUT", "" },
			{ "VR", "" },
			{ "COMPUTESHADER", "" },
			{ "WINPC", "" },
			{ "DX11", "" },
			{ nullptr, nullptr }
		};
		Util::CustomInclude include(L"Data\\Shaders\\Upscaling\\EncodeTexturesCS.hlsl");
		winrt::com_ptr<ID3DBlob> bytecode;
		a_result = D3DCompileFromFile(L"Data\\Shaders\\Upscaling\\EncodeTexturesCS.hlsl", defines.data(), &include,
			"main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			bytecode.put(), a_errors.put());
		return bytecode;
	}
}

TEST_CASE("VR DLSS and FSR typed-depth encoder variants compile", "[upscaling][shader-compile]")
{
	ShaderFixture fixture;
	REQUIRE(std::filesystem::exists(fixture.shaderPath));

	for (const auto* variant : { "DLSS", "FSR" }) {
		DYNAMIC_SECTION("upscaler=" << variant) {
			winrt::com_ptr<ID3DBlob> errors;
			HRESULT result = E_FAIL;
			auto bytecode = CompileTypedDepthVariant(variant, errors, result);
			INFO((errors ? static_cast<const char*>(errors->GetBufferPointer()) : "No compiler diagnostics"));
			REQUIRE(SUCCEEDED(result));
			REQUIRE(bytecode);
		}
	}
}
