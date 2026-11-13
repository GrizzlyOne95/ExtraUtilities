/* Copyright (C) 2026 GrizzlyOne95
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

// exu.TargetChanged(object, oldTarget, newTarget): BZCC's
// SetPostTargetChangedCallback for Redux. GameObject::SetTarget (catalog
// GameObject.SetTarget) is the general target setter and only stores
// target ? GetHandle(target) : 0 at GameObject+0x21C. The entry detour reads
// that field before and after the original and calls Lua only when the stored
// handle actually changed, so the hot AI retarget path costs two loads and a
// compare when nothing changes.

#include "TargetChangedCallback.h"

#include "bzr.h"
#include "EntryDetour32.h"
#include "LuaHelpers.h"
#include "LuaState.h"
#include "LuaCppBarrier.h"
#include "Util/EngineAddresses.generated.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>

#include <lua.hpp>

namespace ExtraUtilities::Patch::TargetChanged
{
	namespace
	{
		using SetTargetFn = void(__thiscall*)(BZR::GameObject* self, BZR::GameObject* target);

		// GameObject.targetHandleOffset in exu.json.
		constexpr uintptr_t kTargetHandleOffset = 0x21C;

		// Thread that owns the mission Lua state; 0 until one attaches.
		std::atomic<DWORD> g_luaThread{ 0 };

		// Callback nesting on the Lua thread. A handler that retargets (directly
		// or through anything that reaches SetTarget) must not re-enter itself.
		int g_callbackDepth = 0;

		struct TargetChangedArgs
		{
			BZR::handle object;
			BZR::handle oldTarget;
			BZR::handle newTarget;
		};

		void PushHandleOrNil(lua_State* L, BZR::handle h)
		{
			if (h == 0)
			{
				lua_pushnil(L);
			}
			else
			{
				lua_pushlightuserdata(L, reinterpret_cast<void*>(h));
			}
		}

		// Runs under lua_cpcall; see BulletHitCallback.cpp. The handler lookup
		// happens here, only after a real change, so a mission without
		// exu.TargetChanged never pays for a barrier on unchanged calls.
		int ProtectedTargetChanged(lua_State* L)
		{
			const auto* args = static_cast<const TargetChangedArgs*>(lua_touserdata(L, 1));
			lua_settop(L, 0);

			lua_getglobal(L, "exu");
			if (!lua_istable(L, -1))
			{
				return 0;
			}
			lua_getfield(L, -1, "TargetChanged");
			if (!lua_isfunction(L, -1))
			{
				return 0;
			}

			lua_pushlightuserdata(L, reinterpret_cast<void*>(args->object));
			PushHandleOrNil(L, args->oldTarget);
			PushHandleOrNil(L, args->newTarget);
			lua_call(L, 3, 0);
			return 0;
		}

		uint32_t ReadTargetHandle(const BZR::GameObject* self) noexcept
		{
			return *reinterpret_cast<const uint32_t*>(
				reinterpret_cast<const uint8_t*>(self) + kTargetHandleOffset);
		}

		void NotifyTargetChanged(BZR::GameObject* self, uint32_t oldTarget, uint32_t newTarget)
		{
			lua_State* L = Lua::state;
			if (L == nullptr || g_callbackDepth > 0 || !IsLuaThread())
			{
				return;
			}

			const BZR::handle object = BZR::GameObject::GetHandle(self);
			if (object == 0)
			{
				// Not a live arena object (being built or torn down); Lua could
				// not address it anyway.
				return;
			}

			// Raw lookup first: no closure and no allocation, so AI retargets
			// cost almost nothing when no mission defines the handler.
			if (!HasLuaHandler(L, "TargetChanged"))
			{
				return;
			}

			++g_callbackDepth;
			try
			{
				StackGuard guard(L);
				TargetChangedArgs args{ object, oldTarget, newTarget };
				const int status = lua_cpcall(L, &Lua::CppBarrier<&ProtectedTargetChanged>, &args);
				if (status != 0)
				{
					LuaCheckStatus(status, L, "Extra Utilities TargetChanged error:\n%s");
				}
			}
			catch (...)
			{
			}
			--g_callbackDepth;
		}

		void __fastcall SetTargetHook(BZR::GameObject* self, void* edx, BZR::GameObject* target);
	}

	// Always on, like the other exu.* callback hooks: activation follows the
	// shared deferred-patch gate and is undone at Lua-state close.
	EntryDetour32 setTargetDetour(
		EngineAddresses::GameObject::SetTarget,
		reinterpret_cast<const void*>(&SetTargetHook),
		6,
		BasicPatch::Status::ACTIVE,
		{ 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08 });

	namespace
	{
		void __fastcall SetTargetHook(BZR::GameObject* self, void*, BZR::GameObject* target)
		{
			const auto original = reinterpret_cast<SetTargetFn>(setTargetDetour.GetTrampoline());
			const uint32_t oldTarget = ReadTargetHandle(self);
			original(self, target);
			const uint32_t newTarget = ReadTargetHandle(self);
			if (oldTarget != newTarget)
			{
				NotifyTargetChanged(self, oldTarget, newTarget);
			}
		}
	}

	bool HasLuaHandler(lua_State* L, const char* field) noexcept
	{
		// Raw gets only: never runs a metamethod, never raises a Lua error.
		lua_pushstring(L, "exu");
		lua_rawget(L, LUA_GLOBALSINDEX);
		bool found = false;
		if (lua_istable(L, -1))
		{
			lua_pushstring(L, field);
			lua_rawget(L, -2);
			found = lua_isfunction(L, -1);
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		return found;
	}

	bool IsLuaThread() noexcept
	{
		return GetCurrentThreadId() == g_luaThread.load(std::memory_order_relaxed);
	}

	void HandleLuaStateAttached() noexcept
	{
		g_luaThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
		g_callbackDepth = 0;
	}
}
