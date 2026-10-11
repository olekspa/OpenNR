#include "Features/Upscaling/NeuralRendering/ShaderInitLatch.h"
#include "Utils/ShaderInclude.h"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <winrt/base.h>

namespace
{
	struct ShaderEntry
	{
		const wchar_t* file;
		const char* entry;
	};

	// Keep this table in sync with the exact entry points compiled by the runtime.
	// The expected file inventory below ensures every shipped NR .hlsl is covered.
	constexpr ShaderEntry kNeuralRenderingEntries[] = {
		{ L"AdaptiveCropHandoffCS.hlsl", "main" },
		{ L"AdaptiveHandoffCS.hlsl", "main" },
		{ L"CopyDepthGuideCS.hlsl", "main" },
		{ L"ModelResolutionCS.hlsl", "main" },
		{ L"PeripheryHybridCS.hlsl", "main" },
		{ L"ResultShapingCS.hlsl", "main" },
		{ L"TemporalReuseCS.hlsl", "Snapshot" },
		{ L"TemporalReuseCS.hlsl", "SnapshotPeripheryCoarse" },
		{ L"TemporalReuseCS.hlsl", "Accumulate" },
		{ L"TemporalReuseCS.hlsl", "Reproject" },
	};

		bool AioFeatureIncluded(const std::filesystem::path& featureRoot)
	{
		if (std::filesystem::exists(featureRoot / "CORE"))
			return true;
#ifndef ENABLE_EFFECTS11
		if (featureRoot.filename() == "Effects11")
			return false;
#endif
		const auto iniDirectory = featureRoot / "Shaders/Features";
		if (!std::filesystem::exists(iniDirectory))
			return false;
		for (const auto& entry : std::filesystem::directory_iterator(iniDirectory)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".ini")
				continue;
			std::ifstream ini(entry.path());
			std::string line;
			while (std::getline(ini, line)) {
				const auto first = line.find_first_not_of(" \t");
				if (first == std::string::npos)
					continue;
				line.erase(0, first);
				const auto equal = line.find('=');
				if (equal == std::string::npos)
					continue;
				auto key = line.substr(0, equal);
				auto value = line.substr(equal + 1);
				const auto trim = [](std::string& text) {
					const auto begin = text.find_first_not_of(" \t");
					if (begin == std::string::npos) { text.clear(); return; }
					const auto end = text.find_last_not_of(" \t\r");
					text = text.substr(begin, end - begin + 1);
				};
				trim(key);
				trim(value);
				std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if ((key == "aio" || key == "autoupload") &&
					(value == "true" || value == "1" || value == "yes" || value == "on"))
					return true;
			}
		}
		return false;
	}

	bool IsTestShaderPath(const std::filesystem::path& relative)
	{
		return std::any_of(relative.begin(), relative.end(), [](const auto& component) {
			return component == "Tests";
		});
	}
	struct ShaderFixture
	{
		std::filesystem::path root = std::filesystem::temp_directory_path() /
			("OpenNR-NeuralRenderingShaderCompile-" + std::to_string(GetCurrentProcessId()));
		std::filesystem::path previousDirectory = std::filesystem::current_path();
	std::filesystem::path dataShaderRoot = root / "Data/Shaders";
	std::filesystem::path runtimeShaderDirectory = dataShaderRoot / "Upscaling/NeuralRendering";

		ShaderFixture()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
			std::filesystem::create_directories(dataShaderRoot);
			const auto packageShaders = std::filesystem::path(OPENNR_SHADER_SOURCE_DIR) / "package/Shaders";
			for (const auto& entry : std::filesystem::recursive_directory_iterator(packageShaders)) {
				if (!entry.is_regular_file())
					continue;
				const auto relative = entry.path().lexically_relative(packageShaders);
				if (IsTestShaderPath(relative))
					continue;
				const auto destination = dataShaderRoot / relative;
				std::filesystem::create_directories(destination.parent_path());
				std::filesystem::copy_file(entry.path(), destination, std::filesystem::copy_options::overwrite_existing);
			}
			const auto featuresRoot = std::filesystem::path(OPENNR_SHADER_SOURCE_DIR) / "features";
			std::vector<std::filesystem::path> featureRoots;
			for (const auto& entry : std::filesystem::directory_iterator(featuresRoot))
				if (entry.is_directory() && AioFeatureIncluded(entry.path()))
					featureRoots.push_back(entry.path());
			std::sort(featureRoots.begin(), featureRoots.end()); // CMake file(GLOB) feature order.
			for (const auto& featureRoot : featureRoots) {
				const auto shaderSource = featureRoot / "Shaders";
				if (!std::filesystem::exists(shaderSource))
					continue;
				std::vector<std::filesystem::path> shaderFiles;
				for (const auto& entry : std::filesystem::recursive_directory_iterator(shaderSource)) {
					if (!entry.is_regular_file())
						continue;
					const auto relative = entry.path().lexically_relative(shaderSource);
					if (!IsTestShaderPath(relative))
						shaderFiles.push_back(entry.path());
				}
				std::sort(shaderFiles.begin(), shaderFiles.end());
				for (const auto& source : shaderFiles) {
					const auto destination = dataShaderRoot / source.lexically_relative(shaderSource);
					std::filesystem::create_directories(destination.parent_path());
					std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing);
				}
			}
			std::filesystem::current_path(root);
		}

		~ShaderFixture()
		{
			std::error_code error;
			std::filesystem::current_path(previousDirectory, error);
			std::filesystem::remove_all(root, error);
		}
	};

	struct RuntimeShaderVariant
	{
		const char* entry;
		const char* profile;
		bool shaderCacheCompile = false;
		std::vector<std::pair<const char*, const char*>> extraDefines;
	};

	winrt::com_ptr<ID3DBlob> CompileRuntimeShader(const std::filesystem::path& relativeFile,
		const RuntimeShaderVariant& variant, winrt::com_ptr<ID3DBlob>& errors, HRESULT& result)
	{
		const auto path = std::filesystem::path(L"Data\\Shaders") / relativeFile;
		std::vector<D3D_SHADER_MACRO> defines;
		if (std::string_view(variant.profile) == "vs_5_0")
			defines.push_back({ "VSHADER", "" });
		else if (std::string_view(variant.profile) == "ps_5_0")
			defines.push_back({ "PSHADER", "" });
		else if (std::string_view(variant.profile) == "cs_5_0")
			defines.push_back({ variant.shaderCacheCompile ? "CSHADER" : "COMPUTESHADER", "" });
		else
			return nullptr;
		if (relativeFile.filename() == "colorgrading.cs.hlsl")
			defines.push_back({ "TONEMAP_FUNC", "OpenDRTTransform" });
		if (relativeFile.filename() == "dof.cs.hlsl")
			defines.push_back({ "GATHER_RING_COUNT", "4" });
		for (const auto& [name, value] : variant.extraDefines)
			defines.push_back({ name, value });
		defines.push_back({ "VR", "" });
		if (!variant.shaderCacheCompile) {
			defines.push_back({ "WINPC", "" });
			defines.push_back({ "DX11", "" });
		}
		defines.push_back({ nullptr, nullptr });
		Util::CustomInclude include(path);
		winrt::com_ptr<ID3DBlob> bytecode;
		result = D3DCompileFromFile(path.c_str(), defines.data(), &include, variant.entry, variant.profile,
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			bytecode.put(), errors.put());
		return bytecode;
	}

	std::vector<RuntimeShaderVariant> RuntimeVariants(const std::filesystem::path& relative)
	{
		const auto name = relative.filename().string();
		if (relative.begin()->string() == "Menu") {
			if (name == "BackgroundBlurHorizontal.hlsl")
				return { { "VS_Main", "vs_5_0" }, { "PS_Copy", "ps_5_0" }, { "PS_Downsample", "ps_5_0" },
					{ "PS_HDRDownsample", "ps_5_0" }, { "PS_Main", "ps_5_0" } };
			if (name == "BackgroundBlurVertical.hlsl")
				return { { "PS_Main", "ps_5_0" } };
			if (name == "BackgroundBlurComposite.hlsl")
				return { { "PS_Main", "ps_5_0" }, { "PS_Layer", "ps_5_0" }, { "PS_Scene", "ps_5_0" }, { "PS_Clear", "ps_5_0" } };
			return {};
		}
		if (name == "TreeWindSpringCS.hlsl")
			return { { "UpdateField", "cs_5_0", true } };
		if (name == "ISVolumetricLightingGenerateCS.hlsl" || name == "ISVolumetricLightingRaymarchCS.hlsl")
			return { { "main", "cs_5_0", true } };
		if (name == "ISRadialBlur.hlsl")
			return { { "main", "ps_5_0", true, { { "NUM_STEPS", "6" } } } };
		if (name == "ISSAOBlur.hlsl")
			return { { "main", "ps_5_0", true, { { "AXIS_H", "" } } }, { "main", "ps_5_0", true, { { "AXIS_V", "" } } } };
		if (name == "Sky.hlsl")
			return { { "main", "vs_5_0", true, { { "OCCLUSION", "" } } }, { "main", "ps_5_0", true, { { "OCCLUSION", "" } } } };
		if (name == "Water.hlsl")
			return { { "main", "vs_5_0", true, { { "WATER", "" }, { "FOG", "" }, { "SPECULAR", "" }, { "NUM_SPECULAR_LIGHTS", "1" } } },
				{ "main", "ps_5_0", true, { { "WATER", "" }, { "FOG", "" }, { "SPECULAR", "" }, { "NUM_SPECULAR_LIGHTS", "1" } } } };
		if (name == "VRPostProcessCS.hlsl" || name == "StereoBlendCS.hlsl")
			return { { "main", "cs_5_0", false, { { "FRAMEBUFFER", "" } } } };
		if (name == "DepthScatterCS.hlsl" || name == "GBufferFillCS.hlsl" || name == "StencilCS.hlsl")
			return { { "main", "cs_5_0", false, { { "FRAMEBUFFER", "" } } } };
		if (name == "DummyVSTexCoord.hlsl")
			return { { "main", "vs_5_0" } };
		if (name == "BlurShadowCS.hlsl")
			return { { "main", "cs_5_0", false, { { "BLUR_HORIZONTAL", "" } } }, { "main", "cs_5_0", false, { { "BLUR_VERTICAL", "" } } } };
		if (name == "DownsampleShadowCS.hlsl")
			return { { "main", "cs_5_0", false, { { "DOWNSAMPLE_SHADOW_MIP0", "" } } }, { "main", "cs_5_0", false, { { "DOWNSAMPLE_SHADOW_MIP1", "" } } } };
		if (name.ends_with("CS.hlsl") || name.ends_with(".cs.hlsl"))
			return { { "main", "cs_5_0" } };
		if (name.ends_with("VS.hlsl") || name.ends_with(".vs.hlsl"))
			return { { "main", "vs_5_0" } };
		if (name.ends_with("PS.hlsl") || name.ends_with(".ps.hlsl") ||
			name.starts_with("IS") || name == "UnderwaterFogToDepthOfField.hlsl")
			return { { "main", "ps_5_0" } };
		// ShaderCache compiles conventional BSShader sources at entry "main" with
		// the actual runtime stage profile and its VSHADER/PSHADER define.
		return { { "main", "vs_5_0", true }, { "main", "ps_5_0", true } };
	}
	std::vector<RuntimeShaderVariant> FeatureRuntimeVariants(const std::filesystem::path& relative)
	{
		const auto name = relative.filename().string();
		if (name == "ShadowDepthCopy.hlsl") return { { "main", "vs_5_0" }, { "main", "ps_5_0" } };
		if (name == "DepthBlend.hlsl" || name == "SPD.hlsl" || name == "RCAS.hlsl") return { { "main", "cs_5_0" } };
		if (name == "prefilterDepths.cs.hlsl") return { { "main", "cs_5_0", false, { { "LINEAR_FILTER", "" } } } };
		if (name == "reproject.cs.hlsl") return { { "main", "cs_5_0", false, { { "FRAMEBUFFER", "" } } } };
		if (name == "stereoSync.cs.hlsl") return { { "main", "cs_5_0", false, { { "FRAMEBUFFER", "" } } } };
		if (name == "RaymarchCS.hlsl") return { { "main", "cs_5_0", false, { { "SAMPLE_COUNT", "16" } } } };
		if (name == "StereoSyncCS.hlsl") return { { "main", "cs_5_0", false, { { "FRAMEBUFFER", "" } } } };
		if (name == "camera.cs.hlsl") return { { "CS_Camera", "cs_5_0" } };
		if (name == "bloom.cs.hlsl") return { { "CS_Threshold", "cs_5_0" }, { "CS_Downsample", "cs_5_0" }, { "CS_Upsample", "cs_5_0" }, { "CS_Composite", "cs_5_0" } };
		if (name == "colorgrading.cs.hlsl") return { { "CSColorGrading", "cs_5_0" }, { "CSLUTGen", "cs_5_0" } };
		if (name == "composite.cs.hlsl" && relative.generic_string().find("PhysicalGlare/") != std::string::npos) return { { "CS_Composite", "cs_5_0" } };
		if (name == "composite.cs.hlsl") return { { "CSComposite", "cs_5_0" } };
		if (name == "dof.cs.hlsl") return { { "CS_UpdateFocus", "cs_5_0" }, { "CS_CalculateCoC", "cs_5_0" }, { "CS_CoCTileFlatten", "cs_5_0" }, { "CS_CoCTileDilateH", "cs_5_0" }, { "CS_CoCTileDilateV", "cs_5_0" }, { "CS_Downsample", "cs_5_0" }, { "CS_DownsampleLegacy", "cs_5_0" }, { "CS_ReduceColorCoC", "cs_5_0" }, { "CS_ReduceColor", "cs_5_0" }, { "CS_FarBlur", "cs_5_0" }, { "CS_NearBlur", "cs_5_0" }, { "CS_FarGather", "cs_5_0" }, { "CS_NearGather", "cs_5_0" }, { "CS_GatherPostfilter", "cs_5_0" }, { "CS_Combiner", "cs_5_0" }, { "CS_PostSmoothing1", "cs_5_0" }, { "CS_PostSmoothing2AndFocusing", "cs_5_0" } };
		if (name == "histogram.cs.hlsl") return { { "CS_Histogram", "cs_5_0" }, { "CS_Average", "cs_5_0" } };
		if (name == "lensflare.cs.hlsl") return { { "CSThreshold", "cs_5_0" }, { "CSGhostHalo", "cs_5_0" }, { "CSFlareDown", "cs_5_0" }, { "CSFlareUp", "cs_5_0" }, { "CSMix", "cs_5_0" } };
		if (name == "lensflare_fft.cs.hlsl") return { { "CSBokehPrepare", "cs_5_0" }, { "CSFFTThreshold", "cs_5_0" }, { "CSFFTGhostCompose", "cs_5_0" }, { "CS_FFT", "cs_5_0" }, { "CS_Multiply", "cs_5_0" } };
		if (name == "localexposure.cs.hlsl") return { { "CSSetupLogLuminance", "cs_5_0" }, { "CSDownsampleLogLuminance", "cs_5_0" }, { "CSBlurHorizontal", "cs_5_0" }, { "CSBlurVertical", "cs_5_0" }, { "CSBuildLuminanceGrid", "cs_5_0" }, { "CSResolveBaseLuminance", "cs_5_0" } };
		if (name == "aperture.cs.hlsl") return { { "CS_Aperture", "cs_5_0" } };
		if (name == "psf.cs.hlsl") return { { "CS_ChromaticBlur", "cs_5_0" } };
		if (name == "tearfilm.cs.hlsl") return { { "CS_TearFilm", "cs_5_0" } };
		if (name == "threshold.cs.hlsl") return { { "CS_Threshold", "cs_5_0" } };
		if (name == "multiply.cs.hlsl") return { { "CS_Multiply", "cs_5_0" } };
		if (name == "pack.cs.hlsl") return { { "CS_Pack", "cs_5_0" } };
		if (name == "fft.cs.hlsl") return { { "CS_FFT", "cs_5_0" } };
		return RuntimeVariants(relative);
	}
}

