/* Copyright (C) 2023-2026 VTrider
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

/*
 * AnimationApi.h
 *
 * exu.animation: high-level Lua animation controls built on top of EXU's
 * existing, SEH-guarded GameObject/Ogre animation-state bridge. This layer
 * intentionally does not cache Ogre::Entity or Ogre::AnimationState pointers
 * and does not install a frame hook. Ogre/Redux remains responsible for
 * skeletal evaluation and time advancement unless later live research proves a
 * managed clock is necessary.
 *
 * Lua accepts either a normal BZR Handle or a target descriptor returned by
 * exu.animation.Target(handle). The descriptor abstraction leaves room for a
 * validated local first-person/viewmodel resolver without changing the public
 * playback API later.
 */

#include <lua.hpp>

namespace ExtraUtilities::Lua::AnimationApi
{
	// Registers exu.animation on the global exu table, if it exists.
	void Install(lua_State* L);
}
