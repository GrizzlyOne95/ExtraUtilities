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
#include "Util/EngineAddresses.generated.h"

#include <lua.hpp>

#include <cstdint>
#include <unordered_map>

namespace ExtraUtilities::Patch
{
	// Hovercraft AI turbo decision (see TurboGate.h for the instructions).
	// Hook site: the thunk in TurboGateThunk.h runs once per unit, before the
	// two compares, and sets the floats their operands read.
	constexpr uintptr_t turboPatchBeginAddr = EngineAddresses::Turbo::TurboPatchBegin;

	// disp32 of 'comiss xmm0, [tolerance]' at 0x00601CA0. Stock value 1.0f.
	constexpr uintptr_t turboToleranceOperandAddr = EngineAddresses::Turbo::TurboToleranceOperand;

	// disp32 of 'movss xmm0, [gateLimit]' at 0x00601CA9, which feeds the
	// compare in front of the final 'jbe' that vetoes turbo. Stock value 0.8f.
	constexpr uintptr_t turboGateOperandAddr = EngineAddresses::Turbo::TurboGateOperand;

	inline bool globalTurboEnabled = false;

	// Per-unit overrides: true forces turbo, false forces stock behaviour even
	// when global turbo is on. Entries for dead handles are pruned on write.
	inline std::unordered_map<BZR::handle, bool> setTurboUnits;

	// Record or read a unit's override, delegating to OpenShim when it owns
	// per-unit turbo. The setter returns whether the override takes effect.
	// Shared by exu.SetUnitTurbo/GetUnitTurbo and the AI task-state API.
	bool SetUnitTurboOverride(BZR::handle h, bool status);
	bool GetUnitTurboOverride(BZR::handle h);

	// Per-mission turbo overrides; handles are only meaningful for one mission.
	inline void ResetTurboMissionState() noexcept
	{
		setTurboUnits.clear();
		globalTurboEnabled = false;
	}
}

namespace ExtraUtilities::Lua::Patches
{
	int GetGlobalTurbo(lua_State* L);
	int SetGlobalTurbo(lua_State* L);
	int GetUnitTurbo(lua_State* L);
	int SetUnitTurbo(lua_State* L);
}