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

/*
 * AnimationApi.h
 *
 * High-level Lua animation controls built on top of EXU's existing, SEH-guarded
 * GameObject/Ogre animation-state bridge. This layer intentionally does not
 * cache Ogre::Entity or Ogre::AnimationState pointers and does not install a
 * frame hook. Ogre/Redux remains responsible for skeletal evaluation and time
 * advancement unless later live research proves a managed clock is necessary.
 *
 * Lua accepts either a normal BZR Handle or a target descriptor returned by
 * exu.animation.Target(handle). The descriptor abstraction leaves room for a
 * validated local first-person/viewmodel resolver without changing the public
 * playback API later.
 */

#include "Game/GameObject.h"
#include "LuaHelpers.h"
#include "OpenShimBridge.h"
#include "Util/Logging.h"

#include <lua.hpp>

#include <cmath>
#include <cstring>
#include <string>

namespace ExtraUtilities::Lua::AnimationApi
{
	namespace Detail
	{
		enum class TargetKind
		{
			GameObject,
			LocalFirstPerson,
		};

		struct Target
		{
			TargetKind kind = TargetKind::GameObject;
			BZR::handle handle{};
		};

		struct PlayOptions
		{
			bool restart = true;
			bool loop = false;
			float weight = 1.0f;
		};

		inline const char* TargetKindName(TargetKind kind)
		{
			return kind == TargetKind::GameObject ? "gameObject" : "localFirstPerson";
		}

		inline bool IsTargetSupported(const Target& target)
		{
			if (target.kind == TargetKind::GameObject)
				return target.handle != 0;
			return OpenShimBridge::HasLocalFirstPersonEntityBridge();
		}

		inline void* ResolveTargetEntity(const Target& target, std::uint64_t* generation = nullptr)
		{
			if (generation)
				*generation = 0;
			if (target.kind == TargetKind::GameObject)
				return target.handle ? GameObject::ResolveAnimationEntity(target.handle) : nullptr;

			void* entity = nullptr;
			std::uint64_t resolvedGeneration = 0;
			if (!OpenShimBridge::ResolveLocalFirstPersonEntity(entity, resolvedGeneration))
				return nullptr;
			if (generation)
				*generation = resolvedGeneration;
			return entity;
		}

		inline Target ReadTarget(lua_State* L, int index)
		{
			const int absIndex = AbsoluteStackIndex(L, index);
			Target target{};

			if (!lua_istable(L, absIndex))
			{
				target.kind = TargetKind::GameObject;
				target.handle = CheckHandle(L, absIndex);
				return target;
			}

			lua_getfield(L, absIndex, "kind");
			const char* kind = luaL_checkstring(L, -1);
			if (std::strcmp(kind, "gameObject") == 0)
			{
				target.kind = TargetKind::GameObject;
			}
			else if (std::strcmp(kind, "localFirstPerson") == 0)
			{
				target.kind = TargetKind::LocalFirstPerson;
			}
			else
			{
				lua_pop(L, 1);
				luaL_argerror(L, index, "unknown animation target kind");
			}
			lua_pop(L, 1);

			if (target.kind == TargetKind::GameObject)
			{
				lua_getfield(L, absIndex, "handle");
				target.handle = CheckHandle(L, -1);
				lua_pop(L, 1);
			}

			return target;
		}

		inline PlayOptions ReadPlayOptions(lua_State* L, int index)
		{
			PlayOptions options{};
			if (lua_isnoneornil(L, index))
			{
				return options;
			}

			luaL_checktype(L, index, LUA_TTABLE);
			const int absIndex = AbsoluteStackIndex(L, index);

			lua_getfield(L, absIndex, "restart");
			if (!lua_isnil(L, -1))
			{
				options.restart = CheckBool(L, -1);
			}
			lua_pop(L, 1);

			lua_getfield(L, absIndex, "loop");
			if (!lua_isnil(L, -1))
			{
				options.loop = CheckBool(L, -1);
			}
			lua_pop(L, 1);

			lua_getfield(L, absIndex, "weight");
			if (!lua_isnil(L, -1))
			{
				options.weight = static_cast<float>(luaL_checknumber(L, -1));
			}
			lua_pop(L, 1);

			if (!std::isfinite(options.weight) || options.weight < 0.0f || options.weight > 1.0f)
			{
				luaL_argerror(L, index, "animation weight must be a finite value in [0, 1]");
			}

			return options;
		}

		inline void PushHandle(lua_State* L, BZR::handle handle)
		{
			lua_pushlightuserdata(L, reinterpret_cast<void*>(handle));
		}

		inline bool RawHas(void* entity, const std::string& name)
		{
			return GameObject::HasAnimation(entity, name);
		}

