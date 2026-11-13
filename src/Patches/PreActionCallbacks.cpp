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

// Veto callbacks, BZCC's SetPreGetInCallback / SetPrePickupPowerupCallback
// without the world-index argument Redux does not have:
//
//   exu.PreGetIn(pilot, vehicle)          -> false blocks the pilot entering
//   exu.PrePickupPowerup(vehicle, powerup) -> false blocks the pickup
//
// Only an explicit boolean false blocks. No handler, any other result, a Lua
// error, no Lua state, a nested event, another thread, or a zero handle all
// take the engine's native path unchanged.
//
// Both hooks sit in a RegCollision after every native check has passed and
// before anything is applied (see Callbacks.PreGetInHook and
// Callbacks.PrePickupPowerupHook in exu.json). Each veto jumps to the label the
// native failed checks already use, at the same stack depth.

#include "bzr.h"
#include "Hook.h"
#include "LuaHelpers.h"
#include "LuaState.h"
#include "LuaCppBarrier.h"
#include "Patches/TargetChangedCallback.h"
#include "Util/EngineAddresses.generated.h"

#include <cstdint>

#include <lua.hpp>

namespace ExtraUtilities::Patch::PreAction
{
	namespace
	{
		// Read by the naked stubs. Constant-initialised: no DllMain work.
		uintptr_t g_isNetGame = EngineAddresses::Multiplayer::IsNetGameFunction;
		uintptr_t g_getInResume = EngineAddresses::Callbacks::PreGetInHook + 7;
		uintptr_t g_getInVeto = EngineAddresses::Callbacks::PreGetInVeto;
		uintptr_t g_pickupResume = EngineAddresses::Callbacks::PrePickupPowerupHook + 7;
		uintptr_t g_pickupVeto = EngineAddresses::Callbacks::PrePickupPowerupVeto;

		// Per-callback nesting on the Lua thread; a nested event allows natively.
		int g_getInDepth = 0;
		int g_pickupDepth = 0;

		struct VetoArgs
		{
			const char* field;
			BZR::handle first;
			BZR::handle second;
			int allow;
		};

		// Runs under lua_cpcall; see BulletHitCallback.cpp. args->allow starts
		// at 1 and is cleared only by an explicit false.
		int ProtectedVeto(lua_State* L)
		{
			auto* args = static_cast<VetoArgs*>(lua_touserdata(L, 1));
			lua_settop(L, 0);

			lua_getglobal(L, "exu");
			if (!lua_istable(L, -1))
			{
				return 0;
			}
			lua_getfield(L, -1, args->field);
			if (!lua_isfunction(L, -1))
			{
				return 0;
			}

			lua_pushlightuserdata(L, reinterpret_cast<void*>(args->first));
			lua_pushlightuserdata(L, reinterpret_cast<void*>(args->second));
			lua_call(L, 2, 1);
			if (lua_isboolean(L, -1) && !lua_toboolean(L, -1))
			{
				args->allow = 0;
			}
			return 0;
		}

		// Returns 1 to allow (native path), 0 to veto.
		int Decide(const char* field, const char* errorFormat, int& depth,
		           BZR::GameObject* first, BZR::GameObject* second) noexcept
		{
			lua_State* L = Lua::state;
			if (L == nullptr || depth > 0 || first == nullptr || second == nullptr ||
				!TargetChanged::IsLuaThread())
			{
				return 1;
			}

			const BZR::handle firstHandle = BZR::GameObject::GetHandle(first);
			const BZR::handle secondHandle = BZR::GameObject::GetHandle(second);
			if (firstHandle == 0 || secondHandle == 0)
			{
				return 1;
			}

			if (!TargetChanged::HasLuaHandler(L, field))
			{
				return 1;
			}

			VetoArgs args{ field, firstHandle, secondHandle, 1 };
			++depth;
			try
			{
				StackGuard guard(L);
				const int status = lua_cpcall(L, &Lua::CppBarrier<&ProtectedVeto>, &args);
				if (status != 0)
				{
					LuaCheckStatus(status, L, errorFormat);
					args.allow = 1;
				}
			}
			catch (...)
			{
				args.allow = 1;
			}
			--depth;
			return args.allow;
		}

