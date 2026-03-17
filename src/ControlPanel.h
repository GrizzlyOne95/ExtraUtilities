/* Copyright (C) 2023-2026 VTrider
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

#pragma once

#include "BZR.h"
#include "Scanner.h"

#include <lua.hpp>

namespace ExtraUtilities::Lua::ControlPanel
{
	inline auto controlPanel = BZR::ControlPanel::p_controlPanel;

	int GetScrapPilotHudOffset(lua_State* L);
	int SetScrapPilotHudOffset(lua_State* L);
	int GetScrapPilotHudTopLeft(lua_State* L);
	int SetScrapPilotHudTopLeft(lua_State* L);
	int GetScrapHudTopLeft(lua_State* L);
	int SetScrapHudTopLeft(lua_State* L);
	int GetPilotHudTopLeft(lua_State* L);
	int SetPilotHudTopLeft(lua_State* L);
	int GetScrapHudColor(lua_State* L);
	int SetScrapHudColor(lua_State* L);
	int GetPilotHudColor(lua_State* L);
	int SetPilotHudColor(lua_State* L);
	int SelectAdd(lua_State* L);
	int SelectNone(lua_State* L);
	int SelectOne(lua_State* L);
}
