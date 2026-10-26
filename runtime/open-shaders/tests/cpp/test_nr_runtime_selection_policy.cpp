#include "Features/Upscaling/NeuralRendering/RuntimeSelectionPolicy.h"
#include <catch2/catch_test_macros.hpp>

using namespace NeuralRendering::RuntimeSelection;

namespace
{
	Candidate Make(const char* file, std::uint32_t major, std::uint32_t minor, std::uint32_t build,
		SignatureStatus signature)
	{
		return Candidate{ file, Version{ major, minor, build, 0 }, signature };
	}
}

TEST_CASE("Runtime version policy accepts 310.8.x and 310.9.x only", "[nr][runtime-select]")
{
	REQUIRE(IsAcceptedVersion({ 310, 8, 0, 0 }));
	REQUIRE(IsAcceptedVersion({ 310, 8, 5, 3 }));
	REQUIRE(IsAcceptedVersion({ 310, 9, 16, 0 }));
	REQUIRE_FALSE(IsAcceptedVersion({ 310, 7, 0, 0 }));
	REQUIRE_FALSE(IsAcceptedVersion({ 310, 10, 0, 0 }));
	REQUIRE_FALSE(IsAcceptedVersion({ 311, 8, 0, 0 }));
	REQUIRE_FALSE(IsAcceptedVersion({ 0, 0, 0, 0 }));
}

TEST_CASE("Version strings parse in NVIDIA comma and dotted styles", "[nr][runtime-select]")
{
	REQUIRE(ParseVersion("310,9,16,0") == std::optional<Version>(Version{ 310, 9, 16, 0 }));
	REQUIRE(ParseVersion("310, 9, 16, 0") == std::optional<Version>(Version{ 310, 9, 16, 0 }));
	REQUIRE(ParseVersion("310.8.0.0") == std::optional<Version>(Version{ 310, 8, 0, 0 }));
	REQUIRE(ParseVersion("310.9") == std::optional<Version>(Version{ 310, 9, 0, 0 }));
	REQUIRE(ParseVersion("310.9.16.0 extra") == std::optional<Version>(Version{ 310, 9, 16, 0 }));
	REQUIRE_FALSE(ParseVersion("").has_value());
	REQUIRE_FALSE(ParseVersion("310").has_value());
	REQUIRE_FALSE(ParseVersion("abc").has_value());
	REQUIRE_FALSE(ParseVersion("99999999999.1").has_value());
	REQUIRE(FormatVersion({ 310, 9, 16, 0 }) == "310.9.16.0");
}

TEST_CASE("NR model hint preserves 310.8 and maps both 310.9 networks", "[nr][runtime-select]")
{
	REQUIRE(RenderPresetHint({ 310, 8, 0, 0 }, 0) == 0);
	REQUIRE(RenderPresetHint({ 310, 8, 0, 0 }, 1) == 0);
	REQUIRE(RenderPresetHint({ 310, 9, 16, 0 }, 0) == 1);
	REQUIRE(RenderPresetHint({ 310, 9, 16, 0 }, 1) == 2);
	REQUIRE(RenderPresetHint({ 310, 10, 0, 0 }, 1) == 0);
	REQUIRE(RenderPresetHint({ 311, 0, 0, 0 }, 1) == 0);
}

TEST_CASE("Runtime file names match nvngx_dlssnr star dll", "[nr][runtime-select]")
{
	REQUIRE(IsRuntimeFileName("nvngx_dlssnr.dll"));
	REQUIRE(IsRuntimeFileName("NVNGX_DLSSNR_310_9_16.DLL"));
	REQUIRE(IsRuntimeFileName("nvngx_dlssnr_310_9_16.dll"));
	REQUIRE_FALSE(IsRuntimeFileName("nvngx_dlss.dll"));
	REQUIRE_FALSE(IsRuntimeFileName("nvngx_dlssnr.dll.bak"));
	REQUIRE_FALSE(IsRuntimeFileName("sub/nvngx_dlssnr.dll"));
	REQUIRE_FALSE(IsRuntimeFileName(".dll"));
}

TEST_CASE("Auto keeps the default-named 310.8 file when it is the only one", "[nr][runtime-select]")
{
	const std::vector<Candidate> list{ Make("nvngx_dlssnr.dll", 310, 8, 0, SignatureStatus::SignedNvidia) };
	const auto selection = Select(list, "", true);
	REQUIRE(selection.index == 0);
	REQUIRE(selection.reason == Reason::Auto);
}

TEST_CASE("Auto prefers the default-named file over a newer variant", "[nr][runtime-select]")
{
	const std::vector<Candidate> list{
		Make("nvngx_dlssnr_310_9_16.dll", 310, 9, 16, SignatureStatus::SignedNvidia),
		Make("nvngx_dlssnr.dll", 310, 8, 0, SignatureStatus::SignedNvidia),
	};
	const auto selection = Select(list, "", true);
	REQUIRE(selection.index == 1);
	REQUIRE(selection.reason == Reason::Auto);
}

