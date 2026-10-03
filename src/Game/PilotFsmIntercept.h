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
		// False while table overrides are in force: the seam is active, its
		// table preimages qualified, the session is single player, and the
		// active policy is non-stock.
		bool observeOnly = true;
		// The seam is active and the native clip tables qualified at install,
		// so a non-stock policy can be applied (single player only).
		bool overridesAvailable = false;
		bool hasLocalSample = false;
		bool hasPolicyDecision = false;

		std::uint32_t calls = 0;
		std::uint32_t localCalls = 0;
		std::uint32_t stateChanges = 0;
		std::uint32_t animationChanges = 0;
		// Local calls around which the clip tables were rewritten and restored.
		std::uint32_t overrideCalls = 0;

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

	// Installs/activates the verified Person::Simulate entry detour, then
	// qualifies the native pilot clip tables (exact stock preimages of every
	// entry the override seam touches). The hook always calls the stock
	// trampoline. For the LOCAL Person only, in single player only, and only
	// when the active policy is non-stock (or a substitute clip it applied is
	// still the current one), it rewrites those table entries immediately
	// before the stock call and restores them immediately after, so every other
	// Person, and every moment outside that call, sees the stock tables.
	bool Install() noexcept;

	bool IsInstalled() noexcept;
	bool IsActive() noexcept;

	// The seam is active and its table preimages qualified. Drives the Lua
	// capability pilotAnimationOverrides together with kBuildSupport.
	bool AreOverridesAvailable() noexcept;

	// True in a network session, where every override stands down.
	bool IsNetworkSession() noexcept;

	// exu.fps.CompleteTransition(): asks the next local call to finish the
	// current crouch transition. True only when overrides are available, the
	// session is single player, the local pilot is in native state 1 or 3,
	// and the active policy gives that transition completion "manual". Lua
	// thread.
	bool CompleteTransition() noexcept;

	// Destroys the lazily allocated detour while BasicPatch's registry is still
	// alive. Called explicitly from the Lua-state shutdown path.
	void Shutdown() noexcept;

	// Also resets the timing trace (off, and empty, in every new Lua state) and
	// the override seam's per-Person bookkeeping and clip-validation cache.
	void ResetStats() noexcept;
	void GetStats(Stats& outStats) noexcept;

	// Opt-in, read-only timing trace of local Person::Simulate calls. Starting
	// discards earlier data; stopping keeps it readable. Records nothing while
	// the seam is inactive. Read fails only on a repeatedly torn read.
	void StartTrace(bool changesOnly) noexcept;
	void StopTrace() noexcept;
	bool ReadTrace(PilotTrace::Snapshot& outSnapshot) noexcept;
}
