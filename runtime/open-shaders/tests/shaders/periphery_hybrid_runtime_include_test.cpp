#define NOMINMAX
#include "Utils/ShaderInclude.h"

#include <catch2/catch_test_macros.hpp>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace
{
	using Microsoft::WRL::ComPtr;

	struct Float4 { float r, g, b, a; };
	struct HybridParams
	{
		std::uint32_t width, height, eyeWidth, mode;
		std::uint32_t leftCropX, leftCropY, rightCropX, rightCropY;
		std::uint32_t cropWidth, cropHeight;
		float gain, feather;
	};
	static_assert(sizeof(HybridParams) == 48);

	struct RuntimeShaderFixture
	{
		std::filesystem::path root = std::filesystem::temp_directory_path() /
			("OpenNR-PeripheryHybridRuntimeInclude-" + std::to_string(GetCurrentProcessId()));
		std::filesystem::path previousDirectory = std::filesystem::current_path();
		std::filesystem::path shaderDirectory = root / "Data/Shaders/Upscaling/NeuralRendering";
		std::filesystem::path productionShader = shaderDirectory / "PeripheryHybridCS.hlsl";
		RuntimeShaderFixture()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
			std::filesystem::create_directories(shaderDirectory);
			const auto source = std::filesystem::path(OPENNR_SHADER_SOURCE_DIR) /
				"features/Upscaling/Shaders/Upscaling/NeuralRendering";
			std::filesystem::copy(source, shaderDirectory,
				std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
			std::filesystem::current_path(root);
		}
		~RuntimeShaderFixture()
		{
			std::error_code error;
			std::filesystem::current_path(previousDirectory, error);
			std::filesystem::remove_all(root, error);
		}
	};

	void Check(HRESULT result, const char* operation)
	{
		if (FAILED(result))
			throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
	}

	ComPtr<ID3D11Texture2D> MakeTexture(ID3D11Device* device, std::uint32_t width,
		std::uint32_t height, Float4 value, UINT bindFlags)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = bindFlags;
		std::vector<Float4> pixels(static_cast<std::size_t>(width) * height, value);
		D3D11_SUBRESOURCE_DATA initial{ pixels.data(), width * sizeof(Float4), 0 };
		ComPtr<ID3D11Texture2D> texture;
		Check(device->CreateTexture2D(&desc, &initial, &texture), "CreateTexture2D");
		return texture;
	}

	float CentreWeight(float edge, float feather)
	{
		const float t = std::clamp(edge / std::max(feather, 1.0f), 0.0f, 1.0f);
		return 0.5f - 0.5f * std::cos(3.14159265359f * t);
	}

	float Compose(float raw, float periphery, float centre, float coarseP, float coarseC, float weight, float gain)
	{
		return raw + std::clamp(gain, 0.0f, 1.5f) * coarseP +
			weight * ((centre - raw) - coarseC) + (1.0f - weight) * ((periphery - raw) - coarseP);
	}
}

TEST_CASE("Periphery hybrid shader compiles through runtime include and validates both display dispatch heights",
	"[neural-rendering][periphery-hybrid][runtime-include][periphery-height]")
{
	RuntimeShaderFixture fixture;
	const std::vector<D3D_SHADER_MACRO> defines = {
		{ "VR", "" }, { "COMPUTESHADER", "" }, { "WINPC", "" }, { "DX11", "" }, { nullptr, nullptr },
	};
	Util::CustomInclude include(fixture.productionShader);
	ComPtr<ID3DBlob> bytecode;
	ComPtr<ID3DBlob> errors;
	const HRESULT compileResult = D3DCompileFromFile(fixture.productionShader.c_str(), defines.data(), &include,
		"main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
		&bytecode, &errors);
	INFO((errors ? static_cast<const char*>(errors->GetBufferPointer()) : "No compiler diagnostics"));
	REQUIRE(SUCCEEDED(compileResult));
	REQUIRE(bytecode);

	for (const std::uint32_t height : { 2428u, 2432u }) {
		const auto dispatchY = (height + 7u) / 8u;
		CHECK(dispatchY == 304u);
		CHECK((dispatchY - 1u) * 8u < height);
		CHECK(dispatchY * 8u >= height);
	}
}

