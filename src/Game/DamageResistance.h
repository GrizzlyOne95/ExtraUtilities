#pragma once
#include <lua.hpp>

namespace ExtraUtilities::Lua::DamageResistance
{
    int HasNativeDamageResistance(lua_State* L);
    int SetUnitDamageMultiplier(lua_State* L);
    int ClearUnitDamageMultiplier(lua_State* L);
    int ResetUnitDamageMultipliers(lua_State* L);
    void ResetMissionState() noexcept;
}
