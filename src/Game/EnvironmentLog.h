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

#include <lua.hpp>

#include <string>

// Environment logging: per-call tracing (EXU_DEBUG_LOG=1) to
// exu_environment_debug.log, and fault lines that always reach exu.log.

namespace ExtraUtilities::Lua::Environment
{
	namespace Detail
	{
		void LogEnvironmentDebug(const char* fmt, ...);
		void LogEnvironmentFault(const char* fmt, ...);
	}

	using namespace Detail;
	std::string DescribeLuaCaller(lua_State* L);
}
