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

/*
 * PublicAPI.cpp — Implements the stable C-linkage exports declared in
 * include/ExtraUtils.h and owns EXU's Lua-state lifecycle bridge.
 */

#include "About.h"
#include "BasicPatch.h"
#include "Game/CommandReplacement.h"
#include "LuaState.h"
#include "UI/Overlay.h"
#include "Util/Logging.h"

#include <Windows.h>

#include <cstdio>
#include <cstdint>

namespace ExtraUtilities::Lua
{
	namespace
	{
		constexpr const char* kLifecycleMetatable = "ExtraUtilities.LuaStateLifecycle";
		constexpr const char* kLifecycleRegistryKey = "ExtraUtilities.LuaStateLifecycle.instance";

		int LuaStateLifecycleGc(lua_State* L)
		{
			HandleLuaStateClosing(L);
			return 0;
		}

		void InstallLifecycleSentinel(lua_State* L)
		{
			if (L == nullptr)
			{
				return;
			}

			lua_newuserdata(L, 1);
			if (luaL_newmetatable(L, kLifecycleMetatable) != 0)
			{
				lua_pushcfunction(L, LuaStateLifecycleGc);
				lua_setfield(L, -2, "__gc");
			}
			lua_setmetatable(L, -2);
			lua_setfield(L, LUA_REGISTRYINDEX, kLifecycleRegistryKey);
		}

		void InitializeDebugConsole()
		{
#ifdef _DEBUG
			static bool initialized = false;
			if (initialized)
			{
				return;
			}

			if (AllocConsole() != FALSE)
			{
				FILE* stream = nullptr;
				freopen_s(&stream, "CONOUT$", "w", stdout);
				SetConsoleTitleA("Extra Utilities Console");
			}
			initialized = true;
#endif
		}
	}

	void HandleLuaStateAttached(lua_State* L)
	{
		if (L == nullptr)
		{
			return;
		}

		// File-system, CRT, mutex, and console work is deliberately performed here
		// rather than from DllMain while the Windows loader lock is held.
		Logging::ResetLogFileForCurrentProcess("exu.log");
		Logging::ResetLogFileForCurrentProcess("exu_native_save.log");
		Logging::ResetLogFileForCurrentProcess("exu_environment_debug.log");
		Logging::ResetLogFileForCurrentProcess("exu_material_debug.log");
		InitializeDebugConsole();
		InstallLifecycleSentinel(L);
		Logging::LogMessage(
			"exu: attached Lua state %p generation=%llu",
			static_cast<void*>(L),
			static_cast<unsigned long long>(state.Generation()));
	}

	void HandleLuaStateClosing(lua_State* L) noexcept
	{
		if (L == nullptr || state.Get() != L)
		{
			return;
		}

		try
		{
			// Release all registry references while the originating VM is still
			// valid, then disable native callbacks before invalidating the pointer.
			ReleaseLuaStateBindings(L);
			CommandReplacement::ReleaseState(L);
			BasicPatch::UnloadAllPatches();
			Overlay::ShutdownOverlaySupport();
			state.Clear(L);
			Logging::LogMessage("exu: Lua state closed; native state invalidated");
		}
		catch (...)
		{
			state.Clear(L);
			OutputDebugStringA("ExtraUtilities: exception during Lua-state shutdown\n");
		}
	}
}

extern "C"
{
    __declspec(dllexport) const char* EXU_GetVersion()
    {
        return ExtraUtilities::version.c_str();
    }

    __declspec(dllexport) lua_State* EXU_GetLuaState()
    {
        return ExtraUtilities::Lua::state.Get();
    }

    __declspec(dllexport) std::uint64_t EXU_GetLuaStateGeneration()
    {
        return ExtraUtilities::Lua::state.Generation();
    }
}
