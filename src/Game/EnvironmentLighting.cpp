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

#include "EnvironmentInternal.h"

// Fog, gravity, sun (ambient, diffuse, specular, direction, power, shadow
// distance) and time of day: the guarded engine calls and their bindings.

namespace ExtraUtilities::Lua::Environment
{
	bool IsExpectedColorRange(const Ogre::Color& color)
	{
		return color.r >= 0.0f && color.r <= 1.0f
			&& color.g >= 0.0f && color.g <= 1.0f
			&& color.b >= 0.0f && color.b <= 1.0f;
	}

	bool IsValidTimeOfDay(int timeOfDay)
	{
		return timeOfDay >= 0
			&& timeOfDay <= 2359
			&& (timeOfDay % 100) < 60;
	}

	bool TryGetSunAmbientColor(void* sceneManager, Ogre::Color& outColor)
	{
		__try
		{
			auto* sunAmbient = Ogre::GetAmbientLight(sceneManager);
			if (sunAmbient != nullptr)
			{
				outColor = *sunAmbient;
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::GetSunAmbient] crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
			return false;
		}
	}

	bool TrySetSunAmbientColor(void* sceneManager, const Ogre::Color& color)
	{
		__try
		{
			Ogre::SetAmbientLight(sceneManager, const_cast<Ogre::Color*>(&color));
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::SetSunAmbient] crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
			return false;
		}
	}

	bool TryGetSunDiffuseColor(void* terrainMasterLight, Ogre::Color& outColor)
	{
		__try
		{
			auto* color = Ogre::GetDiffuseColor(terrainMasterLight);
			if (color != nullptr)
			{
				outColor = *color;
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::GetSunDiffuse] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			return false;
		}
	}

	bool TrySetSunDiffuseColor(void* terrainMasterLight, const Ogre::Color& color)
	{
		__try
		{
			Ogre::SetDiffuseColor(terrainMasterLight, color.r, color.g, color.b);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::SetSunDiffuse] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			return false;
		}
	}

	bool TryGetSunSpecularColor(void* terrainMasterLight, Ogre::Color& outColor)
	{
		__try
		{
			auto* color = Ogre::GetSpecularColor(terrainMasterLight);
			if (color != nullptr)
			{
				outColor = *color;
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::GetSunSpecular] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			return false;
		}
	}

	bool TrySetSunSpecularColor(void* terrainMasterLight, const Ogre::Color& color)
	{
		__try
		{
			Ogre::SetSpecularColor(terrainMasterLight, color.r, color.g, color.b);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::SetSunSpecular] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			return false;
		}
	}

	bool TryGetSunDirection(void* terrainMasterLight, BZR::VECTOR_3D& outDirection)
	{
		__try
		{
			auto* direction = Ogre::GetDirection(terrainMasterLight);
			if (direction != nullptr)
			{
				outDirection = *direction;
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::GetSunDirection] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			return false;
		}
	}

	bool TrySetSunDirection(void* terrainMasterLight, const BZR::VECTOR_3D& direction)
	{
		__try
		{
			Ogre::SetDirection(terrainMasterLight, direction.x, direction.y, direction.z);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::SetSunDirection] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			return false;
		}
	}

	bool TryGetFog(void* sceneManager, Ogre::Fog& outFog)
	{
		if (!Ogre::GetFogColour || !Ogre::GetFogStart || !Ogre::GetFogEnd)
		{
			return false;
		}

		__try
		{
			const Ogre::Color* colour = Ogre::GetFogColour(sceneManager);
			if (colour == nullptr)
			{
				return false;
			}
			outFog.r = colour->r;
			outFog.g = colour->g;
			outFog.b = colour->b;
			outFog.start = Ogre::GetFogStart(sceneManager);
			outFog.ending = Ogre::GetFogEnd(sceneManager);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::GetFog] crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
			return false;
		}
	}

	// Keeps the scene's fog mode, density and colour alpha; changes colour
	// and linear range, as the old direct write did.
	bool TrySetFog(void* sceneManager, const Ogre::Fog& fog)
	{
		if (!Ogre::GetFogColour || !Ogre::GetFogMode || !Ogre::GetFogDensity || !Ogre::SetFog)
		{
			return false;
		}

		__try
		{
			const Ogre::Color* current = Ogre::GetFogColour(sceneManager);
			const Ogre::Color colour{ fog.r, fog.g, fog.b, current != nullptr ? current->a : 1.0f };
			Ogre::SetFog(
				sceneManager,
				Ogre::GetFogMode(sceneManager),
				&colour,
				Ogre::GetFogDensity(sceneManager),
				fog.start,
				fog.ending);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::SetFog] crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
			return false;
		}
	}

	bool TrySetNativeTimeOfDay(int timeOfDay)
	{
		if (!RuntimeGate::IsSupported())
		{
			return false;
		}

		__try
		{
			*BZR::Environment::timeOfDay = timeOfDay;
			BZR::Environment::SetTimeOfDay(timeOfDay / 100);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::SetTimeOfDay] crashed timeOfDay=%d code=0x%08X", timeOfDay, GetExceptionCode());
			return false;
		}
	}

	bool TryRefreshTerrainMasterLight()
	{
		if (!RuntimeGate::IsSupported())
		{
			return false;
		}

		__try
		{
			BZR::Environment::RefreshTerrainMasterLight();
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::SetTimeOfDay] refresh crashed code=0x%08X", GetExceptionCode());
			return false;
		}
	}

	bool TryGetSunPowerScale(void* terrainMasterLight, float& outValue)
	{
		__try
		{
			outValue = Ogre::GetPowerScale(terrainMasterLight);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::GetSunPowerScale] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			outValue = 0.0f;
			return false;
		}
	}

	bool TrySetSunPowerScale(void* terrainMasterLight, float value)
	{
		__try
		{
			Ogre::SetPowerScale(terrainMasterLight, value);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::SetSunPowerScale] crashed terrainMasterLight=%p value=%g code=0x%08X", terrainMasterLight, value, GetExceptionCode());
			return false;
		}
	}

	bool TryGetSunShadowFarDistance(void* terrainMasterLight, float& outValue)
	{
		__try
		{
			outValue = Ogre::GetShadowFarDistance(terrainMasterLight);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::GetSunShadowFarDistance] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			outValue = 0.0f;
			return false;
		}
	}

	bool TrySetSunShadowFarDistance(void* terrainMasterLight, float value)
	{
		__try
		{
			Ogre::SetShadowFarDistance(terrainMasterLight, value);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::SetSunShadowFarDistance] crashed terrainMasterLight=%p value=%g code=0x%08X", terrainMasterLight, value, GetExceptionCode());
			return false;
		}
	}

	int GetGravity(lua_State* L)
	{
		BZR::VECTOR_3D g = gravity.Read();

		PushVector(L, g);

		return 1;
	}

	int SetGravity(lua_State* L)
	{
		auto newGravity = CheckVectorOrSingles(L, 1);
		gravity.Write(newGravity);

		return 0;
	}

	int GetFog(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		Ogre::Fog f{};
		if (sceneManager == nullptr || !TryGetFog(sceneManager, f))
		{
			lua_pushnil(L);
			return 1;
		}

		PushFog(L, f);
		return 1;
	}

	int SetFog(lua_State* L)
	{
		Patch::TryInitializeOgre();

		const auto f = CheckFogOrSingles(L, 1);
		if (!std::isfinite(f.r) || !std::isfinite(f.g) || !std::isfinite(f.b) ||
			!std::isfinite(f.start) || !std::isfinite(f.ending))
		{
			return luaL_argerror(L, 1, "fog values must be finite");
		}

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			return 0;
		}

		// Stop the engine's own SetFog from overwriting the script's fog for
		// the rest of this Lua state.
		Patch::fogResetPatch.SetStatus(true);
		TrySetFog(sceneManager, f);
		return 0;
	}

	int GetSunAmbient(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		Ogre::Color sunColor = DefaultSunColor();
		if (sceneManager == nullptr || !TryGetSunAmbientColor(sceneManager, sunColor))
		{
			lua_pushnil(L);
			return 1;
		}
		PushColor(L, sunColor);

		return 1;
	}

	int SetSunAmbient(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		auto caller = DescribeLuaCaller(L);
		LogEnvironmentDebug(
			"[EXU::SetSunAmbient] enter caller=%s argType=%s sceneManager=%p",
			caller.c_str(),
			luaL_typename(L, 1),
			sceneManager);
		if (sceneManager == nullptr)
		{
			LogEnvironmentDebug("[EXU::SetSunAmbient] skipped: scene manager unavailable");
			return 0;
		}

		auto color = CheckColorOrSingles(L, 1);
		LogEnvironmentDebug(
			"[EXU::SetSunAmbient] parsed r=%.9g g=%.9g b=%.9g finite=%s inRange=%s",
			color.r,
			color.g,
			color.b,
			IsFiniteColor(color) ? "true" : "false",
			IsExpectedColorRange(color) ? "true" : "false");
		if (!IsFiniteColor(color))
		{
			LogEnvironmentDebug("[EXU::SetSunAmbient] rejecting non-finite color");
			return luaL_argerror(L, 1, "SetSunAmbient requires finite numeric color values");
		}

		LogEnvironmentDebug("[EXU::SetSunAmbient] calling Ogre::SetAmbientLight");
		if (!TrySetSunAmbientColor(sceneManager, color))
		{
			return 0;
		}
		LogEnvironmentDebug("[EXU::SetSunAmbient] completed");

		return 0;
	}

	int GetAmbientLight(lua_State* L)
	{
		return GetSunAmbient(L);
	}

	int SetAmbientLight(lua_State* L)
	{
		return SetSunAmbient(L);
	}

	int GetSunDiffuse(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		Ogre::Color diffuseColor = DefaultSunColor();
		if (terrainMasterLight == nullptr || !TryGetSunDiffuseColor(terrainMasterLight, diffuseColor))
		{
			lua_pushnil(L);
			return 1;
		}
		PushColor(L, diffuseColor);

		return 1;
	}

	int SetSunDiffuse(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		auto caller = DescribeLuaCaller(L);
		LogEnvironmentDebug(
			"[EXU::SetSunDiffuse] enter caller=%s argType=%s terrainMasterLight=%p",
			caller.c_str(),
			luaL_typename(L, 1),
			terrainMasterLight);
		if (terrainMasterLight == nullptr)
		{
			LogEnvironmentDebug("[EXU::SetSunDiffuse] skipped: terrain master light unavailable");
			return 0;
		}

		auto color = CheckColorOrSingles(L, 1);
		LogEnvironmentDebug(
			"[EXU::SetSunDiffuse] parsed r=%.9g g=%.9g b=%.9g finite=%s inRange=%s",
			color.r,
			color.g,
			color.b,
			IsFiniteColor(color) ? "true" : "false",
			IsExpectedColorRange(color) ? "true" : "false");
		if (!IsFiniteColor(color))
		{
			LogEnvironmentDebug("[EXU::SetSunDiffuse] rejecting non-finite color");
			return luaL_argerror(L, 1, "SetSunDiffuse requires finite numeric color values");
		}

		LogEnvironmentDebug("[EXU::SetSunDiffuse] calling Ogre::SetDiffuseColor");
		if (!TrySetSunDiffuseColor(terrainMasterLight, color))
		{
			return 0;
		}
		LogEnvironmentDebug("[EXU::SetSunDiffuse] completed");

		return 0;
	}

	int GetSunSpecular(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		Ogre::Color specularColor = DefaultSunColor();
		if (terrainMasterLight == nullptr || !TryGetSunSpecularColor(terrainMasterLight, specularColor))
		{
			lua_pushnil(L);
			return 1;
		}
		PushColor(L, specularColor);

		return 1;
	}

	int SetSunSpecular(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		auto caller = DescribeLuaCaller(L);
		LogEnvironmentDebug(
			"[EXU::SetSunSpecular] enter caller=%s argType=%s terrainMasterLight=%p",
			caller.c_str(),
			luaL_typename(L, 1),
			terrainMasterLight);
		if (terrainMasterLight == nullptr)
		{
			LogEnvironmentDebug("[EXU::SetSunSpecular] skipped: terrain master light unavailable");
			return 0;
		}

		auto color = CheckColorOrSingles(L, 1);
		LogEnvironmentDebug(
			"[EXU::SetSunSpecular] parsed r=%.9g g=%.9g b=%.9g finite=%s inRange=%s",
			color.r,
			color.g,
			color.b,
			IsFiniteColor(color) ? "true" : "false",
			IsExpectedColorRange(color) ? "true" : "false");
		if (!IsFiniteColor(color))
		{
			LogEnvironmentDebug("[EXU::SetSunSpecular] rejecting non-finite color");
			return luaL_argerror(L, 1, "SetSunSpecular requires finite numeric color values");
		}

		LogEnvironmentDebug("[EXU::SetSunSpecular] calling Ogre::SetSpecularColor");
		if (!TrySetSunSpecularColor(terrainMasterLight, color))
		{
			return 0;
		}
		LogEnvironmentDebug("[EXU::SetSunSpecular] completed");

		return 0;
	}

	int GetSunDirection(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		BZR::VECTOR_3D direction{};
		if (terrainMasterLight == nullptr || !TryGetSunDirection(terrainMasterLight, direction))
		{
			lua_pushnil(L);
			return 1;
		}
		PushVector(L, direction);
		return 1;
	}

	int SetSunDirection(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		auto caller = DescribeLuaCaller(L);
		LogEnvironmentDebug(
			"[EXU::SetSunDirection] enter caller=%s argType=%s terrainMasterLight=%p",
			caller.c_str(),
			luaL_typename(L, 1),
			terrainMasterLight);
		if (terrainMasterLight == nullptr)
		{
			LogEnvironmentDebug("[EXU::SetSunDirection] skipped: terrain master light unavailable");
			return 0;
		}

		auto direction = CheckVectorOrSingles(L, 1);
		LogEnvironmentDebug(
			"[EXU::SetSunDirection] parsed x=%.9g y=%.9g z=%.9g finite=%s",
			direction.x,
			direction.y,
			direction.z,
			IsFiniteVector(direction) ? "true" : "false");
		if (!IsFiniteVector(direction))
		{
			LogEnvironmentDebug("[EXU::SetSunDirection] rejecting non-finite direction");
			return luaL_argerror(L, 1, "SetSunDirection requires finite numeric vector values");
		}

		LogEnvironmentDebug("[EXU::SetSunDirection] calling Ogre::SetDirection");
		if (!TrySetSunDirection(terrainMasterLight, direction))
		{
			return 0;
		}
		LogEnvironmentDebug("[EXU::SetSunDirection] completed");
		return 0;
	}

	int SetOgreSunDirection(lua_State* L)
	{
		return SetSunDirection(L);
	}

	int SetTimeOfDay(lua_State* L)
	{
		Patch::TryInitializeOgre();

		const int timeOfDay = static_cast<int>(luaL_checkinteger(L, 1));
		const bool refreshSun = lua_gettop(L) < 2 || lua_toboolean(L, 2) != 0;
		auto* terrainMasterLight = GetTerrainMasterLight();
		auto caller = DescribeLuaCaller(L);
		LogEnvironmentDebug(
			"[EXU::SetTimeOfDay] enter caller=%s timeOfDay=%d refreshSun=%s terrainMasterLight=%p",
			caller.c_str(),
			timeOfDay,
			refreshSun ? "true" : "false",
			terrainMasterLight);
		if (!IsValidTimeOfDay(timeOfDay))
		{
			LogEnvironmentDebug("[EXU::SetTimeOfDay] rejecting invalid HHMM value");
			return luaL_argerror(L, 1, "SetTimeOfDay requires a TRN-style HHMM integer between 0000 and 2359");
		}

		LogEnvironmentDebug("[EXU::SetTimeOfDay] calling native light model hour=%d", timeOfDay / 100);
		if (!TrySetNativeTimeOfDay(timeOfDay))
		{
			return 0;
		}

		if (!refreshSun)
		{
			LogEnvironmentDebug("[EXU::SetTimeOfDay] completed without Ogre refresh");
			return 0;
		}

		if (terrainMasterLight == nullptr)
		{
			LogEnvironmentDebug("[EXU::SetTimeOfDay] completed without Ogre refresh: terrain master light unavailable");
			return 0;
		}

		LogEnvironmentDebug("[EXU::SetTimeOfDay] refreshing terrain master light");
		if (!TryRefreshTerrainMasterLight())
		{
			return 0;
		}

		LogEnvironmentDebug("[EXU::SetTimeOfDay] completed");
		return 0;
	}

	int GetSunPowerScale(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		if (terrainMasterLight == nullptr)
		{
			lua_pushnumber(L, 0.0);
			return 1;
		}

		float powerScale = 0.0f;
		TryGetSunPowerScale(terrainMasterLight, powerScale);
		lua_pushnumber(L, powerScale);
		return 1;
	}

	int SetSunPowerScale(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		auto caller = DescribeLuaCaller(L);
		LogEnvironmentDebug(
			"[EXU::SetSunPowerScale] enter caller=%s terrainMasterLight=%p",
			caller.c_str(),
			terrainMasterLight);
		if (terrainMasterLight == nullptr)
		{
			LogEnvironmentDebug("[EXU::SetSunPowerScale] skipped: terrain master light unavailable");
			return 0;
		}

		float powerScale = static_cast<float>(luaL_checknumber(L, 1));
		LogEnvironmentDebug(
			"[EXU::SetSunPowerScale] parsed value=%.9g finite=%s",
			powerScale,
			IsFiniteScalar(powerScale) ? "true" : "false");
		if (!IsFiniteScalar(powerScale))
		{
			LogEnvironmentDebug("[EXU::SetSunPowerScale] rejecting non-finite value");
			return luaL_argerror(L, 1, "SetSunPowerScale requires a finite numeric value");
		}

		LogEnvironmentDebug("[EXU::SetSunPowerScale] calling Ogre::SetPowerScale");
		if (!TrySetSunPowerScale(terrainMasterLight, powerScale))
		{
			return 0;
		}
		LogEnvironmentDebug("[EXU::SetSunPowerScale] completed");
		return 0;
	}

	int GetSunShadowFarDistance(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		if (terrainMasterLight == nullptr)
		{
			lua_pushnumber(L, 0.0);
			return 1;
		}

		float shadowFarDistance = 0.0f;
		TryGetSunShadowFarDistance(terrainMasterLight, shadowFarDistance);
		lua_pushnumber(L, shadowFarDistance);
		return 1;
	}

	int SetSunShadowFarDistance(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		auto caller = DescribeLuaCaller(L);
		LogEnvironmentDebug(
			"[EXU::SetSunShadowFarDistance] enter caller=%s terrainMasterLight=%p",
			caller.c_str(),
			terrainMasterLight);
		if (terrainMasterLight == nullptr)
		{
			LogEnvironmentDebug("[EXU::SetSunShadowFarDistance] skipped: terrain master light unavailable");
			return 0;
		}

		float shadowFarDistance = static_cast<float>(luaL_checknumber(L, 1));
		LogEnvironmentDebug(
			"[EXU::SetSunShadowFarDistance] parsed value=%.9g finite=%s",
			shadowFarDistance,
			IsFiniteScalar(shadowFarDistance) ? "true" : "false");
		if (!IsFiniteScalar(shadowFarDistance))
		{
			LogEnvironmentDebug("[EXU::SetSunShadowFarDistance] rejecting non-finite value");
			return luaL_argerror(L, 1, "SetSunShadowFarDistance requires a finite numeric value");
		}

		LogEnvironmentDebug("[EXU::SetSunShadowFarDistance] calling Ogre::SetShadowFarDistance");
		if (!TrySetSunShadowFarDistance(terrainMasterLight, shadowFarDistance))
		{
			return 0;
		}
		LogEnvironmentDebug("[EXU::SetSunShadowFarDistance] completed");
		return 0;
	}
}
