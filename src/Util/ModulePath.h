/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#pragma once

#include <string>
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#endif

// Directories of loaded modules: the game root (the executable's directory)
// and exu.dll's own. DirectoryOf is the pure part tests/host checks.
namespace ExtraUtilities::ModulePath
{
	// Everything before the last '\' or '/', or "" when there is none. A
	// drive root keeps its separator ("C:\bzr.exe" gives "C:\", not the
	// drive-relative "C:").
	inline std::string DirectoryOf(std::string_view path)
	{
		const size_t slash = path.find_last_of("\\/");
		if (slash == std::string_view::npos)
		{
			return {};
		}

		const bool driveRoot = slash == 2 && path[1] == ':';
		return std::string(path.substr(0, driveRoot ? slash + 1 : slash));
	}

#ifdef _WIN32
	// Directory of `module`'s file, or "" on failure. A null module is the
	// executable, as for GetModuleFileNameA.
	inline std::string GetModuleDirectory(HMODULE module)
	{
		char path[MAX_PATH]{};
		const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
		if (length == 0 || length >= MAX_PATH)
		{
			return {};
		}

		return DirectoryOf(std::string_view(path, length));
	}

	inline std::string GetGameRootDirectory()
	{
		return GetModuleDirectory(nullptr);
	}
#endif
}
