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

#pragma once

// The C++/Lua boundary.
//
// EXU's Lua core is C: lua_error is a longjmp and knows nothing about C++
// exceptions. A C++ exception that leaves a lua_CFunction unwinds straight
// through the interpreter's frames without restoring its call depth, call-info
// chain or protected-call jump buffer, and the state is corrupt from then on.
//
// Every function EXU hands to Lua therefore runs behind CallWithCppBarrier: a
// C++ exception is caught, logged and destroyed, and only then turned into an
// ordinary Lua error, raised from a frame that holds no C++ objects. Bindings
// still must not keep C++ objects with destructors alive across a call that can
// raise a Lua error (luaL_check*, luaL_error, lua_call, ...): the longjmp skips
// their destructors.
//
// Tables go through RegisterFunctions, which is luaL_register with each entry
// wrapped in a closure; a function pushed on its own uses CppBarrier<Fn>, which
// keeps the function's own upvalues.

#include "Util/SehGuard.h"

#include <lua.hpp>

#include <cstdio>

namespace ExtraUtilities::Lua
{
	namespace Detail
	{
		// Runs fn. On a C++ exception, logs it, writes the Lua error text into
		// message and returns false; the exception is destroyed before return.
		inline bool CallCatchingCpp(lua_State* L, lua_CFunction fn, int& outResults, char* message, size_t messageSize)
		{
			try
			{
				outResults = fn(L);
				return true;
			}
			catch (...)
			{
				char description[400];
				Seh::DescribeCurrentException(description, sizeof(description));
				std::snprintf(message, messageSize, "Extra Utilities internal error (C++ exception): %s", description);
				Seh::LogCurrentCppException("Lua binding");
			}

			return false;
		}

		// No try block and no C++ objects: the Lua error raised here only has
		// POD locals to skip.
		inline int CallWithCppBarrier(lua_State* L, lua_CFunction fn)
		{
			char message[512];
			int results = 0;
			if (CallCatchingCpp(L, fn, results, message, sizeof(message)))
			{
				return results;
			}

			return luaL_error(L, "%s", message);
		}

		// The closure RegisterFunctions pushes: upvalue 1 is the real function.
		inline int CppBarrierTrampoline(lua_State* L)
		{
			const auto fn = reinterpret_cast<lua_CFunction>(lua_touserdata(L, lua_upvalueindex(1)));
			if (fn == nullptr)
			{
				return luaL_error(L, "Extra Utilities internal error: missing binding");
			}

			return CallWithCppBarrier(L, fn);
		}
	}

	// A barrier for one function pushed with lua_pushcfunction/lua_pushcclosure.
	template <lua_CFunction Fn>
	int CppBarrier(lua_State* L)
	{
		return Detail::CallWithCppBarrier(L, Fn);
	}

	// luaL_register (same table lookup and creation for libname, or the table
	// on top of the stack for nullptr), with every function behind the barrier.
	inline void RegisterFunctions(lua_State* L, const char* libname, const luaL_Reg* functions)
	{
		static const luaL_Reg none[] = { { nullptr, nullptr } };
		luaL_register(L, libname, none);
		for (; functions->name != nullptr; ++functions)
		{
			lua_pushlightuserdata(L, reinterpret_cast<void*>(functions->func));
			lua_pushcclosure(L, &Detail::CppBarrierTrampoline, 1);
			lua_setfield(L, -2, functions->name);
		}
	}
}
