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

#include "PlayOption.h"

#include "LuaHelpers.h"

namespace ExtraUtilities::Lua::PlayOption
{
	namespace
	{
		constexpr uint8_t kAutoLevelBit = 1u << 4;
		constexpr uint8_t kTliBit = 1u << 5;
		constexpr uint8_t kReverseMouseBit = 1u << 6;

		// Bits a script changed, and their values before the first change.
		uint8_t g_touchedBits = 0;
		uint8_t g_originalBits = 0;

		void SetPlayOptionBit(uint8_t bit, bool enabled)
		{
			const uint8_t current = playOption.Read();
			if ((g_touchedBits & bit) == 0)
			{
				g_touchedBits |= bit;
				g_originalBits = static_cast<uint8_t>((g_originalBits & ~bit) | (current & bit));
			}

			const uint8_t updated = enabled
				? static_cast<uint8_t>(current | bit)
				: static_cast<uint8_t>(current & ~bit);
			if (updated != current)
			{
				playOption.Write(updated);
			}
		}

		bool GetPlayOptionBit(uint8_t bit)
		{
			return (playOption.Read() & bit) != 0;
		}
	}

	void RestoreScriptChanges() noexcept
	{
		if (g_touchedBits == 0)
		{
			return;
		}

		const uint8_t current = playOption.Read();
		const uint8_t restored = static_cast<uint8_t>((current & ~g_touchedBits) | (g_originalBits & g_touchedBits));
		if (restored != current)
		{
			playOption.Write(restored);
		}
		g_touchedBits = 0;
		g_originalBits = 0;
	}

	int GetAutoLevel(lua_State* L)
	{
		lua_pushboolean(L, GetPlayOptionBit(kAutoLevelBit));
		return 1;
	}

	int SetAutoLevel(lua_State* L)
	{
		SetPlayOptionBit(kAutoLevelBit, CheckBool(L, 1));
		return 0;
	}

	int GetDifficulty(lua_State* L)
	{
		lua_pushnumber(L, difficulty.Read());
		return 1;
	}

	int SetDifficulty(lua_State* L)
	{
		// 0 = Very Easy .. 4 = Very Hard (exu.json PlayOption.difficulty).
		const lua_Integer requested = luaL_checkinteger(L, 1);
		luaL_argcheck(L, requested >= 0 && requested <= 4, 1, "difficulty must be 0-4");
		difficulty.Write(static_cast<uint8_t>(requested));
		return 0;
	}

	int GetTLI(lua_State* L)
	{
		lua_pushboolean(L, GetPlayOptionBit(kTliBit));
		return 1;
	}

	int SetTLI(lua_State* L)
	{
		SetPlayOptionBit(kTliBit, CheckBool(L, 1));
		return 0;
	}

	int GetReverseMouse(lua_State* L)
	{
		lua_pushboolean(L, GetPlayOptionBit(kReverseMouseBit));
		return 1;
	}

	int SetReverseMouse(lua_State* L)
	{
		SetPlayOptionBit(kReverseMouseBit, CheckBool(L, 1));
		return 0;
	}
}