TEST_CASE("Every shipped neural rendering shader compiles through runtime CustomInclude", "[neural-rendering][shader-compile]")
{
	ShaderFixture fixture;
	std::vector<std::wstring> shippedHlsl;
	for (const auto& entry : std::filesystem::directory_iterator(fixture.runtimeShaderDirectory)) {
		if (entry.is_regular_file() && entry.path().extension() == L".hlsl")
			shippedHlsl.push_back(entry.path().filename().wstring());
	}
	std::sort(shippedHlsl.begin(), shippedHlsl.end());
	std::vector<std::wstring> coveredHlsl;
	for (const auto& entry : kNeuralRenderingEntries)
		coveredHlsl.emplace_back(entry.file);
	std::sort(coveredHlsl.begin(), coveredHlsl.end());
	coveredHlsl.erase(std::unique(coveredHlsl.begin(), coveredHlsl.end()), coveredHlsl.end());
	REQUIRE(shippedHlsl == coveredHlsl);

	for (const auto& entry : kNeuralRenderingEntries) {
		DYNAMIC_SECTION("file=" << std::filesystem::path(entry.file).string() << " entry=" << entry.entry) {
			winrt::com_ptr<ID3DBlob> errors;
			HRESULT result = E_FAIL;
			auto bytecode = CompileRuntimeShader(std::filesystem::path("Upscaling/NeuralRendering") / entry.file,
				{ entry.entry, "cs_5_0" }, errors, result);
			INFO((errors ? static_cast<const char*>(errors->GetBufferPointer()) : "No compiler diagnostics"));
			REQUIRE(SUCCEEDED(result));
			REQUIRE(bytecode);
		}
	}
}

