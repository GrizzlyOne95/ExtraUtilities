// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once
#include <lua.hpp>
namespace ExtraUtilities::Lua::ConsoleRegistry
{
    // Per Lua state, stored entirely in its registry. Explicit EXU dispatch;
    // BZR has no BZCC VarSys or stock command-console integration to replace.
    int RegisterCommand(lua_State* L);
    int RegisterVariable(lua_State* L);
    int Unregister(lua_State* L);
    int Execute(lua_State* L);
    int GetVariable(lua_State* L);
    int SetVariable(lua_State* L);
    int List(lua_State* L);
}
