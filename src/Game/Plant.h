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

// Plants: objects whose ODF has [PlantClass] isPlant = 1 (or that a mission
// flags with exu.SetPlant) keep their normal collision but stop reading as
// buildings: no bump sound, no chips and a plant hit explosion when shot, and
// a plant death explosion instead of the building death sequence. Design and
// addresses: Docs/Research/PLANT_COLLISION_RE_20261013.md, Docs/PLANTS.md.

#include "Game/PlantCore.h"

struct lua_State;

namespace ExtraUtilities::Plant
{
	// Installs the four entry detours (ClsnSounds, Ordnance::Hit,
	// ChunkEffect::CreateChunklet, Building::Explode). All or nothing: fails
	// closed (returns false and leaves the engine untouched) on any build or
	// preimage mismatch. Call once the build gate passed and deferred patches
	// are enabled.
	bool Install() noexcept;
	// Restores the engine entries and clears all state. Safe when not installed.
	void Shutdown() noexcept;
	bool IsActive() noexcept;
	// Drops the per-ODF cache and every exu.SetPlant override (mission lifetime).
	void ResetMissionState() noexcept;

	// Effective verdict for an ODF base name (override first, then the ODF).
	// Game thread. The ODF is only read on a supported build.
	bool QueryPlant(const char* odf);
	// overrideState: -1 removes the override, 0 forces not-a-plant, 1 forces plant.
	void SetOverride(const char* odf, int overrideState);

	// exu.SetPlant(odf, enabled) and exu.IsPlant(odf) (PlantApi.cpp).
	int SetPlant(lua_State* L);
	int IsPlant(lua_State* L);
}
