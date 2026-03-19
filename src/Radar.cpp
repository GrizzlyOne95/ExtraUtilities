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
#include "LuaHelpers.h"

#include <cmath>

namespace ExtraUtilities::Lua::Radar
{
	namespace
	{
		struct CockpitWireframeScaleBaselines
		{
			float projectionBase = 1.0f;
			int projectionRadius = 1;
			float radarLeftBase = 0.0f;
			float commandPanelLeftBase = 0.0f;
			bool initialized = false;
		};

		CockpitWireframeScaleBaselines& GetCockpitWireframeScaleBaselines()
		{
			static CockpitWireframeScaleBaselines baselines{};
			if (!baselines.initialized)
			{
				baselines.projectionBase = *BZR::Radar::cockpitWireframeProjectionBase;
				if (!std::isfinite(baselines.projectionBase) || baselines.projectionBase <= 0.0f)
				{
					baselines.projectionBase = 1.0f;
				}

				baselines.projectionRadius = *BZR::Radar::cockpitWireframeProjectionRadius;
				if (baselines.projectionRadius < 1)
				{
					baselines.projectionRadius = 1;
				}

				baselines.radarLeftBase = *BZR::Radar::radarLeftBase;
				baselines.commandPanelLeftBase = *BZR::Radar::commandPanelLeftBase;
				baselines.initialized = true;
			}

			return baselines;
		}

		void ApplyCockpitWireframeScale(float scale)
		{
			const CockpitWireframeScaleBaselines& baselines = GetCockpitWireframeScaleBaselines();
			const float originalRadarWidthFromPanel = baselines.commandPanelLeftBase - baselines.radarLeftBase;

			float projectionBase = baselines.projectionBase;
			if (!std::isfinite(projectionBase) || projectionBase <= 0.f)
			{
				projectionBase = 1.f;
			}

			float radarWidthFromPanel = originalRadarWidthFromPanel;
			if (!std::isfinite(radarWidthFromPanel) || radarWidthFromPanel <= 0.f)
			{
				radarWidthFromPanel = projectionBase + 20.f;
			}

			float scaledProjectionBase = projectionBase * scale;
			if (!std::isfinite(scaledProjectionBase) || scaledProjectionBase <= 0.f)
			{
				scaledProjectionBase = projectionBase;
			}

			int scaledProjectionRadius = static_cast<int>(
				std::lround(static_cast<double>(baselines.projectionRadius) * static_cast<double>(scale)));
			if (scaledProjectionRadius < 1)
			{
				scaledProjectionRadius = 1;
			}

			float scaledRadarLeftBase =
				*BZR::Radar::commandPanelLeftBase - (radarWidthFromPanel * scale);
			if (!std::isfinite(scaledRadarLeftBase))
			{
				scaledRadarLeftBase = *BZR::Radar::commandPanelLeftBase - radarWidthFromPanel;
			}

			*BZR::Radar::cockpitWireframeProjectionBase = scaledProjectionBase;
			*BZR::Radar::cockpitWireframeProjectionRadius = scaledProjectionRadius;
			*BZR::Radar::radarLeftBase = scaledRadarLeftBase;
			BZR::Radar::RefreshCockpitWireframeAnchor();
		}

		int AbsoluteIndex(lua_State* L, int idx)
		{
			return idx > 0 ? idx : lua_gettop(L) + idx + 1;
		}

		BZR::Radar::EdgePathPoint CheckEdgePathPoint(lua_State* L, int idx)
		{
			idx = AbsoluteIndex(L, idx);

			BZR::Radar::EdgePathPoint point{};
			if (lua_isuserdata(L, idx))
			{
				BZR::VECTOR_3D vector = CheckVectorOrSingles(L, idx);
				point.x = vector.x;
				point.z = vector.z;
				return point;
			}

			if (!lua_istable(L, idx))
			{
				luaL_typerror(L, idx, "table");
			}

			StackGuard guard(L);

			lua_getfield(L, idx, "x");
			if (!lua_isnil(L, -1))
			{
				point.x = static_cast<float>(luaL_checknumber(L, -1));

				lua_getfield(L, idx, "z");
				if (lua_isnil(L, -1))
				{
					lua_pop(L, 1);
					lua_getfield(L, idx, "y");
				}
				point.z = static_cast<float>(luaL_checknumber(L, -1));
				return point;
			}

			lua_pop(L, 1);
			lua_rawgeti(L, idx, 1);
			point.x = static_cast<float>(luaL_checknumber(L, -1));
			lua_rawgeti(L, idx, 2);
			point.z = static_cast<float>(luaL_checknumber(L, -1));
			return point;
		}

		void InvokeEdgePathRefresh()
		{
			BZR::Radar::RefreshEdgePathBounds(nullptr);
		}

		bool IsMissionLoaded()
		{
			return BZR::GameObject::user_entity_ptr != nullptr && *BZR::GameObject::user_entity_ptr != nullptr;
		}
	}

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

		(void)GetCockpitWireframeScaleBaselines();
		sizeScale.Write(newScale);

		BZR::BZR_Camera* cam = BZR::Camera::View_Record_MainCam;
		if (cam != nullptr && cam->Orig_y > 0.f)
		{
			int screenHeight = static_cast<int>(std::floor(cam->Orig_y)) * 2;
			BZR::Radar::RefreshLayout(screenHeight);
			ApplyCockpitWireframeScale(newScale);
		}

		return 0;
	}

	int RefreshEdgePathBounds(lua_State* L)
	{
		if (!IsMissionLoaded())
		{
			return luaL_error(L, "RefreshEdgePathBounds requires an active mission");
		}

		InvokeEdgePathRefresh();
		return 0;
	}

	int SetEdgePathCoords(lua_State* L)
	{
		if (!IsMissionLoaded())
		{
			return luaL_error(L, "SetEdgePathCoords requires an active mission");
		}

		luaL_checktype(L, 1, LUA_TTABLE);

		BZR::Radar::RuntimePath* path = BZR::Radar::FindNamedPath("edge_path");
		if (path == nullptr)
		{
			return luaL_error(L, "edge_path was not found");
		}

		if (path->pointCount < 0 || (path->pointCount > 0 && path->points == nullptr))
		{
			return luaL_error(L, "edge_path is not in a writable runtime state");
		}

		const int inputCount = static_cast<int>(lua_objlen(L, 1));
		if (inputCount <= 0)
		{
			return luaL_error(L, "SetEdgePathCoords requires a non-empty point array");
		}

		if (inputCount != path->pointCount)
		{
			return luaL_error(
				L,
				"SetEdgePathCoords point count mismatch: expected %d points to match the existing edge_path, got %d",
				path->pointCount,
				inputCount);
		}

		for (int i = 1; i <= inputCount; ++i)
		{
			lua_rawgeti(L, 1, i);
			BZR::Radar::EdgePathPoint point = CheckEdgePathPoint(L, -1);
			lua_pop(L, 1);

			path->points[i - 1] = point;
		}

		InvokeEdgePathRefresh();
		return 0;
	}
}
