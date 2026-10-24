// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Game/ConsoleRegistry.h"
#include <cmath>
#include <cstring>

namespace ExtraUtilities::Lua::ConsoleRegistry
{
    namespace
    {
        char registryKey;
        const char* Name(lua_State* L)
        {
            std::size_t length = 0;
            const char* name = luaL_checklstring(L, 1, &length);
            if (!length || length > 96) luaL_argerror(L, 1, "name must contain 1..96 ASCII letters, digits, dots, hyphens or underscores");
            for (std::size_t i = 0; i < length; ++i)
            {
                const char ch = name[i];
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_'))
                    luaL_argerror(L, 1, "invalid console item name");
            }
            return name;
        }
        void Registry(lua_State* L)
        {
            lua_pushlightuserdata(L, &registryKey); lua_rawget(L, LUA_REGISTRYINDEX);
            if (!lua_isnil(L, -1)) return;
            lua_pop(L, 1); lua_newtable(L);
            lua_pushlightuserdata(L, &registryKey); lua_pushvalue(L, -2); lua_rawset(L, LUA_REGISTRYINDEX);
        }
        bool Entry(lua_State* L, const char* name, const char* kind)
        {
            Registry(L); lua_getfield(L, -1, name);
            if (!lua_istable(L, -1)) return false;
            lua_getfield(L, -1, "kind");
            const bool matches = std::strcmp(lua_tostring(L, -1), kind) == 0;
            lua_pop(L, 1); return matches;
        }
        int Failure(lua_State* L, const char* message)
        { lua_pushboolean(L, false); lua_pushstring(L, message); return 2; }
        void CheckValue(lua_State* L, int index, int entry)
        {
            lua_getfield(L, entry, "value");
            const int type = lua_type(L, -1); lua_pop(L, 1);
            if (lua_type(L, index) != type) luaL_argerror(L, index, "value type differs from registered default");
            if (type == LUA_TNUMBER)
            {
                const double value = lua_tonumber(L, index);
                if (!std::isfinite(value)) luaL_argerror(L, index, "value must be finite");
                lua_getfield(L, entry, "min");
                if (!lua_isnil(L, -1) && value < lua_tonumber(L, -1)) luaL_argerror(L, index, "value is below minimum");
                lua_pop(L, 1); lua_getfield(L, entry, "max");
                if (!lua_isnil(L, -1) && value > lua_tonumber(L, -1)) luaL_argerror(L, index, "value is above maximum");
                lua_pop(L, 1);
            }
        }
    }

    int RegisterCommand(lua_State* L)
    {
        const char* name = Name(L); luaL_checktype(L, 2, LUA_TFUNCTION);
        Registry(L); const int registry = lua_gettop(L);
        lua_getfield(L, registry, name);
        if (!lua_isnil(L, -1)) return Failure(L, "name already registered");
        lua_pop(L, 1); lua_newtable(L);
        lua_pushliteral(L, "command"); lua_setfield(L, -2, "kind");
        lua_pushvalue(L, 2); lua_setfield(L, -2, "callback");
        lua_setfield(L, registry, name); lua_pushboolean(L, true); return 1;
    }

