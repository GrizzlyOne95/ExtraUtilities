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

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Ogre/OgreGibRuntime.h"

#include "Ogre/OgreProc.h"
#include "Util/Logging.h"
#include "Util/SehGuard.h"
#include "bzr.h"

#include <Windows.h>

#include <array>
#include <cstring>

namespace ExtraUtilities::OgreGibRuntime
{
	namespace
	{
		using ExtraUtilities::OgreDll::OgreProc;

		struct OgreV3 { float x, y, z; };
		struct OgreQ { float w, x, y, z; };

		// Redux's two-word SharedPtr ABI (rep, SharedPtrInfo*).
		struct SharedPtrAbi
		{
			void* rep = nullptr;
			void* info = nullptr;
		};
		static_assert(sizeof(SharedPtrAbi) == 8);

		constexpr float kParkedY = -100000.0f;

		using GetSkeletonFn = void* (__thiscall*)(void*);
		using GetNumBonesFn = uint16_t(__thiscall*)(void*);
		using GetBoneFn = void* (__thiscall*)(void*, uint16_t);
		using GetNameFn = const std::string* (__thiscall*)(void*);
		using GetVector3Fn = const OgreV3* (__thiscall*)(void*);
		using GetQuaternionFn = const OgreQ* (__thiscall*)(void*);
		using GetParentNodeFn = void* (__thiscall*)(void*);
		using SetVisibleFn = void(__thiscall*)(void*, bool);
		using GetMeshFn = const SharedPtrAbi* (__thiscall*)(void*);
		using SingletonFn = void* (__cdecl*)();
		using OpenResourceFn = SharedPtrAbi* (__thiscall*)(void*, SharedPtrAbi*, const std::string&, const std::string&, bool, void*);
		using StreamReadFn = size_t(__thiscall*)(void*, void*, size_t);
		using DeallocateFn = void(__cdecl*)(void*);
		using DestructorFn = void(__thiscall*)(void*, unsigned);
		using GroupExistsFn = bool(__thiscall*)(void*, const std::string&);
		using CreateGroupFn = void(__thiscall*)(void*, const std::string&, bool);
		using AddLocationFn = void(__thiscall*)(void*, const std::string&, const std::string&, const std::string&, bool, bool);
		using InitialiseGroupFn = void(__thiscall*)(void*, const std::string&);
		using GetRootNodeFn = void* (__thiscall*)(void*);
		using CreateNamedChildFn = void* (__thiscall*)(void*, const std::string&, const OgreV3&, const OgreQ&);
		using CreateChildFn = void* (__thiscall*)(void*, const OgreV3&, const OgreQ&);
		using HasSceneNodeFn = bool(__thiscall*)(void*, const std::string&);
		using GetSceneNodeFn = void* (__thiscall*)(void*, const std::string&);
		using DestroySceneNodeFn = void(__thiscall*)(void*, void*);
		using CreateEntityFn = void* (__thiscall*)(void*, const std::string&);
		using DestroyEntityFn = void(__thiscall*)(void*, void*);
		using AttachObjectFn = void(__thiscall*)(void*, void*);
		using NodeSetVectorFn = void(__thiscall*)(void*, const OgreV3&);
		using NodeSetQuaternionFn = void(__thiscall*)(void*, const OgreQ&);
		using RootAddListenerFn = void(__thiscall*)(void*, void*);

#define EXU_STD_STRING "V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@"

