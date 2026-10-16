// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#pragma once
struct lua_State;
namespace ExtraUtilities::Lua::PlayerControls
{
    void ResetMissionState() noexcept;
    void Shutdown() noexcept;
    int SetMask(lua_State* L);
    int ClearMask(lua_State* L);
    int GetState(lua_State* L);
    void RegisterConstants(lua_State* L, int exuIndex);
}
