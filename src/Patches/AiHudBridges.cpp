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

#include "AiHudBridges.h"

#include "LuaHelpers.h"
#include "OpenShimBridge.h"
#include "Util/Logging.h"
#include "bzr.h"

#include <Windows.h>

namespace ExtraUtilities::Lua::Patches
{
	namespace
	{
		using OpenShimSetUnderAttackAlertModeFn = BOOL(WINAPI*)(int);
		using OpenShimSetTargetReticlePopupModeFn = BOOL(WINAPI*)(int);
		using OpenShimSetBomberAiRangeEnabledFn = BOOL(WINAPI*)(BOOL);
		using OpenShimSetHowitzerVolleyEnabledFn = BOOL(WINAPI*)(BOOL);
		using OpenShimSetWeaponMaskCarrierBiasEnabledFn = BOOL(WINAPI*)(BOOL);
        using OpenShimSetAiOdfGameplayTuningEnabledFn = BOOL(WINAPI*)(BOOL);
        using OpenShimSetAiUnitTuningFn = BOOL(WINAPI*)(void*, float, float, float);
        using OpenShimSetAiUnitTuningV2Fn = BOOL(WINAPI*)(void*, float, float, float,
                                                          float, float, float, BOOL);
        using OpenShimSetAiUnitTuningV3Fn = BOOL(WINAPI*)(void*, float, float, float,
                                                          float, float, float, BOOL,
                                                          float, float);
        using OpenShimClearAiUnitTuningFn = BOOL(WINAPI*)(void*);
        using OpenShimClearAllAiUnitTuningFn = BOOL(WINAPI*)();
        using OpenShimSetTurretAimPitchEnabledFn = BOOL(WINAPI*)(BOOL);
        using OpenShimSetAttackRevealEnabledFn = BOOL(WINAPI*)(BOOL);
        using OpenShimSetJumpSnipeCrouchEnabledFn = BOOL(WINAPI*)(BOOL);
		using OpenShimResetMissionHookOverridesFn = BOOL(WINAPI*)();

		OpenShimSetUnderAttackAlertModeFn ResolveUnderAttackAlertBridge()
		{
			static constinit OpenShimBridge::CachedExport<OpenShimSetUnderAttackAlertModeFn> bridge{
				"OpenShimSetUnderAttackAlertMode",
				"[EXU::UnitVo] OpenShim under-attack alert bridge unavailable" };
			return bridge.Get();
		}

		OpenShimSetTargetReticlePopupModeFn ResolveTargetReticlePopupBridge()
		{
			static constinit OpenShimBridge::CachedExport<OpenShimSetTargetReticlePopupModeFn> bridge{
				"OpenShimSetTargetReticlePopupMode",
				"[EXU::UnitVo] OpenShim target reticle popup bridge unavailable" };
			return bridge.Get();
		}

		OpenShimSetBomberAiRangeEnabledFn ResolveBomberAiRangeBridge()
		{
			static constinit OpenShimBridge::CachedExport<OpenShimSetBomberAiRangeEnabledFn> bridge{
				"OpenShimSetBomberAiRangeEnabled",
				"[EXU::UnitVo] OpenShim bomber AI range bridge unavailable" };
			return bridge.Get();
		}

		OpenShimSetHowitzerVolleyEnabledFn ResolveHowitzerVolleyBridge()
		{
			static constinit OpenShimBridge::CachedExport<OpenShimSetHowitzerVolleyEnabledFn> bridge{
				"OpenShimSetHowitzerVolleyEnabled",
				"[EXU::UnitVo] OpenShim howitzer volley bridge unavailable" };
			return bridge.Get();
		}

		OpenShimSetWeaponMaskCarrierBiasEnabledFn ResolveWeaponMaskCarrierBiasBridge()
		{
			static constinit OpenShimBridge::CachedExport<OpenShimSetWeaponMaskCarrierBiasEnabledFn> bridge{
				"OpenShimSetWeaponMaskCarrierBiasEnabled",
				"[EXU::UnitVo] OpenShim weapon-mask carrier bias bridge unavailable" };
			return bridge.Get();
		}

        OpenShimSetAiOdfGameplayTuningEnabledFn ResolveAiOdfGameplayTuningBridge()
        {
            static constinit OpenShimBridge::CachedExport<OpenShimSetAiOdfGameplayTuningEnabledFn> bridge{
                "OpenShimSetAiOdfGameplayTuningEnabled",
                "[EXU::UnitVo] OpenShim AI ODF gameplay tuning bridge unavailable" };
            return bridge.Get();
        }

