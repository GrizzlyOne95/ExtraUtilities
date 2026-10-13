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

#pragma once

// Engine-free rules of the plant feature (exu.SetPlant, ODF [PlantClass]).
// Shared with the host tests: name normalisation, ODF value cleaning,
// explosion-name packing for ExplosionClass::Find and override precedence.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ExtraUtilities::PlantCore
{
	// The engine's class ODF base name holds at most 8 characters (GetOdf), and
	// ExplosionClass::Find takes at most 8.
	constexpr std::size_t kOdfNameMax = 8;
	constexpr std::size_t kExplosionNameMax = 8;

	// What the ODF [PlantClass] section says. isPlant is kept apart from the
	// other keys so a Lua override can replace the verdict and still use them.
	struct PlantParams
	{
		bool isPlant = false;
		bool hitNone = false;                            // hitExplosion = "none"
		char hitExplosion[kExplosionNameMax + 1] = {};   // empty: the ordnance's ground hit
		char explosionName[kExplosionNameMax + 1] = {};  // empty: the ODF's own explosion
		bool dropScrap = false;
	};

	constexpr char LowerAscii(char c) noexcept
	{
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	}

	// Key for the ODF cache and the override table: a trailing ".odf" is
	// dropped (any case), the name is cut to the engine's 8 characters and
	// lower-cased. Stops at an embedded NUL, so a raw class-name buffer that
	// is not terminated can be passed with its 8-byte length. Writes a
	// NUL-terminated key into out (kOdfNameMax + 1 bytes) and returns its
	// length; allocation-free, so Lua bindings can use it before raising errors.
	inline std::size_t NormalizeOdfNameInto(std::string_view name, char* out) noexcept
	{
		const std::size_t nul = name.find('\0');
		if (nul != std::string_view::npos)
			name = name.substr(0, nul);
		if (name.size() >= 4)
		{
			const std::string_view tail = name.substr(name.size() - 4);
			if (tail[0] == '.' && LowerAscii(tail[1]) == 'o' && LowerAscii(tail[2]) == 'd' && LowerAscii(tail[3]) == 'f')
				name = name.substr(0, name.size() - 4);
		}
		if (name.size() > kOdfNameMax)
			name = name.substr(0, kOdfNameMax);
		for (std::size_t i = 0; i < name.size(); ++i)
			out[i] = LowerAscii(name[i]);
		out[name.size()] = '\0';
		return name.size();
	}

	inline std::string NormalizeOdfName(std::string_view name)
	{
		char buffer[kOdfNameMax + 1];
		const std::size_t length = NormalizeOdfNameInto(name, buffer);
		return std::string(buffer, length);
	}

	constexpr bool IsSpace(char c) noexcept
	{
		return c == ' ' || c == '\t' || c == '\r' || c == '\n';
	}

	inline std::string_view TrimSpace(std::string_view v) noexcept
	{
		while (!v.empty() && IsSpace(v.front()))
			v.remove_prefix(1);
		while (!v.empty() && IsSpace(v.back()))
			v.remove_suffix(1);
		return v;
	}

	// A raw ODF string value: surrounding whitespace and one pair of double
	// quotes are removed. Writes at most cap - 1 characters plus a NUL and
	// returns the length. null -> empty.
	inline std::size_t CleanOdfString(const char* raw, char* out, std::size_t cap) noexcept
	{
		if (cap == 0)
			return 0;
		out[0] = '\0';
		if (raw == nullptr)
			return 0;
		std::string_view v = TrimSpace(std::string_view(raw));
		if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
			v = TrimSpace(v.substr(1, v.size() - 2));
		const std::size_t n = v.size() < cap - 1 ? v.size() : cap - 1;
		for (std::size_t i = 0; i < n; ++i)
			out[i] = v[i];
		out[n] = '\0';
		return n;
	}

	inline bool IsNoneName(std::string_view name) noexcept
	{
		return name.size() == 4 && LowerAscii(name[0]) == 'n' && LowerAscii(name[1]) == 'o' &&
			LowerAscii(name[2]) == 'n' && LowerAscii(name[3]) == 'e';
	}

	// The name packed little-endian into two dwords, zero padded, the form
	// ExplosionClass::Find takes (it applies its own 0x7F mask). Names longer
	// than 8 characters are cut. false for an empty name.
	inline bool PackExplosionName(std::string_view name, std::uint32_t (&out)[2]) noexcept
	{
		if (name.empty())
			return false;
		if (name.size() > kExplosionNameMax)
			name = name.substr(0, kExplosionNameMax);
		out[0] = 0;
		out[1] = 0;
		for (std::size_t i = 0; i < name.size(); ++i)
			out[i / 4] |= static_cast<std::uint32_t>(static_cast<unsigned char>(name[i])) << (8 * (i % 4));
		return true;
	}

	// Lua override precedence: overrideState -1 = none, 0 = forced off, 1 = forced on.
	inline bool ResolveIsPlant(bool fromOdf, int overrideState) noexcept
	{
		if (overrideState < 0)
			return fromOdf;
		return overrideState != 0;
	}
}
