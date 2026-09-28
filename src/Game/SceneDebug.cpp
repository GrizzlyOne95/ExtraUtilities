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

#include "EnvironmentInternal.h"

// Scene diagnostics: bounding boxes, debug shadows, viewport shadow and
// overlay toggles, and the scene visibility mask.

namespace ExtraUtilities::Lua::Environment
{
	int GetShowBoundingBoxes(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		bool enabled = false;
		__try
		{
			enabled = Ogre::GetShowBoundingBoxes(sceneManager);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::Scene] getShowBoundingBoxes crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
		}

		lua_pushboolean(L, enabled ? 1 : 0);
		return 1;
	}

	int SetShowBoundingBoxes(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			return 0;
		}

		bool enabled = CheckBool(L, 1);
		__try
		{
			Ogre::ShowBoundingBoxes(sceneManager, enabled);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::Scene] showBoundingBoxes crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
		}

		return 0;
	}

	int GetShowDebugShadows(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		bool enabled = false;
		__try
		{
			enabled = Ogre::GetShowDebugShadows(sceneManager);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::Scene] getShowDebugShadows crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
		}

		lua_pushboolean(L, enabled ? 1 : 0);
		return 1;
	}

	int SetShowDebugShadows(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			return 0;
		}

		bool enabled = CheckBool(L, 1);
		__try
		{
			Ogre::SetShowDebugShadows(sceneManager, enabled);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::Scene] setShowDebugShadows crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
		}

		return 0;
	}

	int GetViewportShadowsEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* viewport = GetCurrentViewport();
		if (viewport == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		bool enabled = false;
		__try
		{
			enabled = Ogre::GetViewportShadowsEnabled(viewport);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::Viewport] getShadowsEnabled crashed viewport=%p code=0x%08X", viewport, GetExceptionCode());
		}

		lua_pushboolean(L, enabled ? 1 : 0);
		return 1;
	}

	int SetViewportShadowsEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* viewport = GetCurrentViewport();
		if (viewport == nullptr)
		{
			return 0;
		}

		bool enabled = CheckBool(L, 1);
		__try
		{
			Ogre::SetViewportShadowsEnabled(viewport, enabled);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::Viewport] setShadowsEnabled crashed viewport=%p code=0x%08X", viewport, GetExceptionCode());
		}

		return 0;
	}

	int GetViewportOverlaysEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* viewport = GetCurrentViewport();
		if (viewport == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		bool enabled = false;
		TryGetViewportOverlaysEnabled(viewport, enabled);

		lua_pushboolean(L, enabled ? 1 : 0);
		return 1;
	}

	int SetViewportOverlaysEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* viewport = GetCurrentViewport();
		if (viewport == nullptr)
		{
			return 0;
		}

		bool enabled = CheckBool(L, 1);
		TrySetViewportOverlaysEnabled(viewport, enabled);

		return 0;
	}

	int GetSceneVisibilityMask(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		uint32_t mask = 0;
		__try
		{
			mask = Ogre::GetSceneVisibilityMask(sceneManager);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::Scene] getVisibilityMask crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
			lua_pushnil(L);
			return 1;
		}

		lua_pushinteger(L, mask);
		return 1;
	}

	int SetSceneVisibilityMask(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			return 0;
		}

		uint32_t mask = static_cast<uint32_t>(luaL_checkinteger(L, 1));
		__try
		{
			Ogre::SetSceneVisibilityMask(sceneManager, mask);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::Scene] setVisibilityMask crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
		}

		return 0;
	}
}
