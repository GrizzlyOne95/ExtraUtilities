/* Copyright (C) 2023-2026 VTrider
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

// Host-side checks for where exu.SaveGame may write. A mission script picks the
// path, so everything outside the game's Save directory must be refused.

#include "Util/SavePathPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace ExtraUtilities::NativeSave;

namespace
{
	int g_failures = 0;

	void Expect(bool condition, const std::string& what)
	{
		if (!condition)
		{
			++g_failures;
			std::cerr << "FAIL: " << what << '\n';
		}
	}

	const std::string kGame = "C:\\Games\\Battlezone 98 Redux";
	const std::vector<std::string> kRoots{ kGame };

	void ExpectAccepted(const std::string& requested, const std::string& expected)
	{
		const auto result = ResolveSavePath(requested, kGame, kRoots);
		Expect(result.ok, "accepted: " + requested);
		Expect(result.path == expected, "resolved " + requested + " to " + result.path + ", expected " + expected);
	}

	void ExpectRejected(const std::string& requested)
	{
		const auto result = ResolveSavePath(requested, kGame, kRoots);
		Expect(!result.ok && result.error != nullptr, "rejected: " + requested);
	}

	void TestPathsInsideSaveAreAccepted()
	{
		ExpectAccepted("Save\\auto.sav", kGame + "\\Save\\auto.sav");
		ExpectAccepted("Save/auto.sav", kGame + "\\Save\\auto.sav");
		ExpectAccepted("save\\Campaign\\m01.sav", kGame + "\\save\\Campaign\\m01.sav");
		ExpectAccepted(".\\Save\\.\\auto.sav", kGame + "\\Save\\auto.sav");
		ExpectAccepted("Save\\sub\\..\\auto.sav", kGame + "\\Save\\auto.sav");
		// Campaign Reimagined passes an absolute path built from the working
		// directory, in whatever case the OS returned it.
		ExpectAccepted(kGame + "\\Save\\auto.sav", kGame + "\\Save\\auto.sav");
		ExpectAccepted("c:\\games\\BATTLEZONE 98 REDUX\\SAVE\\auto.sav", "c:\\games\\BATTLEZONE 98 REDUX\\SAVE\\auto.sav");
	}

	void TestPathsOutsideSaveAreRejected()
	{
		ExpectRejected("auto.sav");
		ExpectRejected("Save");
		ExpectRejected("Save\\");
		ExpectRejected("Save\\..\\auto.sav");
		ExpectRejected("Save\\..\\..\\auto.sav");
		ExpectRejected("Saves\\auto.sav");
		ExpectRejected("Addon\\Save\\auto.sav");
		ExpectRejected("battlezone98redux.exe");
		ExpectRejected("C:\\Windows\\System32\\drivers\\etc\\hosts");
		ExpectRejected("C:\\Games\\Battlezone 98 Redux Other\\Save\\x.sav");
		ExpectRejected("D:\\Games\\Battlezone 98 Redux\\Save\\x.sav");
		ExpectRejected("..\\..\\..\\..\\..\\..\\..\\..\\x.sav");
	}

	void TestAmbiguousOrSpecialPathsAreRejected()
	{
		ExpectRejected("");
		ExpectRejected(std::string("Save\\a\0.sav", 11));
		ExpectRejected("\\\\server\\share\\Save\\x.sav");
		ExpectRejected("\\\\?\\C:\\Games\\Battlezone 98 Redux\\Save\\x.sav");
		ExpectRejected("\\\\.\\PhysicalDrive0");
		ExpectRejected("\\Games\\Battlezone 98 Redux\\Save\\x.sav");
		ExpectRejected("C:Save\\x.sav");
		ExpectRejected("Save\\x.sav:stream");
		ExpectRejected("Save\\con.sav");
		ExpectRejected("Save\\NUL");
		ExpectRejected("Save\\com1.txt");
		ExpectRejected("Save\\lpt9");
		ExpectRejected("Save.\\x.sav");
		ExpectRejected("Save \\x.sav");
	}

	void TestReservedNameCheckIsExact()
	{
		ExpectAccepted("Save\\console.sav", kGame + "\\Save\\console.sav");
		ExpectAccepted("Save\\com10.sav", kGame + "\\Save\\com10.sav");
		ExpectAccepted("Save\\com0.sav", kGame + "\\Save\\com0.sav");
	}

	void TestEitherRootIsAllowed()
	{
		const std::vector<std::string> roots{ kGame, "D:\\Launch" };
		const auto working = ResolveSavePath("D:\\Launch\\Save\\x.sav", kGame, roots);
		Expect(working.ok && working.path == "D:\\Launch\\Save\\x.sav", "the working-directory root is allowed");

		// Relative paths always resolve against the game root.
		const auto relative = ResolveSavePath("Save\\x.sav", kGame, roots);
		Expect(relative.ok && relative.path == kGame + "\\Save\\x.sav", "relative paths use the game root");
	}

	void TestMissingRootFailsClosed()
	{
		const auto noRoot = ResolveSavePath("Save\\x.sav", "", {});
		Expect(!noRoot.ok, "a relative path with no game root is rejected");

		const auto noAllowed = ResolveSavePath(kGame + "\\Save\\x.sav", kGame, {});
		Expect(!noAllowed.ok, "no allowed roots means nothing is writable");

		const auto badRoot = ResolveSavePath("C:\\Save\\x.sav", kGame, { "relative\\root" });
		Expect(!badRoot.ok, "a non-absolute allowed root is ignored");
	}

	void TestForwardSlashRootsWork()
	{
		// Wine and Proton report Win32 paths, but tolerate either separator.
		const std::vector<std::string> roots{ "Z:/home/user/bzr" };
		const auto result = ResolveSavePath("Save/x.sav", "Z:/home/user/bzr", roots);
		Expect(result.ok && result.path == "Z:\\home\\user\\bzr\\Save\\x.sav", "forward-slash roots resolve");
	}
}

int main()
{
	TestPathsInsideSaveAreAccepted();
	TestPathsOutsideSaveAreRejected();
	TestAmbiguousOrSpecialPathsAreRejected();
	TestReservedNameCheckIsExact();
	TestEitherRootIsAllowed();
	TestMissingRootFailsClosed();
	TestForwardSlashRootsWork();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " save path check(s) failed\n";
		return EXIT_FAILURE;
	}

	std::cout << "All save path checks passed.\n";
	return EXIT_SUCCESS;
}
