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

// Host-side checks for where exu.SaveGame may write. A mission script picks the
// path, so everything outside the game's Save directory must be refused.

#include "Util/SavePathPolicy.h"
#include "HostTest.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace ExtraUtilities::NativeSave;

namespace
{
	using HostTest::Expect;

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

	// The engine's save directory, as its startup routine builds it:
	// _getcwd() + "\save".
	const std::string kEngineSave = kGame + "\\save";

	void ExpectSlot(std::string_view directory, int slot, const std::string& expected)
	{
		const auto result = BuildSlotSavePath(directory, slot);
		Expect(result.ok, "slot " + std::to_string(slot) + " in " + std::string(directory) + " is accepted");
		Expect(result.path == expected, "slot path " + result.path + ", expected " + expected);
	}

	void ExpectSlotRejected(std::string_view directory, int slot, const std::string& label)
	{
		const auto result = BuildSlotSavePath(directory, slot);
		Expect(!result.ok && result.error != nullptr && result.path.empty(), "slot path rejected: " + label);
	}

	void TestSlotPathsComeFromTheEngineSaveDirectory()
	{
		ExpectSlot(kEngineSave, 1, kEngineSave + "\\game1.sav");
		ExpectSlot(kEngineSave, 10, kEngineSave + "\\game10.sav");
		// Launched from somewhere other than the exe directory: the engine
		// saves under its startup directory, and so does EXU.
		ExpectSlot("D:\\Launch\\save", 3, "D:\\Launch\\save\\game3.sav");
		// Wine/Proton separators and a trailing separator normalise the same way.
		ExpectSlot("Z:/home/user/bzr/save/", 4, "Z:\\home\\user\\bzr\\save\\game4.sav");
		ExpectSlot("C:\\Games\\BZ\\.\\x\\..\\save", 5, "C:\\Games\\BZ\\save\\game5.sav");
	}

	void TestSlotPathsFailClosed()
	{
		ExpectSlotRejected("", 1, "empty directory");
		ExpectSlotRejected(kEngineSave, 0, "slot 0");
		ExpectSlotRejected(kEngineSave, 11, "slot 11");
		ExpectSlotRejected(kEngineSave, -1, "negative slot");
		ExpectSlotRejected("save", 1, "relative directory");
		ExpectSlotRejected("\\save", 1, "rooted directory without a drive");
		ExpectSlotRejected("C:save", 1, "drive-relative directory");
		ExpectSlotRejected("\\\\server\\share\\save", 1, "UNC directory");
		ExpectSlotRejected("\\\\?\\C:\\Games\\save", 1, "device-namespace directory");
		ExpectSlotRejected("C:\\", 1, "drive root");
		ExpectSlotRejected("C:\\..\\save", 1, "directory above the drive root");
		ExpectSlotRejected("C:\\Games\\con\\save", 1, "reserved device name");
		ExpectSlotRejected("C:\\Games\\save:stream", 1, "alternate data stream");
		ExpectSlotRejected(std::string_view("C:\\Games\0\\save", 14), 1, "embedded NUL");
	}

	void TestEngineBufferMustBeTerminated()
	{
		const char buffer[16] = "C:\\BZ\\save";
		Expect(TerminatedString(buffer, sizeof(buffer)) == "C:\\BZ\\save", "a terminated buffer reads to its NUL");

		const char unterminated[4] = { 'C', ':', '\\', 'x' };
		Expect(TerminatedString(unterminated, sizeof(unterminated)).empty(), "an unterminated buffer reads as empty");
		Expect(TerminatedString(nullptr, 16).empty(), "a null buffer reads as empty");

		const char unset[4] = {};
		Expect(TerminatedString(unset, sizeof(unset)).empty(), "an unset buffer reads as empty");
		ExpectSlotRejected(TerminatedString(unset, sizeof(unset)), 1, "unset engine buffer");
		Expect(ENGINE_SAVE_DIRECTORY_CAPACITY == 0x1000, "engine save directory capacity matches the catalog");
	}

	void TestEngineSaveDirectoryIsAnAllowedDirectory()
	{
		// Script paths may also land in the engine's own save directory when it
		// is neither "<exe dir>\Save" nor "<cwd>\Save".
		const std::vector<std::string> directories{ kGame + "\\Save", "D:\\Launch\\save" };
		const auto engine = ResolveSavePathInDirectories("D:\\Launch\\save\\x.sav", kGame, directories);
		Expect(engine.ok && engine.path == "D:\\Launch\\save\\x.sav", "the engine save directory is allowed");

		const auto outside = ResolveSavePathInDirectories("D:\\Launch\\x.sav", kGame, directories);
		Expect(!outside.ok, "the engine save directory's parent is not");

		const auto itself = ResolveSavePathInDirectories("D:\\Launch\\save", kGame, directories);
		Expect(!itself.ok, "the directory itself is not a file");

		const auto roots = SaveDirectoriesOf({ kGame, "", "relative", "C:" });
		Expect(roots.size() == 1 && roots[0] == kGame + "\\Save", "only absolute roots produce Save directories");
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
	TestSlotPathsComeFromTheEngineSaveDirectory();
	TestSlotPathsFailClosed();
	TestEngineBufferMustBeTerminated();
	TestEngineSaveDirectoryIsAnAllowedDirectory();

	return HostTest::Finish("save path");
}
