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

#include "bzr.h"

#include <cstdint>

namespace ExtraUtilities::Patch::UnitDamage
{
	// Per-craft incoming-damage multiplier, applied inside Craft::DamageAlloc
	// after difficulty adjustment and before health subtraction and death. The
	// hook installs lazily on first use and fails closed: every call returns
	// false on an unqualified build or when the site bytes do not match.
	bool Available() noexcept;

	// object/handle must already be a validated round trip. multiplier is in
	// [0, 1]; 1 clears the override.
	bool Set(BZR::GameObject* object, uint32_t handle, float multiplier) noexcept;
	void Clear(uint32_t handle) noexcept;

	// Mission-scoped: drops every registration, leaves the hook installed.
	void Reset() noexcept;

	// Removes the hook (before the DLL can unload) and drops registrations.
	void Shutdown() noexcept;
}
