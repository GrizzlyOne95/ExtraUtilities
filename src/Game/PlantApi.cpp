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

// exu.SetPlant / exu.IsPlant Lua bindings. The feature lives in Plant.cpp.

#include "Game/Plant.h"

#include "LuaHelpers.h"

#include <lua.hpp>

#include <string_view>

namespace ExtraUtilities::Plant
{
	namespace
	{
		// The Lua core is C: luaL_check*/luaL_argerror longjmp, so nothing here
		// holds a C++ object with a destructor while they can run. The key is
		// a POD buffer.
		struct Key
		{
			char text[PlantCore::kOdfNameMax + 1];
		};

		void CheckOdfName(lua_State* L, int idx, Key& key)
		{
			std::size_t length = 0;
			const char* text = luaL_checklstring(L, idx, &length);
			if (PlantCore::NormalizeOdfNameInto(std::string_view(text, length), key.text) == 0)
				luaL_argerror(L, idx, "expected an ODF name");
		}
	}

	// exu.SetPlant(odf, enabled): true forces plant (ODF keys still apply),
	// false forces not-a-plant, nil removes the override.
	int SetPlant(lua_State* L)
	{
		Key key;
		CheckOdfName(L, 1, key);
		if (lua_isnoneornil(L, 2))
		{
			SetOverride(key.text, -1);
			return 0;
		}
		SetOverride(key.text, Lua::CheckBool(L, 2) ? 1 : 0);
		return 0;
	}

	// exu.IsPlant(odf) -> boolean (override first, then the ODF)
	int IsPlant(lua_State* L)
	{
		Key key;
		CheckOdfName(L, 1, key);
		lua_pushboolean(L, QueryPlant(key.text) ? 1 : 0);
		return 1;
	}
}
