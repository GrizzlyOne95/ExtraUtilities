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

// Opt-in first-person death view (exu.fps.SetDeathCamera).
//
// When the local on-foot pilot is sniped, the pilot-kill path 0x004AD700
// saves the camera (0x0061A000) and switches it to free-eye mode at the body
// (0x0061CF30), so the death1 clip is never seen from the pilot's eyes.
// With mode "first" EXU redirects those two calls (0x004AD831, 0x004AD843)
// to stubs that skip them for the single-player local Person whose camera is
// still attached to its own render bridge. The camera then keeps following
// the pilot's POV bone while death1 plays; at the clip end the stock
// Person::Simulate removal branch (0x0059D488/0x0059D49A) performs the same
// save + free-eye switch before the object is removed. Ordinary (chunk)
// deaths never reach this path. See
// Docs/Research/DEATH_CAMERA_RE_20261005.md.

#include <cstdint>

namespace ExtraUtilities::Lua::DeathCamera
{
	enum class Mode : std::uint8_t
	{
		Stock = 0,
		First = 1,
	};

	struct Stats
	{
		bool available = false;
		bool patched = false;
		bool armed = false;
		bool probe = false;
		// Deaths whose camera switch was skipped / handed back early by the
		// guard / left stock because a condition failed.
		std::uint32_t kept = 0;
		std::uint32_t forced = 0;
		std::uint32_t declined = 0;
	};

	// Lua thread. Returns whether the redirect is in place (mode First) or
	// could be (mode Stock): the build qualified and patching is enabled.
	bool SetMode(Mode mode) noexcept;
	Mode GetMode() noexcept;
	void SetProbe(bool enabled) noexcept;
	void GetStats(Stats& outStats) noexcept;
	bool IsAvailable() noexcept;
	// Stock mode, patches removed, nothing armed.
	void ResetMissionState() noexcept;

	// Hook only (PilotFsmIntercept, every Person, after the stock call).
	// removalPending: the Person entered Simulate with its removal flags set,
	// so it may be gone now and must not be read.
	void AfterSimulate(const void* person, bool removalPending) noexcept;
}
