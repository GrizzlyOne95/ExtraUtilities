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

#include <lua.hpp>

// Free-standing world point lights: Ogre lights on their own scene node under
// the root, owned by the mission Lua state. See Docs/POINT_LIGHTS.md.
namespace ExtraUtilities::Lua::PointLights
{
	int Create(lua_State* L);
	int SetPosition(lua_State* L);
	int SetColour(lua_State* L);
	int SetRange(lua_State* L);
	int SetAttenuation(lua_State* L);
	int SetPowerScale(lua_State* L);
	int SetVisible(lua_State* L);
	int Destroy(lua_State* L);
	int DestroyAll(lua_State* L);
	int Has(lua_State* L);
	int GetInfo(lua_State* L);
	int GetStats(lua_State* L);

	// Destroys every light this Lua state created. Calls into Ogre only while
	// the published SceneManager is still the one each light was created in,
	// and only for lights and nodes Ogre still knows by name.
	void Shutdown() noexcept;
}
