#pragma once
struct lua_State;
namespace ExtraUtilities::Lua::InterfacePresentation
{
    int SetEnabled(lua_State* L);
    void Reset() noexcept;
}
