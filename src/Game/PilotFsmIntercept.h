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

namespace ExtraUtilities::Lua::PilotFsmIntercept
{
	struct Stats
	{
		bool installed = false;
		bool active = false;
		bool observeOnly = true;
		bool hasLocalSample = false;

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
	};

	// Installs/activates the verified Person::Simulate entry detour. The hook is
	// observe-only in this work chunk: it always calls the stock trampoline and
	// performs no native writes before or after it.
	bool Install();

	bool IsInstalled() noexcept;
	bool IsActive() noexcept;

	// Destroys the lazily allocated detour while BasicPatch's registry is still
	// alive. Called explicitly from the Lua-state shutdown path.
	void Shutdown() noexcept;

	void ResetStats() noexcept;
	void GetStats(Stats& outStats) noexcept;
}
