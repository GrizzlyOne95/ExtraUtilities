/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

// Optional OpenShim-owned vehicle contact experiment. No engine patches or
// executable addresses live in EXU; mission close clears the native selection.
#include "Game/GeometryContactApi.h"
#include "OpenShimBridge.h"
#include "LuaHelpers.h"
#include "LuaCppBarrier.h"
#include "Util/RuntimeGate.h"
#include <lua.hpp>

namespace ExtraUtilities::GeometryContact
{
    namespace
    {
        using CapsFn = DWORD(WINAPI*)();
        using SetFn = BOOL(WINAPI*)(DWORD, BOOL);
        using ClearFn = BOOL(WINAPI*)();
        using StatsFn = BOOL(WINAPI*)(DWORD, DWORD*, DWORD*, DWORD*, DWORD*, DWORD*, DWORD*);
        int Capabilities(lua_State* L)
        {
            const auto fn = OpenShimBridge::Resolve<CapsFn>("OpenShimGetGeometryContactCapabilities");
            const bool available = RuntimeGate::IsSupported() && fn && (fn() & 1u) != 0;
            lua_createtable(L, 0, 4);
            lua_pushboolean(L, available); lua_setfield(L, -2, "available");
            lua_pushstring(L, "OpenShim"); lua_setfield(L, -2, "owner");
            lua_pushstring(L, "legacy-GEO-target/COLP-source"); lua_setfield(L, -2, "scope");
            lua_pushboolean(L, 1); lua_setfield(L, -2, "experimental");
            return 1;
        }
        int SetGeometry(lua_State* L)
        {
            const auto handle = Lua::CheckHandle(L, 1);
            const bool enabled = Lua::CheckBool(L, 2);
            const auto fn = OpenShimBridge::Resolve<SetFn>("OpenShimSetGeometryContact");
            lua_pushboolean(L, RuntimeGate::IsSupported() && fn && fn(handle, enabled ? TRUE : FALSE));
            return 1;
        }
        int Clear(lua_State* L)
        {
            const auto fn = OpenShimBridge::Resolve<ClearFn>("OpenShimClearGeometryContact");
            lua_pushboolean(L, fn && fn());
            return 1;
        }
        int GetStats(lua_State* L)
        {
            const auto handle = Lua::CheckHandle(L, 1);
            const auto fn = OpenShimBridge::Resolve<StatsFn>("OpenShimGetGeometryContactStats");
            DWORD enabled = 0, parts = 0, faces = 0, checks = 0, hits = 0, fallbacks = 0;
            if (!RuntimeGate::IsSupported() || !fn || !fn(handle, &enabled, &parts, &faces, &checks, &hits, &fallbacks))
            {
                lua_pushnil(L);
                return 1;
            }
            lua_createtable(L, 0, 7);
            lua_pushboolean(L, enabled != 0); lua_setfield(L, -2, "enabled");
            lua_pushstring(L, enabled ? "geometry" : "box"); lua_setfield(L, -2, "mode");
            lua_pushnumber(L, parts); lua_setfield(L, -2, "parts");
            lua_pushnumber(L, faces); lua_setfield(L, -2, "faces");
            lua_pushnumber(L, checks); lua_setfield(L, -2, "checks");
            lua_pushnumber(L, hits); lua_setfield(L, -2, "hits");
            lua_pushnumber(L, fallbacks); lua_setfield(L, -2, "fallbacks");
            return 1;
        }
    }
    void Install(lua_State* L)
    {
        if (!L) return;
        const int top = lua_gettop(L);
        lua_getglobal(L, "exu");
        if (lua_istable(L, -1))
        {
            lua_newtable(L);
            static const luaL_Reg functions[] = {
                {"GetCapabilities", Capabilities}, {"SetGeometry", SetGeometry},
                {"GetStats", GetStats}, {"Clear", Clear}, {nullptr, nullptr}
            };
            Lua::RegisterFunctions(L, nullptr, functions);
            lua_setfield(L, -2, "collision");
        }
        lua_settop(L, top);
    }
    void ResetMissionState() noexcept
    {
        const auto fn = OpenShimBridge::Resolve<ClearFn>("OpenShimClearGeometryContact");
        if (fn) fn();
    }
}
