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

// Payload preparation for exu.gibs (ported from OpenShim's
// native_chunk_runtime.cpp prepareGibs, commit 72134279c, minus authored
// gibs.txt payloads): reads a dead body's mesh and skeleton through Ogre,
// splits it with the vendored NativeChunks code (or loads the cached split),
// and describes the pieces. Owns the disk cache and the ExuGibPayloads
// resource group that lets Ogre load the cached piece meshes.
//
//   cache root : %LOCALAPPDATA%\ExtraUtilities\cache\
//   pieces     : <root>\gibs\v6\<fnv1a64(mesh + skeleton) hex>\<piece>.mesh
//   materials  : <root>\openshim_gib_flesh*.material and the textures they use
//
// The first death of a model pays for the split; the result is remembered for
// the mission (a failure too, so a bad mesh is not retried per death).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ExtraUtilities::Gibs::Payloads
{
	inline constexpr const char* kResourceGroup = "ExuGibPayloads";

	struct PieceInfo
	{
		std::string resource;   // gibs/v6/<hash>/<piece>.mesh, relative to the cache root
		std::string boneName;   // driving bone
		uint16_t bone = 0;      // its index in the skeleton
		float offset[3] = {};   // piece centre in the bone frame (the pivot)
		float radius = 0.0f;
		bool weapon = false;
	};

	struct ModelInfo
	{
		std::vector<PieceInfo> pieces;
		std::string source;     // the cache folder
	};

	// Splits (or loads) the model for this mesh. Null when the mesh cannot be
	// split; that is remembered until ResetModels.
	std::shared_ptr<const ModelInfo> Prepare(const char* meshName, const char* group) noexcept;

	// Mission lifetime: forgets the prepared models (the disk cache stays).
	void ResetModels() noexcept;
	size_t ModelCount() noexcept;

	// Writes the default flesh materials, creates the cache root and registers
	// the ExuGibPayloads resource group, once per process. False when the
	// group could not be made (then no piece mesh can load).
	bool EnsureResourceGroup() noexcept;
}
