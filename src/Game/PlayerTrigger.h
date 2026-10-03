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

// The local player's "fire held" signal (exu.fps.IsTriggerHeld, the
// first-person layer `fire` option).
//
// The fire bind is not stored on the controlled object: it is the global
// 'weapon_fire' / 'weapon_fire_auto' command state bytes that the input poll
// rebuilds every poll and UserProcess::Execute reads once per tick
// (Docs/Research/PLAYER_TRIGGER_SIGNAL_RE_20261003.md). Version-locked: the
// signal is used only after Qualify() has matched both read sites in
// UserProcess::Execute and read the bytes' addresses from their operands.
// Otherwise it stands down (capability firstPersonTrigger false, IsHeld()
// always false).

namespace ExtraUtilities::Lua::PlayerTrigger
{
	// Install time, after the runtime build gate accepted the executable.
	// Idempotent; returns the qualification result.
	bool Qualify() noexcept;

	// The capability firstPersonTrigger.
	bool IsAvailable() noexcept;

	// True while the fire or auto-fire bind is held (either byte != 0).
	// False when unavailable or when a read faults. Global player input:
	// callers apply it to the LOCAL Person only.
	bool IsHeld() noexcept;
}
