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

#include "Radar.h"

#include <cmath>

namespace ExtraUtilities::Lua::Radar
{
	int GetState(lua_State* L)
	{
		lua_pushnumber(L, state.Read());
		return 1;
	}

	int SetState(lua_State* L)
	{
		uint8_t newState = static_cast<uint8_t>(luaL_checkinteger(L, 1));
		if (newState != 0 && newState != 1)
		{
			luaL_error(L, "Invalid input: options are: 0, 1");
		}
		state.Write(newState);
		return 0;
	}

	int GetSizeScale(lua_State* L)
	{
		lua_pushnumber(L, sizeScale.Read());
		return 1;
	}

	int SetSizeScale(lua_State* L)
	{
		float newScale = static_cast<float>(luaL_checknumber(L, 1));
		if (newScale <= 0.f)
		{
			luaL_error(L, "Invalid input: radar size scale must be greater than 0");
		}

		sizeScale.Write(newScale);

		BZR::BZR_Camera* cam = BZR::Camera::View_Record_MainCam;
		if (cam != nullptr && cam->Orig_y > 0.f)
		{
			int screenHeight = static_cast<int>(std::floor(cam->Orig_y)) * 2;
			BZR::Radar::RefreshLayout(screenHeight);
		}

		return 0;
	}
}
