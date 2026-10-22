#pragma once
#include <lua.hpp>

namespace ExtraUtilities::Lua::ExplosionEvents
{
    // Opt-in, read-only local explosion stream. No Lua runs in the engine hook.
    int SetEnabled(lua_State* L);
    int Drain(lua_State* L);
    void Shutdown() noexcept;
}