		// ----- Exports -------------------------------------------------------
		constinit OgreProc<GetSkeletonFn> g_getSkeleton{ "?getSkeleton@Entity@Ogre@@QBEPAVSkeletonInstance@2@XZ" };
		constinit OgreProc<GetNumBonesFn> g_getNumBones{ "?getNumBones@Skeleton@Ogre@@UBEGXZ" };
		constinit OgreProc<GetBoneFn> g_getBone{ "?getBone@Skeleton@Ogre@@UBEPAVBone@2@G@Z" };
		constinit OgreProc<GetNameFn> g_getNodeName{ "?getName@Node@Ogre@@QBEAB" EXU_STD_STRING "XZ" };
		constinit OgreProc<GetVector3Fn> g_derivedPosition{ "?_getDerivedPosition@Node@Ogre@@UBEABVVector3@2@XZ" };
		constinit OgreProc<GetQuaternionFn> g_derivedOrientation{ "?_getDerivedOrientation@Node@Ogre@@UBEABVQuaternion@2@XZ" };
		constinit OgreProc<GetVector3Fn> g_derivedScale{ "?_getDerivedScale@Node@Ogre@@UBEABVVector3@2@XZ" };
		constinit OgreProc<GetParentNodeFn> g_parentSceneNode{ "?getParentSceneNode@MovableObject@Ogre@@UBEPAVSceneNode@2@XZ" };
		constinit OgreProc<SetVisibleFn> g_setVisible{ "?setVisible@MovableObject@Ogre@@UAEX_N@Z" };
		constinit OgreProc<GetMeshFn> g_getMesh{ "?getMesh@Entity@Ogre@@QBEABV?$SharedPtr@VMesh@Ogre@@@2@XZ" };
		constinit OgreProc<GetNameFn> g_resourceName{ "?getName@Resource@Ogre@@UBEAB" EXU_STD_STRING "XZ" };
		constinit OgreProc<GetNameFn> g_resourceGroup{ "?getGroup@Resource@Ogre@@UBEAB" EXU_STD_STRING "XZ" };
		constinit OgreProc<SingletonFn> g_groupManager{ "?getSingletonPtr@ResourceGroupManager@Ogre@@SAPAV12@XZ" };
		constinit OgreProc<OpenResourceFn> g_openResource{
			"?openResource@ResourceGroupManager@Ogre@@QAE?AV?$SharedPtr@VDataStream@Ogre@@@2@AB" EXU_STD_STRING "0_NPAVResource@2@@Z" };
		constinit OgreProc<DeallocateFn> g_deallocateBytes{ "?deallocateBytes@StdAllocPolicy@Ogre@@SAXPAX@Z" };
		constinit OgreProc<GroupExistsFn> g_groupExists{ "?resourceGroupExists@ResourceGroupManager@Ogre@@QAE_NAB" EXU_STD_STRING "@Z" };
		constinit OgreProc<GroupExistsFn> g_existsInAnyGroup{ "?resourceExistsInAnyGroup@ResourceGroupManager@Ogre@@QAE_NAB" EXU_STD_STRING "@Z" };
		constinit OgreProc<CreateGroupFn> g_createGroup{ "?createResourceGroup@ResourceGroupManager@Ogre@@QAEXAB" EXU_STD_STRING "_N@Z" };
		constinit OgreProc<AddLocationFn> g_addLocation{ "?addResourceLocation@ResourceGroupManager@Ogre@@QAEXAB" EXU_STD_STRING "00_N1@Z" };
		constinit OgreProc<InitialiseGroupFn> g_initialiseGroup{ "?initialiseResourceGroup@ResourceGroupManager@Ogre@@QAEXAB" EXU_STD_STRING "@Z" };
		constinit OgreProc<GetRootNodeFn> g_getRootSceneNode{ "?getRootSceneNode@SceneManager@Ogre@@UAEPAVSceneNode@2@XZ" };
		constinit OgreProc<CreateNamedChildFn> g_createNamedChild{
			"?createChildSceneNode@SceneNode@Ogre@@UAEPAV12@AB" EXU_STD_STRING "ABVVector3@2@ABVQuaternion@2@@Z" };
		constinit OgreProc<CreateChildFn> g_createChild{ "?createChildSceneNode@SceneNode@Ogre@@UAEPAV12@ABVVector3@2@ABVQuaternion@2@@Z" };
		constinit OgreProc<HasSceneNodeFn> g_hasSceneNode{ "?hasSceneNode@SceneManager@Ogre@@UBE_NAB" EXU_STD_STRING "@Z" };
		constinit OgreProc<GetSceneNodeFn> g_getSceneNode{ "?getSceneNode@SceneManager@Ogre@@UBEPAVSceneNode@2@AB" EXU_STD_STRING "@Z" };
		constinit OgreProc<DestroySceneNodeFn> g_destroySceneNode{ "?destroySceneNode@SceneManager@Ogre@@UAEXPAVSceneNode@2@@Z" };
		constinit OgreProc<CreateEntityFn> g_createEntity{ "?createEntity@SceneManager@Ogre@@UAEPAVEntity@2@AB" EXU_STD_STRING "@Z" };
		constinit OgreProc<DestroyEntityFn> g_destroyEntity{ "?destroyEntity@SceneManager@Ogre@@UAEXPAVEntity@2@@Z" };
		constinit OgreProc<AttachObjectFn> g_attachObject{ "?attachObject@SceneNode@Ogre@@UAEXPAVMovableObject@2@@Z" };
		constinit OgreProc<NodeSetVectorFn> g_setPosition{ "?setPosition@Node@Ogre@@UAEXABVVector3@2@@Z" };
		constinit OgreProc<NodeSetQuaternionFn> g_setOrientation{ "?setOrientation@Node@Ogre@@UAEXABVQuaternion@2@@Z" };
		constinit OgreProc<NodeSetVectorFn> g_setScale{ "?setScale@Node@Ogre@@UAEXABVVector3@2@@Z" };
		constinit OgreProc<SingletonFn> g_rootSingleton{ "?getSingletonPtr@Root@Ogre@@SAPAV12@XZ" };
		constinit OgreProc<RootAddListenerFn> g_addFrameListener{ "?addFrameListener@Root@Ogre@@QAEXPAVFrameListener@2@@Z" };
		constinit OgreProc<RootAddListenerFn> g_removeFrameListener{ "?removeFrameListener@Root@Ogre@@QAEXPAVFrameListener@2@@Z" };

#undef EXU_STD_STRING

