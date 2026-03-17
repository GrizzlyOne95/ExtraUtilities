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

/*--------------------------------
* Exported lua functions go here *
--------------------------------*/

#include "About.h"
#include "Exports.h"
#include "Logging.h"
#include "LuaHelpers.h"
#include "LuaState.h"
#include "Patches.h"

#include "lua.hpp"
#include <Windows.h>
#include <algorithm>
#include <vector>

// Avoid name collision with winapi macro
#pragma push_macro("MessageBox")
#undef MessageBox

namespace ExtraUtilities::Lua
{
	namespace
	{
		int g_originalSetObjectiveOnRef = LUA_NOREF;
		int g_originalSetObjectiveOffRef = LUA_NOREF;
		std::vector<BZR::handle> g_activeObjectiveHandles;

		void ResetObjectiveObjectPatchState(lua_State* L)
		{
			if (g_originalSetObjectiveOnRef != LUA_NOREF)
			{
				luaL_unref(L, LUA_REGISTRYINDEX, g_originalSetObjectiveOnRef);
				g_originalSetObjectiveOnRef = LUA_NOREF;
			}

			if (g_originalSetObjectiveOffRef != LUA_NOREF)
			{
				luaL_unref(L, LUA_REGISTRYINDEX, g_originalSetObjectiveOffRef);
				g_originalSetObjectiveOffRef = LUA_NOREF;
			}

			g_activeObjectiveHandles.clear();
		}

		void TrackObjectiveHandleOn(BZR::handle h)
		{
			if (h == 0)
			{
				return;
			}

			if (std::find(g_activeObjectiveHandles.begin(), g_activeObjectiveHandles.end(), h) == g_activeObjectiveHandles.end())
			{
				g_activeObjectiveHandles.push_back(h);
			}
		}

		void TrackObjectiveHandleOff(BZR::handle h)
		{
			if (h == 0)
			{
				return;
			}

			g_activeObjectiveHandles.erase(
				std::remove(g_activeObjectiveHandles.begin(), g_activeObjectiveHandles.end(), h),
				g_activeObjectiveHandles.end());
		}

		int CallLuaRegistryFunction(lua_State* L, int functionRef)
		{
			if (functionRef == LUA_NOREF)
			{
				return luaL_error(L, "ObjectiveObjects patch is not initialized");
			}

			const int argCount = lua_gettop(L);
			lua_rawgeti(L, LUA_REGISTRYINDEX, functionRef);
			lua_insert(L, 1);
			lua_call(L, argCount, LUA_MULTRET);
			return lua_gettop(L);
		}

		int PatchedSetObjectiveOn(lua_State* L)
		{
			if (lua_isuserdata(L, 1))
			{
				TrackObjectiveHandleOn(CheckHandle(L, 1));
			}

			return CallLuaRegistryFunction(L, g_originalSetObjectiveOnRef);
		}

		int PatchedSetObjectiveOff(lua_State* L)
		{
			if (lua_isuserdata(L, 1))
			{
				TrackObjectiveHandleOff(CheckHandle(L, 1));
			}

			return CallLuaRegistryFunction(L, g_originalSetObjectiveOffRef);
		}

		int PatchedObjectiveObjectsNext(lua_State* L)
		{
			lua_Integer index = lua_tointeger(L, lua_upvalueindex(1));

			while (index < static_cast<lua_Integer>(g_activeObjectiveHandles.size()))
			{
				const BZR::handle h = g_activeObjectiveHandles[static_cast<size_t>(index)];
				++index;

				lua_pushinteger(L, index);
				lua_replace(L, lua_upvalueindex(1));

				if (h != 0)
				{
					lua_pushlightuserdata(L, reinterpret_cast<void*>(static_cast<uintptr_t>(h)));
					return 1;
				}
			}

			return 0;
		}

		int PatchedObjectiveObjects(lua_State* L)
		{
			lua_pushinteger(L, 0);
			lua_pushcclosure(L, PatchedObjectiveObjectsNext, 1);
			return 1;
		}

