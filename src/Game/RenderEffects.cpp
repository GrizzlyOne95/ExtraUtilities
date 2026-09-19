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

#include "RenderEffects.h"

#include "LuaHelpers.h"
#include "RenderEffectBridge.h"
#include "Util/Logging.h"

#include <cmath>
#include <string>
#include <string_view>

namespace ExtraUtilities::Lua::Render
{
	namespace
	{
		namespace Names = ExtraUtilities::RenderEffects;

		// An unknown effect or parameter name is a script bug, not a runtime
		// condition: the name set is closed, small and documented. Erroring is
		// louder and more useful than returning false, which would be
		// indistinguishable from "OpenShim said no".
		std::uint32_t CheckEffectId(lua_State* L, int index)
		{
			const std::string_view name = luaL_checkstring(L, index);
			std::uint32_t effectId = 0;
			if (!Names::EffectIdFromName(name, effectId))
			{
				luaL_argerror(L, index, "unknown render effect; expected ssao, depth_haze, or soft_particles");
			}
			return effectId;
		}

		std::uint32_t CheckParameterId(lua_State* L, int index)
		{
			const std::string_view name = luaL_checkstring(L, index);
			std::uint32_t paramId = 0;
			if (!Names::ParameterIdFromName(name, paramId))
			{
				luaL_argerror(L, index, "unknown render effect parameter; expected strength, radius, fade_start, fade_end, or quality");
			}
			return paramId;
		}

		// One line per missing capability, not one per call. An absent OpenShim
		// is a permanent condition and a mission may poll status every frame.
		void WarnOnceIfUnavailable()
		{
			static bool warned = false;
			if (warned || RenderEffectBridge::IsAvailable())
			{
				return;
			}

			warned = true;
			Logging::LogMessage(
				"exu: render-effect bridge unavailable (OpenShim absent or predates the "
				"render-effect ABI); renderer effect requests will be ignored");
		}
	}

	int SetRenderEffectEnabled(lua_State* L)
	{
		const std::uint32_t effectId = CheckEffectId(L, 1);
		const bool enabled = CheckBool(L, 2);

		WarnOnceIfUnavailable();

		// True means "OpenShim recorded the request", which is deliberately
		// not the same as "the effect is now running". GetRenderEffectStatus
		// is the only thing that answers that.
		const std::uint32_t result = RenderEffectBridge::SetEnabled(effectId, enabled);
		lua_pushboolean(L, result == Names::Abi::kResultAccepted ? 1 : 0);
		return 1;
	}

	int SetRenderEffectFloat(lua_State* L)
	{
		const std::uint32_t effectId = CheckEffectId(L, 1);
		const std::uint32_t paramId = CheckParameterId(L, 2);
		const double value = luaL_checknumber(L, 3);

		if (!std::isfinite(value))
		{
			return luaL_argerror(L, 3, "SetRenderEffectFloat requires a finite value");
		}

		WarnOnceIfUnavailable();

		// OpenShim owns the ranges and clamps to them; EXU does not duplicate
		// that policy, or the two would drift.
		const std::uint32_t result =
			RenderEffectBridge::SetFloat(effectId, paramId, static_cast<float>(value));
		lua_pushboolean(L, result == Names::Abi::kResultAccepted ? 1 : 0);
		return 1;
	}

	// Returns requested, supported, effective, reason.
	//
	// Three flags rather than one, because they answer three different
	// questions and a mission that can only see one of them cannot tell a DX9
	// machine from a machine where nobody turned the effect on.
	int GetRenderEffectStatus(lua_State* L)
	{
		const std::uint32_t effectId = CheckEffectId(L, 1);

		RenderEffectBridge::Status status{};
		const bool ok = RenderEffectBridge::GetStatus(effectId, status);

		if (!ok)
		{
			// Only this side can observe OpenShim's absence, so it gets its own
			// reason rather than being folded into one of OpenShim's codes.
			lua_pushboolean(L, 0);
			lua_pushboolean(L, 0);
			lua_pushboolean(L, 0);
			lua_pushlstring(
				L,
				Names::kReasonOpenShimUnavailable.data(),
				Names::kReasonOpenShimUnavailable.size());
			return 4;
		}

		const std::string_view reason = Names::ReasonName(status.reasonCode);
		lua_pushboolean(L, status.requested != FALSE ? 1 : 0);
		lua_pushboolean(L, status.supported != FALSE ? 1 : 0);
		lua_pushboolean(L, status.effective != FALSE ? 1 : 0);
		lua_pushlstring(L, reason.data(), reason.size());
		return 4;
	}

	int ResetRenderEffects(lua_State* L)
	{
		lua_pushboolean(L, RenderEffectBridge::Reset() ? 1 : 0);
		return 1;
	}
}
