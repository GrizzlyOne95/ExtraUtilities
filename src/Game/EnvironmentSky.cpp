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

// Sky box, dome and plane: parameter reads, enable toggles, setters and
// their bindings.

namespace ExtraUtilities::Lua::Environment
{
	using SkyNodeGetter = void*(*)(void*);

	bool TryHasSkyNode(void* sceneManager, SkyNodeGetter getter, bool& outHasNode, const char* label)
	{
		__try
		{
			outHasNode = getter(sceneManager) != nullptr;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::Sky] %s node probe crashed sceneManager=%p code=0x%08X", label, sceneManager, GetExceptionCode());
			outHasNode = false;
			return false;
		}
	}

	bool TryGetSkyBoxGenParameters(void* sceneManager, Ogre::SkyBoxGenParameters& outParams)
	{
		__try
		{
			return Ogre::GetSkyBoxGenParameters(sceneManager, outParams);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::Sky] get skybox params crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
			return false;
		}
	}

	bool TryGetSkyDomeGenParameters(void* sceneManager, Ogre::SkyDomeGenParameters& outParams)
	{
		__try
		{
			return Ogre::GetSkyDomeGenParameters(sceneManager, outParams);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentFault("[EXU::Sky] get skydome params crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
			return false;
		}
	}

		bool TryGetSkyPlaneGenParameters(void* sceneManager, Ogre::SkyPlaneGenParameters& outParams)
		{
			__try
			{
				return Ogre::GetSkyPlaneGenParameters(sceneManager, outParams);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentFault("[EXU::Sky] get skyplane params crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
				return false;
			}
		}

		bool TryGetSkyEnabled(void* sceneManager, IsSkyEnabledFn fn, bool& outEnabled, const char* label)
		{
			outEnabled = false;
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				outEnabled = fn(sceneManager);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentFault("[EXU::Sky] %s enabled probe crashed sceneManager=%p code=0x%08X", label, sceneManager, GetExceptionCode());
				return false;
			}
		}

		bool TrySetSkyEnabled(void* sceneManager, SetSkyEnabledFn fn, bool enabled, const char* label)
		{
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(sceneManager, enabled);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentFault("[EXU::Sky] %s enabled setter crashed sceneManager=%p enabled=%d code=0x%08X", label, sceneManager, enabled ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetSkyBox(void* sceneManager, const std::string& materialName, float distance, bool drawFirst, const std::string& resourceGroup)
		{
			const auto fn = ResolveSetSkyBox();
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			const OgreQuaternionValue identity{};
			__try
			{
				fn(sceneManager, true, materialName, distance, drawFirst, identity, resourceGroup);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentFault(
					"[EXU::Sky] setSkyBox crashed sceneManager=%p material=%s distance=%g drawFirst=%d group=%s code=0x%08X",
					sceneManager,
					materialName.c_str(),
					distance,
					drawFirst ? 1 : 0,
					resourceGroup.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetSkyDome(
			void* sceneManager,
			const std::string& materialName,
			float curvature,
			float tiling,
			float distance,
			bool drawFirst,
			int xsegments,
			int ysegments,
			int ysegmentsKeep,
			const std::string& resourceGroup)
		{
			const auto fn = ResolveSetSkyDome();
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			const OgreQuaternionValue identity{};
			__try
			{
				fn(sceneManager, true, materialName, curvature, tiling, distance, drawFirst, identity, xsegments, ysegments, ysegmentsKeep, resourceGroup);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentFault(
					"[EXU::Sky] setSkyDome crashed sceneManager=%p material=%s curvature=%g tiling=%g distance=%g drawFirst=%d group=%s code=0x%08X",
					sceneManager,
					materialName.c_str(),
					curvature,
					tiling,
					distance,
					drawFirst ? 1 : 0,
					resourceGroup.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetSkyPlane(
			void* sceneManager,
			const OgrePlaneValue& plane,
			const std::string& materialName,
			float scale,
			float tiling,
			bool drawFirst,
			float bow,
			int xsegments,
			int ysegments,
			const std::string& resourceGroup)
		{
			const auto fn = ResolveSetSkyPlane();
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(sceneManager, true, plane, materialName, scale, tiling, drawFirst, bow, xsegments, ysegments, resourceGroup);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentFault(
					"[EXU::Sky] setSkyPlane crashed sceneManager=%p material=%s scale=%g tiling=%g drawFirst=%d bow=%g group=%s code=0x%08X",
					sceneManager,
					materialName.c_str(),
					scale,
					tiling,
					drawFirst ? 1 : 0,
					bow,
					resourceGroup.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TryReadSkyPlane(lua_State* L, int idx, OgrePlaneValue& outPlane)
		{
			luaL_checktype(L, idx, LUA_TTABLE);

			BZR::VECTOR_3D normal{ 0.0f, -1.0f, 0.0f };
			lua_getfield(L, idx, "normal");
			if (!lua_isnil(L, -1))
			{
				normal = CheckVectorOrSingles(L, -1);
			}
			lua_pop(L, 1);

			bool hasDistance = false;
			float distance = 1000.0f;
			lua_getfield(L, idx, "d");
			if (!lua_isnil(L, -1))
			{
				hasDistance = true;
				distance = static_cast<float>(luaL_checknumber(L, -1));
			}
			lua_pop(L, 1);

			if (!hasDistance)
			{
				lua_getfield(L, idx, "distance");
				if (!lua_isnil(L, -1))
				{
					hasDistance = true;
					distance = static_cast<float>(luaL_checknumber(L, -1));
				}
				lua_pop(L, 1);
			}

			if (!hasDistance)
			{
				luaL_error(L, "sky plane table requires a numeric d or distance field");
				return false;
			}

			outPlane.normal = normal;
			outPlane.d = distance;
			return true;
		}

		std::string CheckOptionalSkyResourceGroup(lua_State* L, int idx)
		{
			if (lua_isnoneornil(L, idx))
			{
				return "General";
			}

			return luaL_checkstring(L, idx);
		}

	int GetSkyBoxParams(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		bool hasNode = false;
		Ogre::SkyBoxGenParameters params{};
		if (!TryHasSkyNode(sceneManager, Ogre::GetSkyBoxNode, hasNode, "skybox") || !hasNode ||
			!TryGetSkyBoxGenParameters(sceneManager, params))
		{
			lua_pushnil(L);
			return 1;
		}

		PushSkyBoxParams(L, params);
		return 1;
	}

	int GetSkyDomeParams(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		bool hasNode = false;
		Ogre::SkyDomeGenParameters params{};
		if (!TryHasSkyNode(sceneManager, Ogre::GetSkyDomeNode, hasNode, "skydome") || !hasNode ||
			!TryGetSkyDomeGenParameters(sceneManager, params))
		{
			lua_pushnil(L);
			return 1;
		}

		PushSkyDomeParams(L, params);
		return 1;
	}

	int GetSkyPlaneParams(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		bool hasNode = false;
		Ogre::SkyPlaneGenParameters params{};
		if (!TryHasSkyNode(sceneManager, Ogre::GetSkyPlaneNode, hasNode, "skyplane") || !hasNode ||
			!TryGetSkyPlaneGenParameters(sceneManager, params))
		{
			lua_pushnil(L);
			return 1;
		}

		PushSkyPlaneParams(L, params);
		return 1;
	}

	int GetSkyBoxEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		bool enabled = false;
		if (sceneManager != nullptr)
		{
			TryGetSkyEnabled(sceneManager, ResolveIsSkyBoxEnabled(), enabled, "skybox");
		}

		lua_pushboolean(L, enabled ? 1 : 0);
		return 1;
	}

	int SetSkyBoxEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			return 0;
		}

		const bool enabled = CheckBool(L, 1);
		TrySetSkyEnabled(sceneManager, ResolveSetSkyBoxEnabled(), enabled, "skybox");
		return 0;
	}

	int SetSkyBox(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string materialName = luaL_checkstring(L, 1);
		const float distance = static_cast<float>(luaL_optnumber(L, 2, 5000.0));
		const bool drawFirst = lua_gettop(L) < 3 || lua_toboolean(L, 3) != 0;
		const std::string resourceGroup = CheckOptionalSkyResourceGroup(L, 4);

		lua_pushboolean(L, TrySetSkyBox(sceneManager, materialName, distance, drawFirst, resourceGroup) ? 1 : 0);
		return 1;
	}

	int GetSkyDomeEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		bool enabled = false;
		if (sceneManager != nullptr)
		{
			TryGetSkyEnabled(sceneManager, ResolveIsSkyDomeEnabled(), enabled, "skydome");
		}

		lua_pushboolean(L, enabled ? 1 : 0);
		return 1;
	}

	int SetSkyDomeEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			return 0;
		}

		const bool enabled = CheckBool(L, 1);
		TrySetSkyEnabled(sceneManager, ResolveSetSkyDomeEnabled(), enabled, "skydome");
		return 0;
	}

	int SetSkyDome(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string materialName = luaL_checkstring(L, 1);
		const float curvature = static_cast<float>(luaL_optnumber(L, 2, 10.0));
		const float tiling = static_cast<float>(luaL_optnumber(L, 3, 8.0));
		const float distance = static_cast<float>(luaL_optnumber(L, 4, 4000.0));
		const bool drawFirst = lua_gettop(L) < 5 || lua_toboolean(L, 5) != 0;
		const int xsegments = luaL_optint(L, 6, 16);
		const int ysegments = luaL_optint(L, 7, 16);
		const int ysegmentsKeep = luaL_optint(L, 8, -1);
		const std::string resourceGroup = CheckOptionalSkyResourceGroup(L, 9);

		lua_pushboolean(L, TrySetSkyDome(
			sceneManager,
			materialName,
			curvature,
			tiling,
			distance,
			drawFirst,
			xsegments,
			ysegments,
			ysegmentsKeep,
			resourceGroup) ? 1 : 0);
		return 1;
	}

	int GetSkyPlaneEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		bool enabled = false;
		if (sceneManager != nullptr)
		{
			TryGetSkyEnabled(sceneManager, ResolveIsSkyPlaneEnabled(), enabled, "skyplane");
		}

		lua_pushboolean(L, enabled ? 1 : 0);
		return 1;
	}

	int SetSkyPlaneEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			return 0;
		}

		const bool enabled = CheckBool(L, 1);
		TrySetSkyEnabled(sceneManager, ResolveSetSkyPlaneEnabled(), enabled, "skyplane");
		return 0;
	}

	int SetSkyPlane(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string materialName = luaL_checkstring(L, 1);
		OgrePlaneValue plane{};
		TryReadSkyPlane(L, 2, plane);
		const float scale = static_cast<float>(luaL_optnumber(L, 3, 1000.0));
		const float tiling = static_cast<float>(luaL_optnumber(L, 4, 10.0));
		const bool drawFirst = lua_gettop(L) < 5 || lua_toboolean(L, 5) != 0;
		const float bow = static_cast<float>(luaL_optnumber(L, 6, 0.0));
		const int xsegments = luaL_optint(L, 7, 1);
		const int ysegments = luaL_optint(L, 8, 1);
		const std::string resourceGroup = CheckOptionalSkyResourceGroup(L, 9);

		lua_pushboolean(L, TrySetSkyPlane(
			sceneManager,
			plane,
			materialName,
			scale,
			tiling,
			drawFirst,
			bow,
			xsegments,
			ysegments,
			resourceGroup) ? 1 : 0);
		return 1;
	}

	int HasSkyBoxNode(lua_State* L)
	{
		Patch::TryInitializeOgre();

		bool hasNode = false;
		auto* sceneManager = GetSceneManager();
		if (sceneManager != nullptr)
		{
			TryHasSkyNode(sceneManager, Ogre::GetSkyBoxNode, hasNode, "skybox");
		}

		lua_pushboolean(L, hasNode);
		return 1;
	}

	int HasSkyDomeNode(lua_State* L)
	{
		Patch::TryInitializeOgre();

		bool hasNode = false;
		auto* sceneManager = GetSceneManager();
		if (sceneManager != nullptr)
		{
			TryHasSkyNode(sceneManager, Ogre::GetSkyDomeNode, hasNode, "skydome");
		}

		lua_pushboolean(L, hasNode);
		return 1;
	}

	int HasSkyPlaneNode(lua_State* L)
	{
		Patch::TryInitializeOgre();

		bool hasNode = false;
		auto* sceneManager = GetSceneManager();
		if (sceneManager != nullptr)
		{
			TryHasSkyNode(sceneManager, Ogre::GetSkyPlaneNode, hasNode, "skyplane");
		}

		lua_pushboolean(L, hasNode);
		return 1;
	}
}
