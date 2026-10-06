#pragma once

// exu.flash: BZ2-style 3D muzzle flashes. Lua spawns one per shot (typically
// from exu.BulletInit) with the shot transform; EXU owns it from then on. Every
// RENDERED frame (Ogre::FrameListener) it follows the firing object, scales
// from startScale to finishScale and is destroyed when its simulation-time
// duration ends. The timing/pose policy is MuzzleFlashCore.h, which is shared
// with BZR-OpenShim; this layer owns the Ogre objects, the owner reads and the
// pool.
//
// Lifetimes: flashes, their scene nodes and the frame listener belong to the
// Lua state. Shutdown (from HandleLuaStateClosing) destroys every flash and
// removes the listener BEFORE the DLL can unload; a scene manager that has
// gone (or had its scene cleared) is detected and its objects are forgotten,
// never touched.

#include <lua.hpp>

namespace ExtraUtilities::Lua::MuzzleFlash
{
	// Registers exu.flash on the global exu table, if it exists.
	void Install(lua_State* L);

	// Destroys every flash and removes the frame listener. Safe to call twice.
	void Shutdown() noexcept;
}
