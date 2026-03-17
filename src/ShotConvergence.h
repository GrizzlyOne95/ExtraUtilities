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
#include "InlinePatch.h"

#include <lua.hpp>

#include <cstdint>

namespace ExtraUtilities::Patch
{
	constexpr uintptr_t wingmanWeaponAimVftableEntry = 0x0088A4FC;
	constexpr uintptr_t hovercraftWeaponAimVftableEntry = 0x00889418;
	constexpr uintptr_t hovercraftUpdateWeaponAim = 0x005F0930;
	constexpr uintptr_t walkerUpdateWeaponAim = 0x0060F320;

	constexpr uintptr_t carrierGetWeapon = 0x00417F60;
	constexpr uintptr_t refreshWeaponTransform = 0x00681A00;
}

namespace ExtraUtilities::Lua::Patches
{
	int GetShotConvergence(lua_State* L);
	int SetShotConvergence(lua_State* L);
	int GetPlayerReticleShotConvergence(lua_State* L);
	int SetPlayerReticleShotConvergence(lua_State* L);
}
