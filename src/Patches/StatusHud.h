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

#include <lua.hpp>

// Suppression of the stock player status display's draws (StatusDisplay
// render, fn 0x005DC300) in three groups: "hull" (label and bar), "ammo"
// (label, bar, shots-remaining count and ammo-cost marker) and "weapons"
// (row plates, hardpoint icons and names). Only the draw calls are removed;
// the renderer still runs, so its low-hull and out-of-ammo voice warnings
// and its pane bookkeeping are unchanged. The patches default to inactive and
// BasicPatch resets them at Lua-state close, so suppression is mission scoped.
namespace ExtraUtilities::Lua::Patches
{
	int GetStockStatusHudVisible(lua_State* L);
	int SetStockStatusHudVisible(lua_State* L);
}
