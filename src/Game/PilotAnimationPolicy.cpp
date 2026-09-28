/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#include "Game/PilotAnimationPolicy.h"

namespace ExtraUtilities::Lua::PilotAnimationPolicy
{
	namespace
	{
		// Mission lifetime. Constant-initialised (see the static_assert in the
		// header), so DllMain runs no code for it.
		//
		// Only ResetMissionState() writes it, and today that only ever stores the
		// stock default, so a reader can never observe anything but stock. The
		// first writer that stores a non-stock policy must also settle how the
		// Person::Simulate hook and Lua share it (for example publishing an
		// immutable snapshot); this plain object deliberately does not pretend to
		// be that mechanism.
		Policy g_activePolicy{};
	}

	void ResetMissionState() noexcept
	{
		g_activePolicy = Policy{};
	}

	Policy GetActive() noexcept
	{
		return g_activePolicy;
	}

	Decision EvaluateActive(std::uint32_t nativeState) noexcept
	{
		return Evaluate(g_activePolicy, nativeState);
	}
}
