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

#include "EnvironmentLog.h"

#include "Util/Logging.h"

#include <Windows.h>

#include <cstdarg>
#include <cstdio>

namespace ExtraUtilities::Lua::Environment
{
	namespace Detail
	{
		// Verbose per-call tracing, opt-in through EXU_DEBUG_LOG=1. The weather
		// controller drives several of these bindings every frame and each
		// line costs a log-file open/close, so it must be off by default.
		void LogEnvironmentDebug(const char* fmt, ...)
		{
			va_list args;
			va_start(args, fmt);
			ExtraUtilities::Logging::LogDebugToV("exu_environment_debug.log", fmt, args);
			va_end(args);
		}

		// Fault paths (SEH handlers) always reach exu.log.
		void LogEnvironmentFault(const char* fmt, ...)
		{
			va_list args;
			va_start(args, fmt);
			ExtraUtilities::Logging::LogFaultToV("exu_environment_debug.log", fmt, args);
			va_end(args);
		}
	}

	std::string DescribeLuaCaller(lua_State* L)
	{
		lua_Debug ar{};
		if (lua_getstack(L, 1, &ar) && lua_getinfo(L, "Sln", &ar))
		{
			char caller[512];
			sprintf_s(
				caller,
				sizeof(caller),
				"%s:%d (%s)",
				ar.short_src[0] ? ar.short_src : "?",
				ar.currentline,
				ar.name ? ar.name : "anonymous");
			return caller;
		}

		return "unknown";
	}
}
