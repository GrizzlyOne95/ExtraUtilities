/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#pragma once

#include <cstdint>

namespace ExtraUtilities::Lua::PilotState
{
	inline const char* SemanticStateName(std::uint32_t nativeState) noexcept
	{
		switch (nativeState)
		{
		case 0: return "standing";
		case 1: return "enteringCrouch";
		case 2: return "crouched";
		case 3: return "exitingCrouch";
		default: return "unknown";
		}
	}

	inline bool IsTransitionState(std::uint32_t nativeState) noexcept
	{
		return nativeState == 1u || nativeState == 3u;
	}

	inline bool IsFullyCrouchedState(std::uint32_t nativeState) noexcept
	{
		return nativeState == 2u;
	}

	inline const char* KnownAnimationName(std::int32_t animationIndex) noexcept
	{
		switch (animationIndex)
		{
		case 0: return "stand2Kneel";
		case 1: return "kneel2stand";
		case 2: return "idle";
		case 3: return "fireRecoilSniper";
		case 10: return "landParachute";
		case 11: return "jump";
		default: return nullptr;
		}
	}
}
