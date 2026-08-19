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
			BZR::handle handle = nullptr;
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
			// The first-person target deliberately remains fail-closed until the
			// aspilo_fp ownership experiment identifies a stable resolver.
			return target.kind == TargetKind::GameObject && target.handle != nullptr;
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
			if (_stricmp(kind, "gameObject") == 0)
			{
				target.kind = TargetKind::GameObject;
			}
			else if (_stricmp(kind, "localFirstPerson") == 0)
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

		inline bool RawHas(lua_State* L, BZR::handle handle, const std::string& name)
		{
			lua_settop(L, 0);
			PushHandle(L, handle);
			lua_pushlstring(L, name.data(), name.size());
			GameObject::HasEntityAnimation(L);
			const bool result = lua_toboolean(L, -1) != 0;
			lua_settop(L, 0);
			return result;
		}

		inline int RawGetInfo(lua_State* L, BZR::handle handle, const std::string& name)
		{
			lua_settop(L, 0);
			PushHandle(L, handle);
			lua_pushlstring(L, name.data(), name.size());
			return GameObject::GetEntityAnimationInfo(L);
		}

		inline void RawSetEnabled(lua_State* L, BZR::handle handle, const std::string& name, bool enabled)
		{
			lua_settop(L, 0);
			PushHandle(L, handle);
			lua_pushlstring(L, name.data(), name.size());
			lua_pushboolean(L, enabled ? 1 : 0);
			GameObject::SetEntityAnimationEnabled(L);
		}

		inline void RawSetLoop(lua_State* L, BZR::handle handle, const std::string& name, bool loop)
		{
			lua_settop(L, 0);
			PushHandle(L, handle);
			lua_pushlstring(L, name.data(), name.size());
			lua_pushboolean(L, loop ? 1 : 0);
			GameObject::SetEntityAnimationLoop(L);
		}

		inline void RawSetWeight(lua_State* L, BZR::handle handle, const std::string& name, float weight)
		{
			lua_settop(L, 0);
			PushHandle(L, handle);
			lua_pushlstring(L, name.data(), name.size());
			lua_pushnumber(L, weight);
			GameObject::SetEntityAnimationWeight(L);
		}

		inline void RawSetTime(lua_State* L, BZR::handle handle, const std::string& name, float timePosition)
		{
			lua_settop(L, 0);
			PushHandle(L, handle);
			lua_pushlstring(L, name.data(), name.size());
			lua_pushnumber(L, timePosition);
			GameObject::SetEntityAnimationTime(L);
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

	inline int GetCapabilities(lua_State* L)
	{
		lua_createtable(L, 0, 5);
		lua_pushboolean(L, 1);
		lua_setfield(L, -2, "gameObjectTarget");
		lua_pushboolean(L, 0);
		lua_setfield(L, -2, "localFirstPersonTarget");
		lua_pushboolean(L, 0);
		lua_setfield(L, -2, "managedClock");
		lua_pushstring(L, "unvalidated");
		lua_setfield(L, -2, "nativeAdvancement");
		lua_pushstring(L, "aspilo_fp resolver requires live ownership validation");
		lua_setfield(L, -2, "firstPersonStatus");
		return 1;
	}

	inline int Has(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		if (!Detail::IsTargetSupported(target))
		{
			lua_settop(L, 0);
			lua_pushboolean(L, 0);
			return 1;
		}

		const bool result = Detail::RawHas(L, target.handle, name);
		lua_pushboolean(L, result ? 1 : 0);
		return 1;
	}

	inline int GetInfo(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		if (!Detail::IsTargetSupported(target))
		{
			lua_settop(L, 0);
			lua_pushnil(L);
			return 1;
		}

		Detail::RawGetInfo(L, target.handle, name);
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
		if (!Detail::IsTargetSupported(target) || !Detail::RawHas(L, target.handle, name))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		if (options.restart)
		{
			Detail::RawSetTime(L, target.handle, name, 0.0f);
		}
		Detail::RawSetLoop(L, target.handle, name, options.loop);
		Detail::RawSetWeight(L, target.handle, name, options.weight);
		Detail::RawSetEnabled(L, target.handle, name, true);
		lua_settop(L, 0);
		lua_pushboolean(L, 1);
		return 1;
	}

	inline int Stop(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		const bool reset = lua_isnoneornil(L, 3) ? false : CheckBool(L, 3);
		if (!Detail::IsTargetSupported(target) || !Detail::RawHas(L, target.handle, name))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		Detail::RawSetEnabled(L, target.handle, name, false);
		if (reset)
		{
			Detail::RawSetTime(L, target.handle, name, 0.0f);
		}
		lua_settop(L, 0);
		lua_pushboolean(L, 1);
		return 1;
	}

	inline int Restart(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		if (!Detail::IsTargetSupported(target) || !Detail::RawHas(L, target.handle, name))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		Detail::RawSetTime(L, target.handle, name, 0.0f);
		Detail::RawSetEnabled(L, target.handle, name, true);
		lua_settop(L, 0);
		lua_pushboolean(L, 1);
		return 1;
	}

	inline int SetEnabled(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		const bool enabled = CheckBool(L, 3);
		if (!Detail::IsTargetSupported(target) || !Detail::RawHas(L, target.handle, name))
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		Detail::RawSetEnabled(L, target.handle, name, enabled);
		lua_settop(L, 0);
		lua_pushboolean(L, 1);
		return 1;
	}

	inline int SetLoop(lua_State* L)
	{
		const Detail::Target target = Detail::ReadTarget(L, 1);
		const std::string name = luaL_checkstring(L, 2);
		const bool loop = CheckBool(L, 3);
		if (!Detail::IsTargetSupported(target) || !Detail::RawHas(L, target.handle, name))
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		Detail::RawSetLoop(L, target.handle, name, loop);
		lua_settop(L, 0);
		lua_pushboolean(L, 1);
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
		if (!Detail::IsTargetSupported(target) || !Detail::RawHas(L, target.handle, name))
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		Detail::RawSetWeight(L, target.handle, name, weight);
		lua_settop(L, 0);
		lua_pushboolean(L, 1);
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
		if (!Detail::IsTargetSupported(target) || !Detail::RawHas(L, target.handle, name))
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		Detail::RawSetTime(L, target.handle, name, timePosition);
		lua_settop(L, 0);
		lua_pushboolean(L, 1);
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

		Logging::LogMessage("exu: installed high-level animation API (gameObject target; first-person resolver pending validation)");
	}
}