    int RegisterVariable(lua_State* L)
    {
        const char* name = Name(L); const int type = lua_type(L, 2);
        if (type != LUA_TNUMBER && type != LUA_TSTRING && type != LUA_TBOOLEAN)
            return luaL_argerror(L, 2, "default must be a number, string or boolean");
        if (!lua_isnoneornil(L, 3)) luaL_checktype(L, 3, LUA_TTABLE);
        // Fix the optional slot before pushing the registry or an entry.
        lua_settop(L, 3);
        Registry(L); const int registry = lua_gettop(L);
        lua_getfield(L, registry, name);
        if (!lua_isnil(L, -1)) return Failure(L, "name already registered");
        lua_pop(L, 1); lua_newtable(L); const int entry = lua_gettop(L);
        lua_pushliteral(L, "variable"); lua_setfield(L, entry, "kind");
        lua_pushvalue(L, 2); lua_setfield(L, entry, "value");
        if (lua_istable(L, 3))
        {
            const char* fields[] = {"min", "max", "onChanged"};
            for (const char* field : fields)
            {
                lua_getfield(L, 3, field);
                if (!lua_isnil(L, -1))
                {
                    if (std::strcmp(field, "onChanged") == 0) luaL_checktype(L, -1, LUA_TFUNCTION);
                    else if (type != LUA_TNUMBER || lua_type(L, -1) != LUA_TNUMBER || !std::isfinite(lua_tonumber(L, -1)))
                        return luaL_argerror(L, 3, "min/max require a numeric default and finite numeric bounds");
                }
                lua_setfield(L, entry, field);
            }
            lua_getfield(L, entry, "min"); lua_getfield(L, entry, "max");
            if (!lua_isnil(L, -2) && !lua_isnil(L, -1) && lua_tonumber(L, -2) > lua_tonumber(L, -1))
                return luaL_argerror(L, 3, "minimum exceeds maximum");
            lua_pop(L, 2);
        }
        CheckValue(L, 2, entry);
        lua_setfield(L, registry, name); lua_pushboolean(L, true); return 1;
    }

    int Unregister(lua_State* L)
    {
        const char* name = Name(L); Registry(L);
        lua_getfield(L, -1, name); const bool exists = !lua_isnil(L, -1); lua_pop(L, 1);
        lua_pushnil(L); lua_setfield(L, -2, name); lua_pushboolean(L, exists); return 1;
    }

    int Execute(lua_State* L)
    {
        const char* name = Name(L);
        // Command arguments are an uninterpreted string; never Lua/shell code.
        luaL_optstring(L, 2, ""); lua_settop(L, 2);
        if (!Entry(L, name, "command")) return Failure(L, "unknown command");
        lua_getfield(L, -1, "callback");
        if (lua_isnil(L, 2)) lua_pushliteral(L, ""); else lua_pushvalue(L, 2);
        if (lua_pcall(L, 1, 0, 0) != 0) { lua_pushboolean(L, false); lua_insert(L, -2); return 2; }
        lua_pushboolean(L, true); return 1;
    }

    int GetVariable(lua_State* L)
    {
        const char* name = Name(L);
        if (!Entry(L, name, "variable")) { lua_pushnil(L); return 1; }
        lua_getfield(L, -1, "value"); return 1;
    }

    int SetVariable(lua_State* L)
    {
        const char* name = Name(L); luaL_checkany(L, 2);
        if (!Entry(L, name, "variable")) return Failure(L, "unknown variable");
        const int entry = lua_gettop(L); CheckValue(L, 2, entry);
        lua_getfield(L, entry, "value"); const int previous = lua_gettop(L);
        if (lua_rawequal(L, previous, 2)) { lua_pushboolean(L, true); return 1; }
        lua_pushvalue(L, 2); lua_setfield(L, entry, "value");
        lua_getfield(L, entry, "onChanged");
        if (!lua_isnil(L, -1))
        {
            lua_pushvalue(L, 2); lua_pushvalue(L, previous);
            // Value is committed before notification, including on callback
            // failure. No native lock or C++ owner survives a Lua callback.
            if (lua_pcall(L, 2, 0, 0) != 0) { lua_pushboolean(L, false); lua_insert(L, -2); return 2; }
        }
        lua_pushboolean(L, true); return 1;
    }

    int List(lua_State* L)
    {
        Registry(L); const int registry = lua_gettop(L);
        lua_newtable(L); const int result = lua_gettop(L);
        lua_pushnil(L);
        while (lua_next(L, registry))
        {
            lua_pushvalue(L, -2); lua_getfield(L, -2, "kind"); lua_settable(L, result);
            lua_pop(L, 1);
        }
        return 1;
    }
}
