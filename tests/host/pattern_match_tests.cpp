/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

// Host-side checks for the byte-pattern matchers behind every signature-resolved
// patch target and the runtime build gate. The rules that matter are the
// fail-closed ones: a wildcard must not match past the end of the data, and an
// ambiguous "unique" signature must resolve to nothing rather than to its first
// match.

#include "Util/PatternMatch.h"
#include "HostTest.h"

#include <array>
#include <cstdint>
#include <vector>

using namespace ExtraUtilities::PatternMatch;
using HostTest::Expect;

namespace
{
	constexpr uintptr_t kBase = 0x00400000u;

	// "55 8B EC ?? 90" twice, the second copy at offset 12.
	const std::vector<uint8_t> kData = {
		0x90, 0x90, 0x55, 0x8B, 0xEC, 0x11, 0x90, 0xCC,
		0xCC, 0xCC, 0xCC, 0xCC, 0x55, 0x8B, 0xEC, 0x22,
		0x90, 0xC3,
	};

	void TestIntPatternWildcards()
	{
		const std::array<int, 5> pattern = { 0x55, 0x8B, 0xEC, -1, 0x90 };
		Expect(PatternMatches(kData.data() + 2, kData.size() - 2, pattern.data(), pattern.size()),
			"a negative entry matches any byte");
		Expect(!PatternMatches(kData.data() + 1, kData.size() - 1, pattern.data(), pattern.size()),
			"a literal mismatch fails");
		Expect(FindPattern(kData.data(), kData.size(), pattern) == kData.data() + 2,
			"FindPattern returns the first match");
		Expect(CountPatternMatches(kData.data(), kData.size(), pattern.data(), pattern.size()) == 2,
			"both copies are counted");
		Expect(CountPatternMatches(kData.data(), kData.size(), pattern.data(), pattern.size(), 1) == 1,
			"counting stops at stopAfter");
	}

	void TestIntPatternBounds()
	{
		// The pattern fits only if the data is at least as long as it.
		const std::array<int, 3> tail = { 0x22, 0x90, 0xC3 };
		Expect(FindPattern(kData.data(), kData.size(), tail) == kData.data() + 15,
			"a match ending on the last byte is found");

		const std::array<int, 4> pastEnd = { 0x90, 0xC3, -1, -1 };
		Expect(FindPattern(kData.data(), kData.size(), pastEnd) == nullptr,
			"trailing wildcards never match past the end of the data");

		const std::array<int, 2> any = { -1, -1 };
		Expect(FindPattern(kData.data(), 1, any) == nullptr, "data shorter than the pattern cannot match");
		Expect(FindPattern<2>(nullptr, 16, any) == nullptr, "null data does not match");
		Expect(!PatternMatches(kData.data(), kData.size(), nullptr, 3), "a null pattern does not match");
		Expect(!PatternMatches(kData.data(), kData.size(), any.data(), 0), "an empty pattern does not match");
		Expect(CountPatternMatches(kData.data(), kData.size(), any.data(), 0) == 0,
			"an empty pattern counts no matches");
	}

	void TestIntPatternLiteralFF()
	{
		// 0xFF is a literal byte; only negative values are wildcards.
		const std::vector<uint8_t> data = { 0x00, 0xFF, 0x00 };
		const std::array<int, 1> literal = { 0xFF };
		Expect(FindPattern(data.data(), data.size(), literal) == data.data() + 1, "0xFF is matched literally");
		const std::vector<uint8_t> none = { 0x00, 0xFE };
		Expect(FindPattern(none.data(), none.size(), literal) == nullptr, "0xFF does not act as a wildcard");
	}

