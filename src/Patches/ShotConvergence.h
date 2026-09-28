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

#include "bzr.h"
#include "InlinePatch.h"
#include "Util/EngineAddresses.generated.h"

#include <lua.hpp>

#include <cstdint>

namespace ExtraUtilities::Patch
{
	constexpr uintptr_t wingmanWeaponAimVftableEntry = EngineAddresses::ShotConvergence::Wingman_UpdateWeaponAimSlot;
	constexpr uintptr_t hovercraftWeaponAimVftableEntry = EngineAddresses::ShotConvergence::TurretCraft_UpdateWeaponAimSlot;
	constexpr uintptr_t hovercraftUpdateWeaponAim = EngineAddresses::ShotConvergence::TurretCraft_UpdateWeaponAim;
	constexpr uintptr_t walkerUpdateWeaponAim = EngineAddresses::ShotConvergence::Walker_UpdateWeaponAim;

	constexpr uintptr_t carrierGetWeapon = EngineAddresses::ShotConvergence::Carrier_GetWeapon;
	constexpr uintptr_t refreshWeaponTransform = EngineAddresses::ShotConvergence::RefreshWeaponTransform;
}

namespace ExtraUtilities::Lua::Patches
{
	int GetShotConvergence(lua_State* L);
	int SetShotConvergence(lua_State* L);
	int GetPlayerReticleShotConvergence(lua_State* L);
	int SetPlayerReticleShotConvergence(lua_State* L);
}
