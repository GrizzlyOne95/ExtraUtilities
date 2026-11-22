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

// Mission-path editing: exu.AllPaths, BuildPath, AddPathPoint, RemovePath and
// RemovePathPoint. Paths are the engine's own labeled AiPath objects (the ones
// Goto/Patrol/CameraPath look up by name), built and grown with the engine
// allocator so stock AiPath::Cleanup frees them at mission end. No call frees
// a path mid-mission: units' tasks and the camera hold raw AiPath pointers,
// so RemovePath only takes the name away (see MissionPaths.cpp).
#pragma once

struct lua_State;

namespace ExtraUtilities::MissionPaths
{
	// exu.AllPaths() -> { name, ... } | nil
	int AllPaths(lua_State* L);
	// exu.BuildPath(name, position, ...) -> boolean
	int BuildPath(lua_State* L);
	// exu.AddPathPoint(name, position[, point]) -> boolean
	int AddPathPoint(lua_State* L);
	// exu.RemovePath(name) -> boolean
	int RemovePath(lua_State* L);
	// exu.RemovePathPoint(name[, point]) -> boolean
	int RemovePathPoint(lua_State* L);
}
