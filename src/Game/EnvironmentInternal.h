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

#include "Util/FiniteCheck.h"
#include "EnvironmentLog.h"
#include "Ogre/OgreSceneRuntime.h"
#include "Ogre/OgreParticleAbi.h"

#include "Environment.h"

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

// Internal to the environment feature files: Environment.cpp,
// EnvironmentLighting.cpp, EnvironmentSky.cpp, ParticleRuntime.cpp,
// ParticleBindings.cpp, ViewportLightingMode.cpp and SceneDebug.cpp.

namespace ExtraUtilities::Lua::Environment
{
	namespace Detail
	{
		enum class ViewportLightingMode
		{
			Default = 1,
			Enhanced = 2,
			Retro = 3,
		};
		extern ViewportLightingMode g_desiredLightingMode;
	}

	using namespace Detail;
	using FiniteCheck::IsFiniteColor;
	using FiniteCheck::IsFiniteVector;
	using FiniteCheck::IsFiniteScalar;
		std::string CheckOptionalParticleResourceGroup(lua_State* L, int idx);
		bool TryHasParticleSystem(void* sceneManager, const std::string& name, bool& outHasParticleSystem);
		bool TryDestroyManagedParticleSystem(void* sceneManager, const std::string& name);
		extern std::unordered_set<std::string> g_managedParticleNames;
		extern void* g_managedParticleSceneManager;
		bool TryCreateManagedParticleSystem(void* sceneManager, const std::string& name, const std::string& templateName, const BZR::VECTOR_3D& position);
		bool TrySetManagedParticleSceneNodePosition(void* sceneManager, const std::string& name, const BZR::VECTOR_3D& position);
		bool TrySetManagedParticleSceneNodeDirection(void* sceneManager, const std::string& name, const BZR::VECTOR_3D& direction);
		bool TrySetParticleSystemEmitting(void* sceneManager, const std::string& name, bool enabled);
		bool TrySetParticleSystemVisible(void* sceneManager, const std::string& name, bool enabled);
		bool TrySetParticleSystemSpeedFactor(void* sceneManager, const std::string& name, float speedFactor);
		bool TrySetParticleSystemKeepLocalSpace(void* sceneManager, const std::string& name, bool enabled);
		bool TrySetParticleSystemMaterial(void* sceneManager, const std::string& name, const std::string& materialName, const std::string& resourceGroup);
		bool TrySetParticleSystemRenderQueueGroup(void* sceneManager, const std::string& name, uint8_t renderQueueGroup);
		bool TrySetParticleSystemParticleQuota(void* sceneManager, const std::string& name, uint32_t quota);
		bool TrySetParticleSystemDefaultDimensions(void* sceneManager, const std::string& name, float width, float height);

		// ---------------------------------------------------------------------
		// Particle attachment and emitter parameter control
		//
		// Weather and object VFX both need an emitter that travels with
		// something -- the camera for a camera-centred precipitation volume, a
		// craft's scene node for damage smoke, a bone for a weapon muzzle. Doing
		// that from Lua means a per-frame SetParticleSystemPosition for every
		// live system; doing it here means the Ogre scene graph carries the
		// transform for free.
		//
		// The camera is the one case that can fail: the engine is free to drive
		// its Ogre camera directly instead of through a scene node, and then
		// there is no node to parent to. That case degrades to a registered
		// follower that UpdateParticleFollowers snaps to the camera's derived
		// position -- still one native call per frame instead of one per system,
		// and still no Lua-side transform maths.
		// ---------------------------------------------------------------------

		struct ParticleCameraFollower
		{
			std::string particleName;
			BZR::VECTOR_3D offset{};
		};
		extern std::vector<ParticleCameraFollower> g_particleCameraFollowers;
		void ForgetParticleCameraFollower(const std::string& name);
		void RememberParticleCameraFollower(const std::string& name, const BZR::VECTOR_3D& offset);
		void* GetActiveOgreCamera();
		bool TryGetCameraDerivedPosition(void* camera, BZR::VECTOR_3D& outPosition);
		bool TryConvertSimPositionToRenderSpace(
			const BZR::VECTOR_3D& simPosition,
			BZR::VECTOR_3D& outRenderPosition);
		bool TryAttachManagedParticleToCameraNode(void* sceneManager, const std::string& name, const BZR::VECTOR_3D& offset);
		bool TryUpdateParticleCameraFollower(void* sceneManager, const ParticleCameraFollower& follower, const BZR::VECTOR_3D& cameraPosition);
		bool TryAttachManagedParticleToObjectNode(void* sceneManager, const std::string& name, void* entity, const BZR::VECTOR_3D& offset);
		bool TryAttachManagedParticleToBone(void* sceneManager, const std::string& name, void* entity, const std::string& boneName, const BZR::VECTOR_3D& offset);
		// The re-based MovableObject* of a managed particle system (never the
		// ParticleSystem*), looked up by name through the scene manager.
		bool TryGetParticleMovableObject(void* sceneManager, const std::string& name, void*& outMovableObject);
		// Entity::getSkeleton()->hasBone(boneName); false without a skeleton.
		bool TryEntityHasBone(void* entity, const std::string& boneName);
		// The movable object's live parent when it is a TagPoint: that TagPoint
		// and the Entity owning it. Both null (and true) when it hangs off a
		// SceneNode or nothing. False only when Ogre could not be asked.
		bool TryGetMovableObjectTagPointParent(void* movableObject, void*& outTagPoint, void*& outEntity);
		void* GetParticleEmitter(void* sceneManager, const std::string& name, int emitterIndex);
		bool TryGetParticleEmitterCount(void* sceneManager, const std::string& name, int& outCount);
		void* GetParticleAffector(void* sceneManager, const std::string& name, int affectorIndex);
		bool TryGetParticleAffectorCount(void* sceneManager, const std::string& name, int& outCount);
		bool TryGetStringInterfaceTypeName(void* stringInterface, OgreAbi::GetTypeNameFn fn, std::string& outType);
		bool TrySetStringInterfaceParameter(
			void* stringInterface,
			const std::string& parameter,
			const std::string& value,
			const char* what);
		bool TryGetStringInterfaceParameter(
			void* stringInterface,
			const std::string& parameter,
			std::string& outValue,
			const char* what);
		bool TryGetStringInterfaceParameterNames(
			void* stringInterface,
			std::vector<std::string>& outNames,
			const char* what);
		bool TrySetEmitterEnabled(void* emitter, bool enabled);
		bool TrySetEmitterEmissionRate(void* emitter, float rate);
		bool TryGetEmitterEmissionRate(void* emitter, float& outRate);
		bool TrySetEmitterDirection(void* emitter, const BZR::VECTOR_3D& direction);
		bool TrySetEmitterPosition(void* emitter, const BZR::VECTOR_3D& position);
		bool TrySetEmitterVelocityRange(void* emitter, float minVelocity, float maxVelocity);
		bool TrySetEmitterAngle(void* emitter, float radians);
		bool TrySetEmitterTimeToLiveRange(void* emitter, float minTimeToLive, float maxTimeToLive);
		bool TrySetEmitterColourRange(void* emitter, const Ogre::Color& startColor, const Ogre::Color& endColor);
		bool TryReturnManagedParticleToOwnNode(void* sceneManager, const std::string& name);
		bool TrySetParticleSystemNonVisibleUpdateTimeout(void* sceneManager, const std::string& name, float timeout);
		void ForgetAllParticleCameraFollowers();
	void* GetSceneManager();
	void* GetCurrentViewport();
	void* GetTerrainMasterLight();
}
