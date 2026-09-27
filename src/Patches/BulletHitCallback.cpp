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


#include "bzr.h"
#include "Hook.h"
#include "LuaHelpers.h"
#include "LuaState.h"

#include <lua.hpp>

namespace ExtraUtilities::Patch
{
	namespace
	{
		struct BulletHitArgs
		{
			const char* odf;
			BZR::GameObject* shooter;
			BZR::GameObject* hitObject;
			BZR::MAT_3D* transform;
			BZR::Ordnance* ordnance;
		};

		// Runs under lua_cpcall. Everything that can raise -- indexing exu,
		// the SetMatrix call inside PushMatrix, the callback itself -- happens
		// here, so an error is reported instead of panicking out of engine code.
		int ProtectedBulletHit(lua_State* L)
		{
			const auto* args = static_cast<const BulletHitArgs*>(lua_touserdata(L, 1));
			lua_settop(L, 0);

			lua_getglobal(L, "exu");
			if (!lua_istable(L, -1))
			{
				return 0;
			}
			lua_getfield(L, -1, "BulletHit");
			if (!lua_isfunction(L, -1))
			{
				return 0;
			}

			// odf is the engine's char[16] ODF name. Strip a trailing ".odf" only
			// when it is present; never underflow on a short name.
			size_t odfLength = strnlen(args->odf, 16);
			if (odfLength >= 4 && _strnicmp(args->odf + odfLength - 4, ".odf", 4) == 0)
			{
				odfLength -= 4;
			}
			lua_pushlstring(L, args->odf, odfLength);

			if (args->shooter == nullptr)
			{
				lua_pushnil(L);
			}
			else
			{
				lua_pushlightuserdata(L, reinterpret_cast<void*>(BZR::GameObject::GetHandle(args->shooter)));
			}

			if (args->hitObject == nullptr)
			{
				lua_pushnil(L);
			}
			else
			{
				lua_pushlightuserdata(L, reinterpret_cast<void*>(BZR::GameObject::GetHandle(args->hitObject)));
			}

			if (args->transform == nullptr)
			{
				lua_pushnil(L);
			}
			else
			{
				Lua::PushMatrix(L, *args->transform);
			}

			if (args->ordnance == nullptr)
			{
				lua_pushnil(L);
			}
			else
			{
				lua_pushlightuserdata(L, reinterpret_cast<void*>(args->ordnance));
			}

			lua_call(L, 5, 0);
			return 0;
		}
	}

	static void __cdecl LuaCallback(const char* odf,
								    BZR::GameObject* shooter, 
									BZR::GameObject* hitObject,
									BZR::MAT_3D* transform,
									BZR::Ordnance* ordnanceHandle)
	{
		lua_State* L = Lua::state;
		if (L == nullptr || odf == nullptr)
		{
			return;
		}

		StackGuard guard(L);
		BulletHitArgs args{ odf, shooter, hitObject, transform, ordnanceHandle };
		const int status = lua_cpcall(L, &ProtectedBulletHit, &args);
		if (status != 0)
		{
			LuaCheckStatus(status, L, "Extra Utilities BulletHit error:\n%s");
		}
	}

	static void __declspec(naked) BulletHitCallback()
	{
		__asm
		{
			// Game code
			mov ecx, [eax + 0x14]
			add ecx, 0x38

			pushad
			pushfd

			// The function calls in this patch appear to modify
			// xmm0, xmm2, and xmm3, we need to save them
			sub esp, 0x10
			movdqu [esp], xmm0

			sub esp, 0x10
			movdqu [esp], xmm2

			sub esp, 0x10
			movdqu [esp], xmm3


			// eax has the this pointer (bullet/ordnance inheritance bs)
			push eax // fifth param ordnanceHandle

			lea ebx, [eax] // I stored it here don't remember why

			lea eax, [ecx-0x18] // matrix
			push eax // fourth param transform

			mov eax, [ebp+0x08] // gameobject* of hit object if it exists
			push eax // third param hitObject

			// now we're gonna do some voodoo to get the shooter handle
			mov eax, [ebx+0xD8] // obj76 of the ordnance owner

			// this *shouldn't* be null but apparently BL encountered this,
			// needs more testing with modded weapons
			cmp eax, 0x0
			je skip

			mov eax, [eax+0x8C] // gameobject* of the obj76

			skip:

			push eax // second param shooter

			mov eax, [ebx+0x0C] // OrdnanceClass* - Scanner by -0x04 from 1.5, is 0x10 in 1.5
			lea ebx, [eax+0x20] // odf char*
			push ebx // first param odf

			call LuaCallback
			add esp, 0x14 // five params

			movdqu xmm3, [esp]
			add esp, 0x10

			movdqu xmm2, [esp]
			add esp, 0x10

			movdqu xmm0, [esp]
			add esp, 0x10

			popfd
			popad

			ret
		}
	}
	Hook bulletHitHook(0x00480771, &BulletHitCallback, 6, Hook::Status::ACTIVE, { 0x8B, 0x48, 0x14, 0x83, 0xC1, 0x38 });
}