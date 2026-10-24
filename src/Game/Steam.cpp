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

#include "Steam.h"
#include "Game/SteamIdentity.h"

#include <string>
#include "LuaHelpers.h"
#include "Util/FeatureQualification.h"
#include "Util/SehGuard.h"

namespace ExtraUtilities::Lua::Steam
{
	int GetSteam64(lua_State* L)
	{
		std::string formattedSteam64 = std::to_string(steam64.Read());
		lua_pushstring(L, formattedSteam64.c_str());
		return 1;
	}
}
namespace ExtraUtilities::Lua::Steam
{
    namespace
    {
        bool ReadPlayerSteam64(unsigned short id, uint64_t& out) noexcept
        {
            using FindFn = void*(__cdecl*)(unsigned short);
            __try
            {
                if (!*BZR::Multiplayer::isNetGame) return false;
                const auto* player = static_cast<const unsigned char*>(reinterpret_cast<FindFn>(EngineAddresses::Multiplayer::FindNetPlayer)(id));
                if (!player || *reinterpret_cast<const unsigned short*>(player + 0x28) != id) return false;
                // The released NetPlayer constructor copies its UserId to +0x30.
                // UserId's Steam constructor writes tag 1 and the 64-bit ID at +8;
                // the Steam converter rejects all other tags (including GOG).
                if (*reinterpret_cast<const uint32_t*>(player + 0x30) != 1) return false;
                const uint64_t value = *reinterpret_cast<const uint64_t*>(player + 0x38);
                return SteamIdentity::Decode(1, value, out);
            }
            __except (Seh::Filter(GetExceptionCode())) { return false; }
        }
    }

    bool TryGetPlayerSteam64(unsigned short id, uint64_t& out) noexcept
    {
        out = 0;
        if (!RuntimeGate::IsSupported()) return false;
        static bool qualified = false;
        if (!qualified) qualified = QualifyFeature("Player identity ", 4);
        return qualified && ReadPlayerSteam64(id, out);
    }

    int GetPlayerSteam64(lua_State* L)
    {
        const int id = luaL_checkint(L, 1);
        if (id < 0 || id > 65535) return luaL_argerror(L, 1, "network player id must be in 0..65535");
        uint64_t value = 0;
        if (!TryGetPlayerSteam64(static_cast<unsigned short>(id), value)) return PushUnsupportedBuild(L);
        const auto text = std::to_string(value);
        lua_pushlstring(L, text.data(), text.size());
        return 1;
    }
}
