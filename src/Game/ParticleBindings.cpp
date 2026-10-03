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
#include "Game/FirstPersonParticles.h"
#include "Ogre/OgreResourceScriptBridge.h"

// Particle Lua bindings, including the generic emitter and affector
// StringInterface parameter bindings.

namespace ExtraUtilities::Lua::Environment
{
    int ParseResourceScript(lua_State* L)
    {
        Patch::TryInitializeOgre();
        size_t length = 0;
        const char* text = luaL_checklstring(L, 1, &length);
        const char* source = luaL_checkstring(L, 2);
        const char* group = luaL_optstring(L, 3, "Modable");
        lua_pushboolean(L, OgreScripts::TryParse(text, length, source, group) ? 1 : 0);
        return 1;
    }

    int HasParticleTemplate(lua_State* L)
    {
        Patch::TryInitializeOgre();
        const char* name = luaL_checkstring(L, 1);
        lua_pushboolean(L, OgreScripts::HasParticleTemplate(name) ? 1 : 0);
        return 1;
    }

	int HasParticleSystem(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		bool hasParticleSystem = false;
		if (sceneManager != nullptr)
		{
			const std::string name = luaL_checkstring(L, 1);
			TryHasParticleSystem(sceneManager, name, hasParticleSystem);
		}

		lua_pushboolean(L, hasParticleSystem ? 1 : 0);
		return 1;
	}

	int CreateParticleSystem(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const char* const templateNameArg = luaL_checkstring(L, 2);
		BZR::VECTOR_3D position{ 0.0f, 0.0f, 0.0f };
		if (!lua_isnoneornil(L, 3))
		{
			position = CheckVectorOrSingles(L, 3);
			if (!IsFiniteVector(position))
			{
				return luaL_argerror(L, 3, "CreateParticleSystem requires a finite position vector");
			}
		}
		const std::string name(nameArg);
		const std::string templateName(templateNameArg);

		BZR::VECTOR_3D renderPosition{};
		if (!TryConvertSimPositionToRenderSpace(position, renderPosition))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		lua_pushboolean(L, TryCreateManagedParticleSystem(sceneManager, name, templateName, renderPosition) ? 1 : 0);
		return 1;
	}

