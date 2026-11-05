// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <lua.hpp>
namespace ExtraUtilities::Lua::ConstructionHologram
{
    int SetEnabled(lua_State* L);
    void Shutdown() noexcept;
}
