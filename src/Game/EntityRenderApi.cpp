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

#include "GameObjectInternal.h"
#include "Ogre/OgreAnimationInventoryBridge.h"
#include "Util/SehGuard.h"
#include "Game/FirstPersonTarget.h"

#include <cctype>
#include <cstring>
#include <string>
#include <utility>

// Entity render, light and animation API: the renderable-entity and light
// lookups on a GameObject, the exported animation bridge and their bindings.

namespace ExtraUtilities::Lua::GameObject
{
	namespace
	{
		bool IsRenderableEntityCandidate(void* candidate)
		{
			if (candidate == nullptr)
			{
				return false;
			}

			uint32_t count = 0;
			if (!TryGetNumSubEntities(candidate, count) || count == 0 || count > 64)
			{
				return false;
			}

			void* subEntity = nullptr;
			if (!TryGetSubEntityByIndex(candidate, 0, subEntity) || subEntity == nullptr)
			{
				return false;
			}

			std::string materialName;
			if (!TryGetMaterialName(subEntity, materialName))
			{
				return false;
			}

			return true;
		}

		void* GetLightObject(BZR::GameObject* obj)
		{
			if (obj == nullptr)
			{
				return nullptr;
			}

			void* light = nullptr;
			__try
			{
				light = obj->GetLight();
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Light] Ogre light lookup crashed obj=%p code=0x%08X", obj, GetExceptionCode());
				return nullptr;
			}

			return light;
		}

		void* GetLightObject(BZR::handle h)
		{
			return GetLightObject(BZR::GameObject::GetObj(h));
		}

