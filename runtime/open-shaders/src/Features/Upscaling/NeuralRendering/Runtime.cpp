#include "Runtime.h"

#include "Globals.h"
#include "Util.h"

#include <Windows.h>
#include <Psapi.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <nvsdk_ngx.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <optional>
#include <set>
#include <thread>
#include <cstring>
#include <format>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <nvsdk_ngx_helpers.h>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

namespace NeuralRendering
{
	namespace
	{
		constexpr wchar_t kRuntimeName[] = L"nvngx_dlssnr.dll";
		constexpr auto kFeatureDlssNr = static_cast<NVSDK_NGX_Feature>(18);
		constexpr std::array<const char*, 5> kRequiredExports{
			"NVSDK_NGX_D3D12_Init_Ext",
			"NVSDK_NGX_D3D12_CreateFeature",
			"NVSDK_NGX_D3D12_EvaluateFeature",
			"NVSDK_NGX_D3D12_ReleaseFeature",
			"NVSDK_NGX_D3D12_Shutdown1",
		};

		using GetUnsignedValue = unsigned int(NVSDK_CONV*)();
		using InitD3D12 = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version, const NVSDK_NGX_Parameter*);
		using ShutdownD3D12 = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);
		using AllocateParameters = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter**);
		using DestroyParameters = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter*);
		using CreateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
		using EvaluateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*, PFN_NVSDK_NGX_ProgressCallback);
		using ReleaseFeature = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
		using GetModuleFileNameWFunction = DWORD(WINAPI*)(HMODULE, LPWSTR, DWORD);

		struct RuntimePathProxyState
		{
			GetModuleFileNameWFunction originalGetModuleFileNameW = nullptr;
			HMODULE callerModule = nullptr;
			std::wstring spoofedRuntimePath;
			std::uint32_t proxyHits = 0;
			std::uint32_t activeRuntimePathScopes = 0;
			bool installed = false;
			std::mutex mutex;
			std::condition_variable scopesDrained;
			void** signedRuntimeIatSlot = nullptr;
			HMODULE patchedRuntimeModule = nullptr;
		};

		RuntimePathProxyState& RuntimePathProxy()
		{
			// The carrier IAT can retain this thunk through process teardown. Keep its
			// synchronization and fallback state alive beyond static destruction.
			static RuntimePathProxyState* state = new RuntimePathProxyState();
			return *state;
		}

		struct RuntimeExports
		{
			HMODULE runtime = nullptr;
			HMODULE core = nullptr;
			GetUnsignedValue getApplicationId = nullptr;
			GetUnsignedValue getApiVersion = nullptr;
			InitD3D12 initialize = nullptr;
			ShutdownD3D12 shutdown = nullptr;
			AllocateParameters allocateParameters = nullptr;
			DestroyParameters destroyParameters = nullptr;
			CreateFeature createFeature = nullptr;
			EvaluateFeature evaluateFeature = nullptr;
			ReleaseFeature releaseFeature = nullptr;
		};

		RuntimeExports g_runtimeExports{};

		HMODULE FindNgxCoreModule()
		{
			std::array<HMODULE, 1024> modules{};
			DWORD bytesNeeded = 0;
			if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(), static_cast<DWORD>(sizeof(modules)), &bytesNeeded))
				return nullptr;
			const std::size_t count = std::min<std::size_t>(modules.size(), bytesNeeded / sizeof(HMODULE));
			for (std::size_t index = 0; index < count; ++index) {
				HMODULE pinned = nullptr;
				if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
					reinterpret_cast<LPCWSTR>(modules[index]), &pinned))
					continue;
				if (GetProcAddress(pinned, "NVSDK_NGX_D3D12_AllocateParameters") &&
					GetProcAddress(pinned, "NVSDK_NGX_D3D12_DestroyParameters"))
					return pinned;
				FreeLibrary(pinned);
			}
			return nullptr;
		}

		DWORD WINAPI SignedRuntimeGetModuleFileNameW(HMODULE module, LPWSTR filename, DWORD size)
		{
			auto& proxy = RuntimePathProxy();
			GetModuleFileNameWFunction original = nullptr;
			{
				std::scoped_lock lock(proxy.mutex);
				original = proxy.originalGetModuleFileNameW;
				if (proxy.installed && proxy.activeRuntimePathScopes != 0 && module == proxy.callerModule &&
					filename && size && !proxy.spoofedRuntimePath.empty()) {
					++proxy.proxyHits;
					const DWORD length = static_cast<DWORD>(proxy.spoofedRuntimePath.size());
					const DWORD copyLength = std::min(length, size - 1);
					std::memcpy(filename, proxy.spoofedRuntimePath.data(), copyLength * sizeof(wchar_t));
					filename[copyLength] = L'\0';
					return copyLength < length ? size : length;
				}
			}
			return original ? original(module, filename, size) : 0;
		}

		bool InstallSignedRuntimePathProxy(HMODULE runtime, const std::wstring& signedRuntimePath)
		{
			auto& proxy = RuntimePathProxy();
			if (!runtime || signedRuntimePath.empty())
				return false;
			std::scoped_lock lock(proxy.mutex);
			HMODULE caller = nullptr;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&SignedRuntimeGetModuleFileNameW), &caller))
				return false;
			if (proxy.installed && proxy.patchedRuntimeModule == runtime && proxy.callerModule == caller &&
				proxy.signedRuntimeIatSlot && *proxy.signedRuntimeIatSlot == reinterpret_cast<void*>(&SignedRuntimeGetModuleFileNameW)) {
				proxy.spoofedRuntimePath = signedRuntimePath;
				return true;
			}

			auto* base = reinterpret_cast<std::byte*>(runtime);
			auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
			if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
				return false;
			auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE)
				return false;
			const auto& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (!imports.VirtualAddress)
				return false;

			auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress);
			for (; descriptor->Name; ++descriptor) {
				if (!descriptor->OriginalFirstThunk)
					continue;
				auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
				auto* functions = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
				for (; names->u1.AddressOfData; ++names, ++functions) {
					if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
						continue;
					auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
					if (std::strcmp(reinterpret_cast<const char*>(import->Name), "GetModuleFileNameW"))
						continue;
					void** slot = reinterpret_cast<void**>(&functions->u1.Function);
					auto original = reinterpret_cast<GetModuleFileNameWFunction>(*slot);
					if (original == &SignedRuntimeGetModuleFileNameW)
						original = proxy.originalGetModuleFileNameW;
					if (!original)
						return false;
					if (*slot != reinterpret_cast<void*>(&SignedRuntimeGetModuleFileNameW)) {
						DWORD oldProtection = 0;
						if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtection))
							return false;
						*slot = reinterpret_cast<void*>(&SignedRuntimeGetModuleFileNameW);
						DWORD ignored = 0;
						VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored);
						FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
					}
					proxy.originalGetModuleFileNameW = original;
					proxy.callerModule = caller;
					proxy.signedRuntimeIatSlot = slot;
					proxy.patchedRuntimeModule = runtime;
					proxy.spoofedRuntimePath = signedRuntimePath;
					proxy.installed = true;
					return true;
				}
			}
			return false;
		}

		void RemoveSignedRuntimePathProxy(HMODULE runtime)
		{
			auto& proxy = RuntimePathProxy();
			std::unique_lock lock(proxy.mutex);
			if (!runtime || runtime != proxy.patchedRuntimeModule || !proxy.signedRuntimeIatSlot)
				return;
			proxy.installed = false;
			void** slot = proxy.signedRuntimeIatSlot;
			const auto original = proxy.originalGetModuleFileNameW;
			if (*slot == reinterpret_cast<void*>(&SignedRuntimeGetModuleFileNameW) && original) {
				DWORD oldProtection = 0;
				if (VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtection)) {
					*slot = reinterpret_cast<void*>(original);
					DWORD ignored = 0;
					VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored);
					FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
				}
			}
			proxy.scopesDrained.wait(lock, [&proxy] { return proxy.activeRuntimePathScopes == 0; });
			proxy.spoofedRuntimePath.clear();
			proxy.signedRuntimeIatSlot = nullptr;
			proxy.patchedRuntimeModule = nullptr;
			proxy.callerModule = nullptr;
		}

		class SignedRuntimePathScope
		{
		public:
			explicit SignedRuntimePathScope(const std::wstring& ngxPath)
			{
				auto& proxy = RuntimePathProxy();
				std::scoped_lock lock(proxy.mutex);
				if (proxy.installed && ngxPath == proxy.spoofedRuntimePath) {
					++proxy.activeRuntimePathScopes;
					startHits_ = proxy.proxyHits;
					installed_ = true;
				}
			}

			~SignedRuntimePathScope()
			{
				if (!installed_)
					return;
				auto& proxy = RuntimePathProxy();
				std::scoped_lock lock(proxy.mutex);
				if (--proxy.activeRuntimePathScopes == 0)
					proxy.scopesDrained.notify_all();
			}

			[[nodiscard]] bool IsInstalled() const { return installed_; }
			[[nodiscard]] std::uint32_t Hits() const
			{
				auto& proxy = RuntimePathProxy();
				std::scoped_lock lock(proxy.mutex);
				return proxy.proxyHits - startHits_;
			}

		private:
			std::uint32_t startHits_ = 0;
			bool installed_ = false;
		};

		using RuntimeSelection::SignatureStatus;

		struct RuntimeFile
		{
			std::filesystem::path path;
			std::string file;  // UTF-8 file name (no directory)
			std::uintmax_t size = 0;
			std::int64_t modified = 0;

			static std::string Utf8(const std::filesystem::path& value)
			{
				const auto text = value.u8string();
				return std::string(reinterpret_cast<const char*>(text.data()), text.size());
			}

			[[nodiscard]] std::string Key() const
			{
				return RuntimeSelection::ToLowerAscii(Utf8(path)) + "|" + std::to_string(size) + "|" + std::to_string(modified);
			}
		};

		std::optional<RuntimeFile> DescribeRuntimeFile(const std::filesystem::path& path)
		{
			std::error_code error;
			if (!std::filesystem::is_regular_file(path, error))
				return std::nullopt;
			RuntimeFile file;
			file.path = path;
			file.file = RuntimeFile::Utf8(path.filename());
			file.size = std::filesystem::file_size(path, error);
			if (error)
				file.size = 0;
			error.clear();
			file.modified = static_cast<std::int64_t>(std::filesystem::last_write_time(path, error).time_since_epoch().count());
			return file;
		}

		/** @brief nvngx_dlssnr*.dll directly inside Streamline/ then StreamlineDX12/; the first directory wins per file name. */
		std::vector<RuntimeFile> EnumerateRuntimeFiles()
		{
			std::vector<RuntimeFile> result;
			std::set<std::string> seen;
			const auto dataPath = Util::PathHelpers::GetDataPath();
			for (const auto* directoryName : { L"Shaders/Upscaling/Streamline", L"Shaders/Upscaling/StreamlineDX12" }) {
				std::vector<RuntimeFile> local;
				std::error_code error;
				for (std::filesystem::directory_iterator it(dataPath / directoryName, error), end; !error && it != end; it.increment(error)) {
					std::error_code entryError;
					if (!it->is_regular_file(entryError))
						continue;
					const auto name = RuntimeFile::Utf8(it->path().filename());
					if (!RuntimeSelection::IsRuntimeFileName(name) || seen.contains(RuntimeSelection::ToLowerAscii(name)))
						continue;
					if (auto file = DescribeRuntimeFile(it->path()))
						local.push_back(std::move(*file));
				}
				std::sort(local.begin(), local.end(), [](const RuntimeFile& a, const RuntimeFile& b) {
					return RuntimeSelection::ToLowerAscii(a.file) < RuntimeSelection::ToLowerAscii(b.file);
				});
				for (auto& file : local) {
					seen.insert(RuntimeSelection::ToLowerAscii(file.file));
					result.push_back(std::move(file));
				}
			}
			return result;
		}

		std::optional<RuntimeSelection::Version> ReadVersionString(const std::wstring& path)
		{
			DWORD handle = 0;
			const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
			if (size == 0)
				return std::nullopt;
			std::vector<BYTE> buffer(size);
			if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data()))
				return std::nullopt;
			struct Translation
			{
				WORD language;
				WORD codePage;
			};
			Translation* translations = nullptr;
			UINT translationBytes = 0;
			std::vector<Translation> tries;
			if (VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &translationBytes) &&
				translations)
				for (UINT i = 0; i < translationBytes / sizeof(Translation); ++i)
					tries.push_back(translations[i]);
			tries.push_back({ 0x0409, 0x04B0 });
			tries.push_back({ 0x0409, 0x04E4 });
			for (const auto& t : tries) {
				const auto query = std::format(L"\\StringFileInfo\\{:04x}{:04x}\\FileVersion", t.language, t.codePage);
				wchar_t* value = nullptr;
				UINT length = 0;
				if (VerQueryValueW(buffer.data(), query.c_str(), reinterpret_cast<void**>(&value), &length) && value && length) {
					std::string narrow;
					for (const wchar_t* c = value; *c && narrow.size() < 64; ++c)
						narrow.push_back(*c < 128 ? static_cast<char>(*c) : '?');
					if (auto parsed = RuntimeSelection::ParseVersion(narrow))
						return parsed;
				}
			}
			return std::nullopt;
		}

		std::optional<RuntimeSelection::Version> ReadRuntimeVersion(const std::filesystem::path& path)
		{
			if (const auto version = Util::GetDllVersion(path.wstring()))
				return RuntimeSelection::Version{ version->major(), version->minor(), version->patch(), version->build() };
			return ReadVersionString(path.wstring());
		}

		SignatureStatus VerifyRuntimeSignature(const std::filesystem::path& path)
		{
			WINTRUST_FILE_INFO fileInfo{};
			fileInfo.cbStruct = sizeof(fileInfo);
			fileInfo.pcwszFilePath = path.c_str();
			WINTRUST_DATA data{};
			data.cbStruct = sizeof(data);
			data.dwUIChoice = WTD_UI_NONE;
			data.fdwRevocationChecks = WTD_REVOKE_NONE;  // never touch the network
			data.dwUnionChoice = WTD_CHOICE_FILE;
			data.pFile = &fileInfo;
			data.dwStateAction = WTD_STATEACTION_VERIFY;
			data.dwProvFlags = WTD_REVOCATION_CHECK_NONE | WTD_CACHE_ONLY_URL_RETRIEVAL;
			GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
			const auto window = static_cast<HWND>(INVALID_HANDLE_VALUE);
			const LONG result = WinVerifyTrust(window, &action, &data);
			SignatureStatus status = SignatureStatus::Unknown;
			if (result == ERROR_SUCCESS) {
				status = SignatureStatus::SignedOther;
				if (auto* provider = WTHelperProvDataFromStateData(data.hWVTStateData)) {
					if (auto* signer = WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0)) {
						auto* certificate = WTHelperGetProvCertFromChain(signer, 0);
						if (certificate && certificate->pCert) {
							wchar_t subject[256]{};
							CertGetNameStringW(certificate->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, subject, 256);
							if (std::wstring_view(subject) == L"NVIDIA Corporation")
								status = SignatureStatus::SignedNvidia;
						}
					}
				}
			} else if (result == static_cast<LONG>(TRUST_E_NOSIGNATURE) || result == static_cast<LONG>(TRUST_E_PROVIDER_UNKNOWN) ||
					   result == static_cast<LONG>(TRUST_E_SUBJECT_FORM_UNKNOWN)) {
				status = SignatureStatus::Unsigned;
			} else if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND || result == ERROR_ACCESS_DENIED ||
					   result == ERROR_SHARING_VIOLATION) {
				status = SignatureStatus::Unknown;  // could not even read the file: not a verdict on the signature
			} else {
				status = SignatureStatus::Invalid;
			}
			data.dwStateAction = WTD_STATEACTION_CLOSE;
			WinVerifyTrust(window, &action, &data);
			return status;
		}

		/** @brief Authenticode results cached per path+size+mtime; computed on worker threads, never inline on the render thread. */
		struct SignatureCache
		{
			std::mutex mutex;
			std::map<std::string, std::shared_future<SignatureStatus>> futures;
		};

		SignatureCache& SignatureCacheInstance()
		{
			static auto* cache = new SignatureCache();  // leaked on purpose: workers may outlive static destruction
			return *cache;
		}

		std::shared_future<SignatureStatus> SignatureFuture(const RuntimeFile& file)
		{
			auto& cache = SignatureCacheInstance();
			std::scoped_lock lock(cache.mutex);
			const auto key = file.Key();
			const auto found = cache.futures.find(key);
			if (found != cache.futures.end()) {
				const bool ready = found->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
				if (!ready || found->second.get() != SignatureStatus::Unknown)
					return found->second;  // in flight or conclusive; a failed (unknown) check is retried
			}
			auto future = std::async(std::launch::async, [path = file.path] { return VerifyRuntimeSignature(path); }).share();
			cache.futures[key] = future;
			return future;
		}

		std::optional<SignatureStatus> PeekSignature(const RuntimeFile& file)
		{
			auto& cache = SignatureCacheInstance();
			std::scoped_lock lock(cache.mutex);
			const auto found = cache.futures.find(file.Key());
			if (found == cache.futures.end() || found->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
				return std::nullopt;
			return found->second.get();
		}

		struct Preference
		{
			std::string file;  // empty = Auto
			bool requireSigned = true;
		};

		struct ActiveRuntime
		{
			bool valid = false;
			RuntimeFile file;
			std::string version;
			std::string reason;
		};

		std::mutex g_selectionMutex;
		Preference g_preference;
		std::optional<Preference> g_latchedPreference;  // frozen at the first probe: the runtime library is pinned per process
		ActiveRuntime g_active;

		Preference CurrentPreference()
		{
			std::scoped_lock lock(g_selectionMutex);
			return g_preference;
		}

		struct DiscoveredRuntimes
		{
			std::vector<RuntimeFile> files;
			std::vector<RuntimeSelection::Candidate> candidates;
		};

		DiscoveredRuntimes DiscoverRuntimes(std::vector<RuntimeFile> files)
		{
			DiscoveredRuntimes result;
			for (auto& file : files) {
				RuntimeSelection::Candidate candidate;
				candidate.file = file.file;
				candidate.version = ReadRuntimeVersion(file.path);
				result.candidates.push_back(std::move(candidate));
			}
			result.files = std::move(files);
			return result;
		}

		std::string VersionText(const RuntimeSelection::Candidate& candidate)
		{
			return candidate.version ? RuntimeSelection::FormatVersion(*candidate.version) : std::string();
		}

		std::string CandidatesLogLine(const DiscoveredRuntimes& found, const RuntimeSelection::Selection& selection,
			const Preference& preference)
		{
			std::string line = "[DLSSNR] runtime candidates:";
			for (std::size_t i = 0; i < found.files.size(); ++i) {
				auto candidate = found.candidates[i];
				const auto peeked = PeekSignature(found.files[i]);
				const bool known = peeked.has_value();
				if (known)
					candidate.signature = *peeked;
				const auto verdict = selection.verdicts[i] ? *selection.verdicts[i] :
				                                             RuntimeSelection::Evaluate(candidate, preference.requireSigned && known);
				line += std::format(" [{} {} {} {}]", candidate.file, candidate.version ? VersionText(candidate) : "unknown",
					known ? RuntimeSelection::SignatureText(candidate.signature) : "unchecked",
					verdict == RuntimeSelection::Verdict::Accepted ? "yes" : "no");
			}
			if (found.files.empty())
				line += " none";
			line += std::format("; selected={} reason={}",
				selection.index >= 0 ? found.candidates[static_cast<std::size_t>(selection.index)].file : std::string("none"),
				RuntimeSelection::ReasonText(selection.reason));
			return line;
		}
	}

	std::string RuntimeGpuName()
	{
		static std::mutex mutex;
		static std::string cached;
		std::scoped_lock lock(mutex);
		if (!cached.empty() || !globals::d3d::device)
			return cached;
		IDXGIDevice* dxgiDevice = nullptr;
		if (SUCCEEDED(globals::d3d::device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) && dxgiDevice) {
			IDXGIAdapter* adapter = nullptr;
			if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter) {
				DXGI_ADAPTER_DESC desc{};
				if (SUCCEEDED(adapter->GetDesc(&desc))) {
					for (const wchar_t* c = desc.Description; *c && cached.size() < 128; ++c)
						cached.push_back(*c < 128 ? static_cast<char>(*c) : '?');
				}
				adapter->Release();
			}
			dxgiDevice->Release();
		}
		return cached;
	}

	void SetRuntimeSelectionPreference(const std::string& file, bool requireSigned)
	{
		std::scoped_lock lock(g_selectionMutex);
		g_preference = { file, requireSigned };
	}

	void WarmUpRuntimeSelection()
	{
		static std::atomic<bool> started{ false };
		if (started.exchange(true))
			return;
		const auto preference = CurrentPreference();
		try {
			std::thread([preference] {
				try {
					auto found = DiscoverRuntimes(EnumerateRuntimeFiles());
					const auto resolver = [&](std::size_t i) { return SignatureFuture(found.files[i]).get(); };
					const auto selection = RuntimeSelection::Select(found.candidates, preference.file, preference.requireSigned, resolver);
					if (selection.index >= 0)
						resolver(static_cast<std::size_t>(selection.index));
				} catch (...) {
				}
			}).detach();
		} catch (...) {
		}
	}

	void RequestRuntimeSignatureChecks()
	{
		try {
			for (const auto& file : EnumerateRuntimeFiles())
				SignatureFuture(file);
		} catch (...) {
		}
	}

	RuntimeSelectionView DescribeRuntimeCandidates(bool requireSigned)
	{
		static std::mutex mutex;
		static std::vector<RuntimeFile> files;
		static std::vector<std::string> versions;
		static std::chrono::steady_clock::time_point stamp{};
		std::scoped_lock lock(mutex);
		const auto now = std::chrono::steady_clock::now();
		if (stamp == std::chrono::steady_clock::time_point{} || now - stamp > std::chrono::seconds(2)) {
			static std::map<std::string, std::string> versionCache;  // keyed by path+size+mtime
			files = EnumerateRuntimeFiles();
			versions.clear();
			for (const auto& file : files) {
				auto cached = versionCache.find(file.Key());
				if (cached == versionCache.end()) {
					const auto version = ReadRuntimeVersion(file.path);
					cached = versionCache.emplace(file.Key(), version ? RuntimeSelection::FormatVersion(*version) : std::string()).first;
				}
				versions.push_back(cached->second);
			}
			stamp = now;
		}
		const auto describe = [&](const RuntimeFile& file, const std::string& versionText) {
			RuntimeCandidateView view;
			view.file = file.file;
			view.version = versionText;
			RuntimeSelection::Candidate candidate;
			candidate.file = file.file;
			candidate.version = RuntimeSelection::ParseVersion(versionText);
			if (const auto peeked = PeekSignature(file)) {
				view.signatureReady = true;
				view.signature = *peeked;
				candidate.signature = *peeked;
			}
			view.verdict = RuntimeSelection::Evaluate(candidate, requireSigned && view.signatureReady);
			return view;
		};
		RuntimeSelectionView result;
		for (std::size_t i = 0; i < files.size(); ++i)
			result.candidates.push_back(describe(files[i], versions[i]));
		std::scoped_lock selectionLock(g_selectionMutex);
		if (g_active.valid) {
			result.hasActive = true;
			result.active = describe(g_active.file, g_active.version);
			result.activeReason = g_active.reason;
		}
		if (g_latchedPreference) {
			result.hasLatched = true;
			result.latchedFile = g_latchedPreference->file;
			result.latchedRequireSigned = g_latchedPreference->requireSigned;
		}
		return result;
	}

	Runtime& Runtime::Instance()
	{
		// Keep NGX teardown under the renderer's explicit, fence-aware shutdown path.
		// Static destruction can run before renderer drain/quarantine during process exit.
		static Runtime* instance = new Runtime();
		return *instance;
	}

	Runtime::~Runtime() { Shutdown(); }

	bool Runtime::Probe(const std::filesystem::path& explicitPath)
	{
		Shutdown();
		Preference preference;
		{
			std::scoped_lock lock(g_selectionMutex);
			// The runtime library is pinned per process: the preference seen by the first probe stays in force.
			if (!g_latchedPreference)
				g_latchedPreference = g_preference;
			preference = *g_latchedPreference;
		}
		std::vector<RuntimeFile> runtimeFiles;
		if (!explicitPath.empty()) {
			const auto resolved = std::filesystem::is_directory(explicitPath) ? explicitPath / kRuntimeName : explicitPath;
			if (auto file = DescribeRuntimeFile(resolved))
				runtimeFiles.push_back(std::move(*file));
			preference.file.clear();
		} else {
			runtimeFiles = EnumerateRuntimeFiles();
		}
		const auto found = DiscoverRuntimes(std::move(runtimeFiles));
		// Signatures are resolved lazily and only where the policy needs them; the warm-up started at
		// plugin load normally has the answer cached already.
		const auto resolver = [&found](std::size_t index) { return SignatureFuture(found.files[index]).get(); };
		const auto selection = RuntimeSelection::Select(found.candidates, preference.file, preference.requireSigned, resolver);
		if (selection.index >= 0 && !preference.requireSigned)
			SignatureFuture(found.files[static_cast<std::size_t>(selection.index)]);  // informational, not awaited
		{
			static std::atomic<bool> loggedCandidates{ false };
			static std::mutex refusedMutex;
			static std::set<std::string> refusedLogged;
			if (!loggedCandidates.exchange(true)) {
				logger::info("{}", CandidatesLogLine(found, selection, preference));
				if (selection.reason == RuntimeSelection::Reason::Fallback) {
					logger::info("[DLSSNR] runtime selection: configured file '{}' {}; using Auto", preference.file,
						selection.userFileMissing ? "was not found" :
						selection.userFileVerdict ? RuntimeSelection::VerdictText(*selection.userFileVerdict) : "was not accepted");
				}
			}
			std::scoped_lock lock(refusedMutex);
			for (const auto index : selection.signatureRefused) {
				const auto& name = found.candidates[static_cast<std::size_t>(index)].file;
				if (refusedLogged.insert(name).second)
					logger::warn("[DLSSNR] runtime refused: {} unsigned/invalid", name);
			}
		}
		if (selection.index < 0) {
			if (found.files.empty()) {
				status_ = RuntimeStatus::NotFound;
				detail_ = std::format("nvngx_dlssnr.dll was not found ({})", RuntimeSelection::DescribeNoneSelectable(found.candidates, selection));
			} else if (!selection.signatureRefused.empty()) {
				status_ = RuntimeStatus::SignatureRefused;
				detail_ = RuntimeSelection::DescribeNoneSelectable(found.candidates, selection);
			} else {
				bool unsupported = false;
				for (const auto& verdict : selection.verdicts)
					unsupported = unsupported || (verdict && *verdict == RuntimeSelection::Verdict::UnsupportedVersion);
				status_ = unsupported ? RuntimeStatus::UnsupportedVersion : RuntimeStatus::VersionUnavailable;
				detail_ = unsupported ? RuntimeSelection::DescribeNoneSelectable(found.candidates, selection) :
				                        std::string("DLL version resource is unavailable");
			}
			return false;
		}
		const auto selectedIndex = static_cast<std::size_t>(selection.index);
		path_ = found.files[selectedIndex].path;
		version_ = RuntimeSelection::FormatVersion(*found.candidates[selectedIndex].version);
		module_ = LoadLibraryExW(path_.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (!module_) {
			status_ = RuntimeStatus::LoadFailed;
			detail_ = std::format("LoadLibraryExW failed with {}", GetLastError());
			return false;
		}
		const auto runtime = static_cast<HMODULE>(module_);
		for (const char* exportName : kRequiredExports) {
			if (!GetProcAddress(runtime, exportName)) {
				status_ = RuntimeStatus::MissingExport;
				detail_ = std::format("missing export {}", exportName);
				FreeLibrary(runtime);
				module_ = nullptr;
				return false;
			}
		}
		RuntimeExports exports{};
		exports.runtime = runtime;
		exports.getApplicationId = reinterpret_cast<GetUnsignedValue>(GetProcAddress(runtime, "NVSDK_NGX_GetApplicationId"));
		exports.getApiVersion = reinterpret_cast<GetUnsignedValue>(GetProcAddress(runtime, "NVSDK_NGX_GetAPIVersion"));
		exports.initialize = reinterpret_cast<InitD3D12>(GetProcAddress(runtime, "NVSDK_NGX_D3D12_Init_Ext"));
		exports.shutdown = reinterpret_cast<ShutdownD3D12>(GetProcAddress(runtime, "NVSDK_NGX_D3D12_Shutdown1"));
		exports.createFeature = reinterpret_cast<CreateFeature>(GetProcAddress(runtime, "NVSDK_NGX_D3D12_CreateFeature"));
		exports.evaluateFeature = reinterpret_cast<EvaluateFeature>(GetProcAddress(runtime, "NVSDK_NGX_D3D12_EvaluateFeature"));
		exports.releaseFeature = reinterpret_cast<ReleaseFeature>(GetProcAddress(runtime, "NVSDK_NGX_D3D12_ReleaseFeature"));
		if (!exports.getApplicationId || !exports.getApiVersion || !exports.initialize || !exports.shutdown ||
			!exports.createFeature || !exports.evaluateFeature || !exports.releaseFeature) {
			status_ = RuntimeStatus::MissingExport;
			detail_ = "signed runtime required exports are missing";
			FreeLibrary(runtime);
			module_ = nullptr;
			return false;
		}
		g_runtimeExports = exports;
		signedRuntimePath_ = (path_.parent_path() / L"nvngx.dll").wstring();
		applicationId_ = g_runtimeExports.getApplicationId();
		apiVersion_ = g_runtimeExports.getApiVersion();
		{
			std::scoped_lock lock(g_selectionMutex);
			g_active.valid = true;
			g_active.file = found.files[selectedIndex];
			g_active.version = version_;
			g_active.reason = RuntimeSelection::ReasonText(selection.reason);
		}
		status_ = RuntimeStatus::Ready;
		return true;
	}

	bool Runtime::Initialize(ID3D12Device* device, const std::filesystem::path& dataPath)
	{
		if (!device)
			return false;
		if (status_ == RuntimeStatus::Initialized && device_ != device)
			Shutdown();
		if (!module_ && !Probe())
			return false;
		if (status_ == RuntimeStatus::Initialized && device_ == device)
			return true;

		auto initialize = g_runtimeExports.initialize;
		std::filesystem::path writablePath = dataPath;
		if (writablePath.empty()) {
			wchar_t tempPath[MAX_PATH]{};
			GetTempPathW(MAX_PATH, tempPath);
			writablePath = std::filesystem::path(tempPath) / L"OpenShaders-NGX";
		}
		std::error_code error;
		std::filesystem::create_directories(writablePath, error);
		if (!initialize || !InstallSignedRuntimePathProxy(static_cast<HMODULE>(module_), signedRuntimePath_)) {
			status_ = RuntimeStatus::InitializationFailed;
			detail_ = "failed to install signed-runtime path proxy";
			return false;
		}
		std::uint32_t proxyHits = 0;
		{
			SignedRuntimePathScope scope(signedRuntimePath_);
			if (!scope.IsInstalled()) {
				status_ = RuntimeStatus::InitializationFailed;
				detail_ = "failed to activate signed-runtime path proxy";
				return false;
			}
			ngxResult_ = static_cast<std::uint32_t>(initialize(applicationId_, writablePath.c_str(), device,
				static_cast<NVSDK_NGX_Version>(apiVersion_), nullptr));
			proxyHits = scope.Hits();
		}
		if (ngxResult_ != NVSDK_NGX_Result_Success) {
			status_ = RuntimeStatus::InitializationFailed;
			detail_ = std::format("NGX init failed 0x{:08X}, proxyHits={}", ngxResult_, proxyHits);
			return false;
		}
		device_ = device;
		device_->AddRef();

		HMODULE core = FindNgxCoreModule();
		if (!core) {
			status_ = RuntimeStatus::CoreUnavailable;
			detail_ = "NGX parameter API module was not found";
			Shutdown();
			status_ = RuntimeStatus::CoreUnavailable;
			detail_ = "NGX parameter API module was not found";
			return false;
		}
		g_runtimeExports.core = core;
		g_runtimeExports.allocateParameters = reinterpret_cast<AllocateParameters>(
			GetProcAddress(core, "NVSDK_NGX_D3D12_AllocateParameters"));
		g_runtimeExports.destroyParameters = reinterpret_cast<DestroyParameters>(
			GetProcAddress(core, "NVSDK_NGX_D3D12_DestroyParameters"));
		auto allocate = g_runtimeExports.allocateParameters;
		if (!allocate || !g_runtimeExports.destroyParameters) {
			Shutdown();
			status_ = RuntimeStatus::CoreUnavailable;
			detail_ = "NGX parameter API exports were not found";
			return false;
		}
		NVSDK_NGX_Parameter* parameters = nullptr;
		ngxResult_ = static_cast<std::uint32_t>(allocate(&parameters));
		if (ngxResult_ != NVSDK_NGX_Result_Success || !parameters) {
			const auto allocationResult = ngxResult_;
			Shutdown();
			status_ = RuntimeStatus::ParameterAllocationFailed;
			ngxResult_ = allocationResult;
			detail_ = std::format("NGX parameter allocation failed 0x{:08X}", allocationResult);
			return false;
		}
		parameters_ = parameters;
		status_ = RuntimeStatus::Initialized;
		return true;
	}

	bool Runtime::EnsureFeature(ID3D12GraphicsCommandList* commandList, std::uint32_t slot,
		const Feature18GuideContract& guide, const Tuning& tuning, bool* created)
	{
		if (created)
			*created = false;
		if (status_ != RuntimeStatus::Initialized || !commandList || slot >= kFeatureSlotCount ||
			!guide.IsValid() || !parameters_)
			return false;

		const auto inputWidth = guide.FeatureInputWidth();
		const auto inputHeight = guide.FeatureInputHeight();
		const auto outputWidth = guide.FeatureOutputWidth();
		const auto outputHeight = guide.FeatureOutputHeight();
		// A stable creation envelope may be larger than the current valid
		// evaluation subrect, but never smaller. Rejecting that contract here
		// keeps native Feature 18 from reading outside the current backing region.
		if (inputWidth < guide.colorWidth || inputHeight < guide.colorHeight ||
			outputWidth < guide.outputWidth || outputHeight < guide.outputHeight ||
			inputWidth == 0 || inputHeight == 0 || outputWidth == 0 || outputHeight == 0) {
			ngxResult_ = static_cast<std::uint32_t>(NVSDK_NGX_Result_FAIL_InvalidParameter);
			detail_ = std::format("Feature 18 creation envelope is smaller than evaluation subrect slot={}", slot);
			return false;
		}

		auto* parameters = static_cast<NVSDK_NGX_Parameter*>(parameters_);
		const bool dimensionsChanged = featureInputWidth_[slot] != inputWidth || featureInputHeight_[slot] != inputHeight ||
			featureOutputWidth_[slot] != outputWidth || featureOutputHeight_[slot] != outputHeight ||
			featureMotionVectorsLowResolution_[slot] != guide.motionVectorsLowResolution;
		if (featureHandles_[slot] && dimensionsChanged) {
			ngxResult_ = static_cast<std::uint32_t>(NVSDK_NGX_Result_FAIL_InvalidParameter);
			detail_ = std::format("Feature 18 dimensions changed without GPU-safe retirement slot={}", slot);
			return false;
		}
		if (featureHandles_[slot])
			return true;

		// Keep the signed-runtime proxy installed while the runtime module is loaded.
		// Each short scope enables path spoofing only during an NGX call.
		auto create = g_runtimeExports.createFeature;
		SignedRuntimePathScope scope(signedRuntimePath_);
		if (!create || !scope.IsInstalled()) {
			detail_ = "native Feature 18 create export or signed-runtime proxy is unavailable";
			return false;
		}

		parameters->Reset();
		const auto createFlags = guide.motionVectorsLowResolution ?
			static_cast<unsigned int>(NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) : 0u;
		parameters->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, createFlags);
		parameters->Set("Width", outputWidth);
		parameters->Set("Height", outputHeight);
		parameters->Set("OutWidth", outputWidth);
		parameters->Set("OutHeight", outputHeight);
		parameters->Set("DLSSNR.Width", outputWidth);
		parameters->Set("DLSSNR.Height", outputHeight);
		parameters->Set("DLSSNR.InputWidth", inputWidth);
		parameters->Set("DLSSNR.InputHeight", inputHeight);
		parameters->Set("DLSSNR.OutputWidth", outputWidth);
		parameters->Set("DLSSNR.OutputHeight", outputHeight);
		parameters->Set("DLSSNR.Output.Width", outputWidth);
		parameters->Set("DLSSNR.Output.Height", outputHeight);
		parameters->Set("DLSSNR.Scale", static_cast<float>(outputWidth) / inputWidth);
		parameters->Set("DLSSNR.Upscaling", 1u);
		parameters->Set("DLSSNR.ScalingRatio", static_cast<float>(outputWidth) / inputWidth);
		// 310.8 accepts only the legacy default hint (0). Runtime 310.9 adds two
		// models selected at Feature creation: hint 1 preserves the 310.8 model,
		// while hint 2 selects the newer, faster model. Keep Original as default.
		const auto runtimeVersion = RuntimeSelection::ParseVersion(version_);
		const auto renderPresetHint = runtimeVersion ? RuntimeSelection::RenderPresetHint(*runtimeVersion, tuning.modelPreset) : 0u;
		parameters->Set("DLSSNR.Hint.Render.Preset", renderPresetHint);
		NVSDK_NGX_Handle* handle = nullptr;
		const auto createStart = std::chrono::steady_clock::now();
		ngxResult_ = static_cast<std::uint32_t>(create(commandList, kFeatureDlssNr, parameters, &handle));
		const auto createMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - createStart).count();
		logger::info("[DLSSNR] Feature create slot={} createInput={}x{} createOutput={}x{} evalInput={}x{} evalOutput={}x{} ms={:.2f} result=0x{:08X}",
			slot, inputWidth, inputHeight, outputWidth, outputHeight,
			guide.colorWidth, guide.colorHeight, guide.outputWidth, guide.outputHeight, createMs, ngxResult_);
		if (ngxResult_ != NVSDK_NGX_Result_Success || !handle) {
			static std::atomic<bool> loggedCreateFailure{ false };
			if (!loggedCreateFailure.exchange(true)) {
				std::string file = "unknown", version = version_, signature = "unknown";
				{
					std::scoped_lock lock(g_selectionMutex);
					if (g_active.valid) {
						file = g_active.file.file;
						if (const auto peeked = PeekSignature(g_active.file))
							signature = RuntimeSelection::SignatureText(*peeked);
					}
				}
				logger::warn("[DLSSNR] feature creation failed with runtime {} {} {}; GPU={}", file, version, signature,
					RuntimeGpuName().empty() ? std::string("unknown") : RuntimeGpuName());
			}
			detail_ = std::format("Feature 18 create failed slot={} result=0x{:08X} proxyHits={}", slot, ngxResult_, scope.Hits());
			return false;
		}
		featureHandles_[slot] = handle;
		featureInputWidth_[slot] = inputWidth;
		featureInputHeight_[slot] = inputHeight;
		featureOutputWidth_[slot] = outputWidth;
		featureOutputHeight_[slot] = outputHeight;
		featureMotionVectorsLowResolution_[slot] = guide.motionVectorsLowResolution;
		if (created)
			*created = true;
		return true;
	}

	bool Runtime::Execute(ID3D12GraphicsCommandList* commandList, std::uint32_t slot,
		ID3D12Resource* color, ID3D12Resource* depth, ID3D12Resource* motionVectors, ID3D12Resource* output,
		const Feature18GuideContract& guide, const Tuning& tuning, bool reset)
	{
		if (status_ != RuntimeStatus::Initialized || !commandList || slot >= kFeatureSlotCount ||
			!color || !depth || !motionVectors || !output || !guide.IsValid())
			return false;

		const auto inputWidth = guide.colorWidth;
		const auto inputHeight = guide.colorHeight;
		auto* parameters = static_cast<NVSDK_NGX_Parameter*>(parameters_);
		auto evaluate = g_runtimeExports.evaluateFeature;
		if (!parameters || !evaluate)
			return false;
		bool created = false;
		if (!EnsureFeature(commandList, slot, guide, tuning, &created))
			return false;
		reset = reset || created;

		SignedRuntimePathScope scope(signedRuntimePath_);
		if (!scope.IsInstalled())
			return false;

		parameters->Reset();
		parameters->Set("DLSSNR.Color", color);
		parameters->Set("DLSSNR.Depth", depth);
		parameters->Set("DLSSNR.MVec", motionVectors);
		parameters->Set("DLSSNR.Output", output);
		// Publish the standard NGX subrect contract as well as the carrier-specific
		// DLSSNR names. The resources are isolated to zero-based per-eye textures,
		// but keeping the bases explicit prevents a future caller from silently
		// inheriting a stale SBS offset.
		parameters->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, guide.colorBaseX);
		parameters->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, guide.colorBaseY);
		parameters->Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, guide.depthBaseX);
		parameters->Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, guide.depthBaseY);
		parameters->Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, guide.motionBaseX);
		parameters->Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, guide.motionBaseY);
		parameters->Set(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, guide.outputBaseX);
		parameters->Set(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y, guide.outputBaseY);
		parameters->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, inputWidth);
		parameters->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, inputHeight);
		parameters->Set("DLSSNR.ColorSubrectBaseX", guide.colorBaseX);
		parameters->Set("DLSSNR.ColorSubrectBaseY", guide.colorBaseY);
		parameters->Set("DLSSNR.ColorSubrectWidth", guide.colorWidth);
		parameters->Set("DLSSNR.ColorSubrectHeight", guide.colorHeight);
		parameters->Set("DLSSNR.DepthSubrectBaseX", guide.depthBaseX);
		parameters->Set("DLSSNR.DepthSubrectBaseY", guide.depthBaseY);
		parameters->Set("DLSSNR.DepthSubrectWidth", guide.depthWidth);
		parameters->Set("DLSSNR.DepthSubrectHeight", guide.depthHeight);
		parameters->Set("DLSSNR.MVecSubrectBaseX", guide.motionBaseX);
		parameters->Set("DLSSNR.MVecSubrectBaseY", guide.motionBaseY);
		parameters->Set("DLSSNR.MVecSubrectWidth", guide.motionWidth);
		parameters->Set("DLSSNR.MVecSubrectHeight", guide.motionHeight);
		parameters->Set("DLSSNR.OutputSubrectBaseX", guide.outputBaseX);
		parameters->Set("DLSSNR.OutputSubrectBaseY", guide.outputBaseY);
		parameters->Set("DLSSNR.OutputSubrectWidth", guide.outputWidth);
		parameters->Set("DLSSNR.OutputSubrectHeight", guide.outputHeight);
		parameters->Set("DLSSNR.MVecScaleX", guide.motionVectorScaleX);
		parameters->Set("DLSSNR.MVecScaleY", guide.motionVectorScaleY);
		parameters->Set("DLSSNR.MVecLowRes", guide.motionVectorsLowResolution ? 1u : 0u);
		parameters->Set("DLSSNR.DepthInverted", 0u);
		parameters->Set("DLSSNR.Enabled", 1u);
		parameters->Set("DLSSNR.Reset", reset ? 1u : 0u);
		parameters->Set("DLSSNR.Intensity", tuning.intensity);
		parameters->Set("DLSSNR.LocalToneStrength", tuning.localToneStrength);
		parameters->Set("DLSSNR.LocalStructureStrength", tuning.localStructureStrength);
		parameters->Set("DLSSNR.SkinStructureStrength", tuning.skinStructureStrength);
		parameters->Set("DLSSNR.UseAutoMask", tuning.useAutoMask ? 1u : 0u);
		parameters->Set("DLSSNR.Style", tuning.style);
		parameters->Set("DLSSNR.UICorrection", tuning.uiCorrection ? 1u : 0u);
		const auto evaluateStart = std::chrono::steady_clock::now();
		ngxResult_ = static_cast<std::uint32_t>(evaluate(commandList,
			static_cast<NVSDK_NGX_Handle*>(featureHandles_[slot]), parameters, nullptr));
		if (successfulFrames_ == 0) {
			const auto evaluateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - evaluateStart).count();
			logger::info("[DLSSNR] first Feature 18 evaluate slot={} ms={:.2f} result=0x{:08X}", slot, evaluateMs, ngxResult_);
		}
		if (ngxResult_ != NVSDK_NGX_Result_Success) {
			detail_ = std::format("Feature 18 evaluate failed slot={} result=0x{:08X}", slot, ngxResult_);
			return false;
		}
		++successfulFrames_;
		return true;
	}

	bool Runtime::PrewarmFeature(ID3D12GraphicsCommandList* commandList, std::uint32_t slot,
		const Feature18GuideContract& guide, const Tuning& tuning)
	{
		return EnsureFeature(commandList, slot, guide, tuning);
	}

	bool Runtime::HasFeature(std::uint32_t slot) const
	{
		return slot < kFeatureSlotCount && featureHandles_[slot] != nullptr;
	}

	bool Runtime::NeedsRecreation(std::uint32_t slot, std::uint32_t width, std::uint32_t height, bool lowResolutionMotion) const
	{
		return NeedsRecreation(slot, width, height, width, height, lowResolutionMotion);
	}

	bool Runtime::NeedsRecreation(std::uint32_t slot, std::uint32_t inputWidth, std::uint32_t inputHeight,
		std::uint32_t outputWidth, std::uint32_t outputHeight, bool lowResolutionMotion) const
	{
		return slot < kFeatureSlotCount && featureHandles_[slot] &&
			(featureInputWidth_[slot] != inputWidth || featureInputHeight_[slot] != inputHeight ||
				 featureOutputWidth_[slot] != outputWidth || featureOutputHeight_[slot] != outputHeight ||
				 featureMotionVectorsLowResolution_[slot] != lowResolutionMotion);
	}

	void Runtime::ResetFeature(std::uint32_t slot)
	{
		if (!module_ || slot >= kFeatureSlotCount)
			return;
		SignedRuntimePathScope scope(signedRuntimePath_);
		auto release = g_runtimeExports.releaseFeature;
		if (featureHandles_[slot] && release) {
			const auto result = release(static_cast<NVSDK_NGX_Handle*>(featureHandles_[slot]));
			if (result != NVSDK_NGX_Result_Success)
				logger::warn("[DLSSNR] Feature 18 release failed slot={} result=0x{:08X} proxyInstalled={}",
					slot, static_cast<std::uint32_t>(result), scope.IsInstalled());
		}
		featureHandles_[slot] = nullptr;
		featureInputWidth_[slot] = featureInputHeight_[slot] = 0;
		featureOutputWidth_[slot] = featureOutputHeight_[slot] = 0;
		featureMotionVectorsLowResolution_[slot] = false;
	}

	void Runtime::ResetFeatures()
	{
		for (std::uint32_t slot = 0; slot < kFeatureSlotCount; ++slot)
			ResetFeature(slot);
		successfulFrames_ = 0;
	}

	void Runtime::Shutdown()
	{
		if (device_ && module_) {
			ResetFeatures();
			SignedRuntimePathScope scope(signedRuntimePath_);
			if (parameters_ && g_runtimeExports.destroyParameters)
				g_runtimeExports.destroyParameters(static_cast<NVSDK_NGX_Parameter*>(parameters_));
			parameters_ = nullptr;
			auto shutdown = g_runtimeExports.shutdown;
			if (shutdown) {
				const auto result = shutdown(device_);
				if (result != NVSDK_NGX_Result_Success)
					logger::warn("[DLSSNR] NGX shutdown failed result=0x{:08X} proxyInstalled={}",
						static_cast<std::uint32_t>(result), scope.IsInstalled());
			}
			device_->Release();
			device_ = nullptr;
		}
		parameters_ = nullptr;
		if (module_)
			RemoveSignedRuntimePathProxy(static_cast<HMODULE>(module_));
		if (g_runtimeExports.core)
			FreeLibrary(g_runtimeExports.core);
		if (module_)
			FreeLibrary(static_cast<HMODULE>(module_));
		module_ = nullptr;
		g_runtimeExports = {};
		status_ = RuntimeStatus::NotProbed;
		path_.clear();
		signedRuntimePath_.clear();
		version_.clear();
		detail_.clear();
		ngxResult_ = applicationId_ = apiVersion_ = 0;
		successfulFrames_ = 0;
	}

	const char* ToString(RuntimeStatus status)
	{
		switch (status) {
		case RuntimeStatus::NotProbed: return "not-probed";
		case RuntimeStatus::NotFound: return "not-found";
		case RuntimeStatus::VersionUnavailable: return "version-unavailable";
		case RuntimeStatus::UnsupportedVersion: return "unsupported-version";
		case RuntimeStatus::LoadFailed: return "load-failed";
		case RuntimeStatus::MissingExport: return "missing-export";
		case RuntimeStatus::Ready: return "ready";
		case RuntimeStatus::InitializationFailed: return "initialization-failed";
		case RuntimeStatus::CoreUnavailable: return "core-unavailable";
		case RuntimeStatus::ParameterAllocationFailed: return "parameter-allocation-failed";
		case RuntimeStatus::Initialized: return "initialized";
		case RuntimeStatus::SignatureRefused: return "signature-refused";
		}
		return "unknown";
	}
}
