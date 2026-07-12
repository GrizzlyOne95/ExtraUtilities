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
#include "ControlPanel.h"
#include "InlinePatch.h"
#include "LuaHelpers.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace ExtraUtilities::Lua::Radar
{
	namespace
	{
		struct CockpitWireframeScaleBaselines
		{
			float projectionBase = 1.0f;
			bool initialized = false;
		};

		struct ScrapPilotHudSnapshot
		{
			int scrapLeft = 0;
			int scrapTop = 0;
			int pilotLeft = 0;
			int pilotTop = 0;
			bool valid = false;
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

				baselines.initialized = true;
			}

			return baselines;
		}

		ScrapPilotHudSnapshot CaptureScrapPilotHudSnapshot() noexcept
		{
			ScrapPilotHudSnapshot snapshot{};
			snapshot.valid = ControlPanel::TryGetScrapPilotHudTopLefts(
				snapshot.scrapLeft,
				snapshot.scrapTop,
				snapshot.pilotLeft,
				snapshot.pilotTop);
			return snapshot;
		}

		void ApplyCockpitWireframeScale(float scale)
		{
			const CockpitWireframeScaleBaselines& baselines = GetCockpitWireframeScaleBaselines();

			float projectionBase = baselines.projectionBase;
			if (!std::isfinite(projectionBase) || projectionBase <= 0.f)
			{
				projectionBase = 1.f;
			}

			float scaledProjectionBase = projectionBase * scale;
			if (!std::isfinite(scaledProjectionBase) || scaledProjectionBase <= 0.f)
			{
				scaledProjectionBase = projectionBase;
			}

			*BZR::Radar::cockpitWireframeProjectionBase = scaledProjectionBase;
			BZR::Radar::RefreshCockpitWireframeAnchor();
		}

		// The engine's RefreshLayout shifts the radar background left by the
		// full projection-radius delta but moves the wireframe centre by only
		// half of it, and it never moves the centre vertically at all, so the
		// mesh and its black underlay only line up at 100% scale. Re-run the
		// layout at scale 1 to learn the intended relative geometry, then run
		// it at the requested scale and place the background and wireframe
		// centre so everything scales concentrically from the radar's
		// bottom-left screen anchor.
		void __cdecl RefreshLayoutConcentric(int screenHeight)
		{
			const CockpitWireframeScaleBaselines& baselines = GetCockpitWireframeScaleBaselines();
			const float requestedScale = *BZR::Radar::scale;
			const float scaledProjectionBase = *BZR::Radar::cockpitWireframeProjectionBase;

			if (!std::isfinite(requestedScale) || std::fabs(requestedScale - 1.0f) < 0.001f)
			{
				BZR::Radar::RefreshLayout(screenHeight);
				return;
			}

			// Reference pass at scale 1.
			*BZR::Radar::scale = 1.0f;
			*BZR::Radar::cockpitWireframeProjectionBase = baselines.projectionBase;
			BZR::Radar::RefreshCockpitWireframeAnchor();
			BZR::Radar::RefreshLayout(screenHeight);
			const int centerX1 = *BZR::Radar::cockpitWireframeCenterX;
			const int centerY1 = *BZR::Radar::cockpitWireframeCenterY;
			const int left1 = *BZR::Radar::radarLeft;
			const int bottom1 = *BZR::Radar::radarBottom;

			// Real pass at the requested scale (background bottom anchoring and
			// the projection radius are already correct here).
			*BZR::Radar::scale = requestedScale;
			*BZR::Radar::cockpitWireframeProjectionBase = scaledProjectionBase;
			BZR::Radar::RefreshCockpitWireframeAnchor();
			BZR::Radar::RefreshLayout(screenHeight);
			const int bottomScaled = *BZR::Radar::radarBottom;

			const auto scaleOffset = [requestedScale](int offset)
			{
				return static_cast<int>(std::lround(requestedScale * static_cast<float>(offset)));
			};

			*BZR::Radar::radarLeft = left1;
			*BZR::Radar::cockpitWireframeCenterX = left1 + scaleOffset(centerX1 - left1);
			*BZR::Radar::cockpitWireframeCenterY = bottomScaled + scaleOffset(centerY1 - bottom1);
		}

		// The engine re-runs RefreshLayout when it (re)builds the cockpit HUD,
		// which would restore the misaligned native layout, so both of its
		// call sites are retargeted at the concentric wrapper above.
		constexpr uintptr_t kRefreshLayoutCallSites[] = { 0x0049325F, 0x0049405B };
		constexpr uintptr_t kRefreshLayoutEntry = 0x00492EC0;

		void InstallRefreshLayoutCallSiteHooks()
		{
			static bool attempted = false;
			if (attempted)
			{
				return;
			}
			attempted = true;

			for (uintptr_t site : kRefreshLayoutCallSites)
			{
				const uint8_t* bytes = reinterpret_cast<const uint8_t*>(site);
				int32_t rel = 0;
				std::memcpy(&rel, bytes + 1, sizeof(rel));
				const uintptr_t target = site + 5 + static_cast<uintptr_t>(rel);
				if (bytes[0] != 0xE8 || target != kRefreshLayoutEntry)
				{
					// Unexpected code — leave the site alone rather than corrupt it.
					continue;
				}

				const int32_t newRel = static_cast<int32_t>(
					reinterpret_cast<uintptr_t>(&RefreshLayoutConcentric) - (site + 5));
				// Leaked by design: the patch must outlive every mission/Lua reload.
				new InlinePatch(site + 1, &newRel, sizeof(newRel), BasicPatch::Status::ACTIVE);
			}
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

	void InstallRefreshLayoutHooks()
	{
		InstallRefreshLayoutCallSiteHooks();
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
		const ScrapPilotHudSnapshot hudSnapshot = CaptureScrapPilotHudSnapshot();

		BZR::BZR_Camera* cam = BZR::Camera::View_Record_MainCam;
		if (cam != nullptr && cam->Orig_y > 0.f)
		{
			int screenHeight = static_cast<int>(std::floor(cam->Orig_y)) * 2;
			// RefreshLayout derives both the 3D projection radius and the HUD
			// underlay bounds from the projection base. Set the scaled base and
			// its anchor first, then run the concentric layout wrapper so the
			// wireframe and its background underlay stay aligned at any scale.
			InstallRefreshLayoutCallSiteHooks();
			ApplyCockpitWireframeScale(newScale);
			RefreshLayoutConcentric(screenHeight);
			if (hudSnapshot.valid)
			{
				ControlPanel::RestoreScrapPilotHudTopLefts(
					hudSnapshot.scrapLeft,
					hudSnapshot.scrapTop,
					hudSnapshot.pilotLeft,
					hudSnapshot.pilotTop);
			}
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
