// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once
#include "bzr.h"
#include <lua.hpp>

namespace ExtraUtilities::TerrainPlacement
{
    enum class Status { Buildable, Blocked, OutsideMap, PowerRequired, InvalidOdf, UnsupportedClass, Unavailable };
    // Engine thread only. The native class/bounds readers can load ODF/model
    // resources; this never creates objects, changes selection, or issues Build.
    Status Query(int team, const char* odf, const BZR::VECTOR_3D& position,
        const BZR::VECTOR_3D& facing, BZR::VECTOR_3D& adjusted) noexcept;
    const char* Describe(Status status) noexcept;
}
namespace ExtraUtilities::Lua::TerrainPlacementBindings
{
    int IsTerrainBuildable(lua_State* L);
}