TEST_CASE("Every shipped package shader has runtime entry/profile coverage through CustomInclude", "[shader-compile][package]")
{
	ShaderFixture fixture;
	const auto packageShaders = std::filesystem::path(OPENNR_SHADER_SOURCE_DIR) / "package/Shaders";
	std::size_t inventoryCount = 0;
	std::size_t excludedTestCount = 0;
	std::size_t compiledVariantCount = 0;
	std::vector<std::string> unmapped;
	for (const auto& file : std::filesystem::recursive_directory_iterator(packageShaders)) {
		if (!file.is_regular_file() || file.path().extension() != ".hlsl")
			continue;
		++inventoryCount;
		const auto relative = file.path().lexically_relative(packageShaders);
		if (IsTestShaderPath(relative)) {
			// Package-AIO removes Shaders/Tests from release archives.
			++excludedTestCount;
			continue;
		}
		const auto variants = RuntimeVariants(relative);
		if (variants.empty()) {
			unmapped.push_back(relative.string());
			continue;
		}
		for (const auto& variant : variants) {
			CAPTURE(relative, variant.entry, variant.profile);
			winrt::com_ptr<ID3DBlob> errors;
			HRESULT result = E_FAIL;
			auto bytecode = CompileRuntimeShader(relative, variant, errors, result);
			if (FAILED(result) || !bytecode) {
				INFO((errors ? static_cast<const char*>(errors->GetBufferPointer()) : "No compiler diagnostics"));
				CHECK(SUCCEEDED(result));
				CHECK(bytecode);
			} else {
				++compiledVariantCount;
			}
		}
	}
	INFO("package/Shaders HLSL inventory=" << inventoryCount << ", release-shipped=" <<
		(inventoryCount - excludedTestCount) << ", test-only excluded=" << excludedTestCount <<
		", compiled entry/profile variants=" << compiledVariantCount);
	if (!unmapped.empty()) {
		std::string names;
		for (const auto& path : unmapped)
			names += path + "; ";
		INFO("HLSL files without an entry/profile mapping: " << names);
	}
	CHECK(unmapped.empty());
}