	void TestMaskedPatterns()
	{
		const uint8_t pattern[] = { 0x55, 0x8B, 0xEC, 0x00, 0x90 };
		const uint8_t mask[] = { 1, 1, 1, 0, 1 };

		Expect(FindMaskedPattern(kData.data(), kData.size(), kBase, pattern, mask, sizeof(pattern)) == kBase + 2,
			"a masked search returns base + offset of the first match");
		Expect(FindUniqueMaskedPattern(kData.data(), kData.size(), kBase, pattern, mask, sizeof(pattern)) == 0,
			"an ambiguous unique search fails closed");

		// Pin the second copy's wildcard byte to make it unique.
		const uint8_t uniquePattern[] = { 0x55, 0x8B, 0xEC, 0x22, 0x90 };
		const uint8_t fullMask[] = { 1, 1, 1, 1, 1 };
		Expect(FindUniqueMaskedPattern(kData.data(), kData.size(), kBase, uniquePattern, fullMask, sizeof(uniquePattern)) == kBase + 12,
			"a unique search returns its only match");

		const uint8_t missing[] = { 0x55, 0x8B, 0xEC, 0x33, 0x90 };
		Expect(FindMaskedPattern(kData.data(), kData.size(), kBase, missing, fullMask, sizeof(missing)) == 0,
			"no match returns 0");
		Expect(FindUniqueMaskedPattern(kData.data(), kData.size(), kBase, missing, fullMask, sizeof(missing)) == 0,
			"no unique match returns 0");
	}

	void TestMaskedPatternRejectsBadInput()
	{
		const uint8_t pattern[] = { 0x55 };
		const uint8_t mask[] = { 1 };
		Expect(FindMaskedPattern(nullptr, 4, kBase, pattern, mask, 1) == 0, "null data returns 0");
		Expect(FindMaskedPattern(kData.data(), kData.size(), kBase, nullptr, mask, 1) == 0, "null pattern returns 0");
		Expect(FindMaskedPattern(kData.data(), kData.size(), kBase, pattern, nullptr, 1) == 0, "null mask returns 0");
		Expect(FindMaskedPattern(kData.data(), kData.size(), kBase, pattern, mask, 0) == 0, "empty pattern returns 0");
		Expect(FindUniqueMaskedPattern(kData.data(), 0, kBase, pattern, mask, 1) == 0, "empty data returns 0");
	}

	void TestRelativeCallDecode()
	{
		uintptr_t target = 0xDEADu;

		// call +0x10 at 0x00401000 lands 5 + 0x10 bytes later.
		const uint8_t forward[] = { 0xE8, 0x10, 0x00, 0x00, 0x00 };
		Expect(TryDecodeRelativeCall(forward, 0x00401000u, target) && target == 0x00401015u,
			"a forward rel32 call is measured from the next instruction");

		// call -0x20 (0xFFFFFFE0) at 0x00401000 goes backwards.
		const uint8_t backward[] = { 0xE8, 0xE0, 0xFF, 0xFF, 0xFF };
		Expect(TryDecodeRelativeCall(backward, 0x00401000u, target) && target == 0x00400FE5u,
			"a negative displacement is sign-extended");

		// A call to itself is E8 FB FF FF FF.
		const uint8_t self[] = { 0xE8, 0xFB, 0xFF, 0xFF, 0xFF };
		Expect(TryDecodeRelativeCall(self, 0x00401000u, target) && target == 0x00401000u,
			"displacement -5 targets the call itself");

		target = 0xDEADu;
		const uint8_t jump[] = { 0xE9, 0x10, 0x00, 0x00, 0x00 };
		Expect(!TryDecodeRelativeCall(jump, 0x00401000u, target) && target == 0,
			"a jmp rel32 is not a call and clears the target");

		target = 0xDEADu;
		Expect(!TryDecodeRelativeCall(nullptr, 0x00401000u, target) && target == 0,
			"null input fails closed");
	}
}

int main()
{
	TestIntPatternWildcards();
	TestIntPatternBounds();
	TestIntPatternLiteralFF();
	TestMaskedPatterns();
	TestMaskedPatternRejectsBadInput();
	TestRelativeCallDecode();
	return HostTest::Finish("pattern-match");
}
