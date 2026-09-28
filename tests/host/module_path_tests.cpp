/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

// Host-side checks for the directory split behind the game-root and exu.dll
// directories (save paths, TRN lookup, overlay font assets).

#include "Util/ModulePath.h"
#include "HostTest.h"

using ExtraUtilities::ModulePath::DirectoryOf;
using HostTest::Expect;

namespace
{
	void TestDirectoryOf()
	{
		Expect(DirectoryOf("C:\\Games\\BZR\\Battlezone98Redux.exe") == "C:\\Games\\BZR",
			"a backslash path loses its file name and separator");
		Expect(DirectoryOf("Z:/home/user/bzr/Battlezone98Redux.exe") == "Z:/home/user/bzr",
			"forward slashes (Wine) split the same way");
		Expect(DirectoryOf("C:\\Games/BZR\\mods/exu.dll") == "C:\\Games/BZR\\mods",
			"mixed separators split at the last one");
		Expect(DirectoryOf("C:\\Battlezone98Redux.exe") == "C:\\",
			"a drive root keeps its separator");
		Expect(DirectoryOf("\\\\server\\share\\bzr.exe") == "\\\\server\\share",
			"a UNC path splits normally");
		Expect(DirectoryOf("Battlezone98Redux.exe").empty(), "a bare file name has no directory");
		Expect(DirectoryOf("").empty(), "an empty path has no directory");
	}
}

int main()
{
	TestDirectoryOf();
	return HostTest::Finish("module-path");
}
