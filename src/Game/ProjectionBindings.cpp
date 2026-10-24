// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Game/ProjectionBindings.h"
#include "LuaHelpers.h"
#include "Ogre/OgreCameraRuntime.h"
#include "Util/SehGuard.h"

namespace ExtraUtilities::Lua::ProjectionBindings
{
    namespace
    {
        bool ReadHandlePosition(BZR::handle handle, BZR::VECTOR_3D& out) noexcept
        {
            __try
            {
                auto* object = BZR::GameObject::GetObj(handle);
                if (!object) return false;
                // Match stock GetPosition/GetTransform: OBJ76's simulation-space
                // transform, not GameObject's integration/cache position.
                if (!object->obj) return false;
                const auto* matrix = reinterpret_cast<const BZR::MAT_3D*>(
                    reinterpret_cast<const unsigned char*>(object->obj) + 0x20);
                out = BZR::VECTOR_3D{matrix->posit_x, matrix->posit_y, matrix->posit_z};
                return true;
            }
            __except (Seh::Filter(GetExceptionCode())) { return false; }
        }

        bool ReadPosition(lua_State* L, BZR::VECTOR_3D& out)
        {
            if (lua_isnoneornil(L, 1)) return false;
            if (lua_islightuserdata(L, 1)) return ReadHandlePosition(CheckHandle(L, 1), out);
            if (!lua_isuserdata(L, 1) && !lua_istable(L, 1))
                return luaL_argerror(L, 1, "expected a handle, vector or matrix") != 0;
            lua_getfield(L, 1, "posit_x");
            const bool matrix = !lua_isnil(L, -1);
            lua_pop(L, 1);
            const char* fields[3] = {matrix ? "posit_x" : "x", matrix ? "posit_y" : "y", matrix ? "posit_z" : "z"};
            float* values[3] = {&out.x, &out.y, &out.z};
            for (int i = 0; i < 3; ++i)
            {
                lua_getfield(L, 1, fields[i]);
                *values[i] = static_cast<float>(luaL_checknumber(L, -1));
                lua_pop(L, 1);
            }
            return true;
        }
    }

    int WorldToScreen(lua_State* L)
    {
        BZR::VECTOR_3D position{};
        CameraProjection::Result result;
        if (!RuntimeGate::IsSupported() || !ReadPosition(L, position) || !OgreCameraRuntime::Project(position, result))
        {
            lua_pushnil(L); lua_pushnil(L); lua_pushnil(L); return 3;
        }
        lua_pushnumber(L, result.x); lua_pushnumber(L, result.y); lua_pushnumber(L, result.depth); return 3;
    }

    int IsPointInView(lua_State* L)
    {
        BZR::VECTOR_3D position{};
        bool visible = false;
        if (!RuntimeGate::IsSupported() || !ReadPosition(L, position) || !OgreCameraRuntime::IsPointInView(position, visible))
            return PushUnsupportedBuild(L);
        lua_pushboolean(L, visible); return 1;
    }
}
