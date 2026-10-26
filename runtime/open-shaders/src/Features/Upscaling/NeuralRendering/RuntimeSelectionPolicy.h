#pragma once

// Pure (Windows-free) policy for choosing which nvngx_dlssnr*.dll to pin into the process.
// No filesystem, no WinVerifyTrust: callers inject the candidate list and, optionally, a lazy
// signature resolver so the expensive Authenticode hash is only computed for files that matter.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NeuralRendering::RuntimeSelection
{
	inline constexpr std::string_view kDefaultRuntimeFile = "nvngx_dlssnr.dll";

	enum class SignatureStatus : std::uint8_t
	{
		Unknown,       // not computed yet, or the check itself failed
		SignedNvidia,  // valid Authenticode signature, signer "NVIDIA Corporation"
		SignedOther,   // valid Authenticode signature by another publisher
		Unsigned,
		Invalid,       // signature present but does not verify (modified file, bad chain, ...)
	};

	[[nodiscard]] constexpr const char* SignatureText(SignatureStatus status)
	{
		switch (status) {
		case SignatureStatus::SignedNvidia: return "Signed by NVIDIA Corporation";
		case SignatureStatus::SignedOther: return "Signed (other)";
		case SignatureStatus::Unsigned: return "Unsigned";
		case SignatureStatus::Invalid: return "Invalid";
		case SignatureStatus::Unknown: break;
		}
		return "unknown";
	}

	struct Version
	{
		std::uint32_t major = 0;
		std::uint32_t minor = 0;
		std::uint32_t build = 0;
		std::uint32_t revision = 0;

		[[nodiscard]] bool operator==(const Version&) const = default;
		[[nodiscard]] bool operator<(const Version& other) const
		{
			if (major != other.major) return major < other.major;
			if (minor != other.minor) return minor < other.minor;
			if (build != other.build) return build < other.build;
			return revision < other.revision;
		}
	};

	/** @brief Maps the UI selection to the 310.9 preset hint; 310.8 requires the legacy 0 hint. */
	[[nodiscard]] constexpr std::uint32_t RenderPresetHint(const Version& version, std::uint32_t modelPreset)
	{
		if (version.major != 310 || version.minor != 9)
			return 0;
		return modelPreset == 1 ? 2u : 1u;
	}

	[[nodiscard]] inline std::string FormatVersion(const Version& v)
	{
		return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.build) + "." +
		       std::to_string(v.revision);
	}

	/** @brief Parses "310,9,16,0", "310.9.16.0", "310, 9, 16, 0" or a shorter "310.9". Trailing text is ignored. */
	[[nodiscard]] inline std::optional<Version> ParseVersion(std::string_view text)
	{
		std::uint32_t parts[4]{};
		std::size_t count = 0;
		std::size_t i = 0;
		while (i < text.size() && count < 4) {
			while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
				++i;
			if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i])))
				break;
			std::uint64_t value = 0;
			while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
				value = value * 10 + static_cast<std::uint64_t>(text[i] - '0');
				if (value > 0xFFFFFFFFull)
					return std::nullopt;
				++i;
			}
			parts[count++] = static_cast<std::uint32_t>(value);
			while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
				++i;
			if (i < text.size() && (text[i] == ',' || text[i] == '.'))
				++i;
			else
				break;
		}
		if (count < 2)
			return std::nullopt;
		return Version{ parts[0], parts[1], parts[2], parts[3] };
	}

	/** @brief 310.8.x and 310.9.x are accepted; everything else is refused. */
	[[nodiscard]] constexpr bool IsAcceptedVersion(const Version& v) { return v.major == 310 && (v.minor == 8 || v.minor == 9); }

	[[nodiscard]] inline std::string ToLowerAscii(std::string_view text)
	{
		std::string out(text);
		std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	[[nodiscard]] inline bool EqualsNoCase(std::string_view a, std::string_view b) { return ToLowerAscii(a) == ToLowerAscii(b); }

	/** @brief True for a bare file name matching nvngx_dlssnr*.dll (no directory components). */
	[[nodiscard]] inline bool IsRuntimeFileName(std::string_view name)
	{
		constexpr std::string_view prefix = "nvngx_dlssnr";
		constexpr std::string_view suffix = ".dll";
		if (name.find_first_of("/\\") != std::string_view::npos || name.size() < prefix.size() + suffix.size())
			return false;
		const auto lower = ToLowerAscii(name);
		return lower.starts_with(prefix) && lower.ends_with(suffix);
	}

	/** @brief UI/display text: an unsigned file is flagged as possibly modified. */
	[[nodiscard]] constexpr const char* SignatureDisplayText(SignatureStatus status)
	{
		return status == SignatureStatus::Unsigned ? "Unsigned (modified?)" : SignatureText(status);
	}

	/** @brief True when the adapter name looks like a GeForce RTX 50 (Blackwell) part. */
	[[nodiscard]] inline bool IsBlackwellAdapterName(std::string_view name)
	{
		return ToLowerAscii(name).find("rtx 50") != std::string::npos;
	}

	struct Candidate
	{
		std::string file;
		std::optional<Version> version;  // empty: the version resource was unreadable
		SignatureStatus signature = SignatureStatus::Unknown;
	};

	enum class Verdict : std::uint8_t
	{
		Accepted,
		NoVersion,
		UnsupportedVersion,
		SignatureRefused,
	};

	[[nodiscard]] constexpr const char* VerdictText(Verdict verdict)
	{
		switch (verdict) {
		case Verdict::Accepted: return "accepted";
		case Verdict::NoVersion: return "no version resource";
		case Verdict::UnsupportedVersion: return "unsupported version (need 310.8.x or 310.9.x)";
		case Verdict::SignatureRefused: return "not signed by NVIDIA Corporation";
		}
		return "";
	}

	/** @brief Short UI reason for a refused candidate ("refused: unsigned", ...). */
	[[nodiscard]] inline std::string RefusalText(Verdict verdict, SignatureStatus signature)
	{
		if (verdict == Verdict::Accepted)
			return {};
		if (verdict == Verdict::SignatureRefused) {
			switch (signature) {
			case SignatureStatus::Unsigned: return "refused: unsigned";
			case SignatureStatus::Invalid: return "refused: invalid signature";
			case SignatureStatus::SignedOther: return "refused: signed by another publisher";
			default: return "refused: signature not verified as NVIDIA";
			}
		}
		return std::string("refused: ") + VerdictText(verdict);
	}

	/** @brief Version check first (cheap), then the signature requirement. */
	[[nodiscard]] inline Verdict Evaluate(const Candidate& candidate, bool requireSigned)
	{
		if (!candidate.version)
			return Verdict::NoVersion;
		if (!IsAcceptedVersion(*candidate.version))
			return Verdict::UnsupportedVersion;
		if (requireSigned && candidate.signature != SignatureStatus::SignedNvidia)
			return Verdict::SignatureRefused;
		return Verdict::Accepted;
	}

	enum class Reason : std::uint8_t
	{
		None,      // nothing selectable
		Auto,      // automatic choice
		User,      // the user's explicit file was selectable
		Fallback,  // the user's explicit file was missing/refused; Auto chose instead
	};

	[[nodiscard]] constexpr const char* ReasonText(Reason reason)
	{
		switch (reason) {
		case Reason::Auto: return "auto";
		case Reason::User: return "user";
		case Reason::Fallback: return "fallback";
		case Reason::None: break;
		}
		return "none";
	}

	struct Selection
	{
		int index = -1;  // into the candidate list; -1 when none is selectable
		Reason reason = Reason::None;
		// Set when a non-empty user file could not be honoured.
		bool userFileMissing = false;
		std::optional<Verdict> userFileVerdict;
		// Candidates refused by the signature requirement while looking (index order of evaluation).
		std::vector<int> signatureRefused;
		// Final verdict for each candidate that was evaluated; Accepted-by-default is NOT implied for the rest.
		std::vector<std::optional<Verdict>> verdicts;
	};

	using SignatureResolver = std::function<SignatureStatus(std::size_t index)>;

	/**
	 * @brief Chooses a runtime.
	 *
	 * Auto order: the file named exactly nvngx_dlssnr.dll first (keeps today's behaviour), then the
	 * remaining candidates by version descending (ties: NVIDIA-signed first, then name). A non-empty
	 * userFile is tried first; if it is absent or refused the choice falls back to Auto.
	 * The resolver (optional) supplies candidate signatures lazily; it is only called when the signature
	 * matters (requireSigned for a version-acceptable candidate, or to break a version tie).
	 */
	[[nodiscard]] inline Selection Select(const std::vector<Candidate>& candidates, std::string_view userFile,
		bool requireSigned, const SignatureResolver& resolver = {})
	{
		Selection result;
		result.verdicts.assign(candidates.size(), std::nullopt);
		std::vector<SignatureStatus> signatures(candidates.size());
		std::vector<bool> signatureKnown(candidates.size(), false);
		const auto signatureOf = [&](std::size_t i) {
			if (!signatureKnown[i]) {
				signatures[i] = resolver ? resolver(i) : candidates[i].signature;
				signatureKnown[i] = true;
			}
			return signatures[i];
		};
		const auto evaluate = [&](std::size_t i) {
			if (result.verdicts[i])
				return *result.verdicts[i];
			Candidate probe = candidates[i];
			if (probe.version && IsAcceptedVersion(*probe.version) && requireSigned)
				probe.signature = signatureOf(i);
			const auto verdict = Evaluate(probe, requireSigned);
			result.verdicts[i] = verdict;
			if (verdict == Verdict::SignatureRefused)
				result.signatureRefused.push_back(static_cast<int>(i));
			return verdict;
		};

		const bool wantsUser = !userFile.empty();
		if (wantsUser) {
			bool found = false;
			for (std::size_t i = 0; i < candidates.size(); ++i) {
				if (!EqualsNoCase(candidates[i].file, userFile))
					continue;
				found = true;
				const auto verdict = evaluate(i);
				if (verdict == Verdict::Accepted) {
					result.index = static_cast<int>(i);
					result.reason = Reason::User;
					return result;
				}
				result.userFileVerdict = verdict;
				break;
			}
			result.userFileMissing = !found;
		}

		std::vector<std::size_t> order;
		order.reserve(candidates.size());
		for (std::size_t i = 0; i < candidates.size(); ++i)
			order.push_back(i);
		const auto isDefault = [&](std::size_t i) { return EqualsNoCase(candidates[i].file, kDefaultRuntimeFile); };
		std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
			const bool defaultA = isDefault(a);
			const bool defaultB = isDefault(b);
			if (defaultA != defaultB)
				return defaultA;
			if (defaultA)
				return false;
			const Version none{};
			const Version& va = candidates[a].version ? *candidates[a].version : none;
			const Version& vb = candidates[b].version ? *candidates[b].version : none;
			if (va != vb)
				return vb < va;  // higher version first
			const bool signedA = signatureOf(a) == SignatureStatus::SignedNvidia;
			const bool signedB = signatureOf(b) == SignatureStatus::SignedNvidia;
			if (signedA != signedB)
				return signedA;
			return ToLowerAscii(candidates[a].file) < ToLowerAscii(candidates[b].file);
		});

		// Auto never picks an unsigned/modified file while an NVIDIA-signed accepted one exists; with
		// signing required unsigned files are not accepted at all.
		for (const bool signedPass : { true, false }) {
			if (!signedPass && requireSigned)
				break;
			for (const auto i : order) {
				if (evaluate(i) != Verdict::Accepted)
					continue;
				if (signedPass && signatureOf(i) != SignatureStatus::SignedNvidia)
					continue;
				result.index = static_cast<int>(i);
				result.reason = wantsUser ? Reason::Fallback : Reason::Auto;
				return result;
			}
		}
		return result;
	}

	/** @brief Human reason for the "none selectable" status string. */
	[[nodiscard]] inline std::string DescribeNoneSelectable(const std::vector<Candidate>& candidates, const Selection& selection)
	{
		if (candidates.empty())
			return "no nvngx_dlssnr*.dll found";
		if (!selection.signatureRefused.empty())
			return "no accepted runtime: " + candidates[static_cast<std::size_t>(selection.signatureRefused.front())].file +
			       " is not signed by NVIDIA Corporation";
		for (std::size_t i = 0; i < candidates.size(); ++i)
			if (selection.verdicts[i] && *selection.verdicts[i] == Verdict::UnsupportedVersion)
				return "expected DLSSNR 310.8.x or 310.9.x, found " +
				       (candidates[i].version ? FormatVersion(*candidates[i].version) : std::string("unknown"));
		return "no accepted runtime (version resource unavailable)";
	}
}