		void InstallObjectiveObjectsPatch(lua_State* L)
		{
			ResetObjectiveObjectPatchState(L);

			lua_getglobal(L, "SetObjectiveOn");
			if (!lua_isfunction(L, -1))
			{
				lua_pop(L, 1);
				Logging::LogMessage("exu: SetObjectiveOn not found; ObjectiveObjects patch skipped");
				return;
			}
			g_originalSetObjectiveOnRef = luaL_ref(L, LUA_REGISTRYINDEX);

			lua_getglobal(L, "SetObjectiveOff");
			if (!lua_isfunction(L, -1))
			{
				lua_pop(L, 1);
				Logging::LogMessage("exu: SetObjectiveOff not found; ObjectiveObjects patch skipped");
				return;
			}
			g_originalSetObjectiveOffRef = luaL_ref(L, LUA_REGISTRYINDEX);

			lua_pushcfunction(L, PatchedSetObjectiveOn);
			lua_setglobal(L, "SetObjectiveOn");

			lua_pushcfunction(L, PatchedSetObjectiveOff);
			lua_setglobal(L, "SetObjectiveOff");

			lua_pushcfunction(L, PatchedObjectiveObjects);
			lua_setglobal(L, "ObjectiveObjects");

			Logging::LogMessage("exu: patched stock ObjectiveObjects iterator");
		}
	}

