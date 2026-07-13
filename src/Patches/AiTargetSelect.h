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

namespace ExtraUtilities::Patch::AiTargetSelect
{
	// Installs the ChooseAttackTarget vtable-slot hooks (idempotent).
	// Every slot is RTTI-verified and its current value checked against the
	// expected implementation before it is written; verification failure
	// aborts that slot without patching.
	bool Install();

	// Restores every patched slot (called automatically on DLL unload).
	void Uninstall();

	// Gates dispatch into Lua; patches stay installed but pass through
	// untouched when disabled.
	inline bool dispatchEnabled = false;

	// Gates per-candidate distance-score adjustment inside the stock target
	// search. Eligibility, category lanes, range limits, and final assignment
	// remain native; Lua may only return an adjusted comparison score.
	inline bool scoreDispatchEnabled = false;
}

namespace ExtraUtilities::Lua::Patches
{
	// exu.SetAiTargetSelectEnabled(bool) -> bool
	// When enabled, exu.AiTargetSelect(unitHandle, candidateHandle|nil,
	// rangeLimit|nil) is invoked after native AI target selection and may
	// return nil (keep), false (veto -> no target), or a handle (override).
	int SetAiTargetSelectEnabled(lua_State* L);
	int GetAiTargetSelectEnabled(lua_State* L);
	int SetAiTargetScoringEnabled(lua_State* L);
	int GetAiTargetScoringEnabled(lua_State* L);
}