		template <typename Proc>
		bool Has(const Proc& proc) noexcept
		{
			return static_cast<bool>(proc);
		}

		// Order is the order `reason` reports a miss in.
		const char* FindMissing() noexcept
		{
			struct Entry { const char* name; bool present; };
			const Entry table[] = {
				{ "Entity::getSkeleton", Has(g_getSkeleton) },
				{ "Skeleton::getNumBones", Has(g_getNumBones) },
				{ "Skeleton::getBone", Has(g_getBone) },
				{ "Node::getName", Has(g_getNodeName) },
				{ "Node::_getDerivedPosition", Has(g_derivedPosition) },
				{ "Node::_getDerivedOrientation", Has(g_derivedOrientation) },
				{ "Node::_getDerivedScale", Has(g_derivedScale) },
				{ "MovableObject::getParentSceneNode", Has(g_parentSceneNode) },
				{ "MovableObject::setVisible", Has(g_setVisible) },
				{ "Entity::getMesh", Has(g_getMesh) },
				{ "Resource::getName", Has(g_resourceName) },
				{ "Resource::getGroup", Has(g_resourceGroup) },
				{ "ResourceGroupManager::getSingletonPtr", Has(g_groupManager) },
				{ "ResourceGroupManager::openResource", Has(g_openResource) },
				{ "StdAllocPolicy::deallocateBytes", Has(g_deallocateBytes) },
				{ "ResourceGroupManager::resourceGroupExists", Has(g_groupExists) },
				{ "ResourceGroupManager::resourceExistsInAnyGroup", Has(g_existsInAnyGroup) },
				{ "ResourceGroupManager::createResourceGroup", Has(g_createGroup) },
				{ "ResourceGroupManager::addResourceLocation", Has(g_addLocation) },
				{ "ResourceGroupManager::initialiseResourceGroup", Has(g_initialiseGroup) },
				{ "SceneManager::getRootSceneNode", Has(g_getRootSceneNode) },
				{ "SceneNode::createChildSceneNode(name)", Has(g_createNamedChild) },
				{ "SceneNode::createChildSceneNode", Has(g_createChild) },
				{ "SceneManager::hasSceneNode", Has(g_hasSceneNode) },
				{ "SceneManager::getSceneNode", Has(g_getSceneNode) },
				{ "SceneManager::destroySceneNode", Has(g_destroySceneNode) },
				{ "SceneManager::createEntity", Has(g_createEntity) },
				{ "SceneManager::destroyEntity", Has(g_destroyEntity) },
				{ "SceneNode::attachObject", Has(g_attachObject) },
				{ "Node::setPosition", Has(g_setPosition) },
				{ "Node::setOrientation", Has(g_setOrientation) },
				{ "Node::setScale", Has(g_setScale) },
				{ "Root::getSingletonPtr", Has(g_rootSingleton) },
				{ "Root::addFrameListener", Has(g_addFrameListener) },
				{ "Root::removeFrameListener", Has(g_removeFrameListener) },
			};
			for (const Entry& entry : table)
			{
				if (!entry.present)
				{
					return entry.name;
				}
			}
			return nullptr;
		}