		uint32_t CheckSubEntityIndex(lua_State* L, void* entity, int idx)
		{
			int requestedIndex = luaL_checkinteger(L, idx);
			if (requestedIndex < 0)
			{
				luaL_argerror(L, idx, "sub-entity index must be >= 0");
			}

			const auto subEntityIndex = static_cast<uint32_t>(requestedIndex);
			uint32_t numSubEntities = 0;
			if (!TryGetNumSubEntities(entity, numSubEntities))
			{
				luaL_error(L, "Extra Utilities: render entity probe crashed while reading sub-entity count");
			}
			if (subEntityIndex >= numSubEntities)
			{
				luaL_argerror(L, idx, "sub-entity index is out of range");
			}

			return subEntityIndex;
		}
	}

	namespace Detail
	{
		void* GetRenderableEntity(BZR::GameObject* obj)
		{
			if (obj == nullptr)
			{
				return nullptr;
			}

			void* entity = nullptr;
			__try
			{
				entity = obj->GetOgreEntity();
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] Ogre pointer lookup crashed obj=%p code=0x%08X", obj, GetExceptionCode());
				return nullptr;
			}

			if (IsRenderableEntityCandidate(entity))
			{
				return entity;
			}

			LogMaterialDebug("[EXU::Material] Ogre entity validation failed obj=%p entity=%p", obj, entity);
			return nullptr;
		}

		void* GetRenderableEntity(BZR::handle h)
		{
			return GetRenderableEntity(BZR::GameObject::GetObj(h));
		}

		EntityTarget CheckEntityTarget(lua_State* L, int idx)
		{
			EntityTarget target{};
			if (!lua_istable(L, idx))
			{
				target.handle = CheckHandle(L, idx);
				return target;
			}

			const int absIndex = AbsoluteStackIndex(L, idx);
			lua_getfield(L, absIndex, "kind");
			const char* const kind = lua_tostring(L, -1);
			if (kind != nullptr && std::strcmp(kind, "cockpit") == 0)
			{
				target.cockpit = true;
			}
			else if (kind == nullptr || std::strcmp(kind, "gameObject") != 0)
			{
				lua_pop(L, 1);
				luaL_argerror(L, idx, "entity target must be a handle or a gameObject/cockpit target");
			}
			lua_pop(L, 1);

			lua_getfield(L, absIndex, "handle");
			target.handle = CheckHandle(L, -1);
			lua_pop(L, 1);
			return target;
		}

		void* GetRenderableEntity(const EntityTarget& target)
		{
			return target.cockpit ? ResolveCockpitEntity(target.handle) : GetRenderableEntity(target.handle);
		}

		void* GetFirstSubEntity(void* entity)
		{
			uint32_t count = 0;
			if (!TryGetNumSubEntities(entity, count) || count == 0)
			{
				return nullptr;
			}

			void* subEntity = nullptr;
			if (!TryGetSubEntityByIndex(entity, 0, subEntity))
			{
				return nullptr;
			}

			return subEntity;
		}

		void* GetSubEntity(lua_State* L, void* entity, int idx)
		{
			void* subEntity = nullptr;
			if (!TryGetSubEntityByIndex(entity, CheckSubEntityIndex(L, entity, idx), subEntity))
			{
				luaL_error(L, "Extra Utilities: render entity probe crashed while getting sub-entity");
			}
			return subEntity;
		}
	}

	void* ResolveAnimationEntity(BZR::handle handle)
	{
		return GetRenderableEntity(handle);
	}

	void* ResolveCockpitEntity(BZR::handle handle)
	{
		BZR::GameObject* const obj = handle ? BZR::GameObject::GetObj(handle) : nullptr;
		if (obj == nullptr)
		{
			return nullptr;
		}

		void* worldEntity = nullptr;
		void* cockpitEntity = nullptr;
		if (!FirstPersonTarget::ReadRenderBridgeEntities(obj, worldEntity, cockpitEntity) ||
			cockpitEntity == nullptr || cockpitEntity == worldEntity)
		{
			return nullptr;
		}

		if (!IsRenderableEntityCandidate(cockpitEntity))
		{
			LogMaterialDebug("[EXU::Material] cockpit entity validation failed obj=%p entity=%p", obj, cockpitEntity);
			return nullptr;
		}
		return cockpitEntity;
	}

	bool HasAnimation(void* entity, const std::string& name)
	{
		return entity != nullptr && GetNamedAnimationState(entity, name) != nullptr;
	}

	bool GetAnimationInfo(void* entity, const std::string& name, EntityAnimationInfo& outInfo)
	{
		void* animationState = entity ? GetNamedAnimationState(entity, name) : nullptr;
		if (!animationState)
			return false;
		return TryGetAnimationEnabled(animationState, outInfo.enabled) &&
			TryGetAnimationLoop(animationState, outInfo.loop) &&
			TryGetAnimationWeight(animationState, outInfo.weight) &&
			TryGetAnimationTimePosition(animationState, outInfo.timePosition) &&
			TryGetAnimationLength(animationState, outInfo.length);
	}

	bool GetAnimationInventory(void* entity, std::vector<EntityAnimationSnapshot>& outInventory)
	{
		outInventory.clear();
		if (entity == nullptr)
		{
			return false;
		}

		std::vector<OgreAnimationInventory::AnimationStateRef> states;
		if (!OgreAnimationInventory::TryEnumerateAnimationStates(entity, states))
		{
			return false;
		}

		outInventory.reserve(states.size());
		for (const OgreAnimationInventory::AnimationStateRef& entry : states)
		{
			if (entry.state == nullptr)
			{
				outInventory.clear();
				return false;
			}

			EntityAnimationSnapshot snapshot{};
			snapshot.name = entry.name;
			if (!TryGetAnimationEnabled(entry.state, snapshot.info.enabled) ||
				!TryGetAnimationLoop(entry.state, snapshot.info.loop) ||
				!TryGetAnimationWeight(entry.state, snapshot.info.weight) ||
				!TryGetAnimationTimePosition(entry.state, snapshot.info.timePosition) ||
				!TryGetAnimationLength(entry.state, snapshot.info.length))
			{
				outInventory.clear();
				return false;
			}
			outInventory.push_back(std::move(snapshot));
		}

		std::sort(
			outInventory.begin(),
			outInventory.end(),
			[](const EntityAnimationSnapshot& a, const EntityAnimationSnapshot& b)
			{
				return a.name < b.name;
			});
		return true;
	}

	bool SetAnimationEnabled(void* entity, const std::string& name, bool enabled)
	{
		void* animationState = entity ? GetNamedAnimationState(entity, name) : nullptr;
		return animationState && TrySetAnimationEnabled(animationState, enabled);
	}

	bool SetAnimationLoop(void* entity, const std::string& name, bool loop)
	{
		void* animationState = entity ? GetNamedAnimationState(entity, name) : nullptr;
		return animationState && TrySetAnimationLoop(animationState, loop);
	}

	bool SetAnimationWeight(void* entity, const std::string& name, float weight)
	{
		void* animationState = entity ? GetNamedAnimationState(entity, name) : nullptr;
		return animationState && TrySetAnimationWeight(animationState, weight);
	}

	bool SetAnimationTime(void* entity, const std::string& name, float timePosition)
	{
		void* animationState = entity ? GetNamedAnimationState(entity, name) : nullptr;
		return animationState && TrySetAnimationTimePosition(animationState, timePosition);
	}

	int GetEntityVisible(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		bool visible = false;
		if (!TryGetVisible(entity, visible))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushboolean(L, visible ? 1 : 0);
		return 1;
	}

	int GetEntityCastShadows(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		bool castShadows = false;
		if (!TryGetCastShadows(entity, castShadows))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushboolean(L, castShadows ? 1 : 0);
		return 1;
	}

	int GetEntityRenderingDistance(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		float distance = 0.0f;
		if (!TryGetRenderingDistance(entity, distance))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushnumber(L, distance);
		return 1;
	}

	int GetEntityVisibilityFlags(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		uint32_t flags = 0;
		if (!TryGetVisibilityFlags(entity, flags))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushinteger(L, flags);
		return 1;
	}

	int GetEntityQueryFlags(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		uint32_t flags = 0;
		if (!TryGetQueryFlags(entity, flags))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushinteger(L, flags);
		return 1;
	}

	int GetEntityRenderQueueGroup(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		uint8_t group = 0;
		if (!TryGetRenderQueueGroup(entity, group))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushinteger(L, group);
		return 1;
	}

	int SetEntityVisible(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		bool visible = CheckBool(L, 2);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		TrySetVisible(entity, visible);
		return 0;
	}

	int SetEntityCastShadows(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		bool castShadows = CheckBool(L, 2);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		TrySetCastShadows(entity, castShadows);
		return 0;
	}

	int SetEntityRenderingDistance(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		float distance = static_cast<float>(luaL_checknumber(L, 2));
		if (!std::isfinite(distance) || distance < 0.0f)
		{
			return luaL_argerror(L, 2, "rendering distance must be a finite non-negative number");
		}

		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		TrySetRenderingDistance(entity, distance);
		return 0;
	}

	int SetEntityVisibilityFlags(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		uint32_t flags = static_cast<uint32_t>(luaL_checkinteger(L, 2));
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		TrySetVisibilityFlags(entity, flags);
		return 0;
	}

	int SetEntityQueryFlags(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		uint32_t flags = static_cast<uint32_t>(luaL_checkinteger(L, 2));
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		TrySetQueryFlags(entity, flags);
		return 0;
	}

	int SetEntityRenderQueueGroup(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		int groupValue = luaL_checkinteger(L, 2);
		if (groupValue < 0 || groupValue > 255)
		{
			return luaL_argerror(L, 2, "render queue group must be in range 0..255");
		}

		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		TrySetRenderQueueGroup(entity, static_cast<uint8_t>(groupValue));
		return 0;
	}

	int SetSubEntityVisible(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		void* subEntity = GetSubEntity(L, entity, 2);
		bool visible = CheckBool(L, 3);
		// SubEntity::setVisible, not MovableObject::setVisible: a SubEntity is
		// a Renderable, and the MovableObject export wrote into the wrong
		// object layout.
		TrySetSubEntityVisible(subEntity, visible);
		return 0;
	}

	namespace
	{
		bool MaterialNameMatches(const std::string& a, const char* b)
		{
			const size_t n = std::strlen(b);
			if (a.size() != n)
			{
				return false;
			}
			for (size_t i = 0; i < n; ++i)
			{
				const unsigned char x = static_cast<unsigned char>(a[i]);
				const unsigned char y = static_cast<unsigned char>(b[i]);
				if (std::tolower(x) != std::tolower(y))
				{
					return false;
				}
			}
			return true;
		}
	}

	// exu.SetSubEntityRenderQueueGroup(target, indexOrMaterialName, group[, priority])
	// Moves one sub-entity (a 0-based index) or every sub-entity drawn with a
	// material (name, case-insensitive) into its own render queue group.
	// Ogre's Entity::_updateRenderQueue uses a sub-entity's own group before
	// the entity's, so this is how a cockpit's glass submesh is drawn after the
	// world while the rest of the cockpit stays in group 10. Works with
	// exu.animation.TargetCockpit(h) (re-resolved on every call: the cockpit
	// entity is rebuilt on vehicle and view changes, which also drops the
	// setting, so callers re-apply it). Returns the number of sub-entities
	// changed and the first index changed, or 0 when the entity is absent or
	// nothing matched.
	int SetSubEntityRenderQueueGroup(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		const bool byName = lua_type(L, 2) == LUA_TSTRING;
		if (!byName)
		{
			luaL_checkinteger(L, 2);
		}
		const int groupValue = luaL_checkinteger(L, 3);
		if (groupValue < 0 || groupValue > 255)
		{
			return luaL_argerror(L, 3, "render queue group must be in range 0..255");
		}
		const bool hasPriority = !lua_isnoneornil(L, 4);
		int priorityValue = 0;
		if (hasPriority)
		{
			priorityValue = luaL_checkinteger(L, 4);
			if (priorityValue < 0 || priorityValue > 65535)
			{
				return luaL_argerror(L, 4, "render queue priority must be in range 0..65535");
			}
		}

		void* entity = GetRenderableEntity(h);
		if (entity == nullptr || !SubEntityRenderQueueSupported())
		{
			lua_pushinteger(L, 0);
			return 1;
		}

		const auto group = static_cast<uint8_t>(groupValue);
		const auto priority = static_cast<uint16_t>(priorityValue);
		if (!byName)
		{
			void* subEntity = GetSubEntity(L, entity, 2);
			const bool ok = TrySetSubEntityRenderQueue(subEntity, group, hasPriority, priority);
			lua_pushinteger(L, ok ? 1 : 0);
			if (!ok)
			{
				return 1;
			}
			lua_pushinteger(L, lua_tointeger(L, 2));
			return 2;
		}

		const char* const materialName = lua_tostring(L, 2);
		uint32_t count = 0;
		if (!TryGetNumSubEntities(entity, count) || count > 256)
		{
			lua_pushinteger(L, 0);
			return 1;
		}
		int changed = 0;
		int first = -1;
		std::string name;
		for (uint32_t i = 0; i < count; ++i)
		{
			void* subEntity = nullptr;
			if (!TryGetSubEntityByIndex(entity, i, subEntity) || subEntity == nullptr ||
				!TryGetMaterialName(subEntity, name) || !MaterialNameMatches(name, materialName))
			{
				continue;
			}
			if (TrySetSubEntityRenderQueue(subEntity, group, hasPriority, priority))
			{
				if (first < 0)
				{
					first = static_cast<int>(i);
				}
				++changed;
			}
		}
		lua_pushinteger(L, changed);
		if (first < 0)
		{
			return 1;
		}
		lua_pushinteger(L, first);
		return 2;
	}

	// exu.GetSubEntityRenderQueueGroup(target, index) -> group, isOwnGroup
	// isOwnGroup is false while the sub-entity still follows its entity's group.
	int GetSubEntityRenderQueueGroup(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		luaL_checkinteger(L, 2);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		void* subEntity = GetSubEntity(L, entity, 2);
		uint8_t group = 0;
		bool isSet = false;
		if (!TryGetSubEntityRenderQueue(subEntity, group, isSet))
		{
			return 0;
		}
		lua_pushinteger(L, group);
		lua_pushboolean(L, isSet ? 1 : 0);
		return 2;
	}

	int SetHeadlightDiffuse(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float r = static_cast<float>(luaL_checknumber(L, 2));
		float g = static_cast<float>(luaL_checknumber(L, 3));
		float b = static_cast<float>(luaL_checknumber(L, 4));

		void* light = GetLightObject(h);

		if (light == nullptr)
		{
			return 0;
		}

		TrySetLightDiffuse(light, r, g, b);

		return 0;
	}

	int SetHeadlightSpecular(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float r = static_cast<float>(luaL_checknumber(L, 2));
		float g = static_cast<float>(luaL_checknumber(L, 3));
		float b = static_cast<float>(luaL_checknumber(L, 4));

		void* light = GetLightObject(h);

		if (light == nullptr)
		{
			return 0;
		}

		TrySetLightSpecular(light, r, g, b);

		return 0;
	}

	int SetHeadlightRange(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float innerAngle = static_cast<float>(luaL_checknumber(L, 2));
		float outerAngle = static_cast<float>(luaL_checknumber(L, 3));
		float falloff = static_cast<float>(luaL_checknumber(L, 4));

		void* light = GetLightObject(h);

		if (light == nullptr)
		{
			return 0;
		}

		TrySetSpotlightRange(light, innerAngle, outerAngle, falloff);

		return 0;
	}

	int SetHeadlightVisible(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		bool visible = CheckBool(L, 2);

		void* light = GetLightObject(h);

		if (light == nullptr)
		{
			return 0;
		}

		TrySetVisible(light, visible);

		return 0;
	}

	int GetLightPowerScale(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		void* light = GetLightObject(h);
		if (light == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		float powerScale = 0.0f;
		if (!TryGetLightPowerScale(light, powerScale))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushnumber(L, powerScale);
		return 1;
	}

	int SetLightPowerScale(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float powerScale = static_cast<float>(luaL_checknumber(L, 2));
		if (!std::isfinite(powerScale))
		{
			return luaL_argerror(L, 2, "power scale must be a finite number");
		}

		void* light = GetLightObject(h);
		if (light == nullptr)
		{
			return 0;
		}

		TrySetLightPowerScale(light, powerScale);
		return 0;
	}

	int GetLightPosition(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		void* light = GetLightObject(h);
		if (light == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		BZR::VECTOR_3D position{};
		if (!TryGetLightPosition(light, position))
		{
			lua_pushnil(L);
			return 1;
		}

		PushVector(L, position);
		return 1;
	}

	int SetLightPosition(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		auto position = CheckVectorOrSingles(L, 2);
		if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
		{
			return luaL_argerror(L, 2, "light position must use finite numeric values");
		}

		void* light = GetLightObject(h);
		if (light == nullptr)
		{
			return 0;
		}

		TrySetLightPosition(light, position);
		return 0;
	}

	int GetLightDirection(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		void* light = GetLightObject(h);
		if (light == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		BZR::VECTOR_3D direction{};
		if (!TryGetLightDirection(light, direction))
		{
			lua_pushnil(L);
			return 1;
		}

		PushVector(L, direction);
		return 1;
	}

	int SetLightDirection(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		auto direction = CheckVectorOrSingles(L, 2);
		if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z))
		{
			return luaL_argerror(L, 2, "light direction must use finite numeric values");
		}

		void* light = GetLightObject(h);
		if (light == nullptr)
		{
			return 0;
		}

		TrySetLightDirection(light, direction);
		return 0;
	}

	int SetLightAttenuation(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float range = static_cast<float>(luaL_checknumber(L, 2));
		float constant = static_cast<float>(luaL_checknumber(L, 3));
		float linear = static_cast<float>(luaL_checknumber(L, 4));
		float quadratic = static_cast<float>(luaL_checknumber(L, 5));
		if (!std::isfinite(range) || !std::isfinite(constant) || !std::isfinite(linear) || !std::isfinite(quadratic))
		{
			return luaL_argerror(L, 2, "light attenuation values must be finite numbers");
		}

		void* light = GetLightObject(h);
		if (light == nullptr)
		{
			return 0;
		}

		TrySetLightAttenuation(light, range, constant, linear, quadratic);
		return 0;
	}

	int HasEntityAnimation(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		std::string animationName = luaL_checkstring(L, 2);
		void* entity = GetRenderableEntity(h);
		lua_pushboolean(L, entity != nullptr && GetNamedAnimationState(entity, animationName) != nullptr);
		return 1;
	}

	int GetEntityAnimationInfo(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		std::string animationName = luaL_checkstring(L, 2);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		void* animationState = GetNamedAnimationState(entity, animationName);
		if (animationState == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		bool enabled = false;
		bool loop = false;
		float weight = 0.0f;
		float timePosition = 0.0f;
		float length = 0.0f;
		if (!TryGetAnimationEnabled(animationState, enabled) ||
			!TryGetAnimationLoop(animationState, loop) ||
			!TryGetAnimationWeight(animationState, weight) ||
			!TryGetAnimationTimePosition(animationState, timePosition) ||
			!TryGetAnimationLength(animationState, length))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_createtable(L, 0, 5);
		lua_pushboolean(L, enabled ? 1 : 0);
		lua_setfield(L, -2, "enabled");
		lua_pushboolean(L, loop ? 1 : 0);
		lua_setfield(L, -2, "loop");
		lua_pushnumber(L, weight);
		lua_setfield(L, -2, "weight");
		lua_pushnumber(L, timePosition);
		lua_setfield(L, -2, "timePosition");
		lua_pushnumber(L, length);
		lua_setfield(L, -2, "length");
		return 1;
	}

	int SetEntityAnimationEnabled(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		const char* const animationName = luaL_checkstring(L, 2);
		bool enabled = CheckBool(L, 3);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		if (void* animationState = GetNamedAnimationState(entity, animationName))
		{
			TrySetAnimationEnabled(animationState, enabled);
		}
		return 0;
	}

	int SetEntityAnimationLoop(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		const char* const animationName = luaL_checkstring(L, 2);
		bool loop = CheckBool(L, 3);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		if (void* animationState = GetNamedAnimationState(entity, animationName))
		{
			TrySetAnimationLoop(animationState, loop);
		}
		return 0;
	}

	int SetEntityAnimationWeight(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		const char* const animationName = luaL_checkstring(L, 2);
		float weight = static_cast<float>(luaL_checknumber(L, 3));
		if (!std::isfinite(weight))
		{
			return luaL_argerror(L, 3, "animation weight must be a finite number");
		}

		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		if (void* animationState = GetNamedAnimationState(entity, animationName))
		{
			TrySetAnimationWeight(animationState, weight);
		}
		return 0;
	}

	int SetEntityAnimationTime(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		const char* const animationName = luaL_checkstring(L, 2);
		float timePosition = static_cast<float>(luaL_checknumber(L, 3));
		if (!std::isfinite(timePosition))
		{
			return luaL_argerror(L, 3, "animation time must be a finite number");
		}

		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		if (void* animationState = GetNamedAnimationState(entity, animationName))
		{
			TrySetAnimationTimePosition(animationState, timePosition);
		}
		return 0;
	}
}