        OpenShimSetAiUnitTuningFn ResolveAiUnitTuningSetBridge()
        {
            static constinit OpenShimBridge::CachedExport<OpenShimSetAiUnitTuningFn> bridge{
                "OpenShimSetAiUnitTuning",
                "[EXU::UnitVo] OpenShim per-unit AI tuning bridge unavailable" };
            return bridge.Get();
        }

        OpenShimSetAiUnitTuningV2Fn ResolveAiUnitTuningSetV2Bridge()
        {
            static constinit OpenShimBridge::CachedExport<OpenShimSetAiUnitTuningV2Fn> bridge{
                "OpenShimSetAiUnitTuningV2" };
            return bridge.Get();
        }

        OpenShimSetAiUnitTuningV3Fn ResolveAiUnitTuningSetV3Bridge()
        {
            static constinit OpenShimBridge::CachedExport<OpenShimSetAiUnitTuningV3Fn> bridge{
                "OpenShimSetAiUnitTuningV3" };
            return bridge.Get();
        }

        OpenShimClearAiUnitTuningFn ResolveAiUnitTuningClearBridge()
        {
            static constinit OpenShimBridge::CachedExport<OpenShimClearAiUnitTuningFn> bridge{
                "OpenShimClearAiUnitTuning",
                "[EXU::UnitVo] OpenShim per-unit AI tuning clear bridge unavailable" };
            return bridge.Get();
        }

        OpenShimClearAllAiUnitTuningFn ResolveAiUnitTuningClearAllBridge()
        {
            static constinit OpenShimBridge::CachedExport<OpenShimClearAllAiUnitTuningFn> bridge{
                "OpenShimClearAllAiUnitTuning",
                "[EXU::UnitVo] OpenShim per-unit AI tuning clear-all bridge unavailable" };
            return bridge.Get();
        }

        OpenShimSetTurretAimPitchEnabledFn ResolveTurretAimPitchBridge()
        {
            static constinit OpenShimBridge::CachedExport<OpenShimSetTurretAimPitchEnabledFn> bridge{
                "OpenShimSetTurretAimPitchEnabled",
                "[EXU::UnitVo] OpenShim turret aim pitch bridge unavailable" };
            return bridge.Get();
        }

        OpenShimSetAttackRevealEnabledFn ResolveAttackRevealBridge()
        {
            static constinit OpenShimBridge::CachedExport<OpenShimSetAttackRevealEnabledFn> bridge{
                "OpenShimSetAttackRevealEnabled",
                "[EXU::UnitVo] OpenShim attack reveal bridge unavailable" };
            return bridge.Get();
        }

        OpenShimSetJumpSnipeCrouchEnabledFn ResolveJumpSnipeCrouchBridge()
        {
            static constinit OpenShimBridge::CachedExport<OpenShimSetJumpSnipeCrouchEnabledFn> bridge{
                "OpenShimSetJumpSnipeCrouchEnabled",
                "[EXU::UnitVo] OpenShim jump-snipe crouch bridge unavailable" };
            return bridge.Get();
        }

		OpenShimResetMissionHookOverridesFn ResolveMissionHookResetBridge()
		{
			static constinit OpenShimBridge::CachedExport<OpenShimResetMissionHookOverridesFn> bridge{
				"OpenShimResetMissionHookOverrides",
				"[EXU::UnitVo] OpenShim mission-hook reset bridge unavailable" };
			return bridge.Get();
		}
	}

