#include "DamageResistance.h"
#include "GameObjectHandle.h"
#include "LuaHelpers.h"
#include "Patches/UnitDamage.h"

#include <cmath>

namespace ExtraUtilities::Lua::DamageResistance
{
    int HasNativeDamageResistance(lua_State* L)
    {
        lua_pushboolean(L, RuntimeGate::IsSupported() && Patch::UnitDamage::Available());
        return 1;
    }
    int SetUnitDamageMultiplier(lua_State* L)
    {
        const auto raw = static_cast<uint32_t>(CheckHandle(L, 1));
        const auto value = luaL_checknumber(L, 2);
        if (!std::isfinite(value) || value < 0.0 || value > 1.0)
            return luaL_argerror(L, 2, "expected finite damage multiplier between 0 and 1");
        BZR::handle handle = 0;
        BZR::GameObject* object = nullptr;
        const bool ok = RuntimeGate::IsSupported() && GameObject::Detail::TryResolveHandleValue(raw, handle, object) &&
            Patch::UnitDamage::Set(object, raw, static_cast<float>(value));
        lua_pushboolean(L, ok);
        return 1;
    }
    int ClearUnitDamageMultiplier(lua_State* L)
    {
        // Clear accepts a destroyed handle: no object dereference is needed.
        Patch::UnitDamage::Clear(static_cast<uint32_t>(CheckHandle(L, 1)));
        lua_pushboolean(L, true);
        return 1;
    }
    int ResetUnitDamageMultipliers(lua_State* L)
    {
        Patch::UnitDamage::Reset();
        lua_pushboolean(L, true);
        return 1;
    }
    void ResetMissionState() noexcept
    {
        Patch::UnitDamage::Reset();
    }
}