TEST_CASE("Auto picks the highest accepted version when the default name is absent or refused", "[nr][runtime-select]")
{
	SECTION("default name absent")
	{
		const std::vector<Candidate> list{
			Make("nvngx_dlssnr_310_8.dll", 310, 8, 0, SignatureStatus::SignedNvidia),
			Make("nvngx_dlssnr_310_9_16.dll", 310, 9, 16, SignatureStatus::SignedNvidia),
			Make("nvngx_dlssnr_old.dll", 310, 7, 0, SignatureStatus::SignedNvidia),
		};
		REQUIRE(Select(list, "", true).index == 1);
	}
	SECTION("default name has an unsupported version")
	{
		const std::vector<Candidate> list{
			Make("nvngx_dlssnr.dll", 311, 0, 0, SignatureStatus::SignedNvidia),
			Make("nvngx_dlssnr_310_9_16.dll", 310, 9, 16, SignatureStatus::SignedNvidia),
		};
		REQUIRE(Select(list, "", true).index == 1);
	}
	SECTION("version tie prefers the signed file")
	{
		const std::vector<Candidate> list{
			Make("nvngx_dlssnr_a.dll", 310, 9, 16, SignatureStatus::Unsigned),
			Make("nvngx_dlssnr_b.dll", 310, 9, 16, SignatureStatus::SignedNvidia),
		};
		REQUIRE(Select(list, "", false).index == 1);
	}
}

TEST_CASE("Signed-only filtering refuses unsigned, invalid, other-signer and unknown", "[nr][runtime-select]")
{
	for (const auto status : { SignatureStatus::Unsigned, SignatureStatus::Invalid, SignatureStatus::SignedOther,
			 SignatureStatus::Unknown }) {
		const std::vector<Candidate> list{ Make("nvngx_dlssnr.dll", 310, 9, 16, status) };
		const auto strict = Select(list, "", true);
		REQUIRE(strict.index == -1);
		REQUIRE(strict.reason == Reason::None);
		REQUIRE(strict.signatureRefused.size() == 1);
		REQUIRE(DescribeNoneSelectable(list, strict).find("not signed by NVIDIA Corporation") != std::string::npos);
		const auto relaxed = Select(list, "", false);
		REQUIRE(relaxed.index == 0);
	}
}

TEST_CASE("An unsigned default-name 310.9 falls through to a signed 310.8 variant", "[nr][runtime-select]")
{
	const std::vector<Candidate> list{
		Make("nvngx_dlssnr.dll", 310, 9, 16, SignatureStatus::Unsigned),
		Make("nvngx_dlssnr_310_8.dll", 310, 8, 0, SignatureStatus::SignedNvidia),
	};
	const auto selection = Select(list, "", true);
	REQUIRE(selection.index == 1);
	REQUIRE(selection.reason == Reason::Auto);
	REQUIRE(selection.signatureRefused == std::vector<int>{ 0 });
}

TEST_CASE("User selection wins when accepted and falls back otherwise", "[nr][runtime-select]")
{
	const std::vector<Candidate> list{
		Make("nvngx_dlssnr.dll", 310, 8, 0, SignatureStatus::SignedNvidia),
		Make("nvngx_dlssnr_310_9_16.dll", 310, 9, 16, SignatureStatus::SignedNvidia),
		Make("nvngx_dlssnr_bad.dll", 310, 9, 16, SignatureStatus::Unsigned),
	};
	SECTION("accepted user file")
	{
		const auto selection = Select(list, "NVNGX_DLSSNR_310_9_16.DLL", true);
		REQUIRE(selection.index == 1);
		REQUIRE(selection.reason == Reason::User);
	}
	SECTION("missing user file")
	{
		const auto selection = Select(list, "nvngx_dlssnr_gone.dll", true);
		REQUIRE(selection.index == 0);
		REQUIRE(selection.reason == Reason::Fallback);
		REQUIRE(selection.userFileMissing);
	}
	SECTION("refused user file")
	{
		const auto selection = Select(list, "nvngx_dlssnr_bad.dll", true);
		REQUIRE(selection.index == 0);
		REQUIRE(selection.reason == Reason::Fallback);
		REQUIRE_FALSE(selection.userFileMissing);
		REQUIRE(selection.userFileVerdict == std::optional<Verdict>(Verdict::SignatureRefused));
	}
	SECTION("the same unsigned file is allowed when signing is not required")
	{
		const auto selection = Select(list, "nvngx_dlssnr_bad.dll", false);
		REQUIRE(selection.index == 2);
		REQUIRE(selection.reason == Reason::User);
	}
}

TEST_CASE("Empty and unusable candidate lists select nothing", "[nr][runtime-select]")
{
	const std::vector<Candidate> none;
	const auto empty = Select(none, "", true);
	REQUIRE(empty.index == -1);
	REQUIRE(DescribeNoneSelectable(none, empty) == "no nvngx_dlssnr*.dll found");

	const std::vector<Candidate> wrong{ Make("nvngx_dlssnr.dll", 311, 1, 0, SignatureStatus::SignedNvidia) };
	const auto unsupported = Select(wrong, "", true);
	REQUIRE(unsupported.index == -1);
	REQUIRE(DescribeNoneSelectable(wrong, unsupported).find("310.8.x or 310.9.x") != std::string::npos);

	const std::vector<Candidate> noVersion{ Candidate{ "nvngx_dlssnr.dll", std::nullopt, SignatureStatus::SignedNvidia } };
	REQUIRE(Select(noVersion, "", false).index == -1);
}

