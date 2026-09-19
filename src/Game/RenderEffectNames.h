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

// Friendly Lua names for renderer effects, and the fixed integer IDs they map
// to on the OpenShim side.
//
// The mapping is a closed set on purpose. Lua sees "ssao" and "strength";
// OpenShim sees 1 and 1. An arbitrary string namespace across a DLL boundary
// would let a mission script name renderer internals - a shader, a sampler
// slot, a compositor pass - and that is exactly the coupling this boundary
// exists to prevent. Adding a name here is a deliberate act that should
// follow, not precede, the renderer feature existing.
//
// This header deliberately has no Windows, Lua or OpenShim dependency so the
// mapping can be tested on a host with none of them. See
// tests/host/render_effect_names_tests.cpp.

#include <cstdint>
#include <string_view>

namespace ExtraUtilities::RenderEffects
{
	// Mirrors OpenShim's include/render_effect_intent.h without linking against
	// it, the same way RenderProfileBridge mirrors the render-profile ABI.
	// DO NOT RENUMBER: these values cross a DLL boundary to an independently
	// built module.
	namespace Abi
	{
		inline constexpr std::uint32_t kEffectSsao = 1u;
		inline constexpr std::uint32_t kEffectDepthHaze = 2u;
		inline constexpr std::uint32_t kEffectSoftParticles = 3u;

		inline constexpr std::uint32_t kParamStrength = 1u;
		inline constexpr std::uint32_t kParamRadius = 2u;
		inline constexpr std::uint32_t kParamFadeStart = 3u;
		inline constexpr std::uint32_t kParamFadeEnd = 4u;
		inline constexpr std::uint32_t kParamQuality = 5u;

		inline constexpr std::uint32_t kReasonEffective = 0u;
		inline constexpr std::uint32_t kReasonNotRequested = 1u;
		inline constexpr std::uint32_t kReasonUnsupportedRenderer = 2u;
		inline constexpr std::uint32_t kReasonSceneDepthUnavailable = 3u;
		inline constexpr std::uint32_t kReasonUnsupportedBuild = 4u;
		inline constexpr std::uint32_t kReasonNotImplemented = 5u;
		inline constexpr std::uint32_t kReasonUnknownEffect = 6u;
		inline constexpr std::uint32_t kReasonPending = 7u;

		inline constexpr std::uint32_t kResultAccepted = 0u;
		inline constexpr std::uint32_t kResultRejectedEffect = 1u;
		inline constexpr std::uint32_t kResultRejectedParam = 2u;
		inline constexpr std::uint32_t kResultRejectedValue = 3u;

		inline constexpr std::uint32_t kStatusVersion = 1u;
	}

	// Returns false for any name outside the closed set.
	inline bool EffectIdFromName(std::string_view name, std::uint32_t& outId)
	{
		if (name == "ssao")
		{
			outId = Abi::kEffectSsao;
			return true;
		}
		if (name == "depth_haze")
		{
			outId = Abi::kEffectDepthHaze;
			return true;
		}
		if (name == "soft_particles")
		{
			outId = Abi::kEffectSoftParticles;
			return true;
		}
		return false;
	}

	// Parameters are semantic, never implementation detail: there is no way to
	// name a shader constant or a sampler from here.
	inline bool ParameterIdFromName(std::string_view name, std::uint32_t& outId)
	{
		if (name == "strength")
		{
			outId = Abi::kParamStrength;
			return true;
		}
		if (name == "radius")
		{
			outId = Abi::kParamRadius;
			return true;
		}
		if (name == "fade_start")
		{
			outId = Abi::kParamFadeStart;
			return true;
		}
		if (name == "fade_end")
		{
			outId = Abi::kParamFadeEnd;
			return true;
		}
		if (name == "quality")
		{
			outId = Abi::kParamQuality;
			return true;
		}
		return false;
	}

	// Stable, human-readable reasons for Lua. These are part of EXU's public
	// surface, so a mission can branch on them; keep the spellings stable even
	// if OpenShim renumbers the codes behind them.
	//
	// "openshim-unavailable" has no OpenShim code because it describes the
	// absence of OpenShim itself, which only this side can observe.
	inline constexpr std::string_view kReasonOpenShimUnavailable = "openshim-unavailable";

	inline std::string_view ReasonName(std::uint32_t reasonCode)
	{
		switch (reasonCode)
		{
		case Abi::kReasonEffective:
			return "effective";
		case Abi::kReasonNotRequested:
			return "not-requested";
		case Abi::kReasonUnsupportedRenderer:
			return "unsupported-renderer";
		case Abi::kReasonSceneDepthUnavailable:
			return "scene-depth-unavailable";
		case Abi::kReasonUnsupportedBuild:
			return "unsupported-build";
		case Abi::kReasonNotImplemented:
			return "not-implemented";
		case Abi::kReasonUnknownEffect:
			return "unknown-effect";
		case Abi::kReasonPending:
			return "pending";
		default:
			// A newer OpenShim may report a code this build has never heard
			// of. Saying so beats inventing a meaning for it.
			return "unknown";
		}
	}
}
