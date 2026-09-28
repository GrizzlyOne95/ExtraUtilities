/* Copyright (C) 2023-2026 VTrider
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Extra Utilities is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 * License for more details.
 */

#pragma once

/*
 * ContinuityApi.h
 *
 * exu.continuity: portable mission-to-mission world snapshots assembled only
 * from the public Battlezone Lua object API documented by UltraKen for
 * Battlezone 98 Redux. This intentionally does not parse BZR's native .sav
 * object layout or retain engine pointers. A snapshot is an ordinary
 * persistable Lua table and can therefore be stored with exu.storage.
 *
 * Format 1 captures reconstruction-safe state:
 *   ODF, team, transform, health/ammo ratios, class metadata, and player/person
 *   classification. It deliberately does not claim to preserve AI task stacks,
 *   target pointers, native group internals, or other engine-owned transient
 *   state.
 *
 * Important BZR API details:
 *   - AllObjects() returns a Lua iterator, not a table.
 *   - SetMatrix() argument order is right, up, front, position.
 *   - There is no public IsPlayer(); player identity is derived by comparing
 *     against the documented GetPlayerHandle() variants.
 */

#include <lua.hpp>

namespace ExtraUtilities::Lua::ContinuityApi
{
	// Registers exu.continuity on the global exu table, if it exists.
	void Install(lua_State* L);
}
