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

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Where exu.SaveGame may write. A mission script chooses the path, so it must
// not be able to overwrite files outside the game's Save directory. The checks
// work on Win32 path strings directly (both separators, drive letters, ASCII
// case folding) rather than std::filesystem, so they behave the same when
// tests/host compiles them on Linux as they do in the game under Windows,
// Proton or Wine.
namespace ExtraUtilities::NativeSave
{
	inline bool IsPathSeparator(char c) noexcept
	{
		return c == '\\' || c == '/';
	}

	inline char FoldAscii(char c) noexcept
	{
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	}

	inline bool EqualsFolded(std::string_view a, std::string_view b) noexcept
	{
		if (a.size() != b.size())
		{
			return false;
		}
		for (std::size_t i = 0; i < a.size(); ++i)
		{
			if (FoldAscii(a[i]) != FoldAscii(b[i]))
			{
				return false;
			}
		}
		return true;
	}

	// CON, NUL, COM1 and friends open devices whatever directory or extension
	// they are given.
	inline bool IsReservedDeviceName(std::string_view component) noexcept
	{
		const std::string_view stem = component.substr(0, component.find('.'));
		for (const std::string_view name : { "con", "prn", "aux", "nul" })
		{
			if (EqualsFolded(stem, name))
			{
				return true;
			}
		}
		if (stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9' &&
			(EqualsFolded(stem.substr(0, 3), "com") || EqualsFolded(stem.substr(0, 3), "lpt")))
		{
			return true;
		}
		return false;
	}

	inline bool HasDrivePrefix(std::string_view path) noexcept
	{
		return path.size() >= 2 && path[1] == ':' &&
			((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'));
	}

	// Splits an absolute "X:\a\b" path into its drive and normalised
	// components, resolving "." and "..". Returns false for anything that is
	// not a plain absolute drive path or that climbs above the drive root.
	inline bool NormalizeAbsolute(std::string_view path, std::string& drive, std::vector<std::string>& components)
	{
		drive.clear();
		components.clear();
		if (!HasDrivePrefix(path) || path.size() < 3 || !IsPathSeparator(path[2]))
		{
			return false;
		}

		drive.assign(path.substr(0, 2));
		std::size_t position = 3;
		while (position <= path.size())
		{
			std::size_t end = position;
			while (end < path.size() && !IsPathSeparator(path[end]))
			{
				++end;
			}

			const std::string_view component = path.substr(position, end - position);
			if (component.empty() || component == ".")
			{
				// Repeated separators and "." do not change the location.
			}
			else if (component == "..")
			{
				if (components.empty())
				{
					return false;
				}
				components.pop_back();
			}
			else
			{
				// Win32 strips trailing dots and spaces, so "Save." names Save;
				// ':' outside the drive prefix selects an alternate data stream.
				if (component.back() == '.' || component.back() == ' ' ||
					component.find(':') != std::string_view::npos ||
					IsReservedDeviceName(component))
				{
					return false;
				}
				components.emplace_back(component);
			}

			position = end + 1;
		}

		return true;
	}

	inline std::string JoinAbsolute(const std::string& drive, const std::vector<std::string>& components)
	{
		std::string result = drive;
		result += '\\';
		for (std::size_t i = 0; i < components.size(); ++i)
		{
			if (i != 0)
			{
				result += '\\';
			}
			result += components[i];
		}
		return result;
	}

	struct SavePathResult
	{
		bool ok = false;
		std::string path;
		const char* error = nullptr;
	};

	// Resolves a script-supplied save path. Relative paths are taken from
	// `resolveRoot`; the result must name a file inside "<root>\Save" for one
	// of `allowedRoots` (the game root, which the engine's own saves use).
	inline SavePathResult ResolveSavePath(
		std::string_view requested,
		std::string_view resolveRoot,
		const std::vector<std::string>& allowedRoots)
	{
		SavePathResult result;
		if (requested.empty())
		{
			result.error = "save path is empty";
			return result;
		}
		if (requested.find('\0') != std::string_view::npos)
		{
			result.error = "save path contains a NUL byte";
			return result;
		}
		// "\\server\share", "\\?\..." and "\\.\device" bypass normal Win32 path
		// handling, and a single leading separator or "X:name" depends on
		// process state (the current drive or that drive's current directory).
		if (IsPathSeparator(requested[0]))
		{
			result.error = "save path must be relative to the game directory or an absolute drive path";
			return result;
		}
		if (HasDrivePrefix(requested) && (requested.size() < 3 || !IsPathSeparator(requested[2])))
		{
			result.error = "drive-relative save paths are not allowed";
			return result;
		}

		std::string absolute;
		if (HasDrivePrefix(requested))
		{
			absolute.assign(requested);
		}
		else
		{
			if (resolveRoot.empty())
			{
				result.error = "game directory could not be determined";
				return result;
			}
			absolute.assign(resolveRoot);
			absolute += '\\';
			absolute += requested;
		}

		std::string drive;
		std::vector<std::string> components;
		if (!NormalizeAbsolute(absolute, drive, components))
		{
			result.error = "save path is not a valid file path";
			return result;
		}

		for (const auto& root : allowedRoots)
		{
			std::string rootDrive;
			std::vector<std::string> rootComponents;
			if (root.empty() || !NormalizeAbsolute(root, rootDrive, rootComponents))
			{
				continue;
			}
			rootComponents.emplace_back("Save");

			// Strictly inside: the Save directory itself is not a file.
			if (!EqualsFolded(drive, rootDrive) || components.size() <= rootComponents.size())
			{
				continue;
			}

			bool inside = true;
			for (std::size_t i = 0; i < rootComponents.size() && inside; ++i)
			{
				inside = EqualsFolded(components[i], rootComponents[i]);
			}
			if (inside)
			{
				result.ok = true;
				result.path = JoinAbsolute(drive, components);
				return result;
			}
		}

		result.error = "save path must be inside the game's Save directory";
		return result;
	}
}