		// A Redux SharedPtr<DataStream> release: the owning DLL's virtual
		// destructor and allocator, including the final reference, never the CRT.
		void ReleaseStream(SharedPtrAbi& p, DeallocateFn deallocate)
		{
			if (p.rep == nullptr || p.info == nullptr)
			{
				p = {};
				return;
			}
			auto* count = reinterpret_cast<volatile LONG*>(static_cast<uint8_t*>(p.info) + 4);
			if (InterlockedDecrement(count) == 0)
			{
				auto destructor = reinterpret_cast<DestructorFn>((*static_cast<void***>(p.info))[0]);
				destructor(p.info, 0);
				deallocate(p.info);
			}
			p = {};
		}

		bool ReadStreamBody(void* manager, const std::string& name, const std::string& group, std::vector<uint8_t>& bytes)
		{
			const OpenResourceFn open = g_openResource.Get();
			const DeallocateFn deallocate = g_deallocateBytes.Get();
			if (open == nullptr || deallocate == nullptr)
			{
				return false;
			}
			SharedPtrAbi stream;
			try
			{
				open(manager, &stream, name, group, false, nullptr);
				if (stream.rep == nullptr)
				{
					return false;
				}
				// Redux's DataStream ABI: isReadable, isWriteable, deleting
				// destructor, read (vtable slot 3). Read into EXU-owned storage;
				// getAsString returns an Ogre-owned STL allocation whose
				// large-buffer alignment differs from ours.
				const auto read = reinterpret_cast<StreamReadFn>((*static_cast<void***>(stream.rep))[3]);
				std::array<uint8_t, 16384> block{};
				bytes.clear();
				for (;;)
				{
					const size_t n = read(stream.rep, block.data(), block.size());
					if (n > block.size() || bytes.size() + n > 64u * 1024u * 1024u)
					{
						ReleaseStream(stream, deallocate);
						bytes.clear();
						return false;
					}
					if (n == 0)
					{
						break;
					}
					bytes.insert(bytes.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(n));
				}
				ReleaseStream(stream, deallocate);
				return !bytes.empty();
			}
			catch (...)
			{
				if (stream.rep != nullptr)
				{
					ReleaseStream(stream, deallocate);
				}
				bytes.clear();
				return false;
			}
		}
	}

	// ----- Availability -------------------------------------------------------------

	bool ExportsAvailable() noexcept
	{
		return FindMissing() == nullptr;
	}

	const char* FirstMissingExport() noexcept
	{
		return FindMissing();
	}

	// ----- Scene manager --------------------------------------------------------------

