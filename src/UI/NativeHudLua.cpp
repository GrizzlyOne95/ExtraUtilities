/* Copyright (C) 2026 GrizzlyOne95
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "UI/NativeHud.h"
#include <lua.hpp>
#include <cmath>

namespace ExtraUtilities::Lua::NativeHud
{
    namespace Api = ExtraUtilities::NativeHud;
    namespace
    {
        bool MeterArg(lua_State* L, Api::Meter& meter)
        {
            size_t length = 0;
            const char* name = luaL_checklstring(L, 1, &length);
            return Api::ParseMeter({name, length}, meter);
        }
        bool PixelArg(lua_State* L, int index, int& value)
        {
            const double number = luaL_checknumber(L, index);
            // Check before the conversion: Lua 5.1 permits fractions, NaN and
            // arbitrarily large numbers through luaL_checkinteger otherwise.
            if (!std::isfinite(number) || std::floor(number) != number ||
                number < -Api::kCoordinateLimit || number > Api::kCoordinateLimit) return false;
            value = static_cast<int>(number);
            return true;
        }
        int PushResult(lua_State* L, Api::Result result)
        {
            lua_pushboolean(L, result == Api::Result::Accepted);
            if (const char* reason = Api::Reason(result))
            {
                lua_pushstring(L, reason);
                return 2;
            }
            return 1;
        }
        int GetRect(lua_State* L, bool stock)
        {
            Api::Meter meter;
            Api::Rect rect;
            if (!MeterArg(L, meter) || !Api::GetRect(meter, stock, rect))
            {
                lua_pushnil(L);
                return 1;
            }
            lua_pushinteger(L, rect.x); lua_pushinteger(L, rect.y);
            lua_pushinteger(L, rect.w); lua_pushinteger(L, rect.h);
            return 4;
        }
    }

    int IsNativeHudLayoutAvailable(lua_State* L)
    {
        Api::Meter meter;
        lua_pushboolean(L, MeterArg(L, meter) && Api::Available(meter));
        return 1;
    }
    int GetNativeHudMeterRect(lua_State* L) { return GetRect(L, false); }
    int GetNativeHudMeterDefaultRect(lua_State* L) { return GetRect(L, true); }
    int SetNativeHudMeterRect(lua_State* L)
    {
        Api::Meter meter;
        if (!MeterArg(L, meter)) return PushResult(L, Api::Result::InvalidMeter);
        Api::Rect rect;
        if (!PixelArg(L, 2, rect.x) || !PixelArg(L, 3, rect.y) ||
            !PixelArg(L, 4, rect.w) || !PixelArg(L, 5, rect.h)) return PushResult(L, Api::Result::InvalidRect);
        return PushResult(L, Api::SetRect(meter, rect));
    }
    int SetNativeHudMeterVisible(lua_State* L)
    {
        Api::Meter meter;
        if (!MeterArg(L, meter)) return PushResult(L, Api::Result::InvalidMeter);
        luaL_checktype(L, 2, LUA_TBOOLEAN);
        return PushResult(L, Api::SetVisible(meter, lua_toboolean(L, 2) != 0));
    }
    int RestoreNativeHudMeter(lua_State* L)
    {
        Api::Meter meter;
        return PushResult(L, MeterArg(L, meter) ? Api::Restore(meter) : Api::Result::InvalidMeter);
    }
    int RestoreAllNativeHudMeters(lua_State* L) { return PushResult(L, Api::RestoreAll()); }
}
