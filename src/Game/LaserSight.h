/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

// exu.SetLaserSightMode / exu.GetLaserSightMode: optional OpenShim laser-sight
// mode export. Older OpenShim builds lack it; both functions then return nil.
#pragma once

struct lua_State;

namespace ExtraUtilities::LaserSight
{
	// exu.SetLaserSightMode(mode) -> modeInEffect | nil
	int SetMode(lua_State* L);
	// exu.GetLaserSightMode() -> mode | nil
	int GetMode(lua_State* L);
}
