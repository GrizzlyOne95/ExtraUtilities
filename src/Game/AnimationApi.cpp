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

#include "Game/AnimationApi.h"

#include "Game/FirstPersonTarget.h"
#include "Game/GameObject.h"
#include "Game/PilotAnimationPolicy.h"
#include "Game/PilotFsmIntercept.h"
#include "Game/PilotState.h"
#include "Game/PilotStateSemantics.h"
#include "LuaHelpers.h"
#include "OpenShimBridge.h"
#include "Util/Logging.h"
#include "LuaCppBarrier.h"

#include <lua.hpp>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace ExtraUtilities::Lua::AnimationApi
{
	namespace
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

			const char* TargetKindName(TargetKind kind)
			{
				return kind == TargetKind::GameObject ? "gameObject" : "localFirstPerson";
			}

			bool IsTargetSupported(const Target& target)
			{
				if (target.kind == TargetKind::GameObject)
					return target.handle != 0;
				return OpenShimBridge::HasLocalFirstPersonEntityBridge() ||
					FirstPersonTarget::IsNativeResolverAvailable();
			}

			void* ResolveTargetEntity(const Target& target, std::uint64_t* generation = nullptr)
			{
				if (generation)
					*generation = 0;
				if (target.kind == TargetKind::GameObject)
					return target.handle ? GameObject::ResolveAnimationEntity(target.handle) : nullptr;

				void* entity = nullptr;
				if (OpenShimBridge::HasLocalFirstPersonEntityBridge())
				{
					std::uint64_t resolvedGeneration = 0;
					if (!OpenShimBridge::ResolveLocalFirstPersonEntity(entity, resolvedGeneration))
						return nullptr;
					if (generation)
						*generation = resolvedGeneration;
					return entity;
				}

				// Standalone EXU path: resolve the dedicated FP entity directly from
				// the live local Person render bridge. No Ogre pointer is retained.
				if (!FirstPersonTarget::ResolveNativeLocalFirstPersonEntity(entity))
					return nullptr;
				return entity;
			}

			Target ReadTarget(lua_State* L, int index)
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

			PlayOptions ReadPlayOptions(lua_State* L, int index)
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

			void PushHandle(lua_State* L, BZR::handle handle)
			{
				lua_pushlightuserdata(L, reinterpret_cast<void*>(handle));
			}

			bool RawHas(void* entity, const std::string& name)
			{
				return GameObject::HasAnimation(entity, name);
			}

			int RawGetInfo(lua_State* L, void* entity, const std::string& name)
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

			bool RawSetEnabled(void* entity, const std::string& name, bool enabled)
			{
				return GameObject::SetAnimationEnabled(entity, name, enabled);
			}

			bool RawSetLoop(void* entity, const std::string& name, bool loop)
			{
				return GameObject::SetAnimationLoop(entity, name, loop);
			}

			bool RawSetWeight(void* entity, const std::string& name, float weight)
			{
				return GameObject::SetAnimationWeight(entity, name, weight);
			}

			bool RawSetTime(void* entity, const std::string& name, float timePosition)
			{
				return GameObject::SetAnimationTime(entity, name, timePosition);
			}

			void PushAnimationSnapshot(lua_State* L, const Target& target, const GameObject::EntityAnimationSnapshot& snapshot)
			{
				lua_createtable(L, 0, 9);
				lua_pushlstring(L, snapshot.name.data(), snapshot.name.size());
				lua_setfield(L, -2, "name");
				lua_pushstring(L, TargetKindName(target.kind));
				lua_setfield(L, -2, "targetKind");
				lua_pushboolean(L, snapshot.info.enabled ? 1 : 0);
				lua_setfield(L, -2, "enabled");
				lua_pushboolean(L, snapshot.info.loop ? 1 : 0);
				lua_setfield(L, -2, "loop");
				lua_pushnumber(L, snapshot.info.weight);
				lua_setfield(L, -2, "weight");
				lua_pushnumber(L, snapshot.info.timePosition);
				lua_setfield(L, -2, "timePosition");
				lua_pushnumber(L, snapshot.info.length);
				lua_setfield(L, -2, "length");

				const float normalizedTime = snapshot.info.length > 0.0f
					? snapshot.info.timePosition / snapshot.info.length
					: 0.0f;
				lua_pushnumber(L, normalizedTime);
				lua_setfield(L, -2, "normalizedTime");
				lua_pushboolean(
					L,
					(!snapshot.info.loop &&
						snapshot.info.length > 0.0f &&
						snapshot.info.timePosition >= snapshot.info.length)
						? 1
						: 0);
				lua_setfield(L, -2, "atEnd");
			}

			void AddDerivedInfo(lua_State* L, const Target& target, const std::string& name)
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

		void PrependLocalFirstPersonTarget(lua_State* L)
		{
			lua_createtable(L, 0, 1);
			lua_pushstring(L, "localFirstPerson");
			lua_setfield(L, -2, "kind");
			lua_insert(L, 1);
		}

		int Target(lua_State* L)
		{
			const BZR::handle handle = CheckHandle(L, 1);
			lua_createtable(L, 0, 2);
			lua_pushstring(L, "gameObject");
			lua_setfield(L, -2, "kind");
			Detail::PushHandle(L, handle);
			lua_setfield(L, -2, "handle");
			return 1;
		}

		int TargetLocalFirstPerson(lua_State* L)
		{
			lua_createtable(L, 0, 1);
			lua_pushstring(L, "localFirstPerson");
			lua_setfield(L, -2, "kind");
			return 1;
		}

		int GetCapabilities(lua_State* L)
		{
			lua_createtable(L, 0, 9);
			lua_pushboolean(L, 1);
			lua_setfield(L, -2, "gameObjectTarget");
			const bool hasFpBridge = OpenShimBridge::HasLocalFirstPersonEntityBridge();
			const bool hasNativeFpResolver = FirstPersonTarget::IsNativeResolverAvailable();
			lua_pushboolean(L, (hasFpBridge || hasNativeFpResolver) ? 1 : 0);
			lua_setfield(L, -2, "localFirstPersonTarget");
			lua_pushboolean(L, 1);
			lua_setfield(L, -2, "animationInventory");
			lua_pushboolean(L, 1);
			lua_setfield(L, -2, "pilotStateInspection");
			lua_pushboolean(L, PilotFsmIntercept::IsActive() ? 1 : 0);
			lua_setfield(L, -2, "pilotFsmIntercept");
			// Reports what this build can apply, not the current policy: the pilot
			// animation policy only represents stock pass-through today.
			lua_pushboolean(L, 0);
			lua_setfield(L, -2, "pilotAnimationOverrides");
			lua_pushboolean(L, 0);
			lua_setfield(L, -2, "managedClock");
			lua_pushstring(L, "unvalidated");
			lua_setfield(L, -2, "nativeAdvancement");
			const char* firstPersonStatus = hasFpBridge
				? (hasNativeFpResolver
					? "OpenShim resolver active; EXU native fallback available"
					: "stock control proven-runtime via OpenShim resolver")
				: (hasNativeFpResolver
					? "EXU native resolver available; standalone runtime qualification pending"
					: "first-person resolver unavailable");
			lua_pushstring(L, firstPersonStatus);
			lua_setfield(L, -2, "firstPersonStatus");
			return 1;
		}

		int Has(lua_State* L)
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

		int GetInfo(lua_State* L)
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

		int List(lua_State* L)
		{
			const Detail::Target target = Detail::ReadTarget(L, 1);
			void* entity = Detail::IsTargetSupported(target) ? Detail::ResolveTargetEntity(target) : nullptr;
			if (!entity)
			{
				lua_settop(L, 0);
				lua_pushnil(L);
				return 1;
			}

			std::vector<GameObject::EntityAnimationSnapshot> inventory;
			if (!GameObject::GetAnimationInventory(entity, inventory))
			{
				lua_settop(L, 0);
				lua_pushnil(L);
				return 1;
			}

			lua_settop(L, 0);
			lua_createtable(L, static_cast<int>(inventory.size()), 0);
			int index = 1;
			for (const GameObject::EntityAnimationSnapshot& snapshot : inventory)
			{
				Detail::PushAnimationSnapshot(L, target, snapshot);
				lua_rawseti(L, -2, index++);
			}
			return 1;
		}

		int Play(lua_State* L)
		{
			const Detail::Target target = Detail::ReadTarget(L, 1);
			const char* const nameArg = luaL_checkstring(L, 2);
			const Detail::PlayOptions options = Detail::ReadPlayOptions(L, 3);
			const std::string name(nameArg);
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

		int Stop(lua_State* L)
		{
			const Detail::Target target = Detail::ReadTarget(L, 1);
			const char* const nameArg = luaL_checkstring(L, 2);
			const bool reset = lua_isnoneornil(L, 3) ? false : CheckBool(L, 3);
			const std::string name(nameArg);
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

		int Restart(lua_State* L)
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

		int SetEnabled(lua_State* L)
		{
			const Detail::Target target = Detail::ReadTarget(L, 1);
			const char* const nameArg = luaL_checkstring(L, 2);
			const bool enabled = CheckBool(L, 3);
			const std::string name(nameArg);
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

		int SetLoop(lua_State* L)
		{
			const Detail::Target target = Detail::ReadTarget(L, 1);
			const char* const nameArg = luaL_checkstring(L, 2);
			const bool loop = CheckBool(L, 3);
			const std::string name(nameArg);
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

		int SetWeight(lua_State* L)
		{
			const Detail::Target target = Detail::ReadTarget(L, 1);
			const char* const nameArg = luaL_checkstring(L, 2);
			const float weight = static_cast<float>(luaL_checknumber(L, 3));
			if (!std::isfinite(weight) || weight < 0.0f || weight > 1.0f)
			{
				return luaL_argerror(L, 3, "animation weight must be a finite value in [0, 1]");
			}
			const std::string name(nameArg);
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

		int Seek(lua_State* L)
		{
			const Detail::Target target = Detail::ReadTarget(L, 1);
			const char* const nameArg = luaL_checkstring(L, 2);
			const float timePosition = static_cast<float>(luaL_checknumber(L, 3));
			if (!std::isfinite(timePosition) || timePosition < 0.0f)
			{
				return luaL_argerror(L, 3, "animation time must be a finite non-negative value");
			}
			const std::string name(nameArg);
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

		void FormatSignature(std::uint32_t signature, char (&out)[5]) noexcept
		{
			out[0] = static_cast<char>((signature >> 24) & 0xFFu);
			out[1] = static_cast<char>((signature >> 16) & 0xFFu);
			out[2] = static_cast<char>((signature >> 8) & 0xFFu);
			out[3] = static_cast<char>(signature & 0xFFu);
			out[4] = '\0';
			for (int i = 0; i < 4; ++i)
			{
				const unsigned char ch = static_cast<unsigned char>(out[i]);
				if (ch < 32 || ch > 126)
				{
					out[i] = '.';
				}
			}
		}

		void PushPilotState(lua_State* L, const PilotState::Snapshot& snapshot)
		{
			lua_createtable(L, 0, 14);

			lua_pushboolean(L, snapshot.available ? 1 : 0);
			lua_setfield(L, -2, "available");

			lua_pushstring(L, PilotState::SemanticStateName(snapshot.nativeState));
			lua_setfield(L, -2, "state");

			lua_pushinteger(L, static_cast<lua_Integer>(snapshot.nativeState));
			lua_setfield(L, -2, "nativeState");

			lua_pushboolean(L, PilotState::IsTransitionState(snapshot.nativeState) ? 1 : 0);
			lua_setfield(L, -2, "transition");

			lua_pushboolean(L, PilotState::IsFullyCrouchedState(snapshot.nativeState) ? 1 : 0);
			lua_setfield(L, -2, "crouched");

			lua_pushboolean(L, snapshot.grounded ? 1 : 0);
			lua_setfield(L, -2, "grounded");

			lua_pushboolean(L, snapshot.sniperSelected ? 1 : 0);
			lua_setfield(L, -2, "sniperSelected");

			lua_pushinteger(L, static_cast<lua_Integer>(snapshot.animationIndex));
			lua_setfield(L, -2, "animationIndex");

			if (const char* animationName = PilotState::KnownAnimationName(snapshot.animationIndex))
			{
				lua_pushstring(L, animationName);
			}
			else
			{
				lua_pushnil(L);
			}
			lua_setfield(L, -2, "animationName");

			lua_pushinteger(L, static_cast<lua_Integer>(snapshot.animationHandle));
			lua_setfield(L, -2, "animationHandle");

			lua_pushinteger(L, static_cast<lua_Integer>(snapshot.selectedWeaponMask));
			lua_setfield(L, -2, "selectedWeaponMask");

			if (snapshot.selectedWeaponSlot >= 0)
			{
				lua_pushinteger(L, static_cast<lua_Integer>(snapshot.selectedWeaponSlot));
				lua_setfield(L, -2, "selectedWeaponSlot");

				lua_pushinteger(L, static_cast<lua_Integer>(snapshot.selectedWeaponSignature));
				lua_setfield(L, -2, "selectedWeaponSignature");

				char signatureText[5]{};
				FormatSignature(snapshot.selectedWeaponSignature, signatureText);
				lua_pushlstring(L, signatureText, 4);
				lua_setfield(L, -2, "selectedWeaponSignatureText");

				if (snapshot.selectedWeaponOdf[0] != '\0')
				{
					lua_pushstring(L, snapshot.selectedWeaponOdf);
					lua_setfield(L, -2, "selectedWeaponOdf");
				}
			}
		}

		int FpsGetPilotState(lua_State* L)
		{
			PilotState::Snapshot snapshot{};
			if (!PilotState::Capture(snapshot))
			{
				lua_settop(L, 0);
				lua_pushnil(L);
				return 1;
			}

			lua_settop(L, 0);
			PushPilotState(L, snapshot);
			return 1;
		}

		int FpsIsCrouched(lua_State* L)
		{
			PilotState::Snapshot snapshot{};
			if (!PilotState::Capture(snapshot))
			{
				lua_settop(L, 0);
				lua_pushnil(L);
				return 1;
			}
			lua_settop(L, 0);
			lua_pushboolean(L, PilotState::IsFullyCrouchedState(snapshot.nativeState) ? 1 : 0);
			return 1;
		}

		int FpsIsGrounded(lua_State* L)
		{
			PilotState::Snapshot snapshot{};
			if (!PilotState::Capture(snapshot))
			{
				lua_settop(L, 0);
				lua_pushnil(L);
				return 1;
			}
			lua_settop(L, 0);
			lua_pushboolean(L, snapshot.grounded ? 1 : 0);
			return 1;
		}

		int FpsIsSniperSelected(lua_State* L)
		{
			PilotState::Snapshot snapshot{};
			if (!PilotState::Capture(snapshot))
			{
				lua_settop(L, 0);
				lua_pushnil(L);
				return 1;
			}
			lua_settop(L, 0);
			lua_pushboolean(L, snapshot.sniperSelected ? 1 : 0);
			return 1;
		}

		// Effective pilot animation profile, read-only. Needs no runtime gate: it
		// describes EXU's own mission-scoped policy, not engine memory.
		int FpsGetPilotAnimationProfile(lua_State* L)
		{
			const PilotAnimationPolicy::Policy policy = PilotAnimationPolicy::GetActive();

			lua_settop(L, 0);
			lua_createtable(L, 0, static_cast<int>(PilotAnimationPolicy::kSlotCount));

			constexpr PilotAnimationPolicy::Slot slots[] = {
				PilotAnimationPolicy::Slot::Stand,
				PilotAnimationPolicy::Slot::EnterCrouch,
				PilotAnimationPolicy::Slot::Crouched,
				PilotAnimationPolicy::Slot::ExitCrouch,
				PilotAnimationPolicy::Slot::Jump,
				PilotAnimationPolicy::Slot::Land,
			};
			static_assert(
				sizeof(slots) / sizeof(slots[0]) == PilotAnimationPolicy::kSlotCount,
				"the profile read-back must list every policy slot");

			for (const PilotAnimationPolicy::Slot slot : slots)
			{
				lua_createtable(L, 0, 2);

				lua_pushstring(L, PilotAnimationPolicy::ModeName(policy.At(slot).mode));
				lua_setfield(L, -2, "mode");

				const std::int32_t nativeState = PilotAnimationPolicy::NativeStateForSlot(slot);
				if (nativeState >= 0)
				{
					lua_pushinteger(L, static_cast<lua_Integer>(nativeState));
					lua_setfield(L, -2, "nativeState");
				}

				lua_setfield(L, -2, PilotAnimationPolicy::SlotName(slot));
			}
			return 1;
		}

		int FpsGetPilotInterceptStatus(lua_State* L)
		{
			PilotFsmIntercept::Stats stats{};
			PilotFsmIntercept::GetStats(stats);

			lua_settop(L, 0);
			lua_createtable(L, 0, 21);

			lua_pushboolean(L, stats.installed ? 1 : 0);
			lua_setfield(L, -2, "installed");
			lua_pushboolean(L, stats.active ? 1 : 0);
			lua_setfield(L, -2, "active");
			lua_pushboolean(L, stats.observeOnly ? 1 : 0);
			lua_setfield(L, -2, "observeOnly");
			lua_pushboolean(L, stats.hasLocalSample ? 1 : 0);
			lua_setfield(L, -2, "hasLocalSample");

			lua_pushinteger(L, static_cast<lua_Integer>(stats.calls));
			lua_setfield(L, -2, "calls");
			lua_pushinteger(L, static_cast<lua_Integer>(stats.localCalls));
			lua_setfield(L, -2, "localCalls");
			lua_pushinteger(L, static_cast<lua_Integer>(stats.stateChanges));
			lua_setfield(L, -2, "stateChanges");
			lua_pushinteger(L, static_cast<lua_Integer>(stats.animationChanges));
			lua_setfield(L, -2, "animationChanges");

			if (stats.hasPolicyDecision)
			{
				lua_pushstring(L, PilotAnimationPolicy::DecisionName(stats.lastPolicyDecision));
				lua_setfield(L, -2, "policyDecision");
			}

			if (stats.hasLocalSample)
			{
				lua_pushinteger(L, static_cast<lua_Integer>(stats.lastBeforeState));
				lua_setfield(L, -2, "beforeNativeState");
				lua_pushinteger(L, static_cast<lua_Integer>(stats.lastAfterState));
				lua_setfield(L, -2, "afterNativeState");

				lua_pushstring(L, PilotState::SemanticStateName(stats.lastBeforeState));
				lua_setfield(L, -2, "beforeState");
				lua_pushstring(L, PilotState::SemanticStateName(stats.lastAfterState));
				lua_setfield(L, -2, "afterState");

				lua_pushinteger(L, static_cast<lua_Integer>(stats.lastBeforeAnimation));
				lua_setfield(L, -2, "beforeAnimationIndex");
				lua_pushinteger(L, static_cast<lua_Integer>(stats.lastAfterAnimation));
				lua_setfield(L, -2, "afterAnimationIndex");

				if (const char* name = PilotState::KnownAnimationName(stats.lastBeforeAnimation))
				{
					lua_pushstring(L, name);
					lua_setfield(L, -2, "beforeAnimationName");
				}
				if (const char* name = PilotState::KnownAnimationName(stats.lastAfterAnimation))
				{
					lua_pushstring(L, name);
					lua_setfield(L, -2, "afterAnimationName");
				}

				lua_pushinteger(L, static_cast<lua_Integer>(stats.lastBeforeAnimationHandle));
				lua_setfield(L, -2, "beforeAnimationHandle");
				lua_pushinteger(L, static_cast<lua_Integer>(stats.lastAfterAnimationHandle));
				lua_setfield(L, -2, "afterAnimationHandle");
			}

			return 1;
		}

		int FpsIsAvailable(lua_State* L)
		{
			Detail::Target target{};
			target.kind = Detail::TargetKind::LocalFirstPerson;
			const bool available = Detail::IsTargetSupported(target) &&
				Detail::ResolveTargetEntity(target) != nullptr;
			lua_settop(L, 0);
			lua_pushboolean(L, available ? 1 : 0);
			return 1;
		}

		int FpsGetCapabilities(lua_State* L)
		{
			lua_settop(L, 0);
			return GetCapabilities(L);
		}

		int FpsListAnimations(lua_State* L)
		{
			PrependLocalFirstPersonTarget(L);
			return List(L);
		}

		int FpsHasAnimation(lua_State* L)
		{
			PrependLocalFirstPersonTarget(L);
			return Has(L);
		}

		int FpsGetInfo(lua_State* L)
		{
			PrependLocalFirstPersonTarget(L);
			return GetInfo(L);
		}

		int FpsPlay(lua_State* L)
		{
			PrependLocalFirstPersonTarget(L);
			return Play(L);
		}

		int FpsStop(lua_State* L)
		{
			PrependLocalFirstPersonTarget(L);
			return Stop(L);
		}

		int FpsRestart(lua_State* L)
		{
			PrependLocalFirstPersonTarget(L);
			return Restart(L);
		}

		int FpsSeek(lua_State* L)
		{
			PrependLocalFirstPersonTarget(L);
			return Seek(L);
		}
	}

	void Install(lua_State* L)
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
			{ "List", &List },
			{ "Play", &Play },
			{ "Stop", &Stop },
			{ "Restart", &Restart },
			{ "SetEnabled", &SetEnabled },
			{ "SetLoop", &SetLoop },
			{ "SetWeight", &SetWeight },
			{ "Seek", &Seek },
			{ nullptr, nullptr },
		};

		static const luaL_Reg fpsFunctions[] = {
			{ "IsAvailable", &FpsIsAvailable },
			{ "GetCapabilities", &FpsGetCapabilities },
			{ "GetPilotState", &FpsGetPilotState },
			{ "GetPilotAnimationProfile", &FpsGetPilotAnimationProfile },
			{ "GetPilotInterceptStatus", &FpsGetPilotInterceptStatus },
			{ "IsCrouched", &FpsIsCrouched },
			{ "IsGrounded", &FpsIsGrounded },
			{ "IsSniperSelected", &FpsIsSniperSelected },
			{ "ListAnimations", &FpsListAnimations },
			{ "HasAnimation", &FpsHasAnimation },
			{ "GetInfo", &FpsGetInfo },
			{ "Play", &FpsPlay },
			{ "Stop", &FpsStop },
			{ "Restart", &FpsRestart },
			{ "Seek", &FpsSeek },
			{ nullptr, nullptr },
		};

		lua_newtable(L);
		RegisterFunctions(L, nullptr, functions);
		lua_setfield(L, -2, "animation");

		lua_newtable(L);
		RegisterFunctions(L, nullptr, fpsFunctions);
		lua_setfield(L, -2, "fps");
		lua_settop(L, originalTop);

		Logging::LogMessage("exu: installed high-level animation API (gameObject + standalone/OpenShim local-first-person targets) and local first-person facade");
	}
}
