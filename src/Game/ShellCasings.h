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

#pragma once

// exu.casing: physical shell casings. Lua only spawns one (typically from
// exu.BulletInit); EXU owns the simulation and the Ogre entity from then on
// and advances both every RENDERED frame from an Ogre::FrameListener, so the
// tumble is smooth regardless of the Lua update rate. The physics is
// ShellCasingMath.h; this layer owns the Ogre objects, the engine reads
// (terrain height, nearby GameObject boxes, sim time/pause) and the pool.
//
// Lifetimes: casings, their scene nodes and the frame listener belong to the
// Lua state. Shutdown (from HandleLuaStateClosing) destroys every casing and
// removes the listener BEFORE the DLL can unload; a scene manager that has
// already gone (or had its scene cleared) is detected and its objects are
// forgotten, never touched.

#include <lua.hpp>

namespace ExtraUtilities::Lua::ShellCasings
{
	// Registers exu.casing on the global exu table, if it exists.
	void Install(lua_State* L);

	// Destroys every casing and removes the frame listener. Safe to call twice.
	void Shutdown() noexcept;
}
