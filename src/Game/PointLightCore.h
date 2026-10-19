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

// Pure rules for EXU's free-standing world point lights (PointLights.cpp):
// the cap, name and value limits, the attenuation curve a bare range maps to,
// and how the power scale is applied. No Windows, Ogre or Lua dependency, so
// tests/host can exercise it.

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace ExtraUtilities::PointLightCore
{
	// Ogre builds one light list per renderable and the shader evaluates only
	// the first N of it: N is 8 for stock Redux "high" materials (the sun plus
	// seven locals), 1 for stock "low", and 24 / 8 / 1 for Campaign
	// Reimagined's Enhanced High / Medium / Low. Every EXU light that reaches
	// a renderable competes for those slots with the sun, headlights and
	// weapon/explosion lights, ordered by distance. Eight mission lights is
	// already enough to evict a headlight from a terrain patch under stock
	// "high" when they overlap, so the cap is a hard ceiling, not a budget to
	// fill. See Docs/POINT_LIGHTS.md.
	constexpr std::size_t kMaxPointLights = 8;

	constexpr std::size_t kMaxNameLength = 64;

	// Range is the Ogre attenuation range: lights farther than this from a
	// renderable's bounding sphere are not offered to it at all. A huge range
	// puts the light in every list on the map, which is exactly the slot
	// competition the cap exists to bound.
	constexpr float kMaxRange = 2000.0f;
	constexpr float kMaxColourComponent = 100.0f;
	constexpr float kMaxPowerScale = 100.0f;
	constexpr float kMaxAttenuationTerm = 1.0e6f;

	// Prefixes for the names EXU gives Ogre, so a mission light can never
	// collide with an engine light or another subsystem's scene node.
	constexpr std::string_view kOgreLightPrefix = "__exu_pl_";
	constexpr std::string_view kOgreNodePrefix = "__exu_pl_node_";

	struct Colour
	{
		float r = 1.0f;
		float g = 1.0f;
		float b = 1.0f;
	};

	struct Attenuation
	{
		float range = 100.0f;
		float constant = 1.0f;
		float linear = 0.0f;
		float quadratic = 0.0015f;
	};

	inline bool IsFinite(float value) noexcept
	{
		return std::isfinite(value);
	}

	inline bool IsValidName(std::string_view name) noexcept
	{
		if (name.empty() || name.size() > kMaxNameLength)
		{
			return false;
		}
		for (const char c : name)
		{
			const unsigned char u = static_cast<unsigned char>(c);
			if (u < 0x20 || u == 0x7F)
			{
				return false;
			}
		}
		return true;
	}

	inline std::string OgreLightName(std::string_view name)
	{
		std::string result(kOgreLightPrefix);
		result += name;
		return result;
	}

	inline std::string OgreNodeName(std::string_view name)
	{
		std::string result(kOgreNodePrefix);
		result += name;
		return result;
	}

	inline bool IsValidRange(float range) noexcept
	{
		return IsFinite(range) && range > 0.0f && range <= kMaxRange;
	}

	inline bool IsValidColour(const Colour& colour) noexcept
	{
		const float components[3] = { colour.r, colour.g, colour.b };
		for (const float c : components)
		{
			if (!IsFinite(c) || c < 0.0f || c > kMaxColourComponent)
			{
				return false;
			}
		}
		return true;
	}

	inline bool IsValidPowerScale(float scale) noexcept
	{
		return IsFinite(scale) && scale >= 0.0f && scale <= kMaxPowerScale;
	}

	inline bool IsValidAttenuation(const Attenuation& attenuation) noexcept
	{
		const float terms[3] = { attenuation.constant, attenuation.linear, attenuation.quadratic };
		bool anyPositive = false;
		for (const float t : terms)
		{
			if (!IsFinite(t) || t < 0.0f || t > kMaxAttenuationTerm)
			{
				return false;
			}
			anyPositive = anyPositive || t > 0.0f;
		}
		// All three zero divides by zero in the shader.
		return IsValidRange(attenuation.range) && anyPositive;
	}

	// The curve Redux gives its own weapon, explosion and map lights
	// (LightRender, GOG VA 0x0044C840): constant 1, linear 0, quadratic
	// 15 / range^2, so intensity falls to 1/16 at the range edge. Using the
	// engine's curve means a mission light of a given range reads as bright as
	// a stock light of that range.
	inline Attenuation AttenuationForRange(float range) noexcept
	{
		Attenuation result;
		result.range = range;
		result.constant = 1.0f;
		result.linear = 0.0f;
		result.quadratic = 15.0f / (range * range);
		return result;
	}

	// Neither stock Redux nor Campaign Reimagined shaders read Ogre's
	// light_power parameter, so Light::setPowerScale would be a no-op on
	// screen. EXU applies the power scale to the colours it uploads instead
	// and leaves Ogre's own power scale at 1.
	inline Colour ScaledColour(const Colour& colour, float powerScale) noexcept
	{
		return Colour{ colour.r * powerScale, colour.g * powerScale, colour.b * powerScale };
	}

	inline bool HasCapacity(std::size_t liveCount) noexcept
	{
		return liveCount < kMaxPointLights;
	}
}