		inline int RawGetInfo(lua_State* L, void* entity, const std::string& name)
		{
			lua_settop(L, 0);
			GameObject::EntityAnimationInfo info{};
			if (!GameObject::GetAnimationInfo(entity, name, info))
			{
				lua_pushnil(L);
				return 1;
			}
			lua_createtable(L, 0, 5);
			lua_pushboolean(L, info.enabled ? 1 : 0);
			lua_setfield(L, -2, "enabled");
			lua_pushboolean(L, info.loop ? 1 : 0);
			lua_setfield(L, -2, "loop");
			lua_pushnumber(L, info.weight);
			lua_setfield(L, -2, "weight");
			lua_pushnumber(L, info.timePosition);
			lua_setfield(L, -2, "timePosition");
			lua_pushnumber(L, info.length);
			lua_setfield(L, -2, "length");
			return 1;
		}

		inline bool RawSetEnabled(void* entity, const std::string& name, bool enabled)
		{
			return GameObject::SetAnimationEnabled(entity, name, enabled);
		}

		inline bool RawSetLoop(void* entity, const std::string& name, bool loop)
		{
			return GameObject::SetAnimationLoop(entity, name, loop);
		}

		inline bool RawSetWeight(void* entity, const std::string& name, float weight)
		{
			return GameObject::SetAnimationWeight(entity, name, weight);
		}

		inline bool RawSetTime(void* entity, const std::string& name, float timePosition)
		{
			return GameObject::SetAnimationTime(entity, name, timePosition);
		}

		inline void AddDerivedInfo(lua_State* L, const Target& target, const std::string& name)
		{
			if (!lua_istable(L, -1))
			{
				return;
			}

			lua_pushlstring(L, name.data(), name.size());
			lua_setfield(L, -2, "name");
			lua_pushstring(L, TargetKindName(target.kind));
			lua_setfield(L, -2, "targetKind");

			lua_getfield(L, -1, "timePosition");
			const float timePosition = static_cast<float>(lua_tonumber(L, -1));
			lua_pop(L, 1);

			lua_getfield(L, -1, "length");
			const float length = static_cast<float>(lua_tonumber(L, -1));
			lua_pop(L, 1);

			lua_getfield(L, -1, "loop");
			const bool loop = lua_toboolean(L, -1) != 0;
			lua_pop(L, 1);

			const float normalizedTime = length > 0.0f ? timePosition / length : 0.0f;
			lua_pushnumber(L, normalizedTime);
			lua_setfield(L, -2, "normalizedTime");

			lua_pushboolean(L, (!loop && length > 0.0f && timePosition >= length) ? 1 : 0);
			lua_setfield(L, -2, "atEnd");
		}
	}

	inline int Target(lua_State* L)
	{
		const BZR::handle handle = CheckHandle(L, 1);
		lua_createtable(L, 0, 2);
		lua_pushstring(L, "gameObject");
		lua_setfield(L, -2, "kind");
		Detail::PushHandle(L, handle);
		lua_setfield(L, -2, "handle");
		return 1;
	}

	inline int TargetLocalFirstPerson(lua_State* L)
	{
		lua_createtable(L, 0, 1);
		lua_pushstring(L, "localFirstPerson");
		lua_setfield(L, -2, "kind");
		return 1;
	}

	inline int GetCapabilities(lua_State* L)
	{
		lua_createtable(L, 0, 5);
		lua_pushboolean(L, 1);
		lua_setfield(L, -2, "gameObjectTarget");
		const bool hasFpBridge = OpenShimBridge::HasLocalFirstPersonEntityBridge();
		lua_pushboolean(L, hasFpBridge ? 1 : 0);
		lua_setfield(L, -2, "localFirstPersonTarget");
		lua_pushboolean(L, 0);
		lua_setfield(L, -2, "managedClock");
		lua_pushstring(L, "unvalidated");
		lua_setfield(L, -2, "nativeAdvancement");
		lua_pushstring(L, hasFpBridge ? "stock control proven-runtime via OpenShim resolver" : "OpenShim resolver unavailable");
		lua_setfield(L, -2, "firstPersonStatus");
		return 1;
	}

	inline int Has(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		void* entity = Detail::IsTargetSupported(target) ? Detail::ResolveTargetEntity(target) : nullptr;
		if (!entity)
		{
			lua_settop(L, 0);
			lua_pushboolean(L, 0);
			return 1;
		}

		const bool result = Detail::RawHas(entity, name);
		lua_pushboolean(L, result ? 1 : 0);
		return 1;
	}

	inline int GetInfo(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		void* entity = Detail::IsTargetSupported(target) ? Detail::ResolveTargetEntity(target) : nullptr;
		if (!entity)
		{
			lua_settop(L, 0);
			lua_pushnil(L);
			return 1;
		}

		Detail::RawGetInfo(L, entity, name);
		if (!lua_istable(L, -1))
		{
			return 1;
		}
		Detail::AddDerivedInfo(L, target, name);
		return 1;
	}