	void* ReadSceneManager() noexcept
	{
		__try
		{
			void* structure = *BZR::Ogre::sceneManagerStructure;
			if (structure == nullptr)
			{
				return nullptr;
			}
			return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(structure) + BZR::Ogre::sceneManagerOffset);
		}
		__except (Seh::Filter(GetExceptionCode()))
		{
			return nullptr;
		}
	}

	bool LooksLikeOgreObject(const void* candidate) noexcept
	{
		const uintptr_t address = reinterpret_cast<uintptr_t>(candidate);
		// Cheap shape rejects first: null, misaligned, or in the bottom 64K.
		if (address < 0x00010000u || (address % sizeof(void*)) != 0)
		{
			return false;
		}
		// Only a mapped module settles it: asking before OgreMain loads is
		// timing, not an answer.
		const HMODULE ogreMain = OgreDll::GetOgreMainModule();
		if (ogreMain == nullptr)
		{
			return false;
		}

		__try
		{
			const auto* moduleBytes = reinterpret_cast<const uint8_t*>(ogreMain);
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBytes);
			if (dos->e_magic != IMAGE_DOS_SIGNATURE)
			{
				return false;
			}
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(moduleBytes + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfImage == 0)
			{
				return false;
			}
			const uintptr_t base = reinterpret_cast<uintptr_t>(moduleBytes);
			const uintptr_t end = base + nt->OptionalHeader.SizeOfImage;

			MEMORY_BASIC_INFORMATION mbi{};
			if (VirtualQuery(candidate, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT ||
				(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
			{
				return false;
			}
			const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(candidate);
			return vtable >= base && vtable < end;
		}
		__except (Seh::Filter(GetExceptionCode()))
		{
			return false;
		}
	}

	// ----- The dead body ------------------------------------------------------------------

	bool ReadEntityMeshIdentity(void* entity, char* outName, size_t nameCapacity, char* outGroup,
		size_t groupCapacity) noexcept
	{
		if (outName == nullptr || nameCapacity == 0 || outGroup == nullptr || groupCapacity == 0)
		{
			return false;
		}
		outName[0] = '\0';
		outGroup[0] = '\0';
		const GetMeshFn getMesh = g_getMesh.Get();
		const GetNameFn getName = g_resourceName.Get();
		const GetNameFn getGroup = g_resourceGroup.Get();
		if (entity == nullptr || getMesh == nullptr || getName == nullptr || getGroup == nullptr)
		{
			return false;
		}
		bool ok = false;
		Seh::Guard("OgreGibRuntime::ReadEntityMeshIdentity", [&] {
			const SharedPtrAbi* mesh = getMesh(entity);
			if (mesh == nullptr || mesh->rep == nullptr)
			{
				return;
			}
			const std::string* name = getName(mesh->rep);
			const std::string* group = getGroup(mesh->rep);
			if (name == nullptr || group == nullptr || name->empty() || name->size() >= nameCapacity ||
				group->size() >= groupCapacity)
			{
				return;
			}
			std::memcpy(outName, name->c_str(), name->size() + 1);
			std::memcpy(outGroup, group->c_str(), group->size() + 1);
			ok = true;
		});
		if (!ok)
		{
			outName[0] = '\0';
			outGroup[0] = '\0';
		}
		return ok;
	}

	void* GetParentSceneNode(void* entity) noexcept
	{
		const GetParentNodeFn fn = g_parentSceneNode.Get();
		if (entity == nullptr || fn == nullptr)
		{
			return nullptr;
		}
		__try
		{
			return fn(entity);
		}
		__except (Seh::Filter(GetExceptionCode()))
		{
			return nullptr;
		}
	}

	bool ReadNodePose(void* node, float outPosition[3], float outOrientation[4], float outScale[3]) noexcept
	{
		const GetVector3Fn position = g_derivedPosition.Get();
		const GetQuaternionFn orientation = g_derivedOrientation.Get();
		const GetVector3Fn scale = g_derivedScale.Get();
		if (node == nullptr || position == nullptr || orientation == nullptr)
		{
			return false;
		}
		__try
		{
			const OgreV3* p = position(node);
			const OgreQ* q = orientation(node);
			if (p == nullptr || q == nullptr)
			{
				return false;
			}
			outPosition[0] = p->x;
			outPosition[1] = p->y;
			outPosition[2] = p->z;
			outOrientation[0] = q->w;
			outOrientation[1] = q->x;
			outOrientation[2] = q->y;
			outOrientation[3] = q->z;
			outScale[0] = outScale[1] = outScale[2] = 1.0f;
			if (scale != nullptr)
			{
				const OgreV3* s = scale(node);
				if (s != nullptr)
				{
					outScale[0] = s->x;
					outScale[1] = s->y;
					outScale[2] = s->z;
				}
			}
			return true;
		}
		__except (Seh::Filter(GetExceptionCode()))
		{
			return false;
		}
	}

	bool GetSkeleton(void* entity, void*& outSkeleton, uint16_t& outBoneCount) noexcept
	{
		outSkeleton = nullptr;
		outBoneCount = 0;
		const GetSkeletonFn getSkeleton = g_getSkeleton.Get();
		const GetNumBonesFn getNumBones = g_getNumBones.Get();
		if (entity == nullptr || getSkeleton == nullptr || getNumBones == nullptr)
		{
			return false;
		}
		__try
		{
			outSkeleton = getSkeleton(entity);
			if (outSkeleton == nullptr)
			{
				return false;
			}
			outBoneCount = getNumBones(outSkeleton);
			return outBoneCount > 0;
		}
		__except (Seh::Filter(GetExceptionCode()))
		{
			outSkeleton = nullptr;
			outBoneCount = 0;
			return false;
		}
	}

	bool ReadBone(void* skeleton, uint16_t index, BoneRead& out) noexcept
	{
		out.name[0] = '\0';
		const GetBoneFn getBone = g_getBone.Get();
		const GetNameFn getName = g_getNodeName.Get();
		const GetVector3Fn position = g_derivedPosition.Get();
		const GetQuaternionFn orientation = g_derivedOrientation.Get();
		if (skeleton == nullptr || getBone == nullptr || getName == nullptr || position == nullptr || orientation == nullptr)
		{
			return false;
		}
		bool ok = false;
		Seh::Guard("OgreGibRuntime::ReadBone", [&] {
			void* const bone = getBone(skeleton, index);
			if (bone == nullptr)
			{
				return;
			}
			const std::string* name = getName(bone);
			if (name != nullptr && name->size() < sizeof(out.name))
			{
				std::memcpy(out.name, name->c_str(), name->size() + 1);
			}
			const OgreV3* p = position(bone);
			const OgreQ* q = orientation(bone);
			if (p == nullptr || q == nullptr)
			{
				return;
			}
			out.position[0] = p->x;
			out.position[1] = p->y;
			out.position[2] = p->z;
			out.orientation[0] = q->w;
			out.orientation[1] = q->x;
			out.orientation[2] = q->y;
			out.orientation[3] = q->z;
			ok = true;
		});
		return ok;
	}

	bool SetVisible(void* movable, bool visible) noexcept
	{
		const SetVisibleFn fn = g_setVisible.Get();
		if (movable == nullptr || fn == nullptr)
		{
			return false;
		}
		__try
		{
			fn(movable, visible);
			return true;
		}
		__except (Seh::Filter(GetExceptionCode()))
		{
			return false;
		}
	}

	// ----- Resources -------------------------------------------------------------------------

	bool ReadResource(const std::string& name, const std::string& group, std::vector<uint8_t>& outBytes) noexcept
	{
		outBytes.clear();
		const SingletonFn singleton = g_groupManager.Get();
		void* manager = nullptr;
		bool ok = false;
		Seh::Guard("OgreGibRuntime::ReadResource", [&] {
			manager = singleton != nullptr ? singleton() : nullptr;
			if (manager != nullptr)
			{
				ok = ReadStreamBody(manager, name, group, outBytes);
			}
		});
		if (!ok)
		{
			outBytes.clear();
		}
		return ok;
	}

	bool ResourceGroupExists(const std::string& group) noexcept
	{
		const SingletonFn singleton = g_groupManager.Get();
		const GroupExistsFn exists = g_groupExists.Get();
		if (singleton == nullptr || exists == nullptr)
		{
			return false;
		}
		bool found = false;
		Seh::Guard("OgreGibRuntime::ResourceGroupExists", [&] {
			void* manager = singleton();
			found = manager != nullptr && exists(manager, group);
		});
		return found;
	}

	bool ResourceExistsInAnyGroup(const std::string& name) noexcept
	{
		const SingletonFn singleton = g_groupManager.Get();
		const GroupExistsFn exists = g_existsInAnyGroup.Get();
		if (singleton == nullptr || exists == nullptr)
		{
			return false;
		}
		bool found = false;
		Seh::Guard("OgreGibRuntime::ResourceExistsInAnyGroup", [&] {
			void* manager = singleton();
			found = manager != nullptr && exists(manager, name);
		});
		return found;
	}

	bool EnsureResourceGroup(const std::string& group, const std::string& directory, bool& outCreated) noexcept
	{
		outCreated = false;
		const SingletonFn singleton = g_groupManager.Get();
		const GroupExistsFn exists = g_groupExists.Get();
		const CreateGroupFn create = g_createGroup.Get();
		const AddLocationFn addLocation = g_addLocation.Get();
		const InitialiseGroupFn initialise = g_initialiseGroup.Get();
		if (singleton == nullptr || exists == nullptr || create == nullptr || addLocation == nullptr || initialise == nullptr)
		{
			return false;
		}
		bool ok = false;
		Seh::Guard("OgreGibRuntime::EnsureResourceGroup", [&] {
			void* manager = singleton();
			if (manager == nullptr)
			{
				return;
			}
			if (exists(manager, group))
			{
				ok = true;
				return;
			}
			const std::string locationType = "FileSystem";
			create(manager, group, false);
			outCreated = true;
			addLocation(manager, directory, locationType, group, true, true);
			initialise(manager, group);
			ok = true;
		});
		return ok;
	}

	// ----- Scene objects --------------------------------------------------------------------

	void* EnsureNamedChild(void* sceneManager, const std::string& name) noexcept
	{
		if (sceneManager == nullptr)
		{
			return nullptr;
		}
		void* node = nullptr;
		Seh::Guard("OgreGibRuntime::EnsureNamedChild", [&] {
			if (g_hasSceneNode.Get()(sceneManager, name))
			{
				node = g_getSceneNode.Get()(sceneManager, name);
				return;
			}
			void* root = g_getRootSceneNode.Get()(sceneManager);
			if (root != nullptr)
			{
				node = g_createNamedChild.Get()(root, name, OgreV3{ 0.0f, 0.0f, 0.0f }, OgreQ{ 1.0f, 0.0f, 0.0f, 0.0f });
			}
		});
		return node;
	}

	bool NamedChildAlive(void* sceneManager, void* node, const std::string& name) noexcept
	{
		if (sceneManager == nullptr || node == nullptr)
		{
			return false;
		}
		bool alive = false;
		Seh::Guard("OgreGibRuntime::NamedChildAlive", [&] {
			alive = g_hasSceneNode.Get()(sceneManager, name) && g_getSceneNode.Get()(sceneManager, name) == node;
		});
		return alive;
	}

	bool DestroySceneNode(void* sceneManager, void* node) noexcept
	{
		if (sceneManager == nullptr || node == nullptr)
		{
			return false;
		}
		return Seh::Guard("OgreGibRuntime::DestroySceneNode", [&] { g_destroySceneNode.Get()(sceneManager, node); });
	}

	bool CreateMeshObject(void* sceneManager, void* parent, const std::string& mesh, void*& outNode, void*& outEntity) noexcept
	{
		outNode = nullptr;
		outEntity = nullptr;
		if (sceneManager == nullptr || parent == nullptr || mesh.empty())
		{
			return false;
		}
		void* node = nullptr;
		void* entity = nullptr;
		const bool completed = Seh::Guard("OgreGibRuntime::CreateMeshObject", [&] {
			node = g_createChild.Get()(parent, OgreV3{ 0.0f, kParkedY, 0.0f }, OgreQ{ 1.0f, 0.0f, 0.0f, 0.0f });
			if (node == nullptr)
			{
				return;
			}
			entity = g_createEntity.Get()(sceneManager, mesh);
			if (entity == nullptr)
			{
				return;
			}
			g_attachObject.Get()(node, entity);
			g_setVisible.Get()(entity, false);
		});
		if (completed && node != nullptr && entity != nullptr)
		{
			outNode = node;
			outEntity = entity;
			return true;
		}
		// Partial: undo whatever was made. A node left without its entity would
		// otherwise stay attached until scene teardown.
		Seh::Guard("OgreGibRuntime::CreateMeshObjectUndo", [&] {
			if (entity != nullptr)
			{
				g_destroyEntity.Get()(sceneManager, entity);
			}
			if (node != nullptr)
			{
				g_destroySceneNode.Get()(sceneManager, node);
			}
		});
		return false;
	}

	bool ReplaceMeshEntity(void* sceneManager, void* node, void*& entity, const std::string& mesh) noexcept
	{
		if (sceneManager == nullptr || node == nullptr || mesh.empty())
		{
			return false;
		}
		void* const old = entity;
		void* created = nullptr;
		const bool completed = Seh::Guard("OgreGibRuntime::ReplaceMeshEntity", [&] {
			if (old != nullptr && LooksLikeOgreObject(old))
			{
				g_destroyEntity.Get()(sceneManager, old);
			}
			created = g_createEntity.Get()(sceneManager, mesh);
			if (created != nullptr)
			{
				g_attachObject.Get()(node, created);
				g_setVisible.Get()(created, false);
			}
		});
		// The old entity is gone (or was never live) either way.
		entity = (completed && created != nullptr) ? created : nullptr;
		return entity != nullptr;
	}

	bool DestroyEntity(void* sceneManager, void* entity) noexcept
	{
		if (sceneManager == nullptr || entity == nullptr)
		{
			return false;
		}
		return Seh::Guard("OgreGibRuntime::DestroyEntity", [&] { g_destroyEntity.Get()(sceneManager, entity); });
	}

	bool SetNodeScale(void* node, const float scale[3]) noexcept
	{
		const NodeSetVectorFn fn = g_setScale.Get();
		if (node == nullptr || fn == nullptr)
		{
			return false;
		}
		__try
		{
			fn(node, OgreV3{ scale[0], scale[1], scale[2] });
			return true;
		}
		__except (Seh::Filter(GetExceptionCode()))
		{
			return false;
		}
	}

	bool PlaceNode(void* node, const float position[3], const float orientation[4]) noexcept
	{
		const NodeSetVectorFn setPosition = g_setPosition.Get();
		const NodeSetQuaternionFn setOrientation = g_setOrientation.Get();
		if (node == nullptr || setPosition == nullptr || setOrientation == nullptr)
		{
			return false;
		}
		__try
		{
			setPosition(node, OgreV3{ position[0], position[1], position[2] });
			setOrientation(node, OgreQ{ orientation[0], orientation[1], orientation[2], orientation[3] });
			return true;
		}
		__except (Seh::Filter(GetExceptionCode()))
		{
			return false;
		}
	}

	bool ShowObject(void* node, void* entity, const float position[3], const float orientation[4]) noexcept
	{
		return PlaceNode(node, position, orientation) && SetVisible(entity, true);
	}

	void HideObject(void* node, void* entity) noexcept
	{
		SetVisible(entity, false);
		const float parked[3] = { 0.0f, kParkedY, 0.0f };
		const float identity[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
		PlaceNode(node, parked, identity);
	}

	// ----- Frame listener ------------------------------------------------------------------

	void* AddFrameListener(void* listener) noexcept
	{
		if (listener == nullptr)
		{
			return nullptr;
		}
		void* root = nullptr;
		Seh::Guard("OgreGibRuntime::AddFrameListener", [&] {
			root = g_rootSingleton.Get()();
			if (root != nullptr)
			{
				g_addFrameListener.Get()(root, listener);
			}
		});
		return root;
	}

	void RemoveFrameListener(void* root, void* listener) noexcept
	{
		if (root == nullptr || listener == nullptr)
		{
			return;
		}
		Seh::Guard("OgreGibRuntime::RemoveFrameListener", [&] {
			void* current = g_rootSingleton.Get()();
			if (current != nullptr && current == root)
			{
				g_removeFrameListener.Get()(root, listener);
			}
		});
	}
}
