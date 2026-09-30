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

#include "SoundOptions.h"
#include "Util/Soundtrack.h"
#include <cmath>

namespace ExtraUtilities::Lua::SoundOptions
{
    namespace
    {
        int TrackArgument(lua_State* L)
        {
            const lua_Number value = luaL_checknumber(L, 1);
            if (!std::isfinite(value) || value < 0 || value > 255 || std::floor(value) != value)
                luaL_argerror(L, 1, "music track must be an integer between 0 and 255");
            return static_cast<int>(value);
        }

        double DurationArgument(lua_State* L, int argument, double fallback)
        {
            const double seconds = luaL_optnumber(L, argument, fallback);
            if (!Soundtrack::ValidDuration(seconds))
                luaL_argerror(L, argument, "duration must be finite and non-negative");
            return seconds;
        }

        int Result(lua_State* L, bool result)
        {
            lua_pushboolean(L, result);
            return 1;
        }
    }

    int GetMusicVolume(lua_State* L)
    {
        lua_pushnumber(L, musicVolume.Read());
        return 1;
    }

    int SetMusicTrack(lua_State* L) { return Result(L, Soundtrack::Play(TrackArgument(L))); }
    int StopMusic(lua_State* L) { return Result(L, Soundtrack::Stop()); }
    int PauseMusic(lua_State* L) { return Result(L, Soundtrack::Pause()); }
    int ResumeMusic(lua_State* L) { return Result(L, Soundtrack::Resume()); }

    int GetMusicTrack(lua_State* L)
    {
        int track = -1;
        if (Soundtrack::GetTrack(track))
            lua_pushinteger(L, track);
        else
            lua_pushnil(L);
        return 1;
    }

    int GetMusicState(lua_State* L)
    {
        Soundtrack::State state;
        if (!Soundtrack::GetState(state))
        {
            lua_pushnil(L);
            return 1;
        }
        lua_createtable(L, 0, 6);
        lua_pushinteger(L, state.track); lua_setfield(L, -2, "track");
        lua_pushboolean(L, state.started && !state.paused); lua_setfield(L, -2, "playing");
        lua_pushboolean(L, state.paused); lua_setfield(L, -2, "paused");
        lua_pushnumber(L, Soundtrack::GetGain()); lua_setfield(L, -2, "gain");
        lua_pushboolean(L, Soundtrack::IsFading()); lua_setfield(L, -2, "fading");
        lua_pushinteger(L, state.userVolume); lua_setfield(L, -2, "userVolume");
        return 1;
    }

    int FadeMusic(lua_State* L)
    {
        const double gain = luaL_checknumber(L, 1);
        if (!std::isfinite(gain) || gain < 0 || gain > 1)
            return luaL_argerror(L, 1, "gain must be finite and between 0 and 1");
        const double seconds = DurationArgument(L, 2, 1);
        return Result(L, Soundtrack::Fade(static_cast<float>(gain), seconds));
    }

    int ChangeMusicTrack(lua_State* L)
    {
        const int track = TrackArgument(L);
        const double fadeOut = DurationArgument(L, 2, 1);
        const double fadeIn = DurationArgument(L, 3, 1);
        return Result(L, Soundtrack::Change(track, fadeOut, fadeIn));
    }

    int UpdateMusic(lua_State* L)
    {
        const double seconds = luaL_checknumber(L, 1);
        if (!Soundtrack::ValidDuration(seconds))
            return luaL_argerror(L, 1, "timestep must be finite and non-negative");
        return Result(L, Soundtrack::Update(seconds));
    }

    int ResetMusic(lua_State* L) { return Result(L, Soundtrack::Reset()); }
}
