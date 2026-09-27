/* Copyright (C) 2023-2025 VTrider
 *
 * This file is part of Extra Utilities.
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
