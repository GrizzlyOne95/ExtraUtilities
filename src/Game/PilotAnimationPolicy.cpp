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

// exu.fps.SetPilotAnimationProfile (AnimationApi.cpp) validates through
// ProfileBuilder and publishes through SetActive below; included here as well
// so the policy TU compiles the validator against kBuildSupport on its own.
#include "Game/PilotAnimationProfile.h"

namespace ExtraUtilities::Lua::PilotAnimationPolicy
{
	namespace
	{
		// Mission lifetime. Constant-initialised, so DllMain runs no code for it.
		PolicyPublisher g_active;

		Policy ReadActiveOrStock() noexcept
		{
			Policy policy{};
			if (!g_active.TryRead(policy))
			{
				return Policy{};
			}
			return policy;
		}
	}

	void ResetMissionState() noexcept
	{
		g_active.Publish(Policy{});
	}

	bool SetActive(const Policy& policy) noexcept
	{
		if (!IsSupported(policy, kBuildSupport))
		{
			return false;
		}
		g_active.Publish(policy);
		return true;
	}

	Policy GetActive() noexcept
	{
		return ReadActiveOrStock();
	}

	Decision EvaluateActive(std::uint32_t nativeState) noexcept
	{
		return Evaluate(ReadActiveOrStock(), nativeState);
	}
}
