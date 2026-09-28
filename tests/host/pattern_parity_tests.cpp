/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

// The C++ half of the qualifier parity check: every fixture in
// pattern_parity_cases.inc must give the same answer here, through the DLL's
// matchers, as tools/test_bzr_qualification.py gets through the Python
// qualifier. See the .inc for why that matters.

#include "Util/PatternMatch.h"
#include "HostTest.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

using namespace ExtraUtilities::PatternMatch;
using HostTest::Expect;

namespace
{
	struct ParityCase
	{
		const char* name;
		const char* pattern;
		const char* haystack;
		const char* offsets;
	};

#define PARITY_CASE(name, pattern, haystack, offsets) { name, pattern, haystack, offsets },
	const ParityCase kCases[] = {
#include "pattern_parity_cases.inc"
	};
#undef PARITY_CASE

	constexpr uintptr_t kBase = 0x00400000u;

	std::vector<std::string> Tokens(const std::string& text, char separator)
	{
		std::vector<std::string> tokens;
		std::stringstream stream(text);
		std::string token;
		while (std::getline(stream, token, separator))
		{
			if (!token.empty())
			{
				tokens.push_back(token);
			}
		}
		return tokens;
	}

	int ParseHexByte(const std::string& token)
	{
		if (token.size() != 2)
		{
			return -2;
		}
		char* end = nullptr;
		const long value = std::strtol(token.c_str(), &end, 16);
		return (end == token.c_str() + 2 && value >= 0 && value <= 0xFF) ? static_cast<int>(value) : -2;
	}

	// The generator's rendering of a qualifier pattern: a wildcard ("?" or
	// "??") becomes -1, as in BzrBuildProfile.generated.h.
	bool ParsePattern(const std::string& text, std::vector<int>& pattern)
	{
		for (const std::string& token : Tokens(text, ' '))
		{
			if (token == "?" || token == "??")
			{
				pattern.push_back(-1);
				continue;
			}
			const int value = ParseHexByte(token);
			if (value < 0)
			{
				return false;
			}
			pattern.push_back(value);
		}
		return !pattern.empty();
	}

	bool ParseHaystack(const std::string& text, std::vector<uint8_t>& bytes)
	{
		for (const std::string& token : Tokens(text, ' '))
		{
			const int value = ParseHexByte(token);
			if (value < 0)
			{
				return false;
			}
			bytes.push_back(static_cast<uint8_t>(value));
		}
		return true;
	}

	std::vector<size_t> ParseOffsets(const std::string& text)
	{
		std::vector<size_t> offsets;
		for (const std::string& token : Tokens(text, ','))
		{
			offsets.push_back(static_cast<size_t>(std::strtoul(token.c_str(), nullptr, 10)));
		}
		return offsets;
	}

	void CheckCase(const ParityCase& parity)
	{
		const std::string name = parity.name;
		std::vector<int> pattern;
		std::vector<uint8_t> data;
		if (!ParsePattern(parity.pattern, pattern) || !ParseHaystack(parity.haystack, data))
		{
			Expect(false, name + ": fixture does not parse");
			return;
		}
		const std::vector<size_t> expected = ParseOffsets(parity.offsets);

		// Byte + mask form, as SignatureResolver's masked searches take it.
		std::vector<uint8_t> bytes;
		std::vector<uint8_t> mask;
		for (int value : pattern)
		{
			bytes.push_back(static_cast<uint8_t>(value < 0 ? 0 : value));
			mask.push_back(static_cast<uint8_t>(value < 0 ? 0 : 1));
		}

		const uint8_t* base = data.empty() ? nullptr : data.data();
		std::vector<size_t> found;
		for (size_t offset = 0; offset < data.size(); ++offset)
		{
			if (PatternMatches(base + offset, data.size() - offset, pattern.data(), pattern.size()))
			{
				found.push_back(offset);
			}
		}
		Expect(found == expected, name + ": every match offset agrees");

		Expect(CountPatternMatches(base, data.size(), pattern.data(), pattern.size()) == expected.size(),
			name + ": the match count agrees");

		// BuildValidation's modes: UniqueExecutable counts to 2 and needs 1;
		// ExecutableContains needs any; ExpectedVa tests one address.
		const size_t capped = CountPatternMatches(base, data.size(), pattern.data(), pattern.size(), 2);
		Expect((capped == 1) == (expected.size() == 1), name + ": unique_executable agrees");
		Expect((capped >= 1) == !expected.empty(), name + ": executable_contains agrees");
		const bool atZero = !expected.empty() && expected.front() == 0;
		Expect(PatternMatches(base, data.size(), pattern.data(), pattern.size()) == atZero,
			name + ": expected_va at offset 0 agrees");

		const uintptr_t first = FindMaskedPattern(base, data.size(), kBase, bytes.data(), mask.data(), bytes.size());
		Expect(first == (expected.empty() ? 0 : kBase + expected.front()), name + ": the first masked match agrees");

		const uintptr_t unique = FindUniqueMaskedPattern(base, data.size(), kBase, bytes.data(), mask.data(), bytes.size());
		Expect(unique == (expected.size() == 1 ? kBase + expected.front() : 0),
			name + ": the unique masked match agrees, failing closed when ambiguous");
	}
}

int main()
{
	const size_t count = sizeof(kCases) / sizeof(kCases[0]);
	Expect(count >= 20, "the shared fixture file was read");
	for (const ParityCase& parity : kCases)
	{
		CheckCase(parity);
	}
	return HostTest::Finish("pattern-parity");
}