TEST_CASE("Periphery hybrid production shader compiles through runtime CustomInclude and matches CPU fixture",
	"[neural-rendering][periphery-hybrid][runtime-include][gpu]")
{
	RuntimeShaderFixture fixture;
	const std::vector<D3D_SHADER_MACRO> defines = {
		{ "VR", "" }, { "COMPUTESHADER", "" }, { "WINPC", "" }, { "DX11", "" }, { nullptr, nullptr },
	};
	Util::CustomInclude include(fixture.productionShader);
	ComPtr<ID3DBlob> bytecode;
	ComPtr<ID3DBlob> errors;
	const HRESULT compileResult = D3DCompileFromFile(fixture.productionShader.c_str(), defines.data(), &include,
		"main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
		&bytecode, &errors);
	INFO((errors ? static_cast<const char*>(errors->GetBufferPointer()) : "No compiler diagnostics"));
	REQUIRE(SUCCEEDED(compileResult));
	REQUIRE(bytecode);

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL featureLevel{};
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, &device, &featureLevel, &context), "D3D11CreateDevice");
	REQUIRE(featureLevel >= D3D_FEATURE_LEVEL_11_0);
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader),
		"CreateComputeShader");

	constexpr std::uint32_t width = 8;
	for (const std::uint32_t height : { 2428u, 2432u }) {
	const auto coarseHeight = (height + 7u) / 8u;
	constexpr Float4 raw{ 0.20f, 0.35f, 0.80f, 0.67f };
	constexpr Float4 periphery{ 0.50f, 0.55f, 0.90f, 0.11f };
	constexpr Float4 centre{ 0.70f, 0.75f, 0.95f, 0.22f };
	constexpr Float4 coarseP{ 0.10f, 0.12f, 0.08f, 0.0f };
	constexpr Float4 coarseC{ 0.20f, 0.15f, 0.04f, 0.0f };
	auto rawTex = MakeTexture(device.Get(), width, height, raw, D3D11_BIND_SHADER_RESOURCE);
	auto peripheryTex = MakeTexture(device.Get(), width, height, periphery, D3D11_BIND_SHADER_RESOURCE);
	auto centreTex = MakeTexture(device.Get(), width, height, centre, D3D11_BIND_SHADER_RESOURCE);
	auto coarsePTex = MakeTexture(device.Get(), 1, coarseHeight, coarseP, D3D11_BIND_SHADER_RESOURCE);
	auto coarseCTex = MakeTexture(device.Get(), 1, coarseHeight, coarseC, D3D11_BIND_SHADER_RESOURCE);
	auto outputTex = MakeTexture(device.Get(), width, height, {}, D3D11_BIND_UNORDERED_ACCESS);
	ComPtr<ID3D11ShaderResourceView> rawSRV, peripherySRV, centreSRV, coarsePSRV, coarseCSRV;
	ComPtr<ID3D11UnorderedAccessView> outputUAV;
	Check(device->CreateShaderResourceView(rawTex.Get(), nullptr, &rawSRV), "Create raw SRV");
	Check(device->CreateShaderResourceView(peripheryTex.Get(), nullptr, &peripherySRV), "Create periphery SRV");
	Check(device->CreateShaderResourceView(centreTex.Get(), nullptr, &centreSRV), "Create centre SRV");
	Check(device->CreateShaderResourceView(coarsePTex.Get(), nullptr, &coarsePSRV), "Create periphery coarse SRV");
	Check(device->CreateShaderResourceView(coarseCTex.Get(), nullptr, &coarseCSRV), "Create centre coarse SRV");
	Check(device->CreateUnorderedAccessView(outputTex.Get(), nullptr, &outputUAV), "Create output UAV");

	const HybridParams params{ width, height, width, 1, 0, 0, 0, 0, width, height, 0.83f, 4.0f };
	D3D11_BUFFER_DESC cbDesc{};
	cbDesc.ByteWidth = sizeof(params);
	cbDesc.Usage = D3D11_USAGE_DEFAULT;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	D3D11_SUBRESOURCE_DATA cbData{ &params, 0, 0 };
	ComPtr<ID3D11Buffer> constants;
	Check(device->CreateBuffer(&cbDesc, &cbData, &constants), "Create constant buffer");
	D3D11_SAMPLER_DESC samplerDesc{};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	ComPtr<ID3D11SamplerState> sampler;
	Check(device->CreateSamplerState(&samplerDesc, &sampler), "Create sampler");
	ID3D11ShaderResourceView* srvs[]{ rawSRV.Get(), peripherySRV.Get(), centreSRV.Get(), coarsePSRV.Get(), coarseCSRV.Get(), nullptr };
	ID3D11UnorderedAccessView* uavs[]{ nullptr, nullptr, outputUAV.Get() };
	ID3D11Buffer* cb = constants.Get();
	ID3D11SamplerState* samplerPtr = sampler.Get();
	context->CSSetShader(shader.Get(), nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetShaderResources(0, static_cast<UINT>(std::size(srvs)), srvs);
	context->CSSetUnorderedAccessViews(0, static_cast<UINT>(std::size(uavs)), uavs, nullptr);
	context->CSSetSamplers(0, 1, &samplerPtr);
	context->Dispatch(1, (height + 7u) / 8u, 1);
	ID3D11ShaderResourceView* nullSRVs[6]{};
	ID3D11UnorderedAccessView* nullUAVs[3]{};
	context->CSSetShaderResources(0, 6, nullSRVs);
	context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);

	D3D11_TEXTURE2D_DESC stagingDesc{};
	outputTex->GetDesc(&stagingDesc);
	stagingDesc.BindFlags = 0;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging;
	Check(device->CreateTexture2D(&stagingDesc, nullptr, &staging), "Create staging texture");
	context->CopyResource(staging.Get(), outputTex.Get());
	D3D11_MAPPED_SUBRESOURCE mapped{};
	Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map staging texture");
	float maxError = 0.0f;
	for (std::uint32_t y = 0; y < height; ++y) {
		const auto* row = reinterpret_cast<const Float4*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch);
		for (std::uint32_t x = 0; x < width; ++x) {
			// The fixture crop touches every image boundary, so those sides are
			// intentionally not feathered and every sample is in the centre region.
			const float weight = 1.0f;
			const Float4 expected{
				Compose(raw.r, periphery.r, centre.r, coarseP.r, coarseC.r, weight, params.gain),
				Compose(raw.g, periphery.g, centre.g, coarseP.g, coarseC.g, weight, params.gain),
				Compose(raw.b, periphery.b, centre.b, coarseP.b, coarseC.b, weight, params.gain), raw.a,
			};
			maxError = std::max({ maxError, std::abs(row[x].r - expected.r), std::abs(row[x].g - expected.g),
				std::abs(row[x].b - expected.b), std::abs(row[x].a - expected.a) });
		}
	}
	context->Unmap(staging.Get(), 0);
	INFO("Maximum per-channel absolute GPU/CPU error: " << maxError);
	CHECK(maxError <= 1.0f / 255.0f);
	}
}
