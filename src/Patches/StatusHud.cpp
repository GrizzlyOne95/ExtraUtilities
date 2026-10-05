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

#include "StatusHud.h"

#include "InlinePatch.h"
#include "Util/EngineAddresses.generated.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ExtraUtilities::Patch
{
	// Every site is a 5-byte "call rel32" to a cdecl draw helper (sprite,
	// font or filled rect) whose caller pops the arguments with the "add esp"
	// that follows, and none of the callers read the return value. NOPing the
	// call therefore drops exactly that draw and leaves the stack balanced.
	// The expected bytes include the rel32, so each patch is tied to its own
	// site and refuses to install over a call someone else already redirected.
	static std::vector<uint8_t> DropCall()
	{
		return std::vector<uint8_t>(5, BasicPatch::NOP);
	}

	namespace Sites = EngineAddresses::StatusHud;

	InlinePatch weaponRowPlate(Sites::WeaponRowPlateSite, DropCall(), BasicPatch::Status::INACTIVE, { 0xE8, 0xE8, 0x01, 0x0B, 0x00 });
	InlinePatch weaponIcon(Sites::WeaponIconSite, DropCall(), BasicPatch::Status::INACTIVE, { 0xE8, 0xA6, 0xFA, 0x0A, 0x00 });
	InlinePatch weaponName(Sites::WeaponNameSite, DropCall(), BasicPatch::Status::INACTIVE, { 0xE8, 0xD4, 0xD1, 0x0A, 0x00 });

	InlinePatch hullLabel(Sites::HullLabelSite, DropCall(), BasicPatch::Status::INACTIVE, { 0xE8, 0x37, 0xFC, 0x0A, 0x00 });
	InlinePatch hullBar(Sites::HullBarSite, DropCall(), BasicPatch::Status::INACTIVE, { 0xE8, 0x36, 0xFB, 0x0A, 0x00 });

	InlinePatch ammoLabel(Sites::AmmoLabelSite, DropCall(), BasicPatch::Status::INACTIVE, { 0xE8, 0x67, 0xF9, 0x0A, 0x00 });
	InlinePatch ammoBar(Sites::AmmoBarSite, DropCall(), BasicPatch::Status::INACTIVE, { 0xE8, 0x66, 0xF8, 0x0A, 0x00 });
	InlinePatch ammoShotsText(Sites::AmmoShotsTextSite, DropCall(), BasicPatch::Status::INACTIVE, { 0xE8, 0x13, 0xCA, 0x0A, 0x00 });
	InlinePatch ammoCostMark1(Sites::AmmoCostMarkSite1, DropCall(), BasicPatch::Status::INACTIVE, { 0xE8, 0x7D, 0xDB, 0x0A, 0x00 });
	InlinePatch ammoCostMark2(Sites::AmmoCostMarkSite2, DropCall(), BasicPatch::Status::INACTIVE, { 0xE8, 0x4D, 0xDB, 0x0A, 0x00 });
	InlinePatch ammoCostMark3(Sites::AmmoCostMarkSite3, DropCall(), BasicPatch::Status::INACTIVE, { 0xE8, 0x1D, 0xDB, 0x0A, 0x00 });
}

namespace ExtraUtilities::Lua::Patches
{
	namespace
	{
		struct Group
		{
			const char* name;
			std::vector<InlinePatch*> patches;
		};

		const std::array<Group, 3>& Groups()
		{
			static const std::array<Group, 3> groups{ {
				{ "hull", { &Patch::hullLabel, &Patch::hullBar } },
				{ "ammo", { &Patch::ammoLabel, &Patch::ammoBar, &Patch::ammoShotsText,
					&Patch::ammoCostMark1, &Patch::ammoCostMark2, &Patch::ammoCostMark3 } },
				{ "weapons", { &Patch::weaponRowPlate, &Patch::weaponIcon, &Patch::weaponName } },
			} };
			return groups;
		}

		const Group& CheckGroup(lua_State* L, int idx)
		{
			const char* const name = luaL_checkstring(L, idx);
			for (const Group& group : Groups())
			{
				if (std::strcmp(group.name, name) == 0)
				{
					return group;
				}
			}
			luaL_argerror(L, idx, "expected \"hull\", \"ammo\" or \"weapons\"");
			return Groups()[0]; // unreachable: luaL_argerror raises
		}

		// Visible unless every draw of the group is suppressed, so a partial
		// install (an unsupported build or a foreign patch) reports visible.
		bool IsVisible(const Group& group)
		{
			for (const InlinePatch* patch : group.patches)
			{
				if (!patch->IsActive())
				{
					return true;
				}
			}
			return false;
		}
	}

	int GetStockStatusHudVisible(lua_State* L)
	{
		lua_pushboolean(L, IsVisible(CheckGroup(L, 1)));
		return 1;
	}

	// Returns whether the engine now matches the request; false when hiding
	// failed on an unsupported build or a site another module had patched.
	int SetStockStatusHudVisible(lua_State* L)
	{
		const Group& group = CheckGroup(L, 1);
		luaL_checktype(L, 2, LUA_TBOOLEAN);
		const bool visible = lua_toboolean(L, 2) != 0;

		const BasicPatch::Status status = visible ? BasicPatch::Status::INACTIVE : BasicPatch::Status::ACTIVE;
		for (InlinePatch* patch : group.patches)
		{
			patch->SetStatus(status);
		}

		lua_pushboolean(L, IsVisible(group) == visible);
		return 1;
	}
}
