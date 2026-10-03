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

#include "ScreenFlash.h"

#include "InlinePatch.h"
#include "Util/EngineAddresses.generated.h"

#include <cstdint>
#include <vector>

namespace ExtraUtilities::Patch
{
	// Each site is the 10-byte tail of a ColorFade::SetFade call whose five
	// arguments are already pushed:
	//     mov ecx, colorFade      B9 8C 83 97 00
	//     call ColorFade::SetFade E8 <rel32>      ; thiscall, callee pops 0x14
	// Suppressing replaces it with "add esp, 0x14" so the caller's stack stays
	// balanced and the fade never accumulates. The rel32 in the preimage keeps
	// each patch tied to its own call site.
	static std::vector<uint8_t> DropSetFadeCall()
	{
		return { 0x83, 0xC4, 0x14, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	}

	// Craft::DamageAlloc: SetFade(damage * 0.002, 5.0, 255, 0, 0) for the user's craft.
	InlinePatch craftDamageFlash(
		EngineAddresses::ScreenFlash::CraftDamageFlashSite,
		DropSetFadeCall(),
		BasicPatch::Status::INACTIVE,
		{ 0xB9, 0x8C, 0x83, 0x97, 0x00, 0xE8, 0x30, 0x0D, 0xFF, 0xFF });

	// Person::DamageAlloc: SetFade(damage * 0.03, 5.0, 255, 0, 0) for the user's pilot.
	InlinePatch personDamageFlash(
		EngineAddresses::ScreenFlash::PersonDamageFlashSite,
		DropSetFadeCall(),
		BasicPatch::Status::INACTIVE,
		{ 0xB9, 0x8C, 0x83, 0x97, 0x00, 0xE8, 0xD4, 0xA6, 0xEF, 0xFF });

	// Craft::UpdateTemperature: SetFade(heat * 0.01, 5.0, 255, 64, 0) while the user's craft overheats.
	InlinePatch craftHeatFlash(
		EngineAddresses::ScreenFlash::CraftHeatFlashSite,
		DropSetFadeCall(),
		BasicPatch::Status::INACTIVE,
		{ 0xB9, 0x8C, 0x83, 0x97, 0x00, 0xE8, 0xB3, 0x01, 0xFF, 0xFF });
}

namespace ExtraUtilities::Lua::Patches
{
	static bool CheckEnabled(lua_State* L)
	{
		luaL_checktype(L, 1, LUA_TBOOLEAN);
		return lua_toboolean(L, 1) != 0;
	}

	static BasicPatch::Status SuppressionStatus(bool flashEnabled)
	{
		return flashEnabled ? BasicPatch::Status::INACTIVE : BasicPatch::Status::ACTIVE;
	}

	// Reports what the engine will actually do: on an unsupported build the
	// patches never install and the flash stays enabled.
	int GetDamageFlashEnabled(lua_State* L)
	{
		lua_pushboolean(L, !(Patch::craftDamageFlash.IsActive() && Patch::personDamageFlash.IsActive()));
		return 1;
	}

	int SetDamageFlashEnabled(lua_State* L)
	{
		const BasicPatch::Status status = SuppressionStatus(CheckEnabled(L));
		Patch::craftDamageFlash.SetStatus(status);
		Patch::personDamageFlash.SetStatus(status);
		return 0;
	}

	int GetHeatFlashEnabled(lua_State* L)
	{
		lua_pushboolean(L, !Patch::craftHeatFlash.IsActive());
		return 1;
	}

	int SetHeatFlashEnabled(lua_State* L)
	{
		Patch::craftHeatFlash.SetStatus(SuppressionStatus(CheckEnabled(L)));
		return 0;
	}
}