	void MakeEnums(lua_State* L, int exuIdx)
	{
		// Camera enum
		lua_newtable(L);

		lua_pushinteger(L, BZR::Camera::FIRST_PERSON);
		lua_setfield(L, -2, "FIRST_PERSON");

		lua_pushinteger(L, BZR::Camera::THIRD_PERSON);
		lua_setfield(L, -2, "THIRD_PERSON");

		lua_pushinteger(L, BZR::Camera::COCKPIT);
		lua_setfield(L, -2, "COCKPIT");

		lua_pushinteger(L, BZR::Camera::NO_COCKPIT);
		lua_setfield(L, -2, "NO_COCKPIT");

		lua_pushinteger(L, BZR::Camera::CHASE);
		lua_setfield(L, -2, "CHASE");

		lua_pushinteger(L, BZR::Camera::ORBIT);
		lua_setfield(L, -2, "ORBIT");

		lua_pushinteger(L, BZR::Camera::NO_HUD);
		lua_setfield(L, -2, "NO_HUD");

		lua_pushinteger(L, BZR::Camera::EDITOR);
		lua_setfield(L, -2, "EDITOR");

		lua_pushinteger(L, BZR::Camera::CHEAT_SATELLITE);
		lua_setfield(L, -2, "CHEAT_SATELLITE");

		lua_pushinteger(L, BZR::Camera::FREECAM);
		lua_setfield(L, -2, "FREECAM");

		lua_pushinteger(L, BZR::Camera::TOGGLE_SATELLITE);
		lua_setfield(L, -2, "TOGGLE_SATELLITE");

		lua_pushinteger(L, BZR::Camera::TERRAIN_EDIT);
		lua_setfield(L, -2, "TERRAIN_EDIT");

		lua_setfield(L, -2, "CAMERA"); // end camera enum

		// Defaults enum
		lua_newtable(L);

		lua_pushnumber(L, 0.99f);
		lua_setfield(L, -2, "CAMERA_MIN_ZOOM");

		lua_pushnumber(L, 1.60f);
		lua_setfield(L, -2, "CAMERA_MAX_ZOOM");

		lua_pushnumber(L, 4.9f);
		lua_setfield(L, -2, "COEFF_BALLISTIC");

		PushVector(L, { 0.f, -9.8f, 0.f });
		lua_setfield(L, -2, "GRAVITY_VECTOR");

		lua_pushnumber(L, 200.f);
		lua_setfield(L, -2, "RETICLE_RANGE");

		lua_pushnumber(L, 8.f);
		lua_setfield(L, -2, "SAT_MAX_ZOOM");

		lua_pushnumber(L, 2.f);
		lua_setfield(L, -2, "SAT_MIN_ZOOM");

		lua_pushnumber(L, 1250.f);
		lua_setfield(L, -2, "SAT_PAN_SPEED");

		lua_setfield(L, exuIdx, "DEFAULTS"); // Set the field inside the library table

		// Difficulty enum
		lua_newtable(L);

		lua_pushinteger(L, 0);
		lua_setfield(L, -2, "VERY_EASY");

		lua_pushinteger(L, 1);
		lua_setfield(L, -2, "EASY");

		lua_pushinteger(L, 2);
		lua_setfield(L, -2, "MEDIUM");

		lua_pushinteger(L, 3);
		lua_setfield(L, -2, "HARD");

		lua_pushinteger(L, 4);
		lua_setfield(L, -2, "VERY_HARD");

		lua_setfield(L, exuIdx, "DIFFICULTY");

		// Ogre
		lua_newtable(L);

		lua_newtable(L); // Default headlight color (diffuse and specular)

		lua_pushnumber(L, 1.0f);
		lua_setfield(L, -2, "R");

		lua_pushnumber(L, 1.0f);
		lua_setfield(L, -2, "G");

		lua_pushnumber(L, 1.0f);
		lua_setfield(L, -2, "B");

		lua_setfield(L, -2, "HEADLIGHT_COLOR"); // end color rable

		lua_newtable(L); // Default headlight range

		lua_pushnumber(L, 0.1745f);
		lua_setfield(L, -2, "INNER_ANGLE");

		lua_pushnumber(L, 0.3490f);
		lua_setfield(L, -2, "OUTER_ANGLE");

		lua_pushnumber(L, 1.0f);
		lua_setfield(L, -2, "FALLOFF");

		lua_setfield(L, -2, "HEADLIGHT_RANGE"); // end range table

		lua_setfield(L, -2, "OGRE"); // end ogre enum

		// Overlay metrics enum
		lua_newtable(L);

		lua_pushinteger(L, 0);
		lua_setfield(L, -2, "RELATIVE");

		lua_pushinteger(L, 1);
		lua_setfield(L, -2, "PIXELS");

		lua_pushinteger(L, 2);
		lua_setfield(L, -2, "RELATIVE_ASPECT_ADJUSTED");

		lua_setfield(L, exuIdx, "OVERLAY_METRICS");

		// Ordnance enum
		lua_newtable(L);

		lua_pushinteger(L, Ordnance::AttributeCode::ODF);
		lua_setfield(L, -2, "ODF");

		lua_pushinteger(L, Ordnance::AttributeCode::TRANSFORM);
		lua_setfield(L, -2, "TRANSFORM");

		lua_pushinteger(L, Ordnance::AttributeCode::INIT_TRANSFORM);
		lua_setfield(L, -2, "INIT_TRANSFORM");

		lua_pushinteger(L, Ordnance::AttributeCode::OWNER);
		lua_setfield(L, -2, "OWNER");

		lua_pushinteger(L, Ordnance::AttributeCode::INIT_TIME);
		lua_setfield(L, -2, "INIT_TIME");

		lua_setfield(L, exuIdx, "ORDNANCE"); // end ordnance enum

		// Radar state enum
		lua_newtable(L);

		lua_pushinteger(L, 0);
		lua_setfield(L, -2, "MINIMAP");

		lua_pushinteger(L, 1);
		lua_setfield(L, -2, "RADAR");

		lua_setfield(L, exuIdx, "RADAR"); // end radar state enum

		// Satellite state enum
		lua_newtable(L);

		lua_pushinteger(L, 0);
		lua_setfield(L, -2, "DISABLED");

		lua_pushinteger(L, 1);
		lua_setfield(L, -2, "ENABLED");

		lua_setfield(L, exuIdx, "SATELLITE"); // end satellite state enum
	}

	void DoEventHooks(lua_State*)
	{

	}

	int Init(lua_State* L)
	{
		StackGuard guard(L);
		state = L; // save the state pointer to use in callbacks
		CommandReplacement::ResetState(L);
		Logging::LogMessage("exu: Init starting");
		BasicPatch::EnableDeferredPatchActivation();
		Logging::LogMessage("exu: deferred patches activated");

		// Register all this stuff inside the library table
		lua_getglobal(L, "exu");

		int exuIdx = lua_gettop(L);

		// Version string
		lua_pushstring(L, version.c_str());
		lua_setfield(L, exuIdx, "VERSION");

		MakeEnums(L, exuIdx);
		DoEventHooks(L);
		InstallObjectiveObjectsPatch(L);
		
		return 0;
	}

