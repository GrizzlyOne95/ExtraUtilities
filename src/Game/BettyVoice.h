/* Copyright (C) 2026 GrizzlyOne95
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

// Betty "ammo depleted" voice for the player's own object. Scripts that park
// the engine ammo at 0 as a dead trigger (reloads, bolt cycling) can silence
// the line and play it themselves when truly dry. Docs/BETTY_AMMO_WARNING.md.

struct lua_State;

namespace ExtraUtilities::BettyVoice
{
	// Restores Betty to stock behaviour and removes the detour. Safe when not
	// installed. Mission end and Lua state teardown.
	void Shutdown() noexcept;

	// exu.SetUserAmmoWarning(enabled) -> ok, exu.GetUserAmmoWarning() -> enabled,
	// exu.PlayUserAmmoWarning() -> played.
	int SetUserAmmoWarning(lua_State* L);
	int GetUserAmmoWarning(lua_State* L);
	int PlayUserAmmoWarning(lua_State* L);
}
