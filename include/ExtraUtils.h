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

/*
 * ExtraUtils.h — Public C++ consumer API for Extra Utilities (EXU) G1.
 *
 * USAGE (C++ DLL consumer):
 *   1. Add the EXU repo's `include/` directory to your project's include paths.
 *   2. Link against `Release/exu.lib` (import library produced by the EXU build).
 *      Or, delay-load `exu.dll` and resolve symbols via GetProcAddress at runtime.
 *   3. Include this header. `EXTRAUTILITIES_EXPORTS` must NOT be defined in consumer projects.
 *
 * QUICK EXAMPLE (mission DLL that needs the Lua state after EXU is loaded):
 *
 *   #include "ExtraUtils.h"
 *
 *   void OnInit()
 *   {
 *       lua_State* L = EXU_GetLuaState();
 *       if (!L) return; // EXU not yet initialized from Lua
 *       lua_getglobal(L, "exu");
 *       // ... call EXU API through Lua C API
 *   }
 *
 * ABI NOTE:
 *   EXU is compiled for 32-bit x86 (matching the BZR process). Consumer DLLs must
 *   also target x86 Win32. Calling convention for all EXU_* functions is __cdecl
 *   via the C linkage block below.
 *
 * VERSION CHECKING:
 *   Compare EXU_VERSION_EXPECTED (compile-time string) against the return value of
 *   EXU_GetVersion() (runtime string) to detect DLL/header mismatches. A mismatch
 *   doesn't guarantee incompatibility, but ABI changes between minor versions are
 *   rare — treat major version changes as breaking.
 */

#pragma once

// Forward declaration — avoids pulling in lua headers from the consumer side.
struct lua_State;

// ---------------------------------------------------------------------------
// DLL import/export
// ---------------------------------------------------------------------------
// EXTRAUTILITIES_EXPORTS is defined only when building exu.dll itself
// (set in the project preprocessor for both Debug and Release).
// Consumer projects that include this header must NOT define it.
#ifdef EXTRAUTILITIES_EXPORTS
#  define EXU_API __declspec(dllexport)
#else
#  define EXU_API __declspec(dllimport)
#endif

// ---------------------------------------------------------------------------
// Compile-time version constant
// Match against EXU_GetVersion() at runtime to detect stale DLL deployments.
// ---------------------------------------------------------------------------
#define EXU_VERSION_EXPECTED "1.1.0"

// ---------------------------------------------------------------------------
// C linkage exports
// ---------------------------------------------------------------------------
extern "C"
{
    // Standard Lua module entry point.
    // BZR's Lua runtime calls this automatically when a mission does:
    //   local exu = require("exu")
    // C++ consumers that want to initialize EXU without going through Lua
    // can call this directly after obtaining a lua_State* from the game.
    EXU_API int luaopen_exu(lua_State* L);

    // Returns the runtime EXU version string (e.g. "1.1.0").
    // Compare against EXU_VERSION_EXPECTED to detect DLL/header mismatches.
    EXU_API const char* EXU_GetVersion();

    // Returns the lua_State* that was registered with EXU during luaopen_exu,
    // or nullptr if EXU has not yet been initialized from Lua.
    //
    // This is useful for C++ DLL mission authors that need to call back into
    // the Lua runtime without holding their own state pointer. The returned
    // pointer remains valid for the lifetime of the current mission.
    EXU_API lua_State* EXU_GetLuaState();
}
