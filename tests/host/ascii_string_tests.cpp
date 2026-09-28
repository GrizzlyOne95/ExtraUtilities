/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

// Host-side checks for the ASCII case folding used to match engine names
// (unit VO file names, TRN keys, flame colour and overlay tokens).

#include "Util/AsciiString.h"
#include "HostTest.h"

#include <string>

using namespace ExtraUtilities::AsciiString;
using HostTest::Expect;

namespace
{
	void TestToLower()
	{
		Expect(ToLowerAscii("AbC_09-Z.WAV") == "abc_09-z.wav", "only A-Z are folded");
		Expect(ToLowerAscii("") == "", "empty stays empty");
		Expect(ToLowerAscii('@') == '@' && ToLowerAscii('[') == '[',
			"the bytes either side of A-Z are unchanged");

		const std::string high = "\xC4\xD6\xDC";
		Expect(ToLowerAscii(high) == high, "bytes >= 0x80 pass through");

		const std::string withNul("A\0B", 3);
		Expect(ToLowerAscii(withNul) == std::string("a\0b", 3), "embedded NUL keeps the full length");

		static_assert(ToLowerAscii('Q') == 'q', "usable in constant expressions");
	}

	void TestEqualsIgnoreCase()
	{
		Expect(EqualsIgnoreCase("TextureAtlas", "textureatlas"), "case differences match");
		Expect(EqualsIgnoreCase("", ""), "two empty strings match");
		Expect(!EqualsIgnoreCase("Size", "Sizes"), "a prefix does not match");
		Expect(!EqualsIgnoreCase("a", "b"), "different letters do not match");
		Expect(!EqualsIgnoreCase("\xC4", "\xE4"), "non-ASCII is compared exactly");

		static_assert(EqualsIgnoreCase("ATLAS", "atlas"), "usable in constant expressions");
	}
}

int main()
{
	TestToLower();
	TestEqualsIgnoreCase();
	return HostTest::Finish("ascii-string");
}
