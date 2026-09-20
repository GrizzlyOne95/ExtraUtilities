/* Copyright (C) 2023-2026 VTrider
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#pragma once

#include <lua.hpp>

namespace ExtraUtilities::Lua::StaticGeometry
{
	int Create(lua_State* L);
	int Destroy(lua_State* L);
	int DestroyAll(lua_State* L);
	int GetInfo(lua_State* L);
	int SetVisible(lua_State* L);

	// Mission-owned geometry must not outlive the Lua state that created it.
	// SceneManager itself is mission-scoped, so shutdown only calls into Ogre
	// while the currently published manager still matches the recorded owner.
	void Shutdown() noexcept;
}
