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

#include <lua.hpp>

namespace ExtraUtilities::Lua::CommandReplacement
{
	void ResetState(lua_State* L);

	bool DispatchRegisteredReplacement(BZR::handle handle, const char* stockCommandName, const char* origin);

	int ReplaceStockCmd(lua_State* L);
	int RemoveStockCmdReplacement(lua_State* L);
	int HasStockCmdReplacement(lua_State* L);
	int GetStockCmdReplacement(lua_State* L);
	int TriggerStockCmdReplacement(lua_State* L);
	int UpdateCommandReplacements(lua_State* L);
}
