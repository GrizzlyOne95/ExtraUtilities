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

// Mission-facing renderer-effect intent.
//
// These are requests, not commands. EXU owns none of the rendering: it
// forwards a mission's intent to OpenShim over the optional winmm bridge and
// reports back what OpenShim says about it. With no OpenShim present, or an
// OpenShim without the render-effect ABI, every one of these is a clean no-op
// and the mission keeps running.

#include <lua.hpp>

namespace ExtraUtilities::Lua::Render
{
	int SetRenderEffectEnabled(lua_State* L);
	int SetRenderEffectFloat(lua_State* L);
	int GetRenderEffectStatus(lua_State* L);
	int ResetRenderEffects(lua_State* L);
}
