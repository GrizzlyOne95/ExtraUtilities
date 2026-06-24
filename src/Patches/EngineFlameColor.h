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

#ifndef EXU_ENGINE_FLAME_COLOR_API
#define EXU_ENGINE_FLAME_COLOR_API extern "C" __declspec(dllexport)
#endif

#include <lua.hpp>

namespace ExtraUtilities::Lua::Patches
{
	int GetTeamEngineFlameColor(lua_State* L);
	int SetTeamEngineFlameColor(lua_State* L);
	int ClearTeamEngineFlameColor(lua_State* L);
}

EXU_ENGINE_FLAME_COLOR_API int EXU_GetTeamEngineFlameColor(int team);
