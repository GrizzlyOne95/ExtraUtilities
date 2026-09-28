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

// ASCII case folding for engine names (file names, ODF/TRN keys, colour and
// alignment tokens). Only A-Z change: the result does not depend on the CRT
// locale, and bytes >= 0x80 pass through untouched.
namespace ExtraUtilities::AsciiString
{
	constexpr char ToLowerAscii(char ch) noexcept
	{
		return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
	}

	inline std::string ToLowerAscii(std::string_view value)
	{
		std::string lowered(value);
		for (char& ch : lowered)
		{
			ch = ToLowerAscii(ch);
		}
		return lowered;
	}

	constexpr bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept
	{
		if (a.size() != b.size())
		{
			return false;
		}

		for (size_t i = 0; i < a.size(); ++i)
		{
			if (ToLowerAscii(a[i]) != ToLowerAscii(b[i]))
			{
				return false;
			}
		}
		return true;
	}
}
