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
	struct Snapshot
	{
		bool available = false;
		bool grounded = false;
		bool sniperSelected = false;
		std::uint32_t nativeState = 0;
		std::int32_t animationIndex = -1;
		std::int32_t animationHandle = -1;
		std::uint32_t selectedWeaponMask = 0;
		std::int32_t selectedWeaponSlot = -1;
		std::uint32_t selectedWeaponSignature = 0;
		char selectedWeaponOdf[16]{};
	};

	// Reads a one-operation snapshot of the current local on-foot Person.
	// Returns false when EXU's qualified runtime gate is closed, the user object
	// is not a Person, or any native read faults. No engine pointer is retained.
	bool Capture(Snapshot& outSnapshot) noexcept;
}
