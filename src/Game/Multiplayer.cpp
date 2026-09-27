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

#include "Multiplayer.h"

#include "LuaHelpers.h"

#include <lua.hpp>

namespace ExtraUtilities::Lua::Multiplayer
{
	namespace
	{
		// Run stock BuildObject with a temporary patch active. The call must be
		// protected: an error would longjmp past Unload() and leave the patch
		// on for the rest of the mission, forcing every later BuildObject
		// async or sync.
		int BuildObjectWithPatch(lua_State* L, InlinePatch& patch)
		{
			const int argC = lua_gettop(L);

			lua_getglobal(L, "BuildObject");
			lua_insert(L, 1);

			patch.Reload();
			const int status = lua_pcall(L, argC, 1, 0);
			patch.Unload();

			if (status != 0)
			{
				return lua_error(L);
			}
			return 1;
		}
	}

	int BuildAsyncObject(lua_State* L)
	{
		return BuildObjectWithPatch(L, buildObjectAlwaysAsync);
	}

	int BuildSyncObject(lua_State* L)
	{
		return BuildObjectWithPatch(L, buildObjectAlwaysSync);
	}

	int GetLives(lua_State* L)
	{
		lua_pushinteger(L, lives.Read());
		return 1;
	}

	int SetLives(lua_State* L)
	{
		int newLives = luaL_checkinteger(L, 1);
		lives.Write(newLives);

		// This will crash if done in singleplayer,
		// not like lives do anything in SP anyways tho
		if (isNetGame.Read() == true)
		{
			BZR::Multiplayer::UpdateLives();
		}

		return 0;
	}

	int GetMyNetID(lua_State* L)
	{
		lua_pushinteger(L, myNetID.Read());
		return 1;
	}

	int GetShowScoreboard(lua_State* L)
	{
		lua_pushboolean(L, showScoreboard.Read());
		return 1;
	}

	int SetShowScoreboard(lua_State* L)
	{
		bool status = CheckBool(L, 1);
		showScoreboard.Write(status);
		return 0;
	}

	int DisableStartingRecycler(lua_State*)
	{
		skipStartingRecycler.Reload();
		return 0;
	}
}