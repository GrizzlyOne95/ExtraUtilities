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

#pragma once

#include "bzr.h"
#include "Scanner.h"

#include <lua.hpp>

namespace ExtraUtilities::Lua::Radar
{
	inline Scanner state(BZR::Radar::state);
	inline Scanner sizeScale(BZR::Radar::scale);

	// Retargets the engine's RefreshLayout call sites at the concentric
	// wrapper so radar mesh and background stay aligned at any size scale.
	// Idempotent; safe to call every Init.
	void InstallRefreshLayoutHooks();

	// 1 = radar
	// 0 = minimap
	int SetPresentation(lua_State* L);
    int SetTerrain(lua_State* L);
    int SetMesh(lua_State* L);
    void DisableTerrain() noexcept;
    void ResetTerrainState() noexcept;
    bool IsPresentationActive() noexcept;
    uint64_t ProjectPoint(const float* point, float range);
    bool ProjectPointExact(const float* point, float range, float& x, float& y);
    // Shrinks this frame's projection so a dish of `extent` ranges stays on
    // screen. The layout builder rewrites the radius before every radar draw.
    void FitProjection(float extent);
	int GetLayout(lua_State* L);
	int GetState(lua_State* L);
	int SetState(lua_State* L);
	int GetSizeScale(lua_State* L);
	int SetSizeScale(lua_State* L);
	int RefreshEdgePathBounds(lua_State* L);
	int SetEdgePathCoords(lua_State* L);
}