	inline int Play(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		const Detail::PlayOptions options = Detail::ReadPlayOptions(L, 3);
		void* entity = Detail::IsTargetSupported(target) ? Detail::ResolveTargetEntity(target) : nullptr;
		if (!entity || !Detail::RawHas(entity, name))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		if (options.restart)
		{
			if (!Detail::RawSetTime(entity, name, 0.0f))
			{
				lua_pushboolean(L, 0);
				return 1;
			}
		}
		const bool applied = Detail::RawSetLoop(entity, name, options.loop) &&
			Detail::RawSetWeight(entity, name, options.weight) &&
			Detail::RawSetEnabled(entity, name, true);
		lua_settop(L, 0);
		lua_pushboolean(L, applied ? 1 : 0);
		return 1;
	}

	inline int Stop(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		const bool reset = lua_isnoneornil(L, 3) ? false : CheckBool(L, 3);
		void* entity = Detail::IsTargetSupported(target) ? Detail::ResolveTargetEntity(target) : nullptr;
		if (!entity || !Detail::RawHas(entity, name))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		bool applied = Detail::RawSetEnabled(entity, name, false);
		if (reset)
		{
			applied = Detail::RawSetTime(entity, name, 0.0f) && applied;
		}
		lua_settop(L, 0);
		lua_pushboolean(L, applied ? 1 : 0);
		return 1;
	}

	inline int Restart(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		void* entity = Detail::IsTargetSupported(target) ? Detail::ResolveTargetEntity(target) : nullptr;
		if (!entity || !Detail::RawHas(entity, name))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const bool applied = Detail::RawSetTime(entity, name, 0.0f) &&
			Detail::RawSetEnabled(entity, name, true);
		lua_settop(L, 0);
		lua_pushboolean(L, applied ? 1 : 0);
		return 1;
	}

	inline int SetEnabled(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		const bool enabled = CheckBool(L, 3);
		void* entity = Detail::IsTargetSupported(target) ? Detail::ResolveTargetEntity(target) : nullptr;
		if (!entity || !Detail::RawHas(entity, name))
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		const bool applied = Detail::RawSetEnabled(entity, name, enabled);
		lua_settop(L, 0);
		lua_pushboolean(L, applied ? 1 : 0);
		return 1;
	}

	inline int SetLoop(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		const bool loop = CheckBool(L, 3);
		void* entity = Detail::IsTargetSupported(target) ? Detail::ResolveTargetEntity(target) : nullptr;
		if (!entity || !Detail::RawHas(entity, name))
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		const bool applied = Detail::RawSetLoop(entity, name, loop);
		lua_settop(L, 0);
		lua_pushboolean(L, applied ? 1 : 0);
		return 1;
	}

	inline int SetWeight(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		const float weight = static_cast<float>(luaL_checknumber(L, 3));
		if (!std::isfinite(weight) || weight < 0.0f || weight > 1.0f)
		{
			return luaL_argerror(L, 3, "animation weight must be a finite value in [0, 1]");
		}
		void* entity = Detail::IsTargetSupported(target) ? Detail::ResolveTargetEntity(target) : nullptr;
		if (!entity || !Detail::RawHas(entity, name))
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		const bool applied = Detail::RawSetWeight(entity, name, weight);
		lua_settop(L, 0);
		lua_pushboolean(L, applied ? 1 : 0);
		return 1;
	}

	inline int Seek(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		const float timePosition = static_cast<float>(luaL_checknumber(L, 3));
		if (!std::isfinite(timePosition) || timePosition < 0.0f)
		{
			return luaL_argerror(L, 3, "animation time must be a finite non-negative value");
		}
		void* entity = Detail::IsTargetSupported(target) ? Detail::ResolveTargetEntity(target) : nullptr;
		if (!entity || !Detail::RawHas(entity, name))
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		const bool applied = Detail::RawSetTime(entity, name, timePosition);
		lua_settop(L, 0);
		lua_pushboolean(L, applied ? 1 : 0);
		return 1;
	}

	inline void Install(lua_State* L)
	{
		if (L == nullptr)
		{
			return;
		}

		const int originalTop = lua_gettop(L);
		lua_getglobal(L, "exu");
		if (!lua_istable(L, -1))
		{
			lua_settop(L, originalTop);
			Logging::LogMessage("exu: animation API install skipped; exu table is unavailable");
			return;
		}

		static const luaL_Reg functions[] = {
			{ "Target", &Target },
			{ "TargetLocalFirstPerson", &TargetLocalFirstPerson },
			{ "GetCapabilities", &GetCapabilities },
			{ "Has", &Has },
			{ "GetInfo", &GetInfo },
			{ "Play", &Play },
			{ "Stop", &Stop },
			{ "Restart", &Restart },
			{ "SetEnabled", &SetEnabled },
			{ "SetLoop", &SetLoop },
			{ "SetWeight", &SetWeight },
			{ "Seek", &Seek },
			{ nullptr, nullptr },
		};

		lua_newtable(L);
		luaL_register(L, nullptr, functions);
		lua_setfield(L, -2, "animation");
		lua_settop(L, originalTop);

		Logging::LogMessage("exu: installed high-level animation API (gameObject + OpenShim local-first-person targets)");
	}
}
