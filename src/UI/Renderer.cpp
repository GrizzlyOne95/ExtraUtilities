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

#include "Renderer.h"

#include "LuaHelpers.h"
#include "Ogre/Ogre.h"

namespace ExtraUtilities::Lua::Renderer
{
	namespace
	{
		constexpr int kPolygonModeWireframe = 2;
		constexpr int kPolygonModeSolid = 3;

		bool g_isWireframe = false;

		void* GetCurrentOgreCamera()
		{
			void* sceneManager = ExtraUtilities::Ogre::sceneManager.Read();
			if (sceneManager == nullptr)
			{
				return nullptr;
			}

			void* viewport = ExtraUtilities::Ogre::GetCurrentViewport(sceneManager);
			return viewport == nullptr ? nullptr : ExtraUtilities::Ogre::GetViewportCamera(viewport);
		}
	}

	int SetWireframe(lua_State* L)
	{
		g_isWireframe = CheckBool(L, 1);

		if (void* camera = GetCurrentOgreCamera())
		{
			ExtraUtilities::Ogre::SetCameraPolygonMode(camera, g_isWireframe ? kPolygonModeWireframe : kPolygonModeSolid);
		}

		return 0;
	}

	int GetWireframe(lua_State* L)
	{
		lua_pushboolean(L, g_isWireframe);
		return 1;
	}

	int ClearVisuals(lua_State*)
	{
		return 0;
	}

	int DrawLine(lua_State* L)
	{
		(void)CheckVectorOrSingles(L, 1);
		(void)CheckVectorOrSingles(L, 2);
		(void)CheckColorOrSingles(L, 3);
		return 0;
	}

	int DrawBox(lua_State* L)
	{
		(void)CheckVectorOrSingles(L, 1);
		(void)CheckVectorOrSingles(L, 2);
		(void)CheckColorOrSingles(L, 3);
		return 0;
	}
}
