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

namespace ExtraUtilities::Patch
{
	namespace
	{
		struct BulletInitArgs
		{
			const char* odf;
			BZR::GameObject* shooter;
			BZR::MAT_3D* transform;
			BZR::Ordnance* ordnance;
		};

		// Runs under lua_cpcall; see BulletHitCallback.cpp.
		int ProtectedBulletInit(lua_State* L)
		{
			const auto* args = static_cast<const BulletInitArgs*>(lua_touserdata(L, 1));
			lua_settop(L, 0);

			lua_getglobal(L, "exu");
			if (!lua_istable(L, -1))
			{
				return 0;
			}
			lua_getfield(L, -1, "BulletInit");
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

			lua_call(L, 4, 0);
			return 0;
		}
	}

	static void __cdecl LuaCallback(const char* odf,
									BZR::GameObject* shooter,
									BZR::MAT_3D* transform,
									BZR::Ordnance* ordnanceHandle)
	{
		lua_State* L = Lua::state;
		if (L == nullptr || odf == nullptr)
		{
			return;
		}

		StackGuard guard(L);
		BulletInitArgs args{ odf, shooter, transform, ordnanceHandle };
		const int status = lua_cpcall(L, &ProtectedBulletInit, &args);
		if (status != 0)
		{
			LuaCheckStatus(status, L, "Extra Utilities BulletInit error:\n%s");
		}
	}

	static void __declspec(naked) BulletInitCallback()
	{
		__asm
		{
			 pushad
			 pushfd

			 mov ecx, [ebp-0x20] // bullet* this
			 push ecx // fourth param ordnanceHandle
			 mov eax, [ecx+0xD8] // obj76

			 mov ebx, [ebp+0x08] // MAT_3D transform
			 push ebx // third param

			 mov ebx, 0x0 // load nullptr in case there's no object

			 cmp eax, 0x0 // in rare cases modded objects or maybe explosions can cause this to be null
			 je skip

			 mov ebx, [eax+0x8C] // GameObject* shooter

			 skip:

			 push ebx // second param

			 mov ebx, [ecx+0xC] // OrdnanceClass*
			 lea ebx, [ebx+0x20] // odf char*
			 push ebx // first param

			 call LuaCallback
			 add esp, 0x10 // four params

			 popfd
			 popad

			// game code
			mov edx, [ebp-0x20]
			mov eax, [edx+0x14]

			ret
		}
	}
	Hook bulletInitCallback(0x00480363, &BulletInitCallback, 6, BasicPatch::Status::ACTIVE, { 0x8B, 0x55, 0xE0, 0x8B, 0x42, 0x14 });
}