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

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ExtraUtilities::Patch
{
	using UnitVoQueueFn = int (__cdecl*)(const char* filename, void* owner, int priority);

	inline bool unitVoMuted = false;
	inline uint32_t unitVoThrottleMs = 0;
	inline uint32_t unitVoQueueDepthLimit = 2;
	inline uint32_t unitVoQueueStaleMs = 2000;
	inline std::unordered_map<std::string, std::vector<std::string>> unitVoAlternates;

	// Local mirror of per-unit AI tuning pushed through the OpenShim bridge,
	// keyed by game handle so scripts can read back what they set.
	struct AiUnitTuningMirror
	{
		bool hasEngageRange = false;
		float engageRange = 0.0f;
		bool hasWeaponRangeMin = false;
		float weaponRangeMin = 0.0f;
		bool hasRetargetPeriod = false;
		float retargetPeriod = 0.0f;
		bool hasKiteRanges = false;
		float kiteDesiredRange = 0.0f;
		float kiteEnterRange = 0.0f;
		float kiteExitRange = 0.0f;
		bool kitePreserveLos = false;
		float kiteStrafe = 0.0f;
		float kiteSwitchPeriod = 0.0f;
	};
	inline std::unordered_map<uint32_t, AiUnitTuningMirror> aiUnitTuning;
}

namespace ExtraUtilities::Lua::Patches
{
	int GetUnitVoThrottle(lua_State* L);
	int SetUnitVoThrottle(lua_State* L);
	int GetUnitVoQueueDepthLimit(lua_State* L);
	int SetUnitVoQueueDepthLimit(lua_State* L);
	int GetUnitVoQueueStaleMs(lua_State* L);
	int SetUnitVoQueueStaleMs(lua_State* L);
	int GetUnitVoMuted(lua_State* L);
	int SetUnitVoMuted(lua_State* L);
	int GetUnitVoAlternates(lua_State* L);
	int SetUnitVoAlternates(lua_State* L);
	int SetUnderAttackAlertMode(lua_State* L);
	int SetTargetReticlePopupMode(lua_State* L);
	int SetBomberAiRangeEnabled(lua_State* L);
	int SetHowitzerVolleyEnabled(lua_State* L);
	int SetWeaponMaskCarrierBiasEnabled(lua_State* L);
	int SetAiOdfGameplayTuningEnabled(lua_State* L);
	int SetAiUnitTuning(lua_State* L);
	int GetAiUnitTuning(lua_State* L);
	int ClearAiUnitTuning(lua_State* L);
	int ClearAllAiUnitTuning(lua_State* L);
	int SetTurretAimPitchEnabled(lua_State* L);
	int SetAttackRevealEnabled(lua_State* L);
	int ResetMissionHookOverrides(lua_State* L);
	void ResetOpenShimMissionOverrides();
}
