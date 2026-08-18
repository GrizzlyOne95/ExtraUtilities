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

#include "AddScrapCallback.h"

#include "Hook.h"
#include "LuaHelpers.h"
#include "LuaState.h"

#include <string>

namespace ExtraUtilities::Patch
{
	static void __cdecl LuaCallback(uint32_t teamNumber, uint32_t scrapAmount)
	{
		lua_State* L = Lua::state;
		if (L == nullptr)
		{
			return;
		}

		StackGuard guard(L);

		lua_getglobal(L, "exu");
		lua_getfield(L, -1, "AddScrap");

		if (!lua_isfunction(L, -1))
		{
			return;
		}

		lua_pushinteger(L, teamNumber);
		lua_pushinteger(L, scrapAmount);

		const int status = lua_pcall(L, 2, 0, 0);
		LuaCheckStatus(status, L, "Extra Utilities AddScrap error:\n%s");
	}

	static void __declspec(naked) AddScrapCallback()
	{
		__asm
		{
			/*
			* Notes:
			*
			* Team object in ecx
			* team number in ecx+0x180
			*
			* Scrap amount to add in param 1/ebp+0x08
			*/

			// Game code - side note why tf is it doing this lol
			mov [ebp-0x04], ecx
			mov ecx, [ebp-0x04]

			pushad
			pushfd

			mov eax, [ebp+0x08] // scrap amount
			push eax
			mov ecx, [ecx+0x180] // team number
			push ecx
			call LuaCallback
			add esp, 0x08

			popfd
			popad

			ret
		}
	}
	Hook addScrapHook(0x005E1016, &AddScrapCallback, 6, BasicPatch::Status::ACTIVE);
}

namespace ExtraUtilities::Lua::Patches
{
	int AddScrapSilent(lua_State* L)
	{
		const int teamNum = luaL_checkinteger(L, 1);
		const int amount = luaL_checkinteger(L, 2);

		lua_getglobal(L, "AddScrap");
		if (!lua_isfunction(L, -1))
		{
			return luaL_error(L, "Extra Utilities AddScrapSilent error: stock AddScrap is unavailable");
		}

		lua_pushinteger(L, teamNum);
		lua_pushinteger(L, amount);

		// lua_call can longjmp past C++ destructors, so this path deliberately uses
		// pcall and restores the hook before propagating an error back to Lua.
		const bool wasActive = Patch::addScrapHook.IsActive();
		if (wasActive)
		{
			Patch::addScrapHook.Unload();
		}

		const int status = lua_pcall(L, 2, 1, 0);

		if (wasActive)
		{
			Patch::addScrapHook.Reload();
		}

		if (status != 0)
		{
			const char* luaMessage = lua_tostring(L, -1);
			const std::string errorMessage = luaMessage != nullptr ? luaMessage : "unknown Lua error";
			lua_pop(L, 1);
			return luaL_error(L, "Extra Utilities AddScrapSilent error: %s", errorMessage.c_str());
		}

		return 1;
	}
}
