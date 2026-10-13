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

// The Ogre ABI behind exu.gibs, resolved by mangled export name from
// OgreMain.dll (see OgreProc.h): the dead body's skeleton and node pose, the
// ResourceGroupManager stream/group calls the payload preparation needs, and
// the scene-node/entity operations for the gib pool. Ported from OpenShim's
// skinned_gibs.cpp, native_chunk_runtime.cpp and chunk_proxy_render.cpp
// (commit 72134279c). Plain float arrays throughout (positions {x,y,z},
// orientations {w,x,y,z}) so callers need no Ogre headers.
//
// Every raw engine/Ogre call here is behind SEH and a C++ catch (Util/SehGuard.h); a
// failure returns false/null, and a false from an object operation means the
// Ogre object may be bad and must not be touched again. Ogre object pointers
// are never cached across calls.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ExtraUtilities::OgreGibRuntime
{
	inline constexpr size_t kBoneNameCapacity = 64;

	// One bone's derived pose (model space of the skinned entity).
	struct BoneRead
	{
		char name[kBoneNameCapacity];
		float position[3];
		float orientation[4];
	};

	// ----- Availability -------------------------------------------------------

	// True when every export below resolves. Otherwise names the first missing
	// one (stable, for the capability `reason`).
	bool ExportsAvailable() noexcept;
	const char* FirstMissingExport() noexcept;

	// ----- Scene manager --------------------------------------------------------

	// The active SceneManager, or null.
	void* ReadSceneManager() noexcept;

	// Whether `candidate` begins with a vtable inside OgreMain.dll. The render
	// bridge slots do not always hold a bridge, and a __thiscall on text is a
	// virtual dispatch through it.
	bool LooksLikeOgreObject(const void* candidate) noexcept;

	// ----- The dead body (read-only) ------------------------------------------

	bool ReadEntityMeshIdentity(void* entity, char* outName, size_t nameCapacity, char* outGroup,
		size_t groupCapacity) noexcept;
	void* GetParentSceneNode(void* entity) noexcept;
	bool ReadNodePose(void* node, float outPosition[3], float outOrientation[4], float outScale[3]) noexcept;
	// The skeleton behind an entity and its bone count.
	bool GetSkeleton(void* entity, void*& outSkeleton, uint16_t& outBoneCount) noexcept;
	bool ReadBone(void* skeleton, uint16_t index, BoneRead& out) noexcept;
	bool SetVisible(void* movable, bool visible) noexcept;

	// ----- Resources -----------------------------------------------------------

	// Reads a resource through ResourceGroupManager::openResource into EXU-owned
	// storage. The Ogre stream is released with its owning DLL's destructor and
	// allocator, never the CRT.
	bool ReadResource(const std::string& name, const std::string& group, std::vector<uint8_t>& outBytes) noexcept;

	bool ResourceGroupExists(const std::string& group) noexcept;
	// Whether some already-registered group (a mod's) holds a resource of this
	// file name.
	bool ResourceExistsInAnyGroup(const std::string& name) noexcept;

	// Makes `group` a resource group rooted at `directory` (recursive, read
	// only) and initialises it. A group that already exists is left alone: Ogre
	// keeps it for the process while EXU's own state is rebuilt for every
	// mission. Set `outCreated` when this call created it.
	bool EnsureResourceGroup(const std::string& group, const std::string& directory, bool& outCreated) noexcept;

	// ----- Scene objects (gib pool) ----------------------------------------------

	// The named child of the root scene node that parents every gib node.
	// Returns the existing node when the name is already in the scene.
	void* EnsureNamedChild(void* sceneManager, const std::string& name) noexcept;
	// Whether the scene still holds `node` under `name` (a cleared or replaced
	// scene does not, and everything below it is stale).
	bool NamedChildAlive(void* sceneManager, void* node, const std::string& name) noexcept;
	bool DestroySceneNode(void* sceneManager, void* node) noexcept;

	// A child of `parent` carrying a new hidden entity of `mesh`, at the hidden
	// parking position.
	bool CreateMeshObject(void* sceneManager, void* parent, const std::string& mesh, void*& outNode,
		void*& outEntity) noexcept;
	// Swaps the entity on `node` for one of `mesh`: the old entity is destroyed
	// by the scene manager that made it (between frames, never during a render
	// traversal). The entity is null on failure; the node stays attached.
	bool ReplaceMeshEntity(void* sceneManager, void* node, void*& entity, const std::string& mesh) noexcept;
	bool DestroyEntity(void* sceneManager, void* entity) noexcept;
	bool SetNodeScale(void* node, const float scale[3]) noexcept;
	// Position and orientation only (the per-frame update).
	bool PlaceNode(void* node, const float position[3], const float orientation[4]) noexcept;
	// Places the node and makes the entity visible.
	bool ShowObject(void* node, void* entity, const float position[3], const float orientation[4]) noexcept;
	// Hides the entity and parks the node out of sight (kept for reuse).
	void HideObject(void* node, void* entity) noexcept;

	// ----- Frame listener -----------------------------------------------------

	// Registers a listener object laid out like Ogre::FrameListener 1.10.
	// Returns the Root it was added to, or null.
	void* AddFrameListener(void* listener) noexcept;
	// Queues removal; Ogre drops it at the start of the next frame without
	// calling it. A Root that is no longer the current one is left alone.
	void RemoveFrameListener(void* root, void* listener) noexcept;
}
