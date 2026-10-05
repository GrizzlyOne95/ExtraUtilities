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

// exu.pathing Lua bindings. The feature lives in PathBlock.cpp.

#include "Game/PathBlock.h"

#include "LuaCppBarrier.h"
#include "LuaHelpers.h"
#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "bzr.h"

#include <lua.hpp>

#include <string>

namespace ExtraUtilities::PathBlock
{
	namespace
	{
		// exu.pathing.Refresh() -> number of flagged objects re-applied, or nil
		// when the grid or the build is unavailable.
		int Refresh(lua_State* L)
		{
			if (!RuntimeGate::IsSupported())
				return Lua::PushUnsupportedBuild(L);
			const int applied = PathBlock::Refresh();
			if (applied < 0)
			{
				lua_pushnil(L);
				return 1;
			}
			lua_pushinteger(L, applied);
			return 1;
		}

		// exu.pathing.SetEnabled(bool) -> objects re-applied (as Refresh).
		int SetEnabled(lua_State* L)
		{
			const bool enabled = Lua::CheckBool(L, 1);
			if (!RuntimeGate::IsSupported())
				return Lua::PushUnsupportedBuild(L);
			const int applied = PathBlock::SetEnabled(enabled);
			if (applied < 0)
			{
				lua_pushnil(L);
				return 1;
			}
			lua_pushinteger(L, applied);
			return 1;
		}

		int IsEnabled(lua_State* L)
		{
			lua_pushboolean(L, PathBlock::IsEnabled() ? 1 : 0);
			return 1;
		}

		// exu.pathing.GetMode(handle) -> "box" | "faces" | "none", details
		// details = { odf, configured, effective, cellsInBox, cellsBlocked,
		//             triangles, inGrid }; nil for a dead handle.
		int GetMode(lua_State* L)
		{
			const BZR::handle h = Lua::CheckHandle(L, 1);
			if (!RuntimeGate::IsSupported())
				return Lua::PushUnsupportedBuild(L);
			BZR::GameObject* object = BZR::GameObject::GetObj(h);
			ObjectReport report;
			if (object == nullptr || !PathBlock::GetObjectReport(object, report))
			{
				lua_pushnil(L);
				return 1;
			}
			lua_pushstring(L, report.effective);
			lua_createtable(L, 0, 7);
			lua_pushstring(L, report.odf);
			lua_setfield(L, -2, "odf");
			lua_pushstring(L, report.configured);
			lua_setfield(L, -2, "configured");
			lua_pushstring(L, report.effective);
			lua_setfield(L, -2, "effective");
			lua_pushinteger(L, report.cellsInBox);
			lua_setfield(L, -2, "cellsInBox");
			lua_pushinteger(L, report.cellsBlocked);
			lua_setfield(L, -2, "cellsBlocked");
			lua_pushinteger(L, report.triangles);
			lua_setfield(L, -2, "triangles");
			lua_pushboolean(L, report.inGrid ? 1 : 0);
			lua_setfield(L, -2, "inGrid");
			return 2;
		}

		// exu.pathing.DumpGrid(pos | handle, radius = 40) -> ASCII string.
		int DumpGrid(lua_State* L)
		{
			float x = 0.0f;
			float z = 0.0f;
			if (lua_islightuserdata(L, 1))
			{
				if (!RuntimeGate::IsSupported())
					return Lua::PushUnsupportedBuild(L);
				BZR::GameObject* object = BZR::GameObject::GetObj(Lua::CheckHandle(L, 1));
				if (object == nullptr)
				{
					lua_pushnil(L);
					return 1;
				}
				x = object->pos.x;
				z = object->pos.z;
			}
			else
			{
				const BZR::VECTOR_3D pos = Lua::CheckVectorOrSingles(L, 1);
				x = pos.x;
				z = pos.z;
			}
			const int radiusIndex = (lua_isnumber(L, 1) != 0) ? 4 : 2;
			const float radius = static_cast<float>(luaL_optnumber(L, radiusIndex, 40.0));
			if (!RuntimeGate::IsSupported())
				return Lua::PushUnsupportedBuild(L);
			const std::string text = PathBlock::DumpGrid(x, z, radius);
			if (text.empty())
			{
				lua_pushnil(L);
				return 1;
			}
			lua_pushlstring(L, text.data(), text.size());
			return 1;
		}

		// exu.pathing.GetCapabilities() -> { pathBlock = true, ... }
		int GetCapabilities(lua_State* L)
		{
			Capabilities caps;
			PathBlock::GetCapabilities(caps);
			lua_createtable(L, 0, 7);
			lua_pushboolean(L, caps.available && caps.hookedSites > 0 ? 1 : 0);
			lua_setfield(L, -2, "pathBlock");
			lua_pushboolean(L, caps.available ? 1 : 0);
			lua_setfield(L, -2, "available");
			lua_pushinteger(L, caps.hookedSites);
			lua_setfield(L, -2, "hookedSites");
			lua_pushinteger(L, caps.hookSites);
			lua_setfield(L, -2, "hookSites");
			lua_pushboolean(L, caps.gridReady ? 1 : 0);
			lua_setfield(L, -2, "gridReady");
			lua_pushnumber(L, static_cast<lua_Number>(caps.cellSize));
			lua_setfield(L, -2, "cellSize");
			lua_pushboolean(L, caps.enabled ? 1 : 0);
			lua_setfield(L, -2, "enabled");
			return 1;
		}
	}

	void Install(lua_State* L)
	{
		if (L == nullptr)
			return;
		const int top = lua_gettop(L);
		lua_getglobal(L, "exu");
		if (!lua_istable(L, -1))
		{
			lua_settop(L, top);
			Logging::LogMessage("exu: pathing API install skipped; exu table is unavailable");
			return;
		}
		lua_newtable(L);
		static const luaL_Reg functions[] = {
			{ "Refresh", &Refresh },
			{ "SetEnabled", &SetEnabled },
			{ "IsEnabled", &IsEnabled },
			{ "GetMode", &GetMode },
			{ "DumpGrid", &DumpGrid },
			{ "GetCapabilities", &GetCapabilities },
			{ nullptr, nullptr },
		};
		Lua::RegisterFunctions(L, nullptr, functions);
		lua_setfield(L, -2, "pathing");
		lua_settop(L, top);
	}
}
