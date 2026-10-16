// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#pragma once
struct lua_State;
namespace ExtraUtilities::Lua::VehicleControl
{
    int HasVehicleControl(lua_State* L);
    int ClaimVehicleControl(lua_State* L);
    int SetVehicleControls(lua_State* L);
    int GetVehicleControlState(lua_State* L);
    int ReleaseVehicleControl(lua_State* L);
    int ResetVehicleControls(lua_State* L);
    void ResetMissionState() noexcept;
    void Shutdown() noexcept;
}
