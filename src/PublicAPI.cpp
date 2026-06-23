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
 * include/ExtraUtils.h for use by C++ DLL consumers (e.g. Battlezone98Redux_Shim).
 *
 * These are separate from the Lua exports in LuaExport.cpp to keep the two
 * contracts distinct: the Lua API is everything in the luaL_Reg table, and
 * the C++ API is the narrow set of functions here.
 */

#include "About.h"
#include "LuaState.h"

#include <Windows.h>

extern "C"
{
    // Returns the runtime EXU version string (e.g. "1.1.0").
    // The pointer is valid for the lifetime of the process — the string lives
    // in a static std::string inside About.h.
    __declspec(dllexport) const char* EXU_GetVersion()
    {
        return ExtraUtilities::version.c_str();
    }

    // Returns the lua_State* registered during luaopen_exu, or nullptr if EXU
    // has not yet been initialized from Lua. The returned pointer is valid only
    // for the duration of the current mission — it becomes stale after the map
    // unloads and the Lua runtime is torn down.
    __declspec(dllexport) lua_State* EXU_GetLuaState()
    {
        return ExtraUtilities::Lua::state;
    }
}