TEST_CASE("Every AIO feature shader is staged and runtime entrypoints use CustomInclude", "[shader-compile][feature-aio]")
{
	ShaderFixture fixture;
	const auto featuresRoot = std::filesystem::path(OPENNR_SHADER_SOURCE_DIR) / "features";
	std::size_t featureHlslCount = 0;
	std::size_t compiledVariantCount = 0;
	std::size_t includeOnlyCount = 0;
	std::vector<std::string> unmapped;
	for (const auto& featureRoot : std::filesystem::directory_iterator(featuresRoot)) {
		if (!featureRoot.is_directory() || !AioFeatureIncluded(featureRoot.path()))
			continue;
		const auto shaderSource = featureRoot.path() / "Shaders";
		if (!std::filesystem::exists(shaderSource))
			continue;
		for (const auto& file : std::filesystem::recursive_directory_iterator(shaderSource)) {
			if (!file.is_regular_file() || file.path().extension() != ".hlsl")
				continue;
			const auto relative = file.path().lexically_relative(shaderSource);
			if (IsTestShaderPath(relative))
				continue;
			++featureHlslCount;
			const auto generic = relative.generic_string();
			if (generic.find("/Include/") != std::string::npos) {
				const auto includePath = generic.c_str();
				Util::CustomInclude include(L"Data\\Shaders\\PostProcessing\\ColorGrading\\colorgrading.cs.hlsl");
				LPCVOID data = nullptr;
				UINT size = 0;
				const auto hr = include.Open(D3D_INCLUDE_LOCAL, includePath, nullptr, &data, &size);
				CAPTURE(generic);
				CHECK(SUCCEEDED(hr));
				CHECK(data != nullptr);
				CHECK(size > 0);
				if (data)
					include.Close(data);
				++includeOnlyCount;
				continue;
			}
			// These four names do not encode a shader stage in the filename; their
			// mappings mirror the corresponding runtime compile callsites.
			std::vector<RuntimeShaderVariant> variants;
			if (generic == "LightLimitFix/ShadowDepthCopy.hlsl")
				variants = { { "main", "vs_5_0" }, { "main", "ps_5_0" } };
			else if (generic == "TerrainBlending/DepthBlend.hlsl" ||
				generic == "GrassOptimizations/SPD/SPD.hlsl" || generic == "Upscaling/RCAS/RCAS.hlsl")
				variants = { { "main", "cs_5_0" } };
			else if (generic.find("Upscaling/NeuralRendering/") != std::string::npos)
				continue; // The NR test above enumerates every NR runtime entry explicitly.
			else
				variants = FeatureRuntimeVariants(relative);
			if (variants.empty()) {
				unmapped.push_back(generic);
				continue;
			}
			for (const auto& variant : variants) {
				CAPTURE(generic, variant.entry, variant.profile);
				winrt::com_ptr<ID3DBlob> errors;
				HRESULT result = E_FAIL;
				auto bytecode = CompileRuntimeShader(relative, variant, errors, result);
				if (FAILED(result) || !bytecode) {
					INFO((errors ? static_cast<const char*>(errors->GetBufferPointer()) : "No compiler diagnostics"));
					CHECK(SUCCEEDED(result));
					CHECK(bytecode);
				} else {
					++compiledVariantCount;
				}
			}
		}
	}
#ifdef ENABLE_EFFECTS11
	CHECK(featureHlslCount == 127);
#else
	CHECK(featureHlslCount == 119);
#endif
	CHECK(includeOnlyCount == 31);
	INFO("AIO feature HLSL=" << featureHlslCount << ", compiled runtime variants=" << compiledVariantCount
		<< ", include-only HLSL resolved by CustomInclude=" << includeOnlyCount);
	if (!unmapped.empty()) {
		std::string paths;
		for (const auto& path : unmapped)
			paths += path + "; ";
		INFO("Feature HLSL without a proven runtime entry/profile mapping: " << paths);
	}
	CHECK(unmapped.empty());
}
TEST_CASE("Failed neural rendering shader compilation latches after one frame and retries after reset", "[neural-rendering][shader-compile][failure-latch]")
{
	ShaderFixture fixture;
	const auto brokenShader = fixture.root / "Data/Shaders/Upscaling/NeuralRendering/BrokenFixture.hlsl";
	std::ofstream(brokenShader) << "[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) { this is invalid HLSL; }\n";

	NeuralRendering::ShaderInitLatch latch;
	std::uint32_t compileAttempts = 0;
	std::uint32_t loggedFailures = 0;
	bool compileFailureSeen = false;
	for (std::uint32_t frame = 0; frame < 120; ++frame) {
		const bool enabled = latch.Ensure([&] {
			++compileAttempts;
			winrt::com_ptr<ID3DBlob> bytecode;
			winrt::com_ptr<ID3DBlob> errors;
			const std::vector<D3D_SHADER_MACRO> defines = {
				{ "VR", "" }, { "COMPUTESHADER", "" }, { "WINPC", "" }, { "DX11", "" },
				{ nullptr, nullptr },
			};
			Util::CustomInclude include(brokenShader);
			const auto result = D3DCompileFromFile(brokenShader.c_str(), defines.data(), &include, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
				bytecode.put(), errors.put());
			compileFailureSeen = FAILED(result) && errors.get() != nullptr;
			return SUCCEEDED(result);
		}, [&] { ++loggedFailures; });
		CHECK_FALSE(enabled);
	}
	CHECK(compileFailureSeen);
	CHECK(compileAttempts == 1);
	CHECK(loggedFailures == 1);

	latch.Reset();
	CHECK_FALSE(latch.Ensure([] { return false; }, [] {}));
	CHECK(latch.Failed());
}

TEST_CASE("Ready neural rendering shader initialization is reused across runtime setting changes", "[neural-rendering][shader-compile][failure-latch]")
{
	NeuralRendering::ShaderInitLatch latch;
	std::uint32_t initializationAttempts = 0;
	for (const auto gain : { 0.83f, 0.89f, 0.93f, 0.83f }) {
		const bool ready = latch.Ensure([&] {
			++initializationAttempts;
			return true;
		}, [] {});
		INFO("Current runtime gain: " << gain);
		CHECK(ready);
	}
	CHECK(initializationAttempts == 1);
	CHECK_FALSE(latch.Failed());
}
