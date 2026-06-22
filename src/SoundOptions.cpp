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
#include "Logging.h"

#include <Windows.h>

#include <cstring>

namespace ExtraUtilities::Lua::SoundOptions
{
	namespace
	{
		using OpenShimSetMusicTrackFn = BOOL(WINAPI*)(int);
		using OpenShimSimpleMusicFn = BOOL(WINAPI*)();
		using OpenShimGetMusicTrackFn = BOOL(WINAPI*)(int*);

		bool ShouldLogMissingExport(const char* exportName)
		{
			static bool loggedSet = false;
			static bool loggedStop = false;
			static bool loggedPause = false;
			static bool loggedResume = false;
			static bool loggedGet = false;

			bool* flag = nullptr;
			if (std::strcmp(exportName, "OpenShimSetMusicTrack") == 0)
				flag = &loggedSet;
			else if (std::strcmp(exportName, "OpenShimStopMusic") == 0)
				flag = &loggedStop;
			else if (std::strcmp(exportName, "OpenShimPauseMusic") == 0)
				flag = &loggedPause;
			else if (std::strcmp(exportName, "OpenShimResumeMusic") == 0)
				flag = &loggedResume;
			else if (std::strcmp(exportName, "OpenShimGetMusicTrack") == 0)
				flag = &loggedGet;

			if (!flag || *flag)
				return false;

			*flag = true;
			return true;
		}

		template<typename T>
		T ResolveOpenShimMusicBridge(const char* exportName)
		{
			static bool loggedMissingModule = false;
			HMODULE module = GetModuleHandleA("winmm.dll");
			if (!module)
			{
				if (!loggedMissingModule)
				{
					loggedMissingModule = true;
					Logging::LogMessage("[EXU::SoundOptions] OpenShim winmm.dll bridge unavailable");
				}
				return nullptr;
			}

			T fn = reinterpret_cast<T>(GetProcAddress(module, exportName));
			if (!fn && ShouldLogMissingExport(exportName))
			{
				Logging::LogMessage("[EXU::SoundOptions] OpenShim export missing: %s", exportName);
			}
			return fn;
		}
	}

	int GetMusicVolume(lua_State* L)
	{
		lua_pushnumber(L, musicVolume.Read());
		return 1;
	}

	int SetMusicTrack(lua_State* L)
	{
		lua_Integer requested = luaL_checkinteger(L, 1);
		if (requested < 0 || requested > 255)
		{
			return luaL_argerror(L, 1, "Extra Utilities Error: music track must be between 0 and 255");
		}

		if (OpenShimSetMusicTrackFn fn =
			ResolveOpenShimMusicBridge<OpenShimSetMusicTrackFn>("OpenShimSetMusicTrack"))
		{
			lua_pushboolean(L, fn(static_cast<int>(requested)) ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}

	int StopMusic(lua_State* L)
	{
		if (OpenShimSimpleMusicFn fn =
			ResolveOpenShimMusicBridge<OpenShimSimpleMusicFn>("OpenShimStopMusic"))
		{
			lua_pushboolean(L, fn() ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}

	int PauseMusic(lua_State* L)
	{
		if (OpenShimSimpleMusicFn fn =
			ResolveOpenShimMusicBridge<OpenShimSimpleMusicFn>("OpenShimPauseMusic"))
		{
			lua_pushboolean(L, fn() ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}

	int ResumeMusic(lua_State* L)
	{
		if (OpenShimSimpleMusicFn fn =
			ResolveOpenShimMusicBridge<OpenShimSimpleMusicFn>("OpenShimResumeMusic"))
		{
			lua_pushboolean(L, fn() ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}

	int GetMusicTrack(lua_State* L)
	{
		int track = -1;
		if (OpenShimGetMusicTrackFn fn =
			ResolveOpenShimMusicBridge<OpenShimGetMusicTrackFn>("OpenShimGetMusicTrack"))
		{
			if (fn(&track))
			{
				lua_pushinteger(L, track);
				return 1;
			}
		}

		lua_pushnil(L);
		return 1;
	}

	//int GetEffectsVolume(lua_State* L)
	//{
	//	lua_pushnumber(L, sfxVolume.Read());
	//	return 1;
	//}

	//int SetEffectsVolume(lua_State* L)
	//{
	//	int newVolume = luaL_checkinteger(L, 1);
	//	if (newVolume < 0 || newVolume > 10)
	//	{
	//		luaL_argerror(L, 1, "Value must be between 0 and 10");
	//	}
	//	sfxVolume.Write(newVolume);
	//	return 0;
	//}

	//int GetVoiceVolume(lua_State* L)
	//{
	//	lua_pushnumber(L, voiceVolume.Read());
	//	return 1;
	//}

	//int SetVoiceVolume(lua_State* L)
	//{
	//	int newVolume = luaL_checkinteger(L, 1);
	//	if (newVolume < 0 || newVolume > 10)
	//	{
	//		luaL_argerror(L, 1, "Value must be between 0 and 10");
	//	}
	//	voiceVolume.Write(newVolume);
	//	return 0;
	//}
}
