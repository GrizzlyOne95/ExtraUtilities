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

// Ogre's StringInterface is a *text* interface: every typed emitter/affector
// property is parsed out of a string by a ParamCommand. Lua hands us numbers,
// booleans and strings, so everything has to be rendered into the exact text
// Ogre::StringConverter expects before it crosses the DLL boundary.
//
// This header is deliberately free of <Windows.h>, Ogre and Lua so the
// conversion and validation rules can be unit tested on a host that has no
// game, no OgreMain.dll and no Lua state. See tests/host/parameter_value_tests.cpp.

#include <cmath>
#include <cstddef>
#include <string>

namespace ExtraUtilities::OgreParams
{
	// Ogre parameter names are short identifiers ("width", "force_vector").
	// Values are short too - the longest stock ParticleFX value is a colour
	// quadruple. These caps exist so a hostile or buggy mission script cannot
	// push an unbounded allocation through the Ogre ABI; they are generous
	// enough that no legitimate stock parameter comes close.
	inline constexpr std::size_t kMaxParameterNameLength = 128;
	inline constexpr std::size_t kMaxParameterValueLength = 1024;

	// Ogre::StringConverter::parseBool accepts "true"/"yes"/"1" and
	// "false"/"no"/"0". Emit the canonical spelling.
	inline std::string BoolToParameterValue(bool value)
	{
		return value ? "true" : "false";
	}

	// Ogre::StringConverter::parseReal uses a std::istringstream with the
	// classic locale, so it wants a plain decimal with a '.' separator and no
	// thousands grouping, exponent form, or "inf"/"nan" spelling.
	//
	// Lua numbers are doubles and mission code routinely passes integral
	// values (quotas, counts, 120 for a box width). Printing those as "120"
	// rather than "120.000000" keeps the round-trip through getParameter
	// readable and matches what a hand-authored .particle script contains.
	inline std::string NumberToParameterValue(double value)
	{
		if (!std::isfinite(value))
		{
			return std::string();
		}

		// Integral and small enough that the exact value survives the cast.
		if (value == std::floor(value) && std::fabs(value) < 1.0e15)
		{
			const long long integral = static_cast<long long>(value);
			return std::to_string(integral);
		}

		// std::to_string(double) is fixed-format with 6 fraction digits, which
		// is what Ogre's own StringConverter::toString default produces. Trim
		// the trailing zeros so "0.450000" reads back as "0.45".
		std::string text = std::to_string(value);
		const std::size_t dot = text.find('.');
		if (dot != std::string::npos)
		{
			std::size_t lastSignificant = text.find_last_not_of('0');
			if (lastSignificant == dot)
			{
				// Everything after the dot was zero; drop the dot too.
				--lastSignificant;
			}
			text.erase(lastSignificant + 1);
		}

		return text;
	}

	// A parameter name has to be a plain identifier. Rejecting anything else
	// keeps the lookup inside the ParamDictionary Ogre already built and stops
	// whitespace/control characters reaching a ParamCommand parser.
	inline bool IsValidParameterName(const std::string& name)
	{
		if (name.empty() || name.size() > kMaxParameterNameLength)
		{
			return false;
		}

		for (const char c : name)
		{
			const bool allowed =
				(c >= 'a' && c <= 'z') ||
				(c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') ||
				c == '_';
			if (!allowed)
			{
				return false;
			}
		}

		return true;
	}

	// Values are free-form text (vectors and colours are space separated), but
	// embedded NULs would truncate whatever Ogre does with the string and
	// control characters have no meaning to any stock ParamCommand.
	inline bool IsValidParameterValue(const std::string& value)
	{
		if (value.size() > kMaxParameterValueLength)
		{
			return false;
		}

		for (const char c : value)
		{
			// Space is the separator inside "1 1 1 0.75"; everything below it
			// is a control character.
			if (static_cast<unsigned char>(c) < 0x20)
			{
				return false;
			}
		}

		return true;
	}
}
