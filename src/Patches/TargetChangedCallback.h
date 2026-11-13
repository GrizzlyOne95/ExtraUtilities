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

struct lua_State;

namespace ExtraUtilities::Patch::TargetChanged
{
	// Records the thread that owns the newly attached mission Lua state. The
	// exu.TargetChanged callback only runs on that thread.
	void HandleLuaStateAttached() noexcept;

	// True on the thread recorded above. Shared by the other exu.* engine
	// callbacks (PreActionCallbacks.cpp) so every callback uses one gate.
	bool IsLuaThread() noexcept;

	// True when exu.<field> is a Lua function. Raw lookups only (no
	// metamethods, no allocation), so a missing handler costs almost nothing.
	bool HasLuaHandler(lua_State* L, const char* field) noexcept;
}

namespace ExtraUtilities::Patch::PreAction
{
	// Resets the PreGetIn/PrePickupPowerup nesting counters for a new state.
	void HandleLuaStateAttached() noexcept;
}
