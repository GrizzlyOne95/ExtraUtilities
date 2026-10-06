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

// Long person clips (exu.animation.SetPersonLongClips).
//
// The stock end time (0.967 s) freezes any run clip longer than it mid-stride,
// looped or not, so creatures with long gait cycles slide over the ground; a
// long idle stops at 0.967 s. With runs on, EXU raises endTime[4..7] around
// each Person::Simulate call made while that Person plays a run, and puts the
// stock value back right after. With idle on, a WORLD idle longer than the end
// time is looped (Ogre setLoop) when the Person enters idle and its end time
// is raised the same way. Short stock clips behave exactly as before. Pure
// presentation (no FSM transition reads these entries), so also in
// multiplayer. See PersonLongClipsCore.h.

#include "Game/PersonLongClipsCore.h"

#include <cstdint>

namespace ExtraUtilities::Lua::PersonLongClips
{
	struct Stats
	{
		bool available = false;
		bool faulted = false;
		// Calls made with a raised entry, and long idles looped this mission.
		std::uint32_t raisedCalls = 0;
		std::uint32_t idleLoops = 0;
	};

	// Lua thread.
	void SetSettings(const Settings& settings) noexcept;
	Settings GetSettings() noexcept;
	bool IsAvailable() noexcept;
	void GetStats(Stats& outStats) noexcept;
	// Off, counters and cache cleared. Only while the hook cannot run.
	void ResetMissionState() noexcept;

	// PilotFsmIntercept::Install, after the seam qualified: checks the
	// entries' stock values and names. Until it succeeds nothing is written.
	bool Qualify() noexcept;

	struct PreCall
	{
		std::int32_t index = -1;
		bool raised = false;
	};

	// Hook only (every Person). BeforeCall runs after any pilot policy write,
	// right before the stock call; AfterCall right after it, before the
	// policy restore. removalPending: the Person may be gone after the call.
	void BeforeCall(const void* person, PreCall& out) noexcept;
	void AfterCall(const void* person, const PreCall& pre, bool removalPending) noexcept;
}
