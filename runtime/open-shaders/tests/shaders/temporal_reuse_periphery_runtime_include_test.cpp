#define NOMINMAX
#include "Utils/ShaderInclude.h"

#include <catch2/catch_test_macros.hpp>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace
{
	using Microsoft::WRL::ComPtr;
	struct Float2 { float x, y; };
	struct Float4 { float r, g, b, a; };
	struct ReuseParams
	{
		std::uint32_t colorWidth, colorHeight, guideWidth, guideHeight;
		float motionScaleX, motionScaleY, depthThreshold, colorTolerance;
		std::uint32_t useDepth, useColor, padding0, usePeripheryCoarseFallback;
	};
	static_assert(sizeof(ReuseParams) == 48);

	struct Fixture
	{
		std::filesystem::path root = std::filesystem::temp_directory_path() /
			("OpenNR-TemporalPeripheryRuntimeInclude-" + std::to_string(GetCurrentProcessId()));
		std::filesystem::path previousDirectory = std::filesystem::current_path();
		std::filesystem::path shaderDirectory = root / "Data/Shaders/Upscaling/NeuralRendering";
		std::filesystem::path shader = shaderDirectory / "TemporalReuseCS.hlsl";
		Fixture()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
			const auto source = std::filesystem::path(OPENNR_SHADER_SOURCE_DIR) /
				"features/Upscaling/Shaders/Upscaling/NeuralRendering";
			std::filesystem::create_directories(shaderDirectory);
			std::filesystem::copy(source, shaderDirectory,
				std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
			std::filesystem::current_path(root);
		}
		~Fixture()
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

	ComPtr<ID3D11Texture2D> MakeRGBA(ID3D11Device* device, std::uint32_t width, std::uint32_t height,
		Float4 value, UINT bindFlags)
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
		Check(device->CreateTexture2D(&desc, &initial, &texture), "Create RGBA texture");
		return texture;
	}

	ComPtr<ID3D11Texture2D> MakeMotion(ID3D11Device* device, std::uint32_t width, std::uint32_t height,
		Float2 value)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R32G32_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		std::vector<Float2> pixels(static_cast<std::size_t>(width) * height, value);
		D3D11_SUBRESOURCE_DATA initial{ pixels.data(), width * sizeof(Float2), 0 };
		ComPtr<ID3D11Texture2D> texture;
		Check(device->CreateTexture2D(&desc, &initial, &texture), "Create motion texture");
		return texture;
	}

	float CheckOutput(ID3D11DeviceContext* context, ID3D11Texture2D* output,
		std::uint32_t width, std::uint32_t height, Float4 expected)
	{
		D3D11_TEXTURE2D_DESC desc{};
		output->GetDesc(&desc);
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture2D> staging;
		ComPtr<ID3D11Device> device;
		context->GetDevice(device.GetAddressOf());
		Check(device->CreateTexture2D(&desc, nullptr, &staging), "Create staging texture");
		context->CopyResource(staging.Get(), output);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map output");
		float maxError = 0.0f;
		for (std::uint32_t y = 0; y < height; ++y) {
			const auto* row = reinterpret_cast<const Float4*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch);
			for (std::uint32_t x = 0; x < width; ++x)
				maxError = std::max({ maxError, std::abs(row[x].r - expected.r), std::abs(row[x].g - expected.g),
					std::abs(row[x].b - expected.b), std::abs(row[x].a - expected.a) });
		}
		context->Unmap(staging.Get(), 0);
		return maxError;
	}
}