TEST_CASE("Signature resolver is only consulted when the signature matters", "[nr][runtime-select]")
{
	const std::vector<Candidate> list{
		Make("nvngx_dlssnr.dll", 310, 8, 0, SignatureStatus::Unknown),
		Make("nvngx_dlssnr_310_9_16.dll", 310, 9, 16, SignatureStatus::Unknown),
	};
	std::vector<std::size_t> calls;
	const SignatureResolver resolver = [&](std::size_t i) {
		calls.push_back(i);
		return SignatureStatus::SignedNvidia;
	};
	SECTION("default accepted first: only it is resolved")
	{
		const auto selection = Select(list, "", true, resolver);
		REQUIRE(selection.index == 0);
		REQUIRE(calls == std::vector<std::size_t>{ 0 });
	}
	SECTION("signing not required: the default file is still checked first and wins when NVIDIA-signed")
	{
		const auto selection = Select(list, "", false, resolver);
		REQUIRE(selection.index == 0);
		REQUIRE(calls == std::vector<std::size_t>{ 0 });
	}
}

TEST_CASE("Auto never picks an unsigned file while a signed accepted one exists", "[nr][runtime-select]")
{
	const std::vector<Candidate> list{
		Make("nvngx_dlssnr.dll", 310, 9, 16, SignatureStatus::Unsigned),
		Make("nvngx_dlssnr_310_8.dll", 310, 8, 0, SignatureStatus::SignedNvidia),
	};
	SECTION("signed-only on")
	{
		REQUIRE(Select(list, "", true).index == 1);
	}
	SECTION("signed-only off still prefers the signed file over the unsigned default name")
	{
		const auto selection = Select(list, "", false);
		REQUIRE(selection.index == 1);
		REQUIRE(selection.reason == Reason::Auto);
	}
	SECTION("signed-only off and no signed candidate: the unsigned file is used")
	{
		const std::vector<Candidate> onlyUnsigned{ Make("nvngx_dlssnr.dll", 310, 9, 16, SignatureStatus::Unsigned) };
		REQUIRE(Select(onlyUnsigned, "", false).index == 0);
		REQUIRE(Select(onlyUnsigned, "", true).index == -1);
	}
	SECTION("an unsupported-version signed file does not count as a signed accepted candidate")
	{
		const std::vector<Candidate> mixed{
			Make("nvngx_dlssnr.dll", 310, 9, 16, SignatureStatus::Unsigned),
			Make("nvngx_dlssnr_new.dll", 311, 0, 0, SignatureStatus::SignedNvidia),
		};
		REQUIRE(Select(mixed, "", false).index == 0);
	}
}

TEST_CASE("Unsigned candidates stay visible but are refused with a clear reason when signing is required", "[nr][runtime-select]")
{
	const Candidate unsignedFile = Make("nvngx_dlssnr_patched.dll", 310, 9, 16, SignatureStatus::Unsigned);
	REQUIRE(Evaluate(unsignedFile, true) == Verdict::SignatureRefused);
	REQUIRE(Evaluate(unsignedFile, false) == Verdict::Accepted);
	REQUIRE(RefusalText(Verdict::SignatureRefused, SignatureStatus::Unsigned) == "refused: unsigned");
	REQUIRE(RefusalText(Verdict::SignatureRefused, SignatureStatus::Invalid) == "refused: invalid signature");
	REQUIRE(RefusalText(Verdict::Accepted, SignatureStatus::SignedNvidia).empty());
	REQUIRE(std::string(SignatureDisplayText(SignatureStatus::Unsigned)) == "Unsigned (modified?)");
	REQUIRE(std::string(SignatureDisplayText(SignatureStatus::SignedNvidia)) == "Signed by NVIDIA Corporation");
	// User may pin the unsigned file only with signing off; otherwise Auto fallback applies.
	const std::vector<Candidate> list{ Make("nvngx_dlssnr.dll", 310, 8, 0, SignatureStatus::SignedNvidia), unsignedFile };
	REQUIRE(Select(list, "nvngx_dlssnr_patched.dll", false).index == 1);
	const auto refused = Select(list, "nvngx_dlssnr_patched.dll", true);
	REQUIRE(refused.index == 0);
	REQUIRE(refused.reason == Reason::Fallback);
}

TEST_CASE("GPU hint is shown for anything that is not an RTX 50 adapter", "[nr][runtime-select]")
{
	REQUIRE(IsBlackwellAdapterName("NVIDIA GeForce RTX 5090"));
	REQUIRE(IsBlackwellAdapterName("nvidia geforce rtx 5070 ti"));
	REQUIRE_FALSE(IsBlackwellAdapterName("NVIDIA GeForce RTX 4090"));
	REQUIRE_FALSE(IsBlackwellAdapterName("NVIDIA GeForce RTX 3080"));
	REQUIRE_FALSE(IsBlackwellAdapterName(""));
}
