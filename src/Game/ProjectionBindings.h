// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once
#include <lua.hpp>
namespace ExtraUtilities::Lua::ProjectionBindings
{
    int WorldToScreen(lua_State* L);
    int IsPointInView(lua_State* L);
}
