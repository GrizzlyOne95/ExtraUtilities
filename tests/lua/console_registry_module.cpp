// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Game/ConsoleRegistry.h"
extern "C" int luaopen_console_test(lua_State* L)
{
    using namespace ExtraUtilities::Lua::ConsoleRegistry;
    static const luaL_Reg functions[] = {
        {"RegisterConsoleCommand", RegisterCommand}, {"RegisterConsoleVariable", RegisterVariable},
        {"ExecuteConsoleCommand", Execute}, {"UnregisterConsoleItem", Unregister},
        {"GetConsoleVariable", GetVariable}, {"SetConsoleVariable", SetVariable},
        {"ListConsoleItems", List}, {nullptr, nullptr}
    };
    luaL_register(L, "console_test", functions);
    return 1;
}
