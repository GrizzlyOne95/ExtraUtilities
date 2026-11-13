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

#include "Game/LaserSight.h"

#include "OpenShimBridge.h"

#include <lua.hpp>

namespace ExtraUtilities::LaserSight
{
	namespace
	{
		using SetFn = int(__cdecl*)(int);
		using GetFn = int(__cdecl*)();

		FARPROC Resolve(const char* name) noexcept
		{
			if (!OpenShimBridge::GetModule())
				return nullptr;
			const HMODULE provider = GetModuleHandleW(L"openshim.dll");
			return provider ? GetProcAddress(provider, name) : nullptr;
		}
	}

	// Mode 0 follows openshim.ini LaserSight (off in network games); 1 also
	// allows it in network games. OpenShim rejects other values (returns the
	// mode still in effect) and resets to 0 when a mission ends.
	int SetMode(lua_State* L)
	{
		if (!lua_isnumber(L, 1))
			luaL_typerror(L, 1, "integer");
		const lua_Number value = lua_tonumber(L, 1);
		const int mode = static_cast<int>(value);
		if (static_cast<lua_Number>(mode) != value || (mode != 0 && mode != 1))
			return luaL_argerror(L, 1, "laser sight mode must be 0 or 1");
		const auto set = reinterpret_cast<SetFn>(Resolve("OpenShimSetLaserSightMode"));
		if (!set)
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushinteger(L, set(mode));
		return 1;
	}

	int GetMode(lua_State* L)
	{
		const auto get = reinterpret_cast<GetFn>(Resolve("OpenShimGetLaserSightMode"));
		if (!get)
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushinteger(L, get());
		return 1;
	}
}
