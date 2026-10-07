#include "DamageResistance.h"
#include "GameObjectHandle.h"
#include "LuaHelpers.h"
#include "OpenShimBridge.h"

#include <cmath>

namespace ExtraUtilities::Lua::DamageResistance
{
    namespace
    {
        using HasFn = BOOL(WINAPI*)();
        using SetFn = BOOL(WINAPI*)(void*, DWORD, float);
        using ClearFn = BOOL(WINAPI*)(DWORD);
        using ResetFn = BOOL(WINAPI*)();

        HasFn HasBridge()
        {
            static constinit OpenShimBridge::CachedExport<HasFn> bridge{"OpenShimHasNativeDamageResistance"};
            return bridge.Get();
        }
        SetFn SetBridge()
        {
            static constinit OpenShimBridge::CachedExport<SetFn> bridge{"OpenShimSetUnitDamageMultiplier"};
            return bridge.Get();
        }
        ClearFn ClearBridge()
        {
            static constinit OpenShimBridge::CachedExport<ClearFn> bridge{"OpenShimClearUnitDamageMultiplier"};
            return bridge.Get();
        }
        ResetFn ResetBridge()
        {
            static constinit OpenShimBridge::CachedExport<ResetFn> bridge{"OpenShimResetUnitDamageMultipliers"};
            return bridge.Get();
        }
    }

    int HasNativeDamageResistance(lua_State* L)
    {
        const auto fn = HasBridge();
        lua_pushboolean(L, RuntimeGate::IsSupported() && fn && fn());
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
        const auto fn = SetBridge();
        const bool ok = RuntimeGate::IsSupported() && fn && GameObject::Detail::TryResolveHandleValue(raw, handle, object) &&
            fn(object, raw, static_cast<float>(value));
        lua_pushboolean(L, ok);
        return 1;
    }
    int ClearUnitDamageMultiplier(lua_State* L)
    {
        const auto raw = static_cast<uint32_t>(CheckHandle(L, 1));
        const auto fn = ClearBridge();
        // Clear accepts a destroyed handle: no object dereference is needed.
        lua_pushboolean(L, fn && fn(raw));
        return 1;
    }
    int ResetUnitDamageMultipliers(lua_State* L)
    {
        const auto fn = ResetBridge();
        lua_pushboolean(L, fn && fn());
        return 1;
    }
    void ResetMissionState() noexcept
    {
        if (const auto fn = ResetBridge()) fn();
    }
}
