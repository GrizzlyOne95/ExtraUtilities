// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once
#include <lua.hpp>

// Producer build control, Scavenger scrap hold and current-command inspection.
namespace ExtraUtilities::Lua::ProducerScrap
{
	int CancelBuild(lua_State* L);
	int FinishBuild(lua_State* L);
	int GetBuildTimeLeft(lua_State* L);
	int SetBuildTimeLeft(lua_State* L);
	int GetBuildingOdf(lua_State* L);

	int GetCurScrapHold(lua_State* L);
	int SetCurScrapHold(lua_State* L);
	int GetMaxScrapHold(lua_State* L);
	int SetMaxScrapHold(lua_State* L);

	int GetCurrentParam(lua_State* L);
	int GetCurrentWhen(lua_State* L);
	int GetCurrentWhere(lua_State* L);
}
