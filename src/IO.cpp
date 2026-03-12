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

#include "IO.h"

namespace ExtraUtilities::Lua::IO
{
	namespace
	{
		constexpr uintptr_t kMultiplayerPauseFlagAddr = 0x00945549;
		constexpr uintptr_t kMultiplayerPauseRootAddr = 0x0094557C;

		bool IsCursorVisible() noexcept
		{
			CURSORINFO info{};
			info.cbSize = sizeof(info);
			return GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING) != 0;
		}

		bool IsMultiplayerPauseMenuOpen() noexcept
		{
			__try
			{
				const auto* root = reinterpret_cast<void* const*>(kMultiplayerPauseRootAddr);
				const auto* flag = reinterpret_cast<const uint8_t*>(kMultiplayerPauseFlagAddr);
				return (*root != nullptr || *flag != 0) && IsCursorVisible();
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool IsPauseMenuOpenInternal() noexcept
		{
			static bool s_pauseMenuLatched = false;
			static bool s_lastEscapeDown = false;
			static int s_hiddenCursorFrames = 0;

			const bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
			if (escapeDown && !s_lastEscapeDown)
			{
				s_pauseMenuLatched = !s_pauseMenuLatched;
			}
			s_lastEscapeDown = escapeDown;

			if (IsMultiplayerPauseMenuOpen())
			{
				s_hiddenCursorFrames = 0;
				return true;
			}

			if (!s_pauseMenuLatched)
			{
				s_hiddenCursorFrames = 0;
				return false;
			}

			if (!escapeDown && !IsCursorVisible())
			{
				if (++s_hiddenCursorFrames >= 8)
				{
					s_pauseMenuLatched = false;
					s_hiddenCursorFrames = 0;
				}
			}
			else
			{
				s_hiddenCursorFrames = 0;
			}

			return s_pauseMenuLatched;
		}
	}

	int GetGameKey(lua_State* L)
	{
		std::string key = luaL_checkstring(L, 1);
		
		auto it = keyMap.find(ToUpper(key));

		int vKey;

		if (it != keyMap.end())
		{
			vKey = it->second;
		}
		else
		{
			luaL_argerror(L, 1, "Extra Utilities Error: invalid key - see wiki");
			return 0;
		}

		// this ternary is necessary cause you need to evaluate the
		// truthiness in C++ due to the weird return value of GetAsyncKeyState
		lua_pushboolean(L, GetAsyncKeyState(vKey) ? true : false);

		return 1;
	}

	int IsPauseMenuOpen(lua_State* L)
	{
		lua_pushboolean(L, IsPauseMenuOpenInternal());
		return 1;
	}
}
