/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Game/PlantCore.h"
#include "HostTest.h"

#include <cstring>
#include <string>

using namespace ExtraUtilities::PlantCore;

namespace
{
	void TestNormalize()
	{
		HostTest::Expect(NormalizeOdfName("Tree01.ODF") == "tree01", "extension dropped, lower-cased");
		HostTest::Expect(NormalizeOdfName("tree01") == "tree01", "no extension kept as is");
		HostTest::Expect(NormalizeOdfName("abcdefghijkl") == "abcdefgh", "cut to the engine's 8 characters");
		HostTest::Expect(NormalizeOdfName(".odf").empty(), "bare extension is empty");
		HostTest::Expect(NormalizeOdfName("").empty(), "empty stays empty");

		// A raw class-name buffer: 8 characters, not terminated.
		const char raw[8] = { 'B', 'u', 's', 'h', '0', '2', 'x', 'y' };
		HostTest::Expect(NormalizeOdfName(std::string_view(raw, 8)) == "bush02xy", "unterminated 8-byte name");
		const char shortRaw[8] = { 'B', 'u', 's', 'h', '\0', 'z', 'z', 'z' };
		HostTest::Expect(NormalizeOdfName(std::string_view(shortRaw, 8)) == "bush", "stops at an embedded NUL");
	}

	void TestCleanString()
	{
		char out[16];
		HostTest::Expect(CleanOdfString("  \"xpltree\"  ", out, sizeof(out)) == 7 && std::strcmp(out, "xpltree") == 0,
			"quotes and whitespace stripped");
		HostTest::Expect(CleanOdfString("plain", out, sizeof(out)) == 5 && std::strcmp(out, "plain") == 0, "unquoted value");
		HostTest::Expect(CleanOdfString(nullptr, out, sizeof(out)) == 0 && out[0] == '\0', "null is empty");
		HostTest::Expect(CleanOdfString("\"\"", out, sizeof(out)) == 0, "empty quotes are empty");
		char small[4];
		HostTest::Expect(CleanOdfString("abcdefgh", small, sizeof(small)) == 3 && std::strcmp(small, "abc") == 0,
			"truncates to the buffer");
		HostTest::Expect(IsNoneName("none") && IsNoneName("NoNe") && !IsNoneName("nonexpl") && !IsNoneName(""),
			"none keyword");
	}

	void TestPack()
	{
		std::uint32_t packed[2] = {};
		HostTest::Expect(PackExplosionName("abcd", packed) && packed[0] == 0x64636261u && packed[1] == 0u,
			"four characters fill the low dword little-endian");
		HostTest::Expect(PackExplosionName("abcdefg", packed) && packed[0] == 0x64636261u && packed[1] == 0x00676665u,
			"seven characters zero pad the high dword");
		HostTest::Expect(PackExplosionName("abcdefghij", packed) && packed[1] == 0x68676665u, "cut to 8 characters");
		HostTest::Expect(!PackExplosionName("", packed), "empty name does not pack");
	}

	void TestOverride()
	{
		HostTest::Expect(ResolveIsPlant(true, -1), "no override keeps the ODF verdict (plant)");
		HostTest::Expect(!ResolveIsPlant(false, -1), "no override keeps the ODF verdict (not a plant)");
		HostTest::Expect(ResolveIsPlant(false, 1), "override on beats an ODF without isPlant");
		HostTest::Expect(!ResolveIsPlant(true, 0), "override off beats ODF isPlant");
	}
}

int main()
{
	TestNormalize();
	TestCleanString();
	TestPack();
	TestOverride();
	return HostTest::Finish("plant core");
}
