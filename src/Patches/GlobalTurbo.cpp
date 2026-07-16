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

#include "Game/Culling.h"
#include "GlobalTurbo.h"
#include "BZR.h"
#include "Hook.h"
#include "InlinePatch.h"
#include "LuaHelpers.h"
#include "OpenShimBridge.h"

namespace
{
	using OpenShimGetGlobalTurboFn = BOOL(WINAPI*)();
	using OpenShimSetGlobalTurboFn = BOOL(WINAPI*)(BOOL);
	using OpenShimGetUnitTurboFn = BOOL(WINAPI*)(DWORD);
	using OpenShimSetUnitTurboFn = BOOL(WINAPI*)(DWORD, BOOL);

	bool QueryOpenShimUnitTurboOwnership()
	{
		// Export presence is the ownership contract. Do not activate EXU's fallback
		// hooks merely because the shim is still waiting for Steam's runtime bytes
		// to settle; doing so would create a late-install race over the same sites.
		return ExtraUtilities::OpenShimBridge::HasExport("OpenShimHasUnitTurboHooks");
	}

	const bool g_openShimOwnsUnitTurbo = QueryOpenShimUnitTurboOwnership();
}

namespace ExtraUtilities::Patch
{
	InlinePatch turboPatch1(comissPatch, &patchedTurboTolerance, InlinePatch::Status::INACTIVE);
	InlinePatch turboPatch2(turboConditionPatch, BasicPatch::NOP, 2, InlinePatch::Status::INACTIVE);

	enum class TurboCode
	{
		BEGIN = 0,
		END = 1
	};

	static void __cdecl DoSelectiveTurboPatch(BZR::GameObject* obj, TurboCode code)
	{
		if (code == TurboCode::BEGIN)
		{
			Culling::UpdateUnit(obj);
		}

		BZR::handle h = BZR::GameObject::GetHandle(obj);
		switch (code)
		{
		case TurboCode::BEGIN:
			if (setTurboUnits.contains(h))
			{
				turboPatch1.SetStatus(setTurboUnits.at(h));
				turboPatch2.SetStatus(setTurboUnits.at(h));
			}
			break;
		case TurboCode::END:
			if (setTurboUnits.contains(h))
			{
				turboPatch1.SetStatus(globalTurboEnabled);
				turboPatch2.SetStatus(globalTurboEnabled);
			}
			break;
		}
	}

	static void __declspec(naked) TurboPatchBegin()
	{
		__asm
		{
			// Notes:
			// ecx has the unit task, ecx+0x10 is the "me" gameobject*

			pushad
			pushfd

			push 0x0 // Code for begin
			mov eax, [eax+0x10]
			push eax
			call DoSelectiveTurboPatch
			add esp, 0x08

			popfd
			popad

			// Game code
			mov eax, [ebp-0x70]
			fstp [eax+0x08]

			ret
		}
	}
	Hook turboPatchBegin(
		turboPatchBeginAddr,
		&TurboPatchBegin,
		6,
		g_openShimOwnsUnitTurbo ? InlinePatch::Status::INACTIVE : InlinePatch::Status::ACTIVE);

	static void __declspec(naked) TurboPatchEnd()
	{
		__asm
		{
			pushad
			pushfd

			push 0x1 // Code for end
			mov eax, [edx+0x10]
			push eax
			call DoSelectiveTurboPatch
			add esp, 0x08

			popfd
			popad

			// Game code
			mov edx, [ebp-0x70]
			mov eax, [ebp-0x88]

			ret
		}
	}
	Hook turboPatchEnd(
		turboPatchEndAddr,
		&TurboPatchEnd,
		9,
		g_openShimOwnsUnitTurbo ? InlinePatch::Status::INACTIVE : InlinePatch::Status::ACTIVE);
}

namespace ExtraUtilities::Lua::Patches
{
	int GetGlobalTurbo(lua_State* L)
	{
		if (const auto fn = OpenShimBridge::Resolve<OpenShimGetGlobalTurboFn>(
				"OpenShimGetGlobalTurbo"))
		{
			lua_pushboolean(L, fn() != FALSE);
			return 1;
		}

		if (Patch::turboPatch1.IsActive() && Patch::turboPatch2.IsActive())
		{
			lua_pushboolean(L, true);
		}
		else
		{
			lua_pushboolean(L, false);
		}

		return 1;
	}

	int SetGlobalTurbo(lua_State* L)
	{
		bool status = CheckBool(L, 1);
		Patch::globalTurboEnabled = status;
		if (const auto fn = OpenShimBridge::Resolve<OpenShimSetGlobalTurboFn>(
				"OpenShimSetGlobalTurbo"))
		{
			fn(status ? TRUE : FALSE);
			return 0;
		}

		if (status == true)
		{
			Patch::turboPatch1.Reload();
			Patch::turboPatch2.Reload();
		}
		else
		{
			Patch::turboPatch1.Unload();
			Patch::turboPatch2.Unload();
		}

		return 0;
	}

	int GetUnitTurbo(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);

		bool result;
		if (g_openShimOwnsUnitTurbo)
		{
			const auto fn = OpenShimBridge::Resolve<OpenShimGetUnitTurboFn>(
				"OpenShimGetUnitTurbo");
			result = fn && fn(static_cast<DWORD>(h)) != FALSE;
		}
		else if (Patch::setTurboUnits.contains(h))
		{
			result = Patch::setTurboUnits.at(h);
		}
		else
		{
			result = false;
		}

		lua_pushboolean(L, result);

		return 1;
	}

	int SetUnitTurbo(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		bool status = CheckBool(L, 2);

		if (g_openShimOwnsUnitTurbo)
		{
			if (const auto fn = OpenShimBridge::Resolve<OpenShimSetUnitTurboFn>(
					"OpenShimSetUnitTurbo"))
			{
				fn(static_cast<DWORD>(h), status ? TRUE : FALSE);
			}
		}
		else
		{
			Patch::setTurboUnits[h] = status;
		}

		return 0;
	}
}