	int DestroyParticleSystem(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		ForgetParticleCameraFollower(name);
		FirstPersonParticles::Forget(name);
		g_managedParticleNames.erase(name);
		lua_pushboolean(L, TryDestroyManagedParticleSystem(sceneManager, name) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemPosition(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const BZR::VECTOR_3D position = CheckVectorOrSingles(L, 2);
		if (!IsFiniteVector(position))
		{
			return luaL_argerror(L, 2, "SetParticleSystemPosition requires a finite position vector");
		}
		const std::string name(nameArg);

		BZR::VECTOR_3D renderPosition{};
		if (!TryConvertSimPositionToRenderSpace(position, renderPosition))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		lua_pushboolean(L, TrySetManagedParticleSceneNodePosition(sceneManager, name, renderPosition) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemDirection(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const BZR::VECTOR_3D direction = CheckVectorOrSingles(L, 2);
		if (!IsFiniteVector(direction))
		{
			return luaL_argerror(L, 2, "SetParticleSystemDirection requires a finite direction vector");
		}
		const std::string name(nameArg);

		const BZR::VECTOR_3D renderDirection = OgreRenderSpace::SimDirectionToRender(direction);
		lua_pushboolean(L, TrySetManagedParticleSceneNodeDirection(sceneManager, name, renderDirection) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemEmitting(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const bool enabled = CheckBool(L, 2);
		const std::string name(nameArg);
		lua_pushboolean(L, TrySetParticleSystemEmitting(sceneManager, name, enabled) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemVisible(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const bool enabled = CheckBool(L, 2);
		const std::string name(nameArg);
		lua_pushboolean(L, TrySetParticleSystemVisible(sceneManager, name, enabled) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemSpeedFactor(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const float speedFactor = static_cast<float>(luaL_checknumber(L, 2));
		if (!IsFiniteScalar(speedFactor))
		{
			return luaL_argerror(L, 2, "SetParticleSystemSpeedFactor requires a finite numeric value");
		}
		const std::string name(nameArg);

		lua_pushboolean(L, TrySetParticleSystemSpeedFactor(sceneManager, name, speedFactor) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemKeepLocalSpace(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const bool enabled = CheckBool(L, 2);
		const std::string name(nameArg);
		lua_pushboolean(L, TrySetParticleSystemKeepLocalSpace(sceneManager, name, enabled) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemMaterial(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const char* const materialNameArg = luaL_checkstring(L, 2);
		const std::string resourceGroup = CheckOptionalParticleResourceGroup(L, 3);
		const std::string name(nameArg);
		const std::string materialName(materialNameArg);
		lua_pushboolean(L, TrySetParticleSystemMaterial(sceneManager, name, materialName, resourceGroup) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemRenderQueueGroup(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const lua_Integer queueGroup = luaL_checkinteger(L, 2);
		if (queueGroup < 0 || queueGroup > 255)
		{
			return luaL_argerror(L, 2, "SetParticleSystemRenderQueueGroup requires an integer in the range 0-255");
		}
		const std::string name(nameArg);

		lua_pushboolean(L, TrySetParticleSystemRenderQueueGroup(sceneManager, name, static_cast<uint8_t>(queueGroup)) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemParticleQuota(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const lua_Integer quota = luaL_checkinteger(L, 2);
		if (quota < 0)
		{
			return luaL_argerror(L, 2, "SetParticleSystemParticleQuota requires a non-negative integer");
		}
		const std::string name(nameArg);

		lua_pushboolean(L, TrySetParticleSystemParticleQuota(sceneManager, name, static_cast<uint32_t>(quota)) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemDefaultDimensions(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const float width = static_cast<float>(luaL_checknumber(L, 2));
		const float height = static_cast<float>(luaL_checknumber(L, 3));
		if (!IsFiniteScalar(width))
		{
			return luaL_argerror(L, 2, "SetParticleSystemDefaultDimensions requires a finite width");
		}
		if (!IsFiniteScalar(height))
		{
			return luaL_argerror(L, 3, "SetParticleSystemDefaultDimensions requires a finite height");
		}
		const std::string name(nameArg);

		lua_pushboolean(L, TrySetParticleSystemDefaultDimensions(sceneManager, name, width, height) ? 1 : 0);
		return 1;
	}


	int AttachParticleSystemToCamera(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		BZR::VECTOR_3D offset{ 0.0f, 0.0f, 0.0f };
		if (!lua_isnoneornil(L, 2))
		{
			offset = CheckVectorOrSingles(L, 2);
			if (!IsFiniteVector(offset))
			{
				return luaL_argerror(L, 2, "AttachParticleSystemToCamera requires a finite offset vector");
			}
		}
		const std::string name(nameArg);
		FirstPersonParticles::Forget(name);

		if (TryAttachManagedParticleToCameraNode(sceneManager, name, offset))
		{
			// A real scene-graph attachment needs no per-frame help.
			ForgetParticleCameraFollower(name);
			lua_pushboolean(L, 1);
			return 1;
		}

		// No camera scene node in this build: fall back to a native follower
		// that UpdateParticleFollowers drives, and place it once now so the
		// first frame is already correct.
		BZR::VECTOR_3D cameraPosition{ 0.0f, 0.0f, 0.0f };
		if (!TryGetCameraDerivedPosition(GetActiveOgreCamera(), cameraPosition))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		RememberParticleCameraFollower(name, offset);
		const ParticleCameraFollower follower{ name, offset };
		lua_pushboolean(L, TryUpdateParticleCameraFollower(sceneManager, follower, cameraPosition) ? 1 : 0);
		return 1;
	}

	int AttachParticleSystemToObject(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const BZR::handle h = CheckHandle(L, 2);
		BZR::VECTOR_3D offset{ 0.0f, 0.0f, 0.0f };
		if (!lua_isnoneornil(L, 3))
		{
			offset = CheckVectorOrSingles(L, 3);
			if (!IsFiniteVector(offset))
			{
				return luaL_argerror(L, 3, "AttachParticleSystemToObject requires a finite offset vector");
			}
		}
		const std::string name(nameArg);

		void* entity = GameObject::ResolveAnimationEntity(h);
		if (entity == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		ForgetParticleCameraFollower(name);
		FirstPersonParticles::Forget(name);
		lua_pushboolean(L, TryAttachManagedParticleToObjectNode(sceneManager, name, entity, offset) ? 1 : 0);
		return 1;
	}

	int AttachParticleSystemToBone(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const BZR::handle h = CheckHandle(L, 2);
		const char* const boneNameArg = luaL_checkstring(L, 3);
		BZR::VECTOR_3D offset{ 0.0f, 0.0f, 0.0f };
		if (!lua_isnoneornil(L, 4))
		{
			offset = CheckVectorOrSingles(L, 4);
			if (!IsFiniteVector(offset))
			{
				return luaL_argerror(L, 4, "AttachParticleSystemToBone requires a finite offset vector");
			}
		}
		const std::string name(nameArg);
		const std::string boneName(boneNameArg);

		void* entity = GameObject::ResolveAnimationEntity(h);
		if (entity == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		ForgetParticleCameraFollower(name);
		FirstPersonParticles::Forget(name);
		lua_pushboolean(L, TryAttachManagedParticleToBone(sceneManager, name, entity, boneName, offset) ? 1 : 0);
		return 1;
	}

	int DetachParticleSystem(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		ForgetParticleCameraFollower(name);
		FirstPersonParticles::Forget(name);
		lua_pushboolean(L, TryReturnManagedParticleToOwnNode(sceneManager, name) ? 1 : 0);
		return 1;
	}

	int UpdateParticleFollowers(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr || g_particleCameraFollowers.empty())
		{
			lua_pushinteger(L, 0);
			return 1;
		}

		BZR::VECTOR_3D cameraPosition{ 0.0f, 0.0f, 0.0f };
		if (!TryGetCameraDerivedPosition(GetActiveOgreCamera(), cameraPosition))
		{
			lua_pushinteger(L, 0);
			return 1;
		}

		int updated = 0;
		for (const auto& follower : g_particleCameraFollowers)
		{
			if (TryUpdateParticleCameraFollower(sceneManager, follower, cameraPosition))
			{
				++updated;
			}
		}

		lua_pushinteger(L, updated);
		return 1;
	}

	int GetParticleSystemEmitterCount(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		int count = 0;
		if (!TryGetParticleEmitterCount(sceneManager, name, count))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushinteger(L, count);
		return 1;
	}

	int GetParticleEmitterEmissionRate(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const std::string name(nameArg);
		float rate = 0.0f;
		if (!TryGetEmitterEmissionRate(GetParticleEmitter(sceneManager, name, emitterIndex), rate))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushnumber(L, rate);
		return 1;
	}

	int SetParticleEmitterEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const bool enabled = CheckBool(L, 3);
		const std::string name(nameArg);
		lua_pushboolean(L, TrySetEmitterEnabled(GetParticleEmitter(sceneManager, name, emitterIndex), enabled) ? 1 : 0);
		return 1;
	}

	int SetParticleEmitterEmissionRate(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const float rate = static_cast<float>(luaL_checknumber(L, 3));
		if (!IsFiniteScalar(rate) || rate < 0.0f)
		{
			return luaL_argerror(L, 3, "SetParticleEmitterEmissionRate requires a finite non-negative rate");
		}
		const std::string name(nameArg);

		lua_pushboolean(L, TrySetEmitterEmissionRate(GetParticleEmitter(sceneManager, name, emitterIndex), rate) ? 1 : 0);
		return 1;
	}

	int SetParticleEmitterDirection(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const BZR::VECTOR_3D direction = CheckVectorOrSingles(L, 3);
		if (!IsFiniteVector(direction))
		{
			return luaL_argerror(L, 3, "SetParticleEmitterDirection requires a finite direction vector");
		}
		const std::string name(nameArg);

		lua_pushboolean(L, TrySetEmitterDirection(GetParticleEmitter(sceneManager, name, emitterIndex), direction) ? 1 : 0);
		return 1;
	}

	int SetParticleEmitterPosition(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const BZR::VECTOR_3D position = CheckVectorOrSingles(L, 3);
		if (!IsFiniteVector(position))
		{
			return luaL_argerror(L, 3, "SetParticleEmitterPosition requires a finite position vector");
		}
		const std::string name(nameArg);

		lua_pushboolean(L, TrySetEmitterPosition(GetParticleEmitter(sceneManager, name, emitterIndex), position) ? 1 : 0);
		return 1;
	}

	int SetParticleEmitterVelocity(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const float minVelocity = static_cast<float>(luaL_checknumber(L, 3));
		const float maxVelocity = lua_isnoneornil(L, 4) ? minVelocity : static_cast<float>(luaL_checknumber(L, 4));
		if (!IsFiniteScalar(minVelocity))
		{
			return luaL_argerror(L, 3, "SetParticleEmitterVelocity requires a finite minimum velocity");
		}
		if (!IsFiniteScalar(maxVelocity) || maxVelocity < minVelocity)
		{
			return luaL_argerror(L, 4, "SetParticleEmitterVelocity requires a finite maximum velocity that is not below the minimum");
		}
		const std::string name(nameArg);

		lua_pushboolean(L, TrySetEmitterVelocityRange(GetParticleEmitter(sceneManager, name, emitterIndex), minVelocity, maxVelocity) ? 1 : 0);
		return 1;
	}

	int SetParticleEmitterAngle(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const float degrees = static_cast<float>(luaL_checknumber(L, 3));
		if (!IsFiniteScalar(degrees))
		{
			return luaL_argerror(L, 3, "SetParticleEmitterAngle requires a finite angle in degrees");
		}
		const std::string name(nameArg);

		// .particle scripts spell this in degrees, so the Lua surface does too.
		const float radians = degrees * 0.0174532925f;
		lua_pushboolean(L, TrySetEmitterAngle(GetParticleEmitter(sceneManager, name, emitterIndex), radians) ? 1 : 0);
		return 1;
	}

	int SetParticleEmitterTimeToLive(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const float minTimeToLive = static_cast<float>(luaL_checknumber(L, 3));
		const float maxTimeToLive = lua_isnoneornil(L, 4) ? minTimeToLive : static_cast<float>(luaL_checknumber(L, 4));
		if (!IsFiniteScalar(minTimeToLive) || minTimeToLive < 0.0f)
		{
			return luaL_argerror(L, 3, "SetParticleEmitterTimeToLive requires a finite non-negative minimum");
		}
		if (!IsFiniteScalar(maxTimeToLive) || maxTimeToLive < minTimeToLive)
		{
			return luaL_argerror(L, 4, "SetParticleEmitterTimeToLive requires a finite maximum that is not below the minimum");
		}
		const std::string name(nameArg);

		lua_pushboolean(L, TrySetEmitterTimeToLiveRange(GetParticleEmitter(sceneManager, name, emitterIndex), minTimeToLive, maxTimeToLive) ? 1 : 0);
		return 1;
	}

	int SetParticleEmitterColor(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const Ogre::Color startColor = CheckColorOrSingles(L, 3);
		const Ogre::Color endColor = (lua_istable(L, 3) && !lua_isnoneornil(L, 4)) ? CheckColorOrSingles(L, 4) : startColor;
		if (!IsFiniteColor(startColor))
		{
			return luaL_argerror(L, 3, "SetParticleEmitterColor requires a finite start color");
		}
		if (!IsFiniteColor(endColor))
		{
			return luaL_argerror(L, 4, "SetParticleEmitterColor requires a finite end color");
		}
		const std::string name(nameArg);

		lua_pushboolean(L, TrySetEmitterColourRange(GetParticleEmitter(sceneManager, name, emitterIndex), startColor, endColor) ? 1 : 0);
		return 1;
	}

	namespace
	{
		// Ogre parses every StringInterface property out of text, so a Lua
		// number or boolean has to be rendered the way Ogre::StringConverter
		// reads it back. Strings pass through untouched: vectors and colours
		// are already space-separated text ("1 1 1 0.75") and re-parsing them
		// here would only lose precision.
		enum class SetParameterStatus
		{
			Applied,
			Failed,
			NonFiniteNumber,
			UnsupportedType,
			InvalidValue,
		};

		// Holds the std::strings and never raises; the caller raises for the
		// argument errors once they are gone.
		template<typename Resolver>
		SetParameterStatus SetParameterWithoutRaising(
			lua_State* L,
			Resolver resolve,
			void* sceneManager,
			const char* name,
			int index,
			const char* parameter,
			const char* what)
		{
			std::string value;
			switch (lua_type(L, 4))
			{
			case LUA_TSTRING:
				value = lua_tostring(L, 4);
				break;
			case LUA_TNUMBER:
				value = OgreParams::NumberToParameterValue(lua_tonumber(L, 4));
				if (value.empty())
				{
					return SetParameterStatus::NonFiniteNumber;
				}
				break;
			case LUA_TBOOLEAN:
				value = OgreParams::BoolToParameterValue(lua_toboolean(L, 4) != 0);
				break;
			default:
				return SetParameterStatus::UnsupportedType;
			}

			if (!OgreParams::IsValidParameterValue(value))
			{
				return SetParameterStatus::InvalidValue;
			}

			return TrySetStringInterfaceParameter(resolve(sceneManager, name, index), parameter, value, what)
				? SetParameterStatus::Applied
				: SetParameterStatus::Failed;
		}

		// Shared body for the four generic accessors. `resolve` turns the Lua
		// system name plus index into the emitter or affector to talk to, so
		// emitters and affectors differ only in that one lambda and the log
		// label.
		template<typename Resolver>
		int GenericSetParameter(lua_State* L, Resolver resolve, const char* function, const char* what)
		{
			Patch::TryInitializeOgre();

			auto* sceneManager = GetSceneManager();
			if (sceneManager == nullptr)
			{
				lua_pushboolean(L, 0);
				return 1;
			}

			const char* const name = luaL_checkstring(L, 1);
			const int index = static_cast<int>(luaL_checkinteger(L, 2));
			const char* const parameter = luaL_checkstring(L, 3);
			if (!OgreParams::IsValidParameterName(parameter))
			{
				return luaL_argerror(L, 3, "parameter name must be a short identifier");
			}

			switch (SetParameterWithoutRaising(L, resolve, sceneManager, name, index, parameter, what))
			{
			case SetParameterStatus::NonFiniteNumber:
				return luaL_argerror(L, 4, "parameter value must be a finite number");
			case SetParameterStatus::UnsupportedType:
				return luaL_error(L, "%s requires a string, number, or boolean parameter value", function);
			case SetParameterStatus::InvalidValue:
				return luaL_argerror(L, 4, "parameter value must be printable and within the length limit");
			case SetParameterStatus::Applied:
				lua_pushboolean(L, 1);
				return 1;
			case SetParameterStatus::Failed:
			default:
				lua_pushboolean(L, 0);
				return 1;
			}
		}

		template<typename Resolver>
		int GenericGetParameter(lua_State* L, Resolver resolve, const char* what)
		{
			Patch::TryInitializeOgre();

			auto* sceneManager = GetSceneManager();
			if (sceneManager == nullptr)
			{
				lua_pushnil(L);
				return 1;
			}

			const char* const nameArg = luaL_checkstring(L, 1);
			const int index = static_cast<int>(luaL_checkinteger(L, 2));
			const char* const parameterArg = luaL_checkstring(L, 3);
			if (!OgreParams::IsValidParameterName(parameterArg))
			{
				return luaL_argerror(L, 3, "parameter name must be a short identifier");
			}
			const std::string name(nameArg);
			const std::string parameter(parameterArg);

			std::string value;
			if (!TryGetStringInterfaceParameter(resolve(sceneManager, name, index), parameter, value, what))
			{
				lua_pushnil(L);
				return 1;
			}

			lua_pushlstring(L, value.data(), value.size());
			return 1;
		}

		template<typename Resolver>
		int GenericGetType(lua_State* L, Resolver resolve, OgreAbi::GetTypeNameFn (*resolveGetType)())
		{
			Patch::TryInitializeOgre();

			auto* sceneManager = GetSceneManager();
			if (sceneManager == nullptr)
			{
				lua_pushnil(L);
				return 1;
			}

			const char* const nameArg = luaL_checkstring(L, 1);
			const int index = static_cast<int>(luaL_checkinteger(L, 2));
			const std::string name(nameArg);
			std::string type;
			if (!TryGetStringInterfaceTypeName(resolve(sceneManager, name, index), resolveGetType(), type))
			{
				lua_pushnil(L);
				return 1;
			}

			lua_pushlstring(L, type.data(), type.size());
			return 1;
		}

		template<typename Resolver>
		int GenericGetParameterNames(lua_State* L, Resolver resolve, const char* what)
		{
			Patch::TryInitializeOgre();

			auto* sceneManager = GetSceneManager();
			if (sceneManager == nullptr)
			{
				lua_pushnil(L);
				return 1;
			}

			const char* const nameArg = luaL_checkstring(L, 1);
			const int index = static_cast<int>(luaL_checkinteger(L, 2));
			const std::string name(nameArg);
			std::vector<std::string> names;
			if (!TryGetStringInterfaceParameterNames(resolve(sceneManager, name, index), names, what))
			{
				lua_pushnil(L);
				return 1;
			}

			lua_createtable(L, static_cast<int>(names.size()), 0);
			for (size_t i = 0; i < names.size(); ++i)
			{
				lua_pushlstring(L, names[i].data(), names[i].size());
				lua_rawseti(L, -2, static_cast<int>(i) + 1);
			}
			return 1;
		}

		void* ResolveEmitterTarget(void* sceneManager, const std::string& name, int index)
		{
			return GetParticleEmitter(sceneManager, name, index);
		}

		void* ResolveAffectorTarget(void* sceneManager, const std::string& name, int index)
		{
			return GetParticleAffector(sceneManager, name, index);
		}
	}

	int GetParticleEmitterType(lua_State* L)
	{
		return GenericGetType(L, &ResolveEmitterTarget, &ResolveEmitterGetType);
	}

	int GetParticleEmitterParameterNames(lua_State* L)
	{
		return GenericGetParameterNames(L, &ResolveEmitterTarget, "emitter");
	}

	int GetParticleEmitterParameter(lua_State* L)
	{
		return GenericGetParameter(L, &ResolveEmitterTarget, "emitter");
	}

	int SetParticleEmitterParameter(lua_State* L)
	{
		return GenericSetParameter(L, &ResolveEmitterTarget, "SetParticleEmitterParameter", "emitter");
	}

	int GetParticleSystemAffectorCount(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		int count = 0;
		if (!TryGetParticleAffectorCount(sceneManager, name, count))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushinteger(L, count);
		return 1;
	}

	int GetParticleAffectorType(lua_State* L)
	{
		return GenericGetType(L, &ResolveAffectorTarget, &ResolveAffectorGetType);
	}

	int GetParticleAffectorParameterNames(lua_State* L)
	{
		return GenericGetParameterNames(L, &ResolveAffectorTarget, "affector");
	}

	int GetParticleAffectorParameter(lua_State* L)
	{
		return GenericGetParameter(L, &ResolveAffectorTarget, "affector");
	}

	int SetParticleAffectorParameter(lua_State* L)
	{
		return GenericSetParameter(L, &ResolveAffectorTarget, "SetParticleAffectorParameter", "affector");
	}

	int SetParticleSystemNonVisibleUpdateTimeout(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const char* const nameArg = luaL_checkstring(L, 1);
		const float timeout = static_cast<float>(luaL_checknumber(L, 2));
		if (!IsFiniteScalar(timeout) || timeout < 0.0f)
		{
			return luaL_argerror(L, 2, "SetParticleSystemNonVisibleUpdateTimeout requires a finite non-negative timeout");
		}
		const std::string name(nameArg);

		lua_pushboolean(L, TrySetParticleSystemNonVisibleUpdateTimeout(sceneManager, name, timeout) ? 1 : 0);
		return 1;
	}
}
