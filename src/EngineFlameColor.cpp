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

#include "EngineFlameColor.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ExtraUtilities::Patch
{
	enum class EngineFlameColor : uint8_t
	{
		DEFAULT,
		BLUE,
		RED,
		GREEN
	};

	static std::unordered_map<int, EngineFlameColor> teamEngineFlameColors;

	static std::string NormalizeColor(std::string_view color)
	{
		std::string normalized(color);
		std::transform(
			normalized.begin(),
			normalized.end(),
			normalized.begin(),
			[](unsigned char value)
			{
				return static_cast<char>(std::tolower(value));
			}
		);
		return normalized;
	}

	static bool TryParseColor(std::string_view color, EngineFlameColor& outColor)
	{
		const std::string normalized = NormalizeColor(color);

		if (normalized == "default")
		{
			outColor = EngineFlameColor::DEFAULT;
			return true;
		}
		if (normalized == "blue")
		{
			outColor = EngineFlameColor::BLUE;
			return true;
		}
		if (normalized == "red")
		{
			outColor = EngineFlameColor::RED;
			return true;
		}
		if (normalized == "green")
		{
			outColor = EngineFlameColor::GREEN;
			return true;
		}

		return false;
	}

	static const char* ToString(EngineFlameColor color)
	{
		switch (color)
		{
		case EngineFlameColor::BLUE:
			return "blue";
		case EngineFlameColor::RED:
			return "red";
		case EngineFlameColor::GREEN:
			return "green";
		case EngineFlameColor::DEFAULT:
		default:
			return "default";
		}
	}

	static EngineFlameColor GetTeamColor(int team)
	{
		if (teamEngineFlameColors.contains(team))
		{
			return teamEngineFlameColors.at(team);
		}

		return EngineFlameColor::DEFAULT;
	}
}

namespace ExtraUtilities::Lua::Patches
{
	int GetTeamEngineFlameColor(lua_State* L)
	{
		const int team = static_cast<int>(luaL_checkinteger(L, 1));
		lua_pushstring(L, Patch::ToString(Patch::GetTeamColor(team)));
		return 1;
	}

	int SetTeamEngineFlameColor(lua_State* L)
	{
		const int team = static_cast<int>(luaL_checkinteger(L, 1));

		size_t length{};
		const char* colorName = luaL_checklstring(L, 2, &length);

		Patch::EngineFlameColor color{};
		if (!Patch::TryParseColor(std::string_view(colorName, length), color))
		{
			return luaL_argerror(L, 2, "Extra Utilities Error: valid engine flame colors are default, blue, red, or green");
		}

		if (color == Patch::EngineFlameColor::DEFAULT)
		{
			Patch::teamEngineFlameColors.erase(team);
		}
		else
		{
			Patch::teamEngineFlameColors[team] = color;
		}

		return 0;
	}

	int ClearTeamEngineFlameColor(lua_State* L)
	{
		const int team = static_cast<int>(luaL_checkinteger(L, 1));
		Patch::teamEngineFlameColors.erase(team);
		return 0;
	}
}

int EXU_GetTeamEngineFlameColor(int team)
{
	return static_cast<int>(ExtraUtilities::Patch::GetTeamColor(team));
}
