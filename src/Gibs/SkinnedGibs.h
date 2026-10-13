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

// exu.gibs: when a Person is fully fragmented, replace its legacy body chunks
// with rigid per-limb gibs cut from the dead body's own skinned mesh, posed
// where each limb was on the frame of death. A port of OpenShim's SkinnedGibs
// (Docs/Research/SKINNED_GIBS_PORT_20261013.md): the engine-free launch and
// ground response are SkinnedGibCore.h, the mesh split and its disk cache are
// GibPayloads.h, and the Ogre ABI is Ogre/OgreGibRuntime.h. This layer owns
// the ChunkEffect::FullFragmentObject detour (capture before the original,
// spawn after), the gib pool and the Ogre::FrameListener that steps it.
//
// Off until a mission calls exu.gibs.Enable(true), and never active beside
// OpenShim, which detours the same function for the same feature.
//
// Lifetimes: the detour, the listener, the pool and its scene nodes belong to
// the Lua state. Shutdown (from HandleLuaStateClosing) removes the detour and
// the listener and destroys the gibs BEFORE the DLL can unload; a scene that
// has already gone (or been cleared) is detected and its objects are
// forgotten, never touched.

#include <lua.hpp>

namespace ExtraUtilities::Lua::SkinnedGibs
{
	// Registers exu.gibs on the global exu table, if it exists.
	void Install(lua_State* L);

	// Removes the detour and the frame listener and destroys every gib. Safe
	// to call twice.
	void Shutdown() noexcept;
}