	extern "C" int __declspec(dllexport) luaopen_exu(lua_State* L)
	{
		static bool announced = false;
		const luaL_Reg exuExports[] = {
			// Camera
			{ "GetCameraOrigins", &Camera::GetOrigins },
			{ "GetCameraTransformMatrix", &Camera::GetTransformMatrix },
			{ "GetCameraViewMatrix", &Camera::GetViewMatrix },
			{ "GetCameraMaxZoom", &Camera::GetMaxZoom },
			{ "SetCameraMaxZoom", &Camera::SetMaxZoom },
			{ "GetCameraMinZoom", &Camera::GetMinZoom },
			{ "SetCameraMinZoom", &Camera::SetMinZoom },
			{ "GetCameraView", &Camera::GetView },
			{ "SetCameraView", &Camera::SetView },
			{ "GetCameraFOV", &Camera::GetFOV },
			{ "GetCameraClipDistances", &Camera::GetClipDistances },
			{ "SetCameraClipDistances", &Camera::SetClipDistances },
			{ "GetCameraAspectRatio", &Camera::GetAspectRatio },
			{ "SetCameraAspectRatio", &Camera::SetAspectRatio },
			{ "GetCameraProjectionType", &Camera::GetProjectionType },
			{ "SetCameraProjectionType", &Camera::SetProjectionType },
			{ "GetCameraPolygonMode", &Camera::GetPolygonMode },
			{ "SetCameraPolygonMode", &Camera::SetPolygonMode },
			{ "GetCameraZoom", &Camera::GetZoom },
			{ "SetCameraZoom", &Camera::SetZoom },

			// Command replacement
			{ "ReplaceStockCmd", &CommandReplacement::ReplaceStockCmd },
			{ "RemoveStockCmdReplacement", &CommandReplacement::RemoveStockCmdReplacement },
			{ "HasStockCmdReplacement", &CommandReplacement::HasStockCmdReplacement },
			{ "GetStockCmdReplacement", &CommandReplacement::GetStockCmdReplacement },
			{ "TriggerStockCmdReplacement", &CommandReplacement::TriggerStockCmdReplacement },
			{ "UpdateCommandReplacements", &CommandReplacement::UpdateCommandReplacements },

			// Control Panel
			{ "GetScrapPilotHudOffset", &ControlPanel::GetScrapPilotHudOffset },
			{ "SetScrapPilotHudOffset", &ControlPanel::SetScrapPilotHudOffset },
			{ "GetScrapPilotHudTopLeft", &ControlPanel::GetScrapPilotHudTopLeft },
			{ "SetScrapPilotHudTopLeft", &ControlPanel::SetScrapPilotHudTopLeft },
			{ "GetScrapHudTopLeft", &ControlPanel::GetScrapHudTopLeft },
			{ "SetScrapHudTopLeft", &ControlPanel::SetScrapHudTopLeft },
			{ "GetPilotHudTopLeft", &ControlPanel::GetPilotHudTopLeft },
			{ "SetPilotHudTopLeft", &ControlPanel::SetPilotHudTopLeft },
			{ "GetScrapHudColor", &ControlPanel::GetScrapHudColor },
			{ "SetScrapHudColor", &ControlPanel::SetScrapHudColor },
			{ "GetPilotHudColor", &ControlPanel::GetPilotHudColor },
			{ "SetPilotHudColor", &ControlPanel::SetPilotHudColor },
			{ "SelectAdd",  &ControlPanel::SelectAdd },
			{ "SelectNone", &ControlPanel::SelectNone },
			{ "SelectOne",  &ControlPanel::SelectOne },

			// Environment
			{ "GetFog",		   &Environment::GetFog },
			{ "SetFog",		   &Environment::SetFog },
			{ "GetGravity",    &Environment::GetGravity },
			{ "SetGravity",    &Environment::SetGravity },
			{ "GetAmbientLight", &Environment::GetAmbientLight },
			{ "SetAmbientLight", &Environment::SetAmbientLight },
			{ "GetSunAmbient", &Environment::GetSunAmbient },
			{ "SetSunAmbient", &Environment::SetSunAmbient },
			{ "GetSunDiffuse", &Environment::GetSunDiffuse },
			{ "SetSunDiffuse", &Environment::SetSunDiffuse },
			{ "GetSunSpecular", &Environment::GetSunSpecular },
			{ "SetSunSpecular", &Environment::SetSunSpecular },
			{ "GetSunDirection", &Environment::GetSunDirection },
			{ "SetSunDirection", &Environment::SetSunDirection },
			{ "SetOgreSunDirection", &Environment::SetOgreSunDirection },
			{ "SetTimeOfDay", &Environment::SetTimeOfDay },
			{ "GetSunPowerScale", &Environment::GetSunPowerScale },
			{ "SetSunPowerScale", &Environment::SetSunPowerScale },
			{ "GetSunShadowFarDistance", &Environment::GetSunShadowFarDistance },
			{ "SetSunShadowFarDistance", &Environment::SetSunShadowFarDistance },
			{ "GetSkyBoxParams", &Environment::GetSkyBoxParams },
			{ "GetSkyDomeParams", &Environment::GetSkyDomeParams },
			{ "GetSkyPlaneParams", &Environment::GetSkyPlaneParams },
			{ "GetShowBoundingBoxes", &Environment::GetShowBoundingBoxes },
			{ "SetShowBoundingBoxes", &Environment::SetShowBoundingBoxes },
			{ "GetShowDebugShadows", &Environment::GetShowDebugShadows },
			{ "SetShowDebugShadows", &Environment::SetShowDebugShadows },
			{ "GetViewportShadowsEnabled", &Environment::GetViewportShadowsEnabled },
			{ "SetViewportShadowsEnabled", &Environment::SetViewportShadowsEnabled },
			{ "GetViewportOverlaysEnabled", &Environment::GetViewportOverlaysEnabled },
			{ "SetViewportOverlaysEnabled", &Environment::SetViewportOverlaysEnabled },
			{ "GetSceneVisibilityMask", &Environment::GetSceneVisibilityMask },
			{ "SetSceneVisibilityMask", &Environment::SetSceneVisibilityMask },
			{ "HasSkyBoxNode", &Environment::HasSkyBoxNode },
			{ "HasSkyDomeNode", &Environment::HasSkyDomeNode },
			{ "HasSkyPlaneNode", &Environment::HasSkyPlaneNode },

			// Overlay
			{ "CreateOverlay", &Overlay::CreateOverlay },
			{ "DestroyOverlay", &Overlay::DestroyOverlay },
			{ "ShowOverlay", &Overlay::ShowOverlay },
			{ "HideOverlay", &Overlay::HideOverlay },
			{ "SetOverlayZOrder", &Overlay::SetOverlayZOrder },
			{ "SetOverlayScroll", &Overlay::SetOverlayScroll },
			{ "CreateOverlayElement", &Overlay::CreateOverlayElement },
			{ "DestroyOverlayElement", &Overlay::DestroyOverlayElement },
			{ "HasOverlayElement", &Overlay::HasOverlayElement },
			{ "AddOverlay2D", &Overlay::AddOverlay2D },
			{ "RemoveOverlay2D", &Overlay::RemoveOverlay2D },
			{ "AddOverlayElementChild", &Overlay::AddOverlayElementChild },
			{ "RemoveOverlayElementChild", &Overlay::RemoveOverlayElementChild },
			{ "ShowOverlayElement", &Overlay::ShowOverlayElement },
			{ "HideOverlayElement", &Overlay::HideOverlayElement },
			{ "SetOverlayMetricsMode", &Overlay::SetOverlayMetricsMode },
			{ "SetOverlayPosition", &Overlay::SetOverlayPosition },
			{ "SetOverlayDimensions", &Overlay::SetOverlayDimensions },
			{ "SetOverlayMaterial", &Overlay::SetOverlayMaterial },
			{ "SetOverlayParameter", &Overlay::SetOverlayParameter },
			{ "SetOverlayColor", &Overlay::SetOverlayColor },
			{ "SetOverlayCaption", &Overlay::SetOverlayCaption },
			{ "SetOverlayTextFont", &Overlay::SetOverlayTextFont },
			{ "SetOverlayTextColor", &Overlay::SetOverlayTextColor },
			{ "SetOverlayTextCharHeight", &Overlay::SetOverlayTextCharHeight },

			// GameObject
			{ "SetAsUser",		&GameObject::SetAsUser },
			{ "IsCommTowerPowered", &GameObject::IsCommTowerPowered },
			{ "GetHandle",		&GameObject::GetHandle },
			{ "GetEntityVisible", &GameObject::GetEntityVisible },
			{ "GetEntityCastShadows", &GameObject::GetEntityCastShadows },
			{ "GetEntityRenderingDistance", &GameObject::GetEntityRenderingDistance },
			{ "GetEntityVisibilityFlags", &GameObject::GetEntityVisibilityFlags },
			{ "GetEntityQueryFlags", &GameObject::GetEntityQueryFlags },
			{ "GetEntityRenderQueueGroup", &GameObject::GetEntityRenderQueueGroup },
			{ "SetEntityVisible", &GameObject::SetEntityVisible },
			{ "SetEntityCastShadows", &GameObject::SetEntityCastShadows },
			{ "SetEntityRenderingDistance", &GameObject::SetEntityRenderingDistance },
			{ "SetEntityVisibilityFlags", &GameObject::SetEntityVisibilityFlags },
			{ "SetEntityQueryFlags", &GameObject::SetEntityQueryFlags },
			{ "SetEntityRenderQueueGroup", &GameObject::SetEntityRenderQueueGroup },
			{ "SetSubEntityVisible", &GameObject::SetSubEntityVisible },
			{ "GetSubEntityCount", &GameObject::GetSubEntityCount },
			{ "GetSubEntityMaterial", &GameObject::GetSubEntityMaterial },
			{ "SetSubEntityMaterial", &GameObject::SetSubEntityMaterial },
			{ "SetEntityMaterial", &GameObject::SetEntityMaterial },
			{ "GetNumSubEntities", &GameObject::GetNumSubEntities },
			{ "GetMaterialName", &GameObject::GetMaterialName },
			{ "SetMaterialName", &GameObject::SetMaterialName },
			{ "MaterialExists", &GameObject::MaterialExists },
			{ "CloneMaterial", &GameObject::CloneMaterial },
			{ "GetMaterialPassColors", &GameObject::GetMaterialPassColors },
			{ "SetMaterialPassColors", &GameObject::SetMaterialPassColors },
			{ "SetHeadlightDiffuse", &GameObject::SetHeadlightDiffuse },
			{ "SetHeadlightSpecular", &GameObject::SetHeadlightSpecular },
			{ "SetHeadlightRange", &GameObject::SetHeadlightRange },
			{ "SetHeadlightVisible", &GameObject::SetHeadlightVisible },
			{ "GetLightPowerScale", &GameObject::GetLightPowerScale },
			{ "SetLightPowerScale", &GameObject::SetLightPowerScale },
			{ "GetLightPosition", &GameObject::GetLightPosition },
			{ "SetLightPosition", &GameObject::SetLightPosition },
			{ "GetLightDirection", &GameObject::GetLightDirection },
			{ "SetLightDirection", &GameObject::SetLightDirection },
			{ "SetLightAttenuation", &GameObject::SetLightAttenuation },
			{ "HasEntityAnimation", &GameObject::HasEntityAnimation },
			{ "GetEntityAnimationInfo", &GameObject::GetEntityAnimationInfo },
			{ "SetEntityAnimationEnabled", &GameObject::SetEntityAnimationEnabled },
			{ "SetEntityAnimationLoop", &GameObject::SetEntityAnimationLoop },
			{ "SetEntityAnimationWeight", &GameObject::SetEntityAnimationWeight },
			{ "SetEntityAnimationTime", &GameObject::SetEntityAnimationTime },
			{ "GetMass", &GameObject::GetMass },
			{ "SetMass", &GameObject::SetMass },
			{ "GetObj",			&GameObject::GetObj },
			{ "GetSelectedWeaponMask", &GameObject::GetSelectedWeaponMask },
			{ "GetWeaponSelectionInfo", &GameObject::GetWeaponSelectionInfo },
			{ "GetAiProcess", &GameObject::GetAiProcess },
			{ "GetAiProcessTypeName", &GameObject::GetAiProcessTypeName },
			{ "GetAiProcessInfo", &GameObject::GetAiProcessInfo },
			{ "GetAiProcessState", &GameObject::GetAiProcessState },
			{ "GetAiTaskInfo", &GameObject::GetAiTaskInfo },
			{ "GetAiTaskFieldScan", &GameObject::GetAiTaskFieldScan },
			{ "GetAiTaskState", &GameObject::GetAiTaskState },
			{ "SetAiTaskState", &GameObject::SetAiTaskState },
			{ "GetAiRecycleTaskState", &GameObject::GetAiRecycleTaskState },
			{ "GetRadarRange",	&GameObject::GetRadarRange },
			{ "SetRadarRange",	&GameObject::SetRadarRange },
			{ "GetRadarPeriod", &GameObject::GetRadarPeriod },
			{ "SetRadarPeriod", &GameObject::SetRadarPeriod },
			{ "GetVelocJam",	&GameObject::GetVelocJam },
			{ "SetVelocJam",	&GameObject::SetVelocJam },

			// Graphics Options
			{ "GetFullscreen", &GraphicsOptions::GetFullscreen },
			{ "GetGameResolution", &GraphicsOptions::GetGameResolution },
			{ "GetUIScaling", &GraphicsOptions::GetUIScaling },

			// IO
			{ "GetGameKey", &IO::GetGameKey },
			{ "IsPauseMenuOpen", &IO::IsPauseMenuOpen },
			{ "GetPauseMenuDebugState", &IO::GetPauseMenuDebugState },

			// Multiplayer
			{ "BuildAsyncObject", &Multiplayer::BuildAsyncObject },
			{ "BuildSyncObject", &Multiplayer::BuildSyncObject },
			{ "GetCustomKillMessage", &Patches::GetCustomKillMessage },
			{ "SetCustomKillMessage", &Patches::SetCustomKillMessage },
			{ "GetLives", &Multiplayer::GetLives },
			{ "SetLives", &Multiplayer::SetLives },
			{ "GetMyNetID", &Multiplayer::GetMyNetID },
			{ "GetShowScoreboard", &Multiplayer::GetShowScoreboard },
			{ "SetShowScoreboard", &Multiplayer::SetShowScoreboard },
			{ "DisableStartingRecycler", &Multiplayer::DisableStartingRecycler },

			// Ordnance
			{ "BuildOrdnance", &Ordnance::BuildOrdnance },
			{ "GetOrdnanceAttribute", &Ordnance::GetOrdnanceAttribute },
			{ "GetCoeffBallistic", &Ordnance::GetCoeffBallistic },
			{ "SetCoeffBallistic", &Ordnance::SetCoeffBallistic },

			// OS
			{ "GetScreenResolution", &OS::GetScreenResolution },
			{ "MessageBox",			 &OS::MessageBox },
			{ "SaveGame",			 &OS::SaveGame },

			// Radar
			{ "GetRadarState", &Radar::GetState },
			{ "SetRadarState", &Radar::SetState },
			{ "GetRadarSizeScale", &Radar::GetSizeScale },
			{ "SetRadarSizeScale", &Radar::SetSizeScale },
			{ "RefreshEdgePathBounds", &Radar::RefreshEdgePathBounds },
			{ "SetEdgePathCoords", &Radar::SetEdgePathCoords },

			// Patches
			{ "AddScrapSilent",     &Patches::AddScrapSilent },
			{ "GetTeamEngineFlameColor", &Patches::GetTeamEngineFlameColor },
			{ "SetTeamEngineFlameColor", &Patches::SetTeamEngineFlameColor },
			{ "ClearTeamEngineFlameColor", &Patches::ClearTeamEngineFlameColor },
			{ "GetGlobalTurbo",     &Patches::GetGlobalTurbo },
			{ "SetGlobalTurbo",     &Patches::SetGlobalTurbo },
			{ "GetUnitTurbo", &Patches::GetUnitTurbo },
			{ "SetUnitTurbo", &Patches::SetUnitTurbo },
			{ "GetOrdnanceVelocInheritance", &Patches::GetOrdnanceVelocInheritance },
			{ "SetOrdnanceVelocInheritance", &Patches::SetOrdnanceVelocInheritance },
			{ "GetOrdnanceVelocMode", Patches::GetOrdnanceVelocMode },
			{ "SetOrdnanceVelocMode", Patches::SetOrdnanceVelocMode },
			{ "GetShotConvergence", &Patches::GetShotConvergence },
			{ "SetShotConvergence", &Patches::SetShotConvergence },
			{ "GetUnitVoThrottle", &Patches::GetUnitVoThrottle },
			{ "SetUnitVoThrottle", &Patches::SetUnitVoThrottle },
			{ "GetUnitVoQueueDepthLimit", &Patches::GetUnitVoQueueDepthLimit },
			{ "SetUnitVoQueueDepthLimit", &Patches::SetUnitVoQueueDepthLimit },
			{ "GetUnitVoQueueStaleMs", &Patches::GetUnitVoQueueStaleMs },
			{ "SetUnitVoQueueStaleMs", &Patches::SetUnitVoQueueStaleMs },
			{ "GetUnitVoAlternates", &Patches::GetUnitVoAlternates },
			{ "SetUnitVoAlternates", &Patches::SetUnitVoAlternates },

			// Play Options
			{ "GetAutoLevel",	 &PlayOption::GetAutoLevel },
			{ "SetAutoLevel",	 &PlayOption::SetAutoLevel },
			{ "GetDifficulty",   &PlayOption::GetDifficulty },
			{ "SetDifficulty",   &PlayOption::SetDifficulty },
			{ "GetTLI",			 &PlayOption::GetTLI },
			{ "SetTLI",			 &PlayOption::SetTLI },
			{ "GetReverseMouse", &PlayOption::GetReverseMouse },
			{ "SetReverseMouse", &PlayOption::SetReverseMouse },

			// Radar
			// Reticle
			{ "GetReticleMatrix", &Reticle::GetMatrix },
			{ "GetReticleObject", &Reticle::GetObject },
			{ "GetReticlePos",    &Reticle::GetPosition },
			{ "GetReticleRange",  &Reticle::GetRange },
			{ "SetReticleRange",  &Reticle::SetRange },

			// Satellite

			{ "GetSatCameraPos", &Satellite::GetCameraPos },
			{ "GetSatClickPos",  &Satellite::GetClickPos },
			{ "GetSatCursorPos", &Satellite::GetCursorPos },
			{ "GetSatMaxZoom",	 &Satellite::GetMaxZoom },
			{ "SetSatMaxZoom",	 &Satellite::SetMaxZoom },
			{ "GetSatMinZoom",	 &Satellite::GetMinZoom },
			{ "SetSatMinZoom",	 &Satellite::SetMinZoom },
			{ "GetSatPanSpeed",	 &Satellite::GetPanSpeed },
			{ "SetSatPanSpeed",	 &Satellite::SetPanSpeed },
			{ "GetSatState",     &Satellite::GetState },
			{ "GetSatZoom",		 &Satellite::GetZoom },
			{ "SetSatZoom",		 &Satellite::SetZoom },

			// Sound Options
			{ "GetMusicVolume",   &SoundOptions::GetMusicVolume },
			//{ "GetEffectsVolume", &SoundOptions::GetEffectsVolume },
			//{ "SetEffectsVolume", &SoundOptions::SetEffectsVolume },
			//{ "GetVoiceVolume",   &SoundOptions::GetVoiceVolume },
			//{ "SetVoiceVolume",   &SoundOptions::SetVoiceVolume },

			// Steam
			{ "GetSteam64", &Steam::GetSteam64 },

			// Stock Extensions
			{ "DoString", &StockExtensions::DoString },
			{ "MatrixInverse", &StockExtensions::MatrixInverse },
			{ "ScreenToWorld", &StockExtensions::ScreenToWorld },
			{ "VectorUnrotate", &StockExtensions::VectorUnrotate },

			// Function register table must end with a zero entry
			{ 0, 0 }
		};

		Logging::LogMessage("exu: luaopen_exu called");
		luaL_register(L, "exu", exuExports);
		Init(L);

		if (!announced)
		{
			announced = true;
			lua_getglobal(L, "print");
			if (lua_isfunction(L, -1))
			{
				lua_pushstring(L, "exu.dll loaded");
				if (lua_pcall(L, 1, 0, 0) != 0)
				{
					lua_pop(L, 1);
				}
			}
			else
			{
				lua_pop(L, 1);
			}
		}

		Logging::LogMessage("exu: luaopen_exu finished");

		return 0;
	}
}

#pragma pop_macro("MessageBox")
