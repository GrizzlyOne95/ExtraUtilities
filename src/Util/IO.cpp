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
#include "Game/game_state.h"

namespace ExtraUtilities::Lua::IO
{
	namespace
	{
		bool IsGameWindowForeground() noexcept
		{
			DWORD processId = 0;
			const HWND foreground = GetForegroundWindow();
			return foreground != nullptr &&
				GetWindowThreadProcessId(foreground, &processId) != 0 &&
				processId == GetCurrentProcessId();
		}
	}

	int GetGameKey(lua_State* L)
	{
		const char* keyName = luaL_checkstring(L, 1);
		auto it = keyMap.find(ToUpper(keyName));

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

		// Held means the high bit. The low bit is "pressed since the last
		// query", which reported released keys as held. Keys typed into another
		// window while the game is in the background do not count.
		const bool held = IsGameWindowForeground() && (GetAsyncKeyState(vKey) & 0x8000) != 0;
		lua_pushboolean(L, held ? 1 : 0);

		return 1;
	}

	int IsPauseMenuOpen(lua_State* L)
	{
		lua_pushboolean(L, ExtraUtilities::GameState::IsPauseMenuOpen());
		return 1;
	}

	int IsGameUiOpen(lua_State* L)
	{
		lua_pushboolean(L, ExtraUtilities::GameState::IsGameUiOpen());
		return 1;
	}

	int GetPauseMenuDebugState(lua_State* L)
	{
		ExtraUtilities::GameState::PauseMenuDebugState state{};
		const bool ok = ExtraUtilities::GameState::TryGetPauseMenuDebugState(state);

		lua_newtable(L);

		lua_pushboolean(L, ok);
		lua_setfield(L, -2, "ok");

		lua_pushboolean(L, state.gameUiOpen);
		lua_setfield(L, -2, "gameUiOpen");

		lua_pushboolean(L, state.pauseMenuOpen);
		lua_setfield(L, -2, "pauseOpen");

		lua_pushboolean(L, state.singleplayerPauseOpen);
		lua_setfield(L, -2, "singleplayerPauseOpen");

		lua_pushboolean(L, state.multiplayerPauseOpen);
		lua_setfield(L, -2, "multiplayerPauseOpen");

		lua_pushboolean(L, state.cursorVisible);
		lua_setfield(L, -2, "cursorVisible");

		lua_pushboolean(L, state.currentScreenMatchesPauseRoot);
		lua_setfield(L, -2, "currentScreenMatchesPauseRoot");

		lua_pushinteger(L, static_cast<lua_Integer>(state.singleplayerPauseRoot));
		lua_setfield(L, -2, "singleplayerPauseRoot");

		lua_pushinteger(L, static_cast<lua_Integer>(state.multiplayerPauseRoot));
		lua_setfield(L, -2, "multiplayerPauseRoot");

		lua_pushinteger(L, static_cast<lua_Integer>(state.uiCurrentScreen));
		lua_setfield(L, -2, "uiCurrentScreen");

		lua_pushinteger(L, static_cast<lua_Integer>(state.escapeUiWrapperActive));
		lua_setfield(L, -2, "escapeUiWrapperActive");

		lua_pushinteger(L, static_cast<lua_Integer>(state.uiWrapperActive));
		lua_setfield(L, -2, "uiWrapperActive");

		lua_pushinteger(L, static_cast<lua_Integer>(state.uiCurrentScreenType));
		lua_setfield(L, -2, "uiCurrentScreenType");

		lua_pushstring(L, ExtraUtilities::GameState::DescribeScreenType(state.uiCurrentScreenType));
		lua_setfield(L, -2, "uiCurrentScreenTypeName");

		lua_pushinteger(L, static_cast<lua_Integer>(state.multiplayerPauseFlag));
		lua_setfield(L, -2, "multiplayerPauseFlag");

		return 1;
	}

	int IsTextEntryActive(lua_State* L)
	{
		lua_pushboolean(L, ExtraUtilities::GameState::IsTextEntryActive());
		return 1;
	}

	int IsAllyPromptOpen(lua_State* L)
	{
		lua_pushboolean(L, ExtraUtilities::GameState::IsAllyPromptOpen());
		return 1;
	}

	int GetTextEntryDebugState(lua_State* L)
	{
		ExtraUtilities::GameState::TextEntryDebugState state{};
		const bool ok = ExtraUtilities::GameState::TryGetTextEntryDebugState(state);
		lua_newtable(L);
		lua_pushboolean(L, ok);
		lua_setfield(L, -2, "ok");
		lua_pushboolean(L, state.textEntryActive);
		lua_setfield(L, -2, "textEntryActive");
		lua_pushboolean(L, state.chatOpen);
		lua_setfield(L, -2, "chatOpen");
		lua_pushboolean(L, state.allyPromptOpen);
		lua_setfield(L, -2, "allyPromptOpen");
		lua_pushinteger(L, static_cast<lua_Integer>(state.focusedNode));
		lua_setfield(L, -2, "focusedNode");
		lua_pushinteger(L, static_cast<lua_Integer>(state.focusedEntry));
		lua_setfield(L, -2, "focusedEntry");
		lua_pushinteger(L, static_cast<lua_Integer>(state.chatNode));
		lua_setfield(L, -2, "chatNode");
		lua_pushinteger(L, static_cast<lua_Integer>(state.allyNode));
		lua_setfield(L, -2, "allyNode");
		lua_pushinteger(L, static_cast<lua_Integer>(state.entryFlags));
		lua_setfield(L, -2, "entryFlags");
		return 1;
	}
}
