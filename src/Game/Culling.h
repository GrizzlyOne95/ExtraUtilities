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

#pragma once

#include "bzr.h"
#include <lua.hpp>

namespace ExtraUtilities::Culling
{
    void UpdateUnit(BZR::GameObject* obj);

    inline float cullDistance = 500.0f;
    inline bool enabled = false;

    // Back to defaults at Lua-state close; also forgets which units it hid.
    void ResetMissionState() noexcept;
}

namespace ExtraUtilities::Lua::Culling
{
    int SetCullDistance(lua_State* L);
    int GetCullDistance(lua_State* L);
    int SetCullingEnabled(lua_State* L);
    int GetCullingEnabled(lua_State* L);
}

extern "C" __declspec(dllexport) void __cdecl EXU_UpdateCullingForUnit(void* object);
