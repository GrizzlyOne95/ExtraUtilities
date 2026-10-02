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

#include "Game/PilotAnimationPolicy.h"
#include "Game/PilotTrace.h"

#include <cstdint>

namespace ExtraUtilities::Lua::PilotFsmIntercept
{
	struct Stats
	{
		bool installed = false;
		bool active = false;
		bool observeOnly = true;
		bool hasLocalSample = false;
		bool hasPolicyDecision = false;

		std::uint32_t calls = 0;
		std::uint32_t localCalls = 0;
		std::uint32_t stateChanges = 0;
		std::uint32_t animationChanges = 0;

		std::uint32_t lastBeforeState = 0;
		std::uint32_t lastAfterState = 0;
		std::int32_t lastBeforeAnimation = -1;
		std::int32_t lastAfterAnimation = -1;
		std::int32_t lastBeforeAnimationHandle = -1;
		std::int32_t lastAfterAnimationHandle = -1;

		// What the mission-scoped pilot animation policy told the seam to do for
		// the most recent local call. Only meaningful when hasPolicyDecision.
		PilotAnimationPolicy::Decision lastPolicyDecision =
			PilotAnimationPolicy::Decision::PassThrough;
	};

	// Installs/activates the verified Person::Simulate entry detour. The hook is
	// observe-only in this work chunk: it consults the mission-scoped pilot
	// animation policy (which can only answer "pass through" today), always
	// calls the stock trampoline, and performs no native writes before or after
	// it.
	bool Install() noexcept;

	bool IsInstalled() noexcept;
	bool IsActive() noexcept;

	// Destroys the lazily allocated detour while BasicPatch's registry is still
	// alive. Called explicitly from the Lua-state shutdown path.
	void Shutdown() noexcept;

	// Also resets the timing trace: it is off, and empty, in every new Lua state.
	void ResetStats() noexcept;
	void GetStats(Stats& outStats) noexcept;

	// Opt-in, read-only timing trace of local Person::Simulate calls. Starting
	// discards earlier data; stopping keeps it readable. Records nothing while
	// the seam is inactive. Read fails only on a repeatedly torn read.
	void StartTrace(bool changesOnly) noexcept;
	void StopTrace() noexcept;
	bool ReadTrace(PilotTrace::Snapshot& outSnapshot) noexcept;
}