	int SetUnderAttackAlertMode(lua_State* L)
	{
		lua_Integer requested = luaL_checkinteger(L, 1);
		if (requested < 1 || requested > 3)
		{
			return luaL_argerror(L, 1, "Extra Utilities Error: under-attack alert mode must be 1-3");
		}

		if (OpenShimSetUnderAttackAlertModeFn fn = ResolveUnderAttackAlertBridge())
		{
			lua_pushboolean(L, fn(static_cast<int>(requested)) ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}

	int SetTargetReticlePopupMode(lua_State* L)
	{
		lua_Integer requested = luaL_checkinteger(L, 1);
		if (requested < 1 || requested > 3)
		{
			return luaL_argerror(L, 1, "Extra Utilities Error: target reticle popup mode must be 1-3");
		}

		if (OpenShimSetTargetReticlePopupModeFn fn = ResolveTargetReticlePopupBridge())
		{
			lua_pushboolean(L, fn(static_cast<int>(requested)) ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}

	int SetBomberAiRangeEnabled(lua_State* L)
	{
		const BOOL requested = lua_toboolean(L, 1) ? TRUE : FALSE;
		if (OpenShimSetBomberAiRangeEnabledFn fn = ResolveBomberAiRangeBridge())
		{
			lua_pushboolean(L, fn(requested) ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}

	int SetHowitzerVolleyEnabled(lua_State* L)
	{
		const BOOL requested = lua_toboolean(L, 1) ? TRUE : FALSE;
		if (OpenShimSetHowitzerVolleyEnabledFn fn = ResolveHowitzerVolleyBridge())
		{
			lua_pushboolean(L, fn(requested) ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}

	int SetWeaponMaskCarrierBiasEnabled(lua_State* L)
	{
		const BOOL requested = lua_toboolean(L, 1) ? TRUE : FALSE;
		if (OpenShimSetWeaponMaskCarrierBiasEnabledFn fn = ResolveWeaponMaskCarrierBiasBridge())
		{
			lua_pushboolean(L, fn(requested) ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}

    int SetAiOdfGameplayTuningEnabled(lua_State* L)
    {
        const BOOL requested = lua_toboolean(L, 1) ? TRUE : FALSE;
        if (OpenShimSetAiOdfGameplayTuningEnabledFn fn = ResolveAiOdfGameplayTuningBridge())
        {
            lua_pushboolean(L, fn(requested) ? 1 : 0);
            return 1;
        }

        lua_pushboolean(L, 0);
        return 1;
    }

    // exu.SetAiUnitTuning(handle, { engageRange = m, weaponRangeMin = m,
    //   retargetPeriod = s, kiteDesiredRange = m, kiteEnterRange = m,
    //   kiteExitRange = m, kitePreserveLos = bool })
    // Values act as per-unit floors applied by the OpenShim CalcRange/retarget hooks;
    // they win over ODF-level tuning. Omitted/nil keys are unset; an empty table clears.
    int SetAiUnitTuning(lua_State* L)
    {
        const BZR::handle h = CheckHandle(L, 1);

        float engageRange = -1.0f;
        float weaponRangeMin = -1.0f;
        float retargetPeriod = -1.0f;
        float kiteDesiredRange = -1.0f;
        float kiteEnterRange = -1.0f;
        float kiteExitRange = -1.0f;
        BOOL kitePreserveLos = FALSE;
        float kiteStrafe = -1.0f;
        float kiteSwitchPeriod = -1.0f;

        if (!lua_isnoneornil(L, 2))
        {
            luaL_checktype(L, 2, LUA_TTABLE);

            lua_getfield(L, 2, "engageRange");
            if (lua_isnumber(L, -1))
            {
                engageRange = static_cast<float>(lua_tonumber(L, -1));
            }
            lua_pop(L, 1);

            lua_getfield(L, 2, "weaponRangeMin");
            if (lua_isnumber(L, -1))
            {
                weaponRangeMin = static_cast<float>(lua_tonumber(L, -1));
            }
            lua_pop(L, 1);

            lua_getfield(L, 2, "retargetPeriod");
            if (lua_isnumber(L, -1))
            {
                retargetPeriod = static_cast<float>(lua_tonumber(L, -1));
            }
            lua_pop(L, 1);

            lua_getfield(L, 2, "kiteDesiredRange");
            if (lua_isnumber(L, -1))
            {
                kiteDesiredRange = static_cast<float>(lua_tonumber(L, -1));
            }
            lua_pop(L, 1);

            lua_getfield(L, 2, "kiteEnterRange");
            if (lua_isnumber(L, -1))
            {
                kiteEnterRange = static_cast<float>(lua_tonumber(L, -1));
            }
            lua_pop(L, 1);

            lua_getfield(L, 2, "kiteExitRange");
            if (lua_isnumber(L, -1))
            {
                kiteExitRange = static_cast<float>(lua_tonumber(L, -1));
            }
            lua_pop(L, 1);

            lua_getfield(L, 2, "kitePreserveLos");
            if (lua_isboolean(L, -1))
            {
                kitePreserveLos = lua_toboolean(L, -1) ? TRUE : FALSE;
            }
            lua_pop(L, 1);

            lua_getfield(L, 2, "kiteStrafe");
            if (lua_isnumber(L, -1))
            {
                kiteStrafe = static_cast<float>(lua_tonumber(L, -1));
            }
            lua_pop(L, 1);

            lua_getfield(L, 2, "kiteSwitchPeriod");
            if (lua_isnumber(L, -1))
            {
                kiteSwitchPeriod = static_cast<float>(lua_tonumber(L, -1));
            }
            lua_pop(L, 1);
        }

        OpenShimSetAiUnitTuningV3Fn fnV3 = ResolveAiUnitTuningSetV3Bridge();
        OpenShimSetAiUnitTuningV2Fn fnV2 = ResolveAiUnitTuningSetV2Bridge();
        OpenShimSetAiUnitTuningFn fn = ResolveAiUnitTuningSetBridge();
        const bool requestedKite = kiteDesiredRange > 0.0f ||
                                   kiteEnterRange > 0.0f ||
                                   kiteExitRange > 0.0f;
        const bool requestedStrafe = kiteStrafe > 0.0f || kiteSwitchPeriod > 0.0f;
        if (!fnV3 && requestedStrafe)
        {
            lua_pushboolean(L, 0);
            return 1;
        }
        if (!fnV3 && !fnV2 && (!fn || requestedKite))
        {
            lua_pushboolean(L, 0);
            return 1;
        }

        BZR::GameObject* obj = BZR::GameObject::GetObj(h);
        if (obj == nullptr)
        {
            lua_pushboolean(L, 0);
            return 1;
        }

        const BOOL ok = fnV3
            ? fnV3(obj,
                   engageRange,
                   weaponRangeMin,
                   retargetPeriod,
                   kiteDesiredRange,
                   kiteEnterRange,
                   kiteExitRange,
                   kitePreserveLos,
                   kiteStrafe,
                   kiteSwitchPeriod)
            : fnV2
            ? fnV2(obj,
                   engageRange,
                   weaponRangeMin,
                   retargetPeriod,
                   kiteDesiredRange,
                   kiteEnterRange,
                   kiteExitRange,
                   kitePreserveLos)
            : fn(obj, engageRange, weaponRangeMin, retargetPeriod);
        if (ok)
        {
            const bool hasEngage = engageRange > 0.0f;
            const bool hasWeaponMin = weaponRangeMin > 0.0f;
            const bool hasRetarget = retargetPeriod > 0.0f;
            const bool hasKite = kiteDesiredRange > 0.0f &&
                                 kiteEnterRange > 0.0f &&
                                 kiteExitRange > kiteEnterRange &&
                                 kiteDesiredRange > kiteEnterRange &&
                                 kiteDesiredRange < kiteExitRange;
            if (hasEngage || hasWeaponMin || hasRetarget || hasKite)
            {
                Patch::AiUnitTuningMirror mirror = {};
                mirror.hasEngageRange = hasEngage;
                mirror.engageRange = hasEngage ? engageRange : 0.0f;
                mirror.hasWeaponRangeMin = hasWeaponMin;
                mirror.weaponRangeMin = hasWeaponMin ? weaponRangeMin : 0.0f;
                mirror.hasRetargetPeriod = hasRetarget;
                mirror.retargetPeriod = hasRetarget ? retargetPeriod : 0.0f;
                mirror.hasKiteRanges = hasKite;
                mirror.kiteDesiredRange = hasKite ? kiteDesiredRange : 0.0f;
                mirror.kiteEnterRange = hasKite ? kiteEnterRange : 0.0f;
                mirror.kiteExitRange = hasKite ? kiteExitRange : 0.0f;
                mirror.kitePreserveLos = hasKite && kitePreserveLos != FALSE;
                mirror.kiteStrafe = hasKite && kiteStrafe > 0.0f ? kiteStrafe : 0.0f;
                mirror.kiteSwitchPeriod = hasKite && kiteSwitchPeriod > 0.0f ? kiteSwitchPeriod : 0.0f;
                Patch::aiUnitTuning[static_cast<uint32_t>(h)] = mirror;
            }
            else
            {
                Patch::aiUnitTuning.erase(static_cast<uint32_t>(h));
            }
        }
        lua_pushboolean(L, ok ? 1 : 0);
        return 1;
    }

    int GetAiUnitTuning(lua_State* L)
    {
        const BZR::handle h = CheckHandle(L, 1);
        const auto it = Patch::aiUnitTuning.find(static_cast<uint32_t>(h));
        if (it == Patch::aiUnitTuning.end())
        {
            lua_pushnil(L);
            return 1;
        }

        lua_createtable(L, 0, 9);
        if (it->second.hasEngageRange)
        {
            lua_pushnumber(L, it->second.engageRange);
            lua_setfield(L, -2, "engageRange");
        }
        if (it->second.hasWeaponRangeMin)
        {
            lua_pushnumber(L, it->second.weaponRangeMin);
            lua_setfield(L, -2, "weaponRangeMin");
        }
        if (it->second.hasRetargetPeriod)
        {
            lua_pushnumber(L, it->second.retargetPeriod);
            lua_setfield(L, -2, "retargetPeriod");
        }
        if (it->second.hasKiteRanges)
        {
            lua_pushnumber(L, it->second.kiteDesiredRange);
            lua_setfield(L, -2, "kiteDesiredRange");
            lua_pushnumber(L, it->second.kiteEnterRange);
            lua_setfield(L, -2, "kiteEnterRange");
            lua_pushnumber(L, it->second.kiteExitRange);
            lua_setfield(L, -2, "kiteExitRange");
            lua_pushboolean(L, it->second.kitePreserveLos ? 1 : 0);
            lua_setfield(L, -2, "kitePreserveLos");
            if (it->second.kiteStrafe > 0.0f)
            {
                lua_pushnumber(L, it->second.kiteStrafe);
                lua_setfield(L, -2, "kiteStrafe");
            }
            if (it->second.kiteSwitchPeriod > 0.0f)
            {
                lua_pushnumber(L, it->second.kiteSwitchPeriod);
                lua_setfield(L, -2, "kiteSwitchPeriod");
            }
        }
        return 1;
    }

    int ClearAiUnitTuning(lua_State* L)
    {
        const BZR::handle h = CheckHandle(L, 1);
        Patch::aiUnitTuning.erase(static_cast<uint32_t>(h));

        OpenShimClearAiUnitTuningFn fn = ResolveAiUnitTuningClearBridge();
        if (!fn)
        {
            lua_pushboolean(L, 0);
            return 1;
        }

        BZR::GameObject* obj = BZR::GameObject::GetObj(h);
        if (obj == nullptr)
        {
            lua_pushboolean(L, 0);
            return 1;
        }

        lua_pushboolean(L, fn(obj) ? 1 : 0);
        return 1;
    }

    int ClearAllAiUnitTuning(lua_State* L)
    {
        Patch::aiUnitTuning.clear();
        if (OpenShimClearAllAiUnitTuningFn fn = ResolveAiUnitTuningClearAllBridge())
        {
            lua_pushboolean(L, fn() ? 1 : 0);
            return 1;
        }

        lua_pushboolean(L, 0);
        return 1;
    }

    int SetTurretAimPitchEnabled(lua_State* L)
    {
        const BOOL requested = lua_toboolean(L, 1) ? TRUE : FALSE;
        if (OpenShimSetTurretAimPitchEnabledFn fn = ResolveTurretAimPitchBridge())
        {
            lua_pushboolean(L, fn(requested) ? 1 : 0);
            return 1;
        }

        lua_pushboolean(L, 0);
        return 1;
    }

    int SetAttackRevealEnabled(lua_State* L)
    {
        const BOOL requested = lua_toboolean(L, 1) ? TRUE : FALSE;
        if (OpenShimSetAttackRevealEnabledFn fn = ResolveAttackRevealBridge())
        {
            lua_pushboolean(L, fn(requested) ? 1 : 0);
            return 1;
        }

        lua_pushboolean(L, 0);
        return 1;
    }

    int SetJumpSnipeCrouch(lua_State* L)
    {
        const BOOL requested = lua_toboolean(L, 1) ? TRUE : FALSE;
        if (OpenShimSetJumpSnipeCrouchEnabledFn fn = ResolveJumpSnipeCrouchBridge())
        {
            // Returns the effective state; false means the shim suppressed it
            // (e.g. multiplayer, or the patch site did not match this build).
            lua_pushboolean(L, fn(requested) ? 1 : 0);
            return 1;
        }

        lua_pushboolean(L, 0);
        return 1;
    }

    void ApplyJumpSnipeCrouchDefault()
    {
        if (OpenShimSetJumpSnipeCrouchEnabledFn fn = ResolveJumpSnipeCrouchBridge())
        {
            fn(TRUE);
        }
    }

	int ResetMissionHookOverrides(lua_State* L)
	{
		UNREFERENCED_PARAMETER(L);
		if (OpenShimResetMissionHookOverridesFn fn = ResolveMissionHookResetBridge())
		{
			lua_pushboolean(L, fn() ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}

	void ResetOpenShimMissionOverrides()
	{
		// The shim-side reset also clears its per-unit AI tuning map.
		Patch::aiUnitTuning.clear();
		if (OpenShimResetMissionHookOverridesFn fn = ResolveMissionHookResetBridge())
		{
			fn();
		}
	}
}
