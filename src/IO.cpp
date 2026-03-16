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
#include "game_state.h"

namespace ExtraUtilities::Lua::IO
{
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
		lua_pushboolean(L, ExtraUtilities::GameState::IsPauseMenuOpen());
		return 1;
	}

	int GetPauseMenuDebugState(lua_State* L)
	{
		ExtraUtilities::GameState::PauseMenuDebugState state{};
		const bool ok = ExtraUtilities::GameState::TryGetPauseMenuDebugState(state);

		lua_newtable(L);

		lua_pushboolean(L, ok);
		lua_setfield(L, -2, "ok");

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
}
