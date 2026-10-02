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
#include "Scanner.h"

#include <lua.hpp>

namespace ExtraUtilities::Lua::PlayOption
{
	// This value is a packed byte of bits that serve as flags for the following
	// options. It is the player's profile, and the in-mission options menu
	// writes the same byte, so restoring the whole byte would revert the
	// player's own changes: only the bits a script changed are put back
	// (RestoreScriptChanges).
	inline Scanner playOption((uint8_t*)BZR::PlayOption::userProfilePtr, { BZR::PlayOption::playOptionOffset }, BasicScanner::Restore::DISABLED);

	// Puts back the play-option bits scripts changed during this Lua state.
	void RestoreScriptChanges() noexcept;

	inline Scanner difficulty(BZR::PlayOption::difficulty);

	int GetAutoLevel(lua_State* L);
	int SetAutoLevel(lua_State* L);
	int GetDifficulty(lua_State* L);
	int SetDifficulty(lua_State* L);
	int GetTLI(lua_State* L);
	int SetTLI(lua_State* L);
	int GetReverseMouse(lua_State* L);
	int SetReverseMouse(lua_State* L);
}