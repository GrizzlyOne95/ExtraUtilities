/* Copyright (C) 2026 GrizzlyOne95
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

// exu.pathing: path-grid footprint from collision faces for ODF-flagged
// buildings (pathBlock = "faces" | "none"). The stock code blocks the whole
// oriented SDF bbox of every building in the AI path grid (BlockCells); EXU
// redirects every call of BlockCells to a stub that runs the stock function
// and then recomputes, from scratch, the cells of any region a flagged object
// touches. Refresh() does the same for objects blocked before EXU loaded.
// Contract (shared with OpenShim): Docs/PATH_BLOCK.md.

#include <cstdint>
#include <string>

struct lua_State;

namespace ExtraUtilities::PathBlock
{
	struct ObjectReport
	{
		char odf[16] = {};
		// Configured in the ODF, and what is in force (box while disabled or
		// when the faces could not be read).
		const char* configured = "box";
		const char* effective = "box";
		// Cells whose centre is inside the oriented bbox, and of those the
		// cells the effective mode blocks.
		int cellsInBox = 0;
		int cellsBlocked = 0;
		int triangles = 0;
		bool inGrid = false; // the object has a stock area record
	};

	struct Capabilities
	{
		bool available = false;  // qualified build
		int hookedSites = 0;     // BlockCells call sites redirected
		int hookSites = 0;       // call sites known
		bool gridReady = false;  // the path grid exists
		float cellSize = 0.0f;
		bool enabled = true;
		// OpenShim (or another module) detoured BlockCells: EXU hooks and
		// recomputes nothing; owner names who applies the footprints
		// ("exu", the detour's module, or "none").
		bool standDown = false;
		char owner[64] = {};
	};

	// Game thread only.
	void ResetMissionState() noexcept;
	// After the deferred patches are activated: logs the hook state and
	// re-applies flagged objects already in the grid (EXU usually loads after
	// the map PostLoad pass).
	void OnInit() noexcept;
	// Recomputes the regions of every flagged object. Returns how many flagged
	// objects were re-applied, or -1 when the grid or build is unavailable.
	int Refresh() noexcept;
	// false: flagged objects go back to the stock box (re-applied now).
	int SetEnabled(bool enabled) noexcept;
	bool IsEnabled() noexcept;
	bool GetObjectReport(void* gameObject, ObjectReport& out) noexcept;
	// ASCII dump of the grid around a world point. Empty when unavailable.
	std::string DumpGrid(float x, float z, float radius);
	void GetCapabilities(Capabilities& out) noexcept;

	// exu.pathing (PathBlockApi.cpp).
	void Install(lua_State* L);
}
