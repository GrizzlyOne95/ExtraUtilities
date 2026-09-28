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

#include "Environment.h"
#include "EnvironmentInternal.h"

#include "../RenderProfileBridge.h"
#include "InlinePatch.h"
#include "Ogre/OgreParameterValue.h"
#include "Ogre/OgreRenderSpace.h"
#include "Ogre/OgreStringInterfaceShim.h"
#include "Util/Logging.h"
#include "GameObject.h"
#include "LuaHelpers.h"

#include <Windows.h>

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ExtraUtilities::Lua::Environment
{
	void* GetSceneManager()
	{
		return Ogre::sceneManager.Read();
	}

	void* GetCurrentViewport()
	{
		const ActiveViewportSet activeViewports = GetActiveViewports();
		return activeViewports.count > 0 ? activeViewports.viewports[0] : nullptr;
	}

	void* GetTerrainMasterLight()
	{
		return Ogre::terrain_masterlight.Read();
	}

	Ogre::Color DefaultSunColor()
	{
		return {};
	}

	void Shutdown() noexcept
	{
		try
		{
			// Only systems in the scene they were created in: a different
			// scene manager means the old scene, and its systems, are gone.
			void* sceneManager = GetSceneManager();
			if (sceneManager != nullptr && sceneManager == g_managedParticleSceneManager)
			{
				for (const std::string& name : g_managedParticleNames)
				{
					TryDestroyManagedParticleSystem(sceneManager, name);
				}
			}
			g_managedParticleNames.clear();
			g_managedParticleSceneManager = nullptr;
			ForgetAllParticleCameraFollowers();
			g_desiredLightingMode = ViewportLightingMode::Default;
		}
		catch (...)
		{
			g_managedParticleNames.clear();
			g_managedParticleSceneManager = nullptr;
		}
	}
}

namespace ExtraUtilities::Patch
{
	// Prevents fog reset function from running.
	InlinePatch fogResetPatch(fogReset, BasicPatch::RET, BasicPatch::Status::INACTIVE, { 0x55 });

	namespace
	{
	/*
	* This waits to initialize the ogre patch until you call an ogre function
	* in order to prevent the game crashing when alt tabbed in the loading screen.
	*
	* The old EXU sun reset hooks were unstable on BZR 2.2.301 and could cause
	* access violations during repeated lighting updates. Callers that need
	* dynamic sunlight can safely reapply SetSun* on their own update loop.
	*/
		// Which scene the current binding belongs to. A plain `done` latch was
		// process-lifetime, so after the first mission the fog reset patch was
		// never reloaded again and every later sun/ambient write in
		// Environment.lua's Update went at whatever the first mission had left
		// behind. Redux changes mission in-process and never calls
		// SceneManager::clearScene, so there is no teardown notification --
		// track the identity instead and re-bind whenever it moves.
		void* g_initializedSceneManager = nullptr;
		void* g_initializedTerrainMasterLight = nullptr;
		bool g_ogreInitialized = false;
	}

	void ResetOgreInitialization()
	{
		if (g_ogreInitialized)
		{
			Lua::Environment::LogEnvironmentDebug(
				"[EXU::ResetOgreInitialization] dropping binding sceneManager=%p terrainMasterLight=%p",
				g_initializedSceneManager,
				g_initializedTerrainMasterLight);
		}
		Lua::Environment::ForgetAllParticleCameraFollowers();
		g_initializedSceneManager = nullptr;
		g_initializedTerrainMasterLight = nullptr;
		g_ogreInitialized = false;
	}

	void TryInitializeOgre()
	{
		auto* sceneManager = Lua::Environment::GetSceneManager();
		auto* terrainMasterLight = Lua::Environment::GetTerrainMasterLight();

		if (g_ogreInitialized &&
			sceneManager == g_initializedSceneManager &&
			terrainMasterLight == g_initializedTerrainMasterLight)
		{
			return;
		}

		if (sceneManager == nullptr || terrainMasterLight == nullptr)
		{
			Lua::Environment::LogEnvironmentDebug(
				"[EXU::TryInitializeOgre] waiting sceneManager=%p terrainMasterLight=%p",
				sceneManager,
				terrainMasterLight);
			return;
		}

		Lua::Environment::LogEnvironmentDebug(
			"[EXU::TryInitializeOgre] initialized sceneManager=%p (was %p) terrainMasterLight=%p (was %p) rebind=%d",
			sceneManager,
			g_initializedSceneManager,
			terrainMasterLight,
			g_initializedTerrainMasterLight,
			g_ogreInitialized ? 1 : 0);
		g_initializedSceneManager = sceneManager;
		g_initializedTerrainMasterLight = terrainMasterLight;
		g_ogreInitialized = true;
	}
}