		int __cdecl DecideGetIn(BZR::GameObject* pilot, BZR::GameObject* vehicle) noexcept
		{
			return Decide("PreGetIn", "Extra Utilities PreGetIn error:\n%s", g_getInDepth, pilot, vehicle);
		}

		int __cdecl DecidePickup(BZR::GameObject* vehicle, BZR::GameObject* powerup) noexcept
		{
			return Decide("PrePickupPowerup", "Extra Utilities PrePickupPowerup error:\n%s", g_pickupDepth, vehicle, powerup);
		}

		// Entered by the hook's FF 15 call, so [esp] is the return address
		// (site + 6). Saves flags, integer registers and x87/XMM state
		// (fxsave, 16-aligned) around the C++ call, then leaves the decision in
		// eax through the saved-eax slot of the pushad frame.
		//
		// Allow: drop the return address, replay the stolen call IsNetGame
		// (absolute, not the raw rel32) and test eax,eax, resume at site + 7
		// (the je at 0x005A1644). Veto: drop it and jump to PreGetInVeto.
		void __declspec(naked) PreGetInStub()
		{
			__asm
			{
				pushfd
				pushad
				mov eax, esp
				sub esp, 528
				and esp, -16
				mov [esp + 512], eax
				fxsave [esp]

				push dword ptr [ebp - 0x68] // vehicle GameObject*
				push dword ptr [ebp - 0x6C] // pilot Person*
				call DecideGetIn
				add esp, 8
				mov ecx, eax

				fxrstor [esp]
				mov esp, [esp + 512]
				mov [esp + 28], ecx // pushad saved-eax slot
				popad
				popfd

				test eax, eax
				jz veto

				add esp, 4
				call dword ptr [g_isNetGame]
				test eax, eax
				jmp dword ptr [g_getInResume]

			veto:
				add esp, 4
				jmp dword ptr [g_getInVeto]
			}
		}

		// Same frame discipline. Allow: drop the return address, replay
		// mov ecx,[ebp-0x60]; push ecx; mov edx,[ebp-0x5C] and resume at
		// site + 7. Veto: drop it and jump to PrePickupPowerupVeto WITHOUT the
		// push; that label runs at the pre-push stack depth (it is also the
		// target of the jne at 0x005AA1B6 just before the site).
		void __declspec(naked) PrePickupPowerupStub()
		{
			__asm
			{
				pushfd
				pushad
				mov eax, esp
				sub esp, 528
				and esp, -16
				mov [esp + 512], eax
				fxsave [esp]

				push dword ptr [ebp - 0x5C] // powerup this
				push dword ptr [ebp - 0x60] // vehicle GameObject*
				call DecidePickup
				add esp, 8
				mov ecx, eax

				fxrstor [esp]
				mov esp, [esp + 512]
				mov [esp + 28], ecx // pushad saved-eax slot
				popad
				popfd

				test eax, eax
				jz veto

				add esp, 4
				mov ecx, [ebp - 0x60]
				push ecx
				mov edx, [ebp - 0x5C]
				jmp dword ptr [g_pickupResume]

			veto:
				add esp, 4
				jmp dword ptr [g_pickupVeto]
			}
		}
	}

	void HandleLuaStateAttached() noexcept
	{
		g_getInDepth = 0;
		g_pickupDepth = 0;
	}

	// call IsNetGame (E8 rel32) + test eax,eax. The rel32 is part of the
	// preimage, so the absolute replay in the stub is the same target.
	Hook preGetInHook(EngineAddresses::Callbacks::PreGetInHook, &PreGetInStub, 7, Hook::Status::ACTIVE, { 0xE8, 0x2E, 0x14, 0xFD, 0xFF, 0x85, 0xC0 });

	// mov ecx,[ebp-0x60]; push ecx; mov edx,[ebp-0x5C]
	Hook prePickupPowerupHook(EngineAddresses::Callbacks::PrePickupPowerupHook, &PrePickupPowerupStub, 7, Hook::Status::ACTIVE, { 0x8B, 0x4D, 0xA0, 0x51, 0x8B, 0x55, 0xA4 });
}
