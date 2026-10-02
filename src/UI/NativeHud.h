/* Copyright (C) 2026 GrizzlyOne95
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#pragma once
#include "UI/NativeHudControls.h"
struct lua_State;

namespace ExtraUtilities::NativeHud
{
    uint32_t Capabilities();
    bool Available(Meter meter);
    bool GetRect(Meter meter, bool stock, Rect& rect);
    Result SetRect(Meter meter, Rect rect);
    Result SetVisible(Meter meter, bool visible);
    Result Restore(Meter meter);
    Result RestoreAll();
    void ResetMissionState() noexcept;
}

namespace ExtraUtilities::Lua::NativeHud
{
    int IsNativeHudLayoutAvailable(lua_State* L);
    int GetNativeHudMeterRect(lua_State* L);
    int GetNativeHudMeterDefaultRect(lua_State* L);
    int SetNativeHudMeterRect(lua_State* L);
    int SetNativeHudMeterVisible(lua_State* L);
    int RestoreNativeHudMeter(lua_State* L);
    int RestoreAllNativeHudMeters(lua_State* L);
}