TEST_CASE("Rejected periphery warp uses the last fresh coarse effect through runtime CustomInclude",
	"[neural-rendering][temporal-reuse][periphery][runtime-include][gpu]")
{
	Fixture fixture;
	const D3D_SHADER_MACRO defines[]{ { "COMPUTESHADER", "" }, { "WINPC", "" }, { "DX11", "" }, { nullptr, nullptr } };
	Util::CustomInclude include(fixture.shader);
	ComPtr<ID3DBlob> bytecode, diagnostics;
	const HRESULT compile = D3DCompileFromFile(fixture.shader.c_str(), defines, &include,
		"Reproject", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
		0, &bytecode, &diagnostics);
	INFO((diagnostics ? static_cast<const char*>(diagnostics->GetBufferPointer()) : "No compiler diagnostics"));
	REQUIRE(SUCCEEDED(compile));

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL featureLevel{};
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, &device, &featureLevel, &context), "Create D3D11 device");
	REQUIRE(featureLevel >= D3D_FEATURE_LEVEL_11_0);
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader),
		"Create Reproject shader");

	constexpr std::uint32_t width = 8, height = 8;
	constexpr Float4 raw{ 0.24f, 0.31f, 0.42f, 0.75f };
	constexpr Float4 coarse{ 0.08f, 0.04f, 0.12f, 0.0f };
	auto rawTexture = MakeRGBA(device.Get(), width, height, raw, D3D11_BIND_SHADER_RESOURCE);
	auto coarseTexture = MakeRGBA(device.Get(), 1, 1, coarse, D3D11_BIND_SHADER_RESOURCE);
	auto motionTexture = MakeMotion(device.Get(), width, height, { 32.0f, 32.0f }); // force out-of-bounds rejects
	auto outputTexture = MakeRGBA(device.Get(), width, height, {}, D3D11_BIND_UNORDERED_ACCESS);
	ComPtr<ID3D11ShaderResourceView> rawSRV, coarseSRV, motionSRV;
	ComPtr<ID3D11UnorderedAccessView> outputUAV;
	Check(device->CreateShaderResourceView(rawTexture.Get(), nullptr, &rawSRV), "Create raw SRV");
	Check(device->CreateShaderResourceView(coarseTexture.Get(), nullptr, &coarseSRV), "Create coarse SRV");
	Check(device->CreateShaderResourceView(motionTexture.Get(), nullptr, &motionSRV), "Create motion SRV");
	Check(device->CreateUnorderedAccessView(outputTexture.Get(), nullptr, &outputUAV), "Create output UAV");
	ReuseParams params{ width, height, width, height, 1.0f, 1.0f, 0.05f, 0.08f, 0, 0, 0, 1 };
	D3D11_BUFFER_DESC cbDesc{};
	cbDesc.ByteWidth = sizeof(params);
	cbDesc.Usage = D3D11_USAGE_DEFAULT;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	D3D11_SUBRESOURCE_DATA cbData{ &params, 0, 0 };
	ComPtr<ID3D11Buffer> constants;
	Check(device->CreateBuffer(&cbDesc, &cbData, &constants), "Create constants");
	D3D11_SAMPLER_DESC samplerDesc{};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	ComPtr<ID3D11SamplerState> sampler;
	Check(device->CreateSamplerState(&samplerDesc, &sampler), "Create sampler");
	ID3D11ShaderResourceView* srvs[9]{};
	srvs[0] = rawSRV.Get();
	srvs[7] = motionSRV.Get();
	srvs[8] = coarseSRV.Get();
	ID3D11UnorderedAccessView* uavs[4]{};
	uavs[3] = outputUAV.Get();
	ID3D11Buffer* cb = constants.Get();
	ID3D11SamplerState* samplerPtr = sampler.Get();
	auto dispatch = [&] {
		context->CSSetShader(shader.Get(), nullptr, 0);
		context->CSSetConstantBuffers(0, 1, &cb);
		context->CSSetShaderResources(0, 9, srvs);
		context->CSSetUnorderedAccessViews(0, 4, uavs, nullptr);
		context->CSSetSamplers(0, 1, &samplerPtr);
		context->Dispatch(1, 1, 1);
		ID3D11ShaderResourceView* nullSRVs[9]{};
		ID3D11UnorderedAccessView* nullUAVs[4]{};
		context->CSSetShaderResources(0, 9, nullSRVs);
		context->CSSetUnorderedAccessViews(0, 4, nullUAVs, nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
	};
	dispatch();
	const Float4 hybridExpected{ raw.r + coarse.r, raw.g + coarse.g, raw.b + coarse.b, raw.a };
	const float hybridError = CheckOutput(context.Get(), outputTexture.Get(), width, height, hybridExpected);
	INFO("Periphery coarse fallback CPU/GPU max error: " << hybridError);
	CHECK(hybridError <= 1.0f / 255.0f);

	// The general path keeps rejected pixels raw; it must not read periphery history.
	params.usePeripheryCoarseFallback = 0;
	context->UpdateSubresource(constants.Get(), 0, nullptr, &params, 0, 0);
	dispatch();
	const float generalError = CheckOutput(context.Get(), outputTexture.Get(), width, height, raw);
	INFO("General rejected-warp raw fallback max error: " << generalError);
	CHECK(generalError <= 1.0f / 255.0f);
}
