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
		constexpr uintptr_t kSingleplayerPauseRootAddr = 0x009454EC;
		constexpr uintptr_t kUiWrapperActiveAddr = 0x00918324;
		constexpr uintptr_t kUiCurrentScreenAddr = 0x00918320;
		constexpr uintptr_t kUiCurrentScreenTypeAddr = 0x00918328;

		constexpr uint32_t kPauseScreenType = 0x0B;
		constexpr uint32_t kOptionsScreenType = 0x03;
		constexpr uint32_t kSaveGameScreenType = 0x11;
		constexpr uint32_t kLoadGameScreenType = 0x12;
		constexpr uint32_t kRestartScreenType = 0x17;

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

		bool IsSingleplayerPauseMenuOpen() noexcept
		{
			__try
			{
				const auto* pauseRoot = reinterpret_cast<void* const*>(kSingleplayerPauseRootAddr);
				const auto* uiWrapperActive = reinterpret_cast<const uint32_t*>(kUiWrapperActiveAddr);
				const auto* uiCurrentScreen = reinterpret_cast<void* const*>(kUiCurrentScreenAddr);
				const auto* uiCurrentScreenType = reinterpret_cast<const uint32_t*>(kUiCurrentScreenTypeAddr);

				if (*pauseRoot == nullptr || *uiWrapperActive == 0)
				{
					return false;
				}

				if (*uiCurrentScreen == *pauseRoot)
				{
					return true;
				}

				switch (*uiCurrentScreenType)
				{
				case kPauseScreenType:
				case kOptionsScreenType:
				case kSaveGameScreenType:
				case kLoadGameScreenType:
				case kRestartScreenType:
					return true;
				default:
					return false;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool IsPauseMenuOpenInternal() noexcept
		{
			if (IsMultiplayerPauseMenuOpen())
			{
				return true;
			}

			return IsSingleplayerPauseMenuOpen();
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
