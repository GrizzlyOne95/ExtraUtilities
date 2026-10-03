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
#include "Util/SehGuard.h"

#include "Ogre/OgreRenderOrigin.h"

// EXU-managed particle systems: creation and teardown in the current scene,
// scene-node placement (sim to render space), attachment to the root, the
// camera, objects and bones, the camera follower list, and emitter and
// affector access. No Lua here; the bindings are in ParticleBindings.cpp.

namespace ExtraUtilities::Lua::Environment
{
		constexpr std::string_view kManagedParticleNodePrefix = "__exu_ps_node_";

		std::string BuildManagedParticleNodeName(std::string_view particleName)
		{
			std::string result(kManagedParticleNodePrefix);
			result += particleName;
			return result;
		}

		std::string CheckOptionalParticleResourceGroup(lua_State* L, int idx)
		{
			if (lua_isnoneornil(L, idx))
			{
				return "General";
			}

			return luaL_checkstring(L, idx);
		}

		bool TryHasParticleSystemSeh(void* sceneManager, const std::string& name, bool& outHasParticleSystem)
		{
			outHasParticleSystem = false;
			const auto fn = ResolveHasParticleSystem();
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				outHasParticleSystem = fn(sceneManager, name);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] hasParticleSystem crashed sceneManager=%p name=%s code=0x%08X", sceneManager, name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryHasParticleSystem(void* sceneManager, const std::string& name, bool& outHasParticleSystem)
		{
			return Seh::CatchCpp("TryHasParticleSystem", [&] { return TryHasParticleSystemSeh(sceneManager, name, outHasParticleSystem); }, false);
		}

		bool TryHasSceneNodeSeh(void* sceneManager, const std::string& name, bool& outHasSceneNode)
		{
			outHasSceneNode = false;
			const auto fn = ResolveHasSceneNode();
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				outHasSceneNode = fn(sceneManager, name);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] hasSceneNode crashed sceneManager=%p name=%s code=0x%08X", sceneManager, name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryHasSceneNode(void* sceneManager, const std::string& name, bool& outHasSceneNode)
		{
			return Seh::CatchCpp("TryHasSceneNode", [&] { return TryHasSceneNodeSeh(sceneManager, name, outHasSceneNode); }, false);
		}

		bool TryGetParticleSystemSeh(void* sceneManager, const std::string& name, void*& outParticleSystem)
		{
			outParticleSystem = nullptr;
			bool hasParticleSystem = false;
			if (!TryHasParticleSystem(sceneManager, name, hasParticleSystem) || !hasParticleSystem)
			{
				return false;
			}

			const auto fn = ResolveGetParticleSystem();
			if (fn == nullptr)
			{
				return false;
			}

			__try
			{
				outParticleSystem = fn(sceneManager, name);
				return outParticleSystem != nullptr;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] getParticleSystem crashed sceneManager=%p name=%s code=0x%08X", sceneManager, name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryGetParticleSystem(void* sceneManager, const std::string& name, void*& outParticleSystem)
		{
			return Seh::CatchCpp("TryGetParticleSystem", [&] { return TryGetParticleSystemSeh(sceneManager, name, outParticleSystem); }, false);
		}

		bool TryGetSceneNodeSeh(void* sceneManager, const std::string& nodeName, void*& outSceneNode)
		{
			outSceneNode = nullptr;
			bool hasSceneNode = false;
			if (!TryHasSceneNode(sceneManager, nodeName, hasSceneNode) || !hasSceneNode)
			{
				return false;
			}

			const auto fn = ResolveGetSceneNode();
			if (fn == nullptr)
			{
				return false;
			}

			__try
			{
				outSceneNode = fn(sceneManager, nodeName);
				return outSceneNode != nullptr;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] getSceneNode crashed sceneManager=%p node=%s code=0x%08X", sceneManager, nodeName.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryGetSceneNode(void* sceneManager, const std::string& nodeName, void*& outSceneNode)
		{
			return Seh::CatchCpp("TryGetSceneNode", [&] { return TryGetSceneNodeSeh(sceneManager, nodeName, outSceneNode); }, false);
		}

		bool TryGetManagedParticleSceneNode(void* sceneManager, const std::string& particleName, void*& outSceneNode)
		{
			return TryGetSceneNode(sceneManager, BuildManagedParticleNodeName(particleName), outSceneNode);
		}

		// Ogre declares "class ParticleSystem : public StringInterface, public
		// MovableObject", so the MovableObject subobject does not start at the
		// top of the ParticleSystem and a ParticleSystem* is NOT a usable
		// MovableObject*. Handing the raw pointer to a MovableObject-typed entry
		// point (SceneNode::attachObject, MovableObject::detachFromParent,
		// MovableObject::setVisible, Entity::attachObjectToBone) makes the
		// callee read its vptr out of the StringInterface subobject and index
		// past the end of that much shorter vtable -- an access violation on
		// every call.
		//
		// Rather than hardcode the base offset, which is a property of whatever
		// OgreMain the game shipped, ask the scene manager for the movable
		// object by name: Ogre performs the downcast on its own side and hands
		// back the correctly re-based pointer.
		bool TryGetParticleMovableObjectSeh(void* sceneManager, const std::string& name, void*& outMovableObject)
		{
			outMovableObject = nullptr;
			const auto hasFn = ResolveHasMovableObject();
			const auto getFn = ResolveGetMovableObject();
			const std::string* typeName = ResolveParticleSystemFactoryTypeName();
			if (sceneManager == nullptr || hasFn == nullptr || getFn == nullptr || typeName == nullptr)
			{
				return false;
			}

			__try
			{
				if (!hasFn(sceneManager, name, *typeName))
				{
					return false;
				}

				outMovableObject = getFn(sceneManager, name, *typeName);
				return outMovableObject != nullptr;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] getMovableObject crashed sceneManager=%p name=%s code=0x%08X", sceneManager, name.c_str(), GetExceptionCode());
				outMovableObject = nullptr;
				return false;
			}
		}

		bool TryGetParticleMovableObject(void* sceneManager, const std::string& name, void*& outMovableObject)
		{
			return Seh::CatchCpp("TryGetParticleMovableObject", [&] { return TryGetParticleMovableObjectSeh(sceneManager, name, outMovableObject); }, [&] { outMovableObject = nullptr; return false; });
		}

		bool TryDestroyParticleSystemByNameSeh(void* sceneManager, const std::string& name)
		{
			const auto fn = ResolveDestroyParticleSystem();
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			bool hasParticleSystem = false;
			if (!TryHasParticleSystem(sceneManager, name, hasParticleSystem) || !hasParticleSystem)
			{
				return false;
			}

			__try
			{
				fn(sceneManager, name);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] destroyParticleSystem crashed sceneManager=%p name=%s code=0x%08X", sceneManager, name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryDestroyParticleSystemByName(void* sceneManager, const std::string& name)
		{
			return Seh::CatchCpp("TryDestroyParticleSystemByName", [&] { return TryDestroyParticleSystemByNameSeh(sceneManager, name); }, false);
		}

		bool TryDestroySceneNodeByNameSeh(void* sceneManager, const std::string& nodeName)
		{
			const auto fn = ResolveDestroySceneNode();
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			bool hasSceneNode = false;
			if (!TryHasSceneNode(sceneManager, nodeName, hasSceneNode) || !hasSceneNode)
			{
				return false;
			}

			__try
			{
				fn(sceneManager, nodeName);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] destroySceneNode crashed sceneManager=%p node=%s code=0x%08X", sceneManager, nodeName.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryDestroySceneNodeByName(void* sceneManager, const std::string& nodeName)
		{
			return Seh::CatchCpp("TryDestroySceneNodeByName", [&] { return TryDestroySceneNodeByNameSeh(sceneManager, nodeName); }, false);
		}

		bool TryDestroyManagedParticleSystem(void* sceneManager, const std::string& name)
		{
			if (sceneManager == nullptr)
			{
				return false;
			}

			// A bone attachment leaves the system parented to a TagPoint rather
			// than to its own node. Break whatever link exists before the
			// destroy so nothing is left holding a freed movable object.
			TryReturnManagedParticleToOwnNode(sceneManager, name);

			bool removedAny = false;
			removedAny = TryDestroyParticleSystemByName(sceneManager, name) || removedAny;
			removedAny = TryDestroySceneNodeByName(sceneManager, BuildManagedParticleNodeName(name)) || removedAny;
			return removedAny;
		}

		bool TryCreateParticleSystemAttachmentSeh(void* sceneManager, const std::string& name, const std::string& templateName, const std::string& nodeName, const BZR::VECTOR_3D& position)
		{
			const auto createParticleFn = ResolveCreateParticleSystem();
			const auto getRootSceneNodeFn = ResolveGetRootSceneNode();
			const auto createChildSceneNodeFn = ResolveCreateChildSceneNode();
			const auto attachObjectFn = ResolveAttachObject();
			if (sceneManager == nullptr || createParticleFn == nullptr || getRootSceneNodeFn == nullptr ||
				createChildSceneNodeFn == nullptr || attachObjectFn == nullptr)
			{
				return false;
			}

			void* rootSceneNode = nullptr;
			void* particleSystem = nullptr;
			void* movableObject = nullptr;
			void* childSceneNode = nullptr;
			const OgreQuaternionValue identity{};
			__try
			{
				rootSceneNode = getRootSceneNodeFn(sceneManager);
				if (rootSceneNode == nullptr)
				{
					LogEnvironmentDebug("[EXU::Particle] create skipped sceneManager=%p name=%s reason=no_root_scene_node", sceneManager, name.c_str());
					return false;
				}

				particleSystem = createParticleFn(sceneManager, name, templateName);
				if (particleSystem == nullptr)
				{
					LogEnvironmentDebug("[EXU::Particle] create failed sceneManager=%p name=%s template=%s reason=null_particle", sceneManager, name.c_str(), templateName.c_str());
					return false;
				}

				// attachObject takes a MovableObject*, which is not where the
				// ParticleSystem* points. See TryGetParticleMovableObject.
				if (!TryGetParticleMovableObject(sceneManager, name, movableObject))
				{
					LogEnvironmentDebug("[EXU::Particle] create failed sceneManager=%p name=%s template=%s reason=no_movable_object", sceneManager, name.c_str(), templateName.c_str());
					return false;
				}

				childSceneNode = createChildSceneNodeFn(rootSceneNode, nodeName, position, identity);
				if (childSceneNode == nullptr)
				{
					LogEnvironmentDebug("[EXU::Particle] create failed sceneManager=%p name=%s node=%s reason=null_scene_node", sceneManager, name.c_str(), nodeName.c_str());
					return false;
				}

				attachObjectFn(childSceneNode, movableObject);
				LogEnvironmentDebug(
					"[EXU::Particle] created sceneManager=%p name=%s template=%s particle=%p movable=%p baseOffset=%d",
					sceneManager,
					name.c_str(),
					templateName.c_str(),
					particleSystem,
					movableObject,
					static_cast<int>(reinterpret_cast<char*>(movableObject) - reinterpret_cast<char*>(particleSystem)));
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault(
					"[EXU::Particle] create crashed sceneManager=%p name=%s template=%s node=%s code=0x%08X",
					sceneManager,
					name.c_str(),
					templateName.c_str(),
					nodeName.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TryCreateParticleSystemAttachment(void* sceneManager, const std::string& name, const std::string& templateName, const std::string& nodeName, const BZR::VECTOR_3D& position)
		{
			return Seh::CatchCpp("TryCreateParticleSystemAttachment", [&] { return TryCreateParticleSystemAttachmentSeh(sceneManager, name, templateName, nodeName, position); }, false);
		}

		// Particle systems created through EXU in the current scene manager,
		// by name. The scene outlives the Lua state (Redux changes mission in
		// process), so they are destroyed at Lua-state close rather than left
		// emitting with no owner.
		std::unordered_set<std::string> g_managedParticleNames;
		void* g_managedParticleSceneManager = nullptr;

		bool TryCreateManagedParticleSystem(void* sceneManager, const std::string& name, const std::string& templateName, const BZR::VECTOR_3D& position)
		{
			if (sceneManager == nullptr)
			{
				return false;
			}

			bool hasParticleSystem = false;
			if (TryHasParticleSystem(sceneManager, name, hasParticleSystem) && hasParticleSystem)
			{
				LogEnvironmentDebug("[EXU::Particle] create skipped sceneManager=%p name=%s reason=particle_exists", sceneManager, name.c_str());
				return false;
			}

			const std::string nodeName = BuildManagedParticleNodeName(name);
			bool hasSceneNode = false;
			if (TryHasSceneNode(sceneManager, nodeName, hasSceneNode) && hasSceneNode)
			{
				LogEnvironmentDebug("[EXU::Particle] create skipped sceneManager=%p name=%s node=%s reason=node_exists", sceneManager, name.c_str(), nodeName.c_str());
				return false;
			}

			if (TryCreateParticleSystemAttachment(sceneManager, name, templateName, nodeName, position))
			{
				if (g_managedParticleSceneManager != sceneManager)
				{
					g_managedParticleNames.clear();
					g_managedParticleSceneManager = sceneManager;
				}
				g_managedParticleNames.insert(name);
				return true;
			}

			TryDestroyManagedParticleSystem(sceneManager, name);
			return false;
		}

		bool TrySetManagedParticleSceneNodePositionSeh(void* sceneManager, const std::string& name, const BZR::VECTOR_3D& position)
		{
			void* sceneNode = nullptr;
			const auto fn = ResolveSetNodePosition();
			if (!TryGetManagedParticleSceneNode(sceneManager, name, sceneNode) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(sceneNode, position);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] setPosition crashed sceneNode=%p name=%s code=0x%08X", sceneNode, name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TrySetManagedParticleSceneNodePosition(void* sceneManager, const std::string& name, const BZR::VECTOR_3D& position)
		{
			return Seh::CatchCpp("TrySetManagedParticleSceneNodePosition", [&] { return TrySetManagedParticleSceneNodePositionSeh(sceneManager, name, position); }, false);
		}

		bool TrySetManagedParticleSceneNodeDirectionSeh(void* sceneManager, const std::string& name, const BZR::VECTOR_3D& direction)
		{
			void* sceneNode = nullptr;
			const auto fn = ResolveSetSceneNodeDirection();
			if (!TryGetManagedParticleSceneNode(sceneManager, name, sceneNode) || fn == nullptr)
			{
				return false;
			}

			const BZR::VECTOR_3D localDirectionVector{ 0.0f, 0.0f, -1.0f };
			__try
			{
				fn(sceneNode, direction, kOgreTransformSpaceLocal, localDirectionVector);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] setDirection crashed sceneNode=%p name=%s code=0x%08X", sceneNode, name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TrySetManagedParticleSceneNodeDirection(void* sceneManager, const std::string& name, const BZR::VECTOR_3D& direction)
		{
			return Seh::CatchCpp("TrySetManagedParticleSceneNodeDirection", [&] { return TrySetManagedParticleSceneNodeDirectionSeh(sceneManager, name, direction); }, false);
		}

		bool TrySetParticleSystemEmittingSeh(void* sceneManager, const std::string& name, bool enabled)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetParticleSystemEmitting();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, enabled);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] setEmitting crashed particleSystem=%p name=%s enabled=%d code=0x%08X", particleSystem, name.c_str(), enabled ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemEmitting(void* sceneManager, const std::string& name, bool enabled)
		{
			return Seh::CatchCpp("TrySetParticleSystemEmitting", [&] { return TrySetParticleSystemEmittingSeh(sceneManager, name, enabled); }, false);
		}

		bool TrySetParticleSystemVisibleSeh(void* sceneManager, const std::string& name, bool enabled)
		{
			// setVisible is MovableObject's own body, so it needs the re-based
			// pointer rather than the ParticleSystem*.
			void* movableObject = nullptr;
			const auto fn = ResolveSetMovableObjectVisible();
			if (!TryGetParticleMovableObject(sceneManager, name, movableObject) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(movableObject, enabled);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] setVisible crashed movableObject=%p name=%s enabled=%d code=0x%08X", movableObject, name.c_str(), enabled ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemVisible(void* sceneManager, const std::string& name, bool enabled)
		{
			return Seh::CatchCpp("TrySetParticleSystemVisible", [&] { return TrySetParticleSystemVisibleSeh(sceneManager, name, enabled); }, false);
		}

		bool TrySetParticleSystemSpeedFactorSeh(void* sceneManager, const std::string& name, float speedFactor)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetParticleSystemSpeedFactor();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, speedFactor);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] setSpeedFactor crashed particleSystem=%p name=%s speed=%g code=0x%08X", particleSystem, name.c_str(), speedFactor, GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemSpeedFactor(void* sceneManager, const std::string& name, float speedFactor)
		{
			return Seh::CatchCpp("TrySetParticleSystemSpeedFactor", [&] { return TrySetParticleSystemSpeedFactorSeh(sceneManager, name, speedFactor); }, false);
		}

		bool TrySetParticleSystemKeepLocalSpaceSeh(void* sceneManager, const std::string& name, bool enabled)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetParticleSystemKeepLocalSpace();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, enabled);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] setKeepParticlesInLocalSpace crashed particleSystem=%p name=%s enabled=%d code=0x%08X", particleSystem, name.c_str(), enabled ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemKeepLocalSpace(void* sceneManager, const std::string& name, bool enabled)
		{
			return Seh::CatchCpp("TrySetParticleSystemKeepLocalSpace", [&] { return TrySetParticleSystemKeepLocalSpaceSeh(sceneManager, name, enabled); }, false);
		}

		bool TrySetParticleSystemMaterialSeh(void* sceneManager, const std::string& name, const std::string& materialName, const std::string& resourceGroup)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetParticleSystemMaterial();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, materialName, resourceGroup);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault(
					"[EXU::Particle] setMaterialName crashed particleSystem=%p name=%s material=%s group=%s code=0x%08X",
					particleSystem,
					name.c_str(),
					materialName.c_str(),
					resourceGroup.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemMaterial(void* sceneManager, const std::string& name, const std::string& materialName, const std::string& resourceGroup)
		{
			return Seh::CatchCpp("TrySetParticleSystemMaterial", [&] { return TrySetParticleSystemMaterialSeh(sceneManager, name, materialName, resourceGroup); }, false);
		}

		bool TrySetParticleSystemRenderQueueGroupSeh(void* sceneManager, const std::string& name, uint8_t renderQueueGroup)
		{
			// ParticleSystem::setRenderQueueGroup overrides a MovableObject
			// virtual, so under the MSVC ABI its this-pointer is the re-based
			// MovableObject subobject, exactly like MovableObject's own bodies.
			// Passing the ParticleSystem* wrote the queue id into the
			// StringInterface subobject and made a virtual call through an
			// unrelated field.
			void* movableObject = nullptr;
			const auto fn = ResolveSetParticleSystemRenderQueueGroup();
			if (!TryGetParticleMovableObject(sceneManager, name, movableObject) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(movableObject, renderQueueGroup);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] setRenderQueueGroup crashed movableObject=%p name=%s queue=%u code=0x%08X", movableObject, name.c_str(), renderQueueGroup, GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemRenderQueueGroup(void* sceneManager, const std::string& name, uint8_t renderQueueGroup)
		{
			return Seh::CatchCpp("TrySetParticleSystemRenderQueueGroup", [&] { return TrySetParticleSystemRenderQueueGroupSeh(sceneManager, name, renderQueueGroup); }, false);
		}

		bool TrySetParticleSystemParticleQuotaSeh(void* sceneManager, const std::string& name, uint32_t quota)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetParticleSystemParticleQuota();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, quota);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] setParticleQuota crashed particleSystem=%p name=%s quota=%u code=0x%08X", particleSystem, name.c_str(), quota, GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemParticleQuota(void* sceneManager, const std::string& name, uint32_t quota)
		{
			return Seh::CatchCpp("TrySetParticleSystemParticleQuota", [&] { return TrySetParticleSystemParticleQuotaSeh(sceneManager, name, quota); }, false);
		}

		bool TrySetParticleSystemDefaultDimensionsSeh(void* sceneManager, const std::string& name, float width, float height)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetParticleSystemDefaultDimensions();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, width, height);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] setDefaultDimensions crashed particleSystem=%p name=%s width=%g height=%g code=0x%08X", particleSystem, name.c_str(), width, height, GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemDefaultDimensions(void* sceneManager, const std::string& name, float width, float height)
		{
			return Seh::CatchCpp("TrySetParticleSystemDefaultDimensions", [&] { return TrySetParticleSystemDefaultDimensionsSeh(sceneManager, name, width, height); }, false);
		}

		std::vector<ParticleCameraFollower> g_particleCameraFollowers;

		void ForgetParticleCameraFollower(const std::string& name)
		{
			for (auto it = g_particleCameraFollowers.begin(); it != g_particleCameraFollowers.end(); ++it)
			{
				if (it->particleName == name)
				{
					g_particleCameraFollowers.erase(it);
					return;
				}
			}
		}

		void RememberParticleCameraFollower(const std::string& name, const BZR::VECTOR_3D& offset)
		{
			for (auto& follower : g_particleCameraFollowers)
			{
				if (follower.particleName == name)
				{
					follower.offset = offset;
					return;
				}
			}

			g_particleCameraFollowers.push_back(ParticleCameraFollower{ name, offset });
		}

		// The active viewport's Ogre camera, or null when no viewport is live.
		void* GetActiveOgreCameraSeh()
		{
			const ActiveViewportSet activeViewports = GetActiveViewports();
			if (activeViewports.count == 0 || activeViewports.viewports[0] == nullptr)
			{
				return nullptr;
			}

			__try
			{
				return Ogre::GetViewportCamera(activeViewports.viewports[0]);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] Viewport::getCamera crashed code=0x%08X", GetExceptionCode());
				return nullptr;
			}
		}

		void* GetActiveOgreCamera()
		{
			return Seh::CatchCpp("GetActiveOgreCamera", [&] { return GetActiveOgreCameraSeh(); }, nullptr);
		}

		bool TryGetCameraDerivedPositionSeh(void* camera, BZR::VECTOR_3D& outPosition)
		{
			const auto fn = ResolveCameraGetDerivedPosition();
			if (camera == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				const BZR::VECTOR_3D* position = fn(camera);
				if (position == nullptr)
				{
					return false;
				}

				outPosition = *position;
				return IsFiniteVector(outPosition);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] Camera::getDerivedPosition crashed camera=%p code=0x%08X", camera, GetExceptionCode());
				return false;
			}
		}

		bool TryGetCameraDerivedPosition(void* camera, BZR::VECTOR_3D& outPosition)
		{
			return Seh::CatchCpp("TryGetCameraDerivedPosition", [&] { return TryGetCameraDerivedPositionSeh(camera, outPosition); }, false);
		}

		bool TryConvertSimPositionToRenderSpace(
			const BZR::VECTOR_3D& simPosition,
			BZR::VECTOR_3D& outRenderPosition)
		{
			BZR::VECTOR_3D origin{};
			unsigned long exceptionCode = 0;
			if (!OgreRenderSpace::TryReadWorldRenderOrigin(origin, &exceptionCode))
			{
				LogEnvironmentFault(
					"[EXU::Particle] render-position conversion failed reason=origin_read_crashed code=0x%08X",
					exceptionCode);
				return false;
			}

			if (!IsFiniteVector(origin))
			{
				LogEnvironmentDebug(
					"[EXU::Particle] render-position conversion failed reason=invalid_origin origin=(%.3f,%.3f,%.3f)",
					static_cast<double>(origin.x),
					static_cast<double>(origin.y),
					static_cast<double>(origin.z));
				return false;
			}

			outRenderPosition = OgreRenderSpace::SimPositionToRender(simPosition, origin);
			return IsFiniteVector(outRenderPosition);
		}

		void* GetMovableObjectParentSceneNodeSeh(void* movableObject)
		{
			const auto fn = ResolveGetParentSceneNode();
			if (movableObject == nullptr || fn == nullptr)
			{
				return nullptr;
			}

			__try
			{
				return fn(movableObject);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] getParentSceneNode crashed movableObject=%p code=0x%08X", movableObject, GetExceptionCode());
				return nullptr;
			}
		}

		void* GetMovableObjectParentSceneNode(void* movableObject)
		{
			return Seh::CatchCpp("GetMovableObjectParentSceneNode", [&] { return GetMovableObjectParentSceneNodeSeh(movableObject); }, nullptr);
		}

		// Moves an EXU-owned particle node under a new parent. Ogre asserts if a
		// node is added to a second parent while it still has one, so the
		// current parent link is always broken first.
		bool TryReparentNodeSeh(void* node, void* newParent)
		{
			const auto getParentFn = ResolveNodeGetParent();
			const auto removeChildFn = ResolveNodeRemoveChildPtr();
			const auto addChildFn = ResolveNodeAddChild();
			if (node == nullptr || newParent == nullptr || getParentFn == nullptr ||
				removeChildFn == nullptr || addChildFn == nullptr)
			{
				return false;
			}

			__try
			{
				void* currentParent = getParentFn(node);
				if (currentParent == newParent)
				{
					return true;
				}

				if (currentParent != nullptr)
				{
					removeChildFn(currentParent, node);
				}

				addChildFn(newParent, node);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] reparent crashed node=%p newParent=%p code=0x%08X", node, newParent, GetExceptionCode());
				return false;
			}
		}

		bool TryReparentNode(void* node, void* newParent)
		{
			return Seh::CatchCpp("TryReparentNode", [&] { return TryReparentNodeSeh(node, newParent); }, false);
		}

		bool TrySetNodeInheritOrientationSeh(void* node, bool inherit)
		{
			const auto fn = ResolveNodeSetInheritOrientation();
			if (node == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(node, inherit);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] setInheritOrientation crashed node=%p code=0x%08X", node, GetExceptionCode());
				return false;
			}
		}

		bool TrySetNodeInheritOrientation(void* node, bool inherit)
		{
			return Seh::CatchCpp("TrySetNodeInheritOrientation", [&] { return TrySetNodeInheritOrientationSeh(node, inherit); }, false);
		}

		bool TrySetNodeInheritScaleSeh(void* node, bool inherit)
		{
			const auto fn = ResolveNodeSetInheritScale();
			if (node == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(node, inherit);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] setInheritScale crashed node=%p code=0x%08X", node, GetExceptionCode());
				return false;
			}
		}

		bool TrySetNodeInheritScale(void* node, bool inherit)
		{
			return Seh::CatchCpp("TrySetNodeInheritScale", [&] { return TrySetNodeInheritScaleSeh(node, inherit); }, false);
		}

		bool TrySetNodePositionDirectSeh(void* node, const BZR::VECTOR_3D& position)
		{
			const auto fn = ResolveSetNodePosition();
			if (node == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(node, position);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] setPosition crashed node=%p code=0x%08X", node, GetExceptionCode());
				return false;
			}
		}

		bool TrySetNodePositionDirect(void* node, const BZR::VECTOR_3D& position)
		{
			return Seh::CatchCpp("TrySetNodePositionDirect", [&] { return TrySetNodePositionDirectSeh(node, position); }, false);
		}

		bool TryAttachManagedParticleToRoot(void* sceneManager, const std::string& name)
		{
			const auto getRootFn = ResolveGetRootSceneNode();
			void* node = nullptr;
			if (sceneManager == nullptr || getRootFn == nullptr || !TryGetManagedParticleSceneNode(sceneManager, name, node))
			{
				return false;
			}

			void* rootSceneNode = nullptr;
			if (!Seh::Guard(
					"TryAttachManagedParticleToRoot",
					[&] { rootSceneNode = getRootFn(sceneManager); },
					[&](unsigned long exceptionCode) { LogEnvironmentFault("[EXU::Particle] getRootSceneNode crashed sceneManager=%p code=0x%08X", sceneManager, exceptionCode); }))
			{
				return false;
			}

			if (!TryReparentNode(node, rootSceneNode))
			{
				return false;
			}

			TrySetNodeInheritOrientation(node, true);
			TrySetNodeInheritScale(node, true);
			return true;
		}

		// Returns true when the particle system's node now hangs off the
		// camera's own scene node. Returns false when the engine drives its
		// camera without a node -- the caller falls back to follower mode.
		bool TryAttachManagedParticleToCameraNode(void* sceneManager, const std::string& name, const BZR::VECTOR_3D& offset)
		{
			void* camera = GetActiveOgreCamera();
			if (camera == nullptr)
			{
				return false;
			}

			void* cameraNode = GetMovableObjectParentSceneNode(camera);
			if (cameraNode == nullptr)
			{
				LogEnvironmentDebug("[EXU::Particle] camera attach unavailable name=%s reason=camera_has_no_scene_node", name.c_str());
				return false;
			}

			void* node = nullptr;
			if (!TryGetManagedParticleSceneNode(sceneManager, name, node))
			{
				return false;
			}

			if (!TryReparentNode(node, cameraNode))
			{
				return false;
			}

			// A precipitation volume must not roll or yaw with the view; only
			// its position should track the camera.
			TrySetNodeInheritOrientation(node, false);
			TrySetNodeInheritScale(node, false);
			TrySetNodePositionDirect(node, offset);
			return true;
		}

		bool TryUpdateParticleCameraFollower(void* sceneManager, const ParticleCameraFollower& follower, const BZR::VECTOR_3D& cameraPosition)
		{
			void* node = nullptr;
			if (!TryGetManagedParticleSceneNode(sceneManager, follower.particleName, node))
			{
				return false;
			}

			const BZR::VECTOR_3D position{
				cameraPosition.x + follower.offset.x,
				cameraPosition.y + follower.offset.y,
				cameraPosition.z + follower.offset.z,
			};
			return TrySetNodePositionDirect(node, position);
		}

		bool TryAttachManagedParticleToObjectNode(void* sceneManager, const std::string& name, void* entity, const BZR::VECTOR_3D& offset)
		{
			void* objectNode = GetMovableObjectParentSceneNode(entity);
			if (objectNode == nullptr)
			{
				LogEnvironmentDebug("[EXU::Particle] object attach failed name=%s reason=entity_has_no_scene_node", name.c_str());
				return false;
			}

			void* node = nullptr;
			if (!TryGetManagedParticleSceneNode(sceneManager, name, node))
			{
				return false;
			}

			if (!TryReparentNode(node, objectNode))
			{
				return false;
			}

			// Damage smoke and engine glow are body-relative, so orientation is
			// inherited here even though the camera volume does not inherit it.
			TrySetNodeInheritOrientation(node, true);
			TrySetNodeInheritScale(node, false);
			TrySetNodePositionDirect(node, offset);
			return true;
		}

		bool TryEntityHasBoneSeh(void* entity, const std::string& boneName)
		{
			const auto hasSkeletonFn = ResolveEntityHasSkeleton();
			const auto getSkeletonFn = ResolveEntityGetSkeleton();
			const auto hasBoneFn = ResolveSkeletonHasBone();
			if (entity == nullptr || hasSkeletonFn == nullptr || getSkeletonFn == nullptr || hasBoneFn == nullptr)
			{
				return false;
			}

			__try
			{
				if (!hasSkeletonFn(entity))
				{
					return false;
				}
				void* const skeleton = getSkeletonFn(entity);
				return skeleton != nullptr && hasBoneFn(skeleton, boneName);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] hasBone crashed entity=%p bone=%s code=0x%08X", entity, boneName.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryEntityHasBone(void* entity, const std::string& boneName)
		{
			return Seh::CatchCpp("TryEntityHasBone", [&] { return TryEntityHasBoneSeh(entity, boneName); }, false);
		}

		bool TryGetMovableObjectTagPointParentSeh(void* movableObject, void*& outTagPoint, void*& outEntity)
		{
			outTagPoint = nullptr;
			outEntity = nullptr;
			const auto getParentNodeFn = ResolveMovableObjectGetParentNode();
			const auto isParentTagPointFn = ResolveMovableObjectIsParentTagPoint();
			const auto getParentEntityFn = ResolveTagPointGetParentEntity();
			if (movableObject == nullptr || getParentNodeFn == nullptr || isParentTagPointFn == nullptr || getParentEntityFn == nullptr)
			{
				return false;
			}

			__try
			{
				void* const parent = getParentNodeFn(movableObject);
				if (parent == nullptr || !isParentTagPointFn(movableObject))
				{
					return true;
				}
				outTagPoint = parent;
				outEntity = getParentEntityFn(parent);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] parent query crashed movableObject=%p code=0x%08X", movableObject, GetExceptionCode());
				outTagPoint = nullptr;
				outEntity = nullptr;
				return false;
			}
		}

		bool TryGetMovableObjectTagPointParent(void* movableObject, void*& outTagPoint, void*& outEntity)
		{
			return Seh::CatchCpp("TryGetMovableObjectTagPointParent", [&] { return TryGetMovableObjectTagPointParentSeh(movableObject, outTagPoint, outEntity); }, [&] { outTagPoint = nullptr; outEntity = nullptr; return false; });
		}

		bool TryAttachManagedParticleToBoneSeh(void* sceneManager, const std::string& name, void* entity, const std::string& boneName, const BZR::VECTOR_3D& offset)
		{
			const auto hasSkeletonFn = ResolveEntityHasSkeleton();
			const auto attachFn = ResolveEntityAttachObjectToBone();
			const auto detachFromParentFn = ResolveMovableObjectDetachFromParent();
			if (entity == nullptr || hasSkeletonFn == nullptr || attachFn == nullptr || detachFromParentFn == nullptr)
			{
				return false;
			}

			void* movableObject = nullptr;
			if (!TryGetParticleMovableObject(sceneManager, name, movableObject))
			{
				return false;
			}

			const OgreQuaternionValue identity{};
			__try
			{
				if (!hasSkeletonFn(entity))
				{
					LogEnvironmentDebug("[EXU::Particle] bone attach failed name=%s bone=%s reason=entity_has_no_skeleton", name.c_str(), boneName.c_str());
					return false;
				}

				// Entity::attachObjectToBone throws ItemIdentityException for an
				// unknown bone; ask first so a typo never unwinds out of a
				// foreign-CRT frame after the system was already detached.
				const auto getSkeletonFn = ResolveEntityGetSkeleton();
				const auto hasBoneFn = ResolveSkeletonHasBone();
				if (getSkeletonFn != nullptr && hasBoneFn != nullptr)
				{
					void* const skeleton = getSkeletonFn(entity);
					if (skeleton == nullptr || !hasBoneFn(skeleton, boneName))
					{
						LogEnvironmentDebug("[EXU::Particle] bone attach failed name=%s bone=%s reason=no_such_bone", name.c_str(), boneName.c_str());
						return false;
					}
				}

				// A movable object can only live on one attachment point, and
				// the managed system starts life on its own scene node. Both
				// calls are MovableObject-typed, so they take the re-based
				// pointer, not the ParticleSystem*.
				detachFromParentFn(movableObject);
				return attachFn(entity, boneName, movableObject, identity, offset) != nullptr;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault(
					"[EXU::Particle] bone attach crashed name=%s bone=%s entity=%p code=0x%08X",
					name.c_str(),
					boneName.c_str(),
					entity,
					GetExceptionCode());
				return false;
			}
		}

		bool TryAttachManagedParticleToBone(void* sceneManager, const std::string& name, void* entity, const std::string& boneName, const BZR::VECTOR_3D& offset)
		{
			return Seh::CatchCpp("TryAttachManagedParticleToBone", [&] { return TryAttachManagedParticleToBoneSeh(sceneManager, name, entity, boneName, offset); }, false);
		}

		void* GetParticleEmitterSeh(void* sceneManager, const std::string& name, int emitterIndex)
		{
			const auto getNumFn = ResolveGetNumEmitters();
			const auto getEmitterFn = ResolveGetEmitter();
			void* particleSystem = nullptr;
			if (emitterIndex < 0 || getNumFn == nullptr || getEmitterFn == nullptr ||
				!TryGetParticleSystem(sceneManager, name, particleSystem))
			{
				return nullptr;
			}

			__try
			{
				const uint16_t count = getNumFn(particleSystem);
				if (emitterIndex >= static_cast<int>(count))
				{
					return nullptr;
				}

				return getEmitterFn(particleSystem, static_cast<uint16_t>(emitterIndex));
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] getEmitter crashed name=%s index=%d code=0x%08X", name.c_str(), emitterIndex, GetExceptionCode());
				return nullptr;
			}
		}

		void* GetParticleEmitter(void* sceneManager, const std::string& name, int emitterIndex)
		{
			return Seh::CatchCpp("GetParticleEmitter", [&] { return GetParticleEmitterSeh(sceneManager, name, emitterIndex); }, nullptr);
		}

		bool TryGetParticleEmitterCountSeh(void* sceneManager, const std::string& name, int& outCount)
		{
			outCount = 0;
			const auto fn = ResolveGetNumEmitters();
			void* particleSystem = nullptr;
			if (fn == nullptr || !TryGetParticleSystem(sceneManager, name, particleSystem))
			{
				return false;
			}

			__try
			{
				outCount = static_cast<int>(fn(particleSystem));
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] getNumEmitters crashed name=%s code=0x%08X", name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryGetParticleEmitterCount(void* sceneManager, const std::string& name, int& outCount)
		{
			return Seh::CatchCpp("TryGetParticleEmitterCount", [&] { return TryGetParticleEmitterCountSeh(sceneManager, name, outCount); }, false);
		}

		void* GetParticleAffectorSeh(void* sceneManager, const std::string& name, int affectorIndex)
		{
			const auto getNumFn = ResolveGetNumAffectors();
			const auto getAffectorFn = ResolveGetAffector();
			void* particleSystem = nullptr;
			if (affectorIndex < 0 || getNumFn == nullptr || getAffectorFn == nullptr ||
				!TryGetParticleSystem(sceneManager, name, particleSystem))
			{
				return nullptr;
			}

			__try
			{
				const uint16_t count = getNumFn(particleSystem);
				if (affectorIndex >= static_cast<int>(count))
				{
					return nullptr;
				}

				return getAffectorFn(particleSystem, static_cast<uint16_t>(affectorIndex));
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] getAffector crashed name=%s index=%d code=0x%08X", name.c_str(), affectorIndex, GetExceptionCode());
				return nullptr;
			}
		}

		void* GetParticleAffector(void* sceneManager, const std::string& name, int affectorIndex)
		{
			return Seh::CatchCpp("GetParticleAffector", [&] { return GetParticleAffectorSeh(sceneManager, name, affectorIndex); }, nullptr);
		}

		bool TryGetParticleAffectorCountSeh(void* sceneManager, const std::string& name, int& outCount)
		{
			outCount = 0;
			const auto fn = ResolveGetNumAffectors();
			void* particleSystem = nullptr;
			if (fn == nullptr || !TryGetParticleSystem(sceneManager, name, particleSystem))
			{
				return false;
			}

			__try
			{
				outCount = static_cast<int>(fn(particleSystem));
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] getNumAffectors crashed name=%s code=0x%08X", name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryGetParticleAffectorCount(void* sceneManager, const std::string& name, int& outCount)
		{
			return Seh::CatchCpp("TryGetParticleAffectorCount", [&] { return TryGetParticleAffectorCountSeh(sceneManager, name, outCount); }, false);
		}

		// ---------------------------------------------------------------
		// Generic Ogre::StringInterface bridge
		//
		// ParticleEmitter and ParticleAffector both publish their
		// type-specific properties ("width" on a Box emitter, "force_vector"
		// on a LinearForce affector) only through StringInterface, so the
		// typed EXU wrappers cannot reach them. These helpers are the escape
		// hatch; the typed wrappers stay the preferred API for everything
		// they already cover.
		//
		// The three Invoke* helpers below exist because MSVC refuses __try in
		// any function holding an object that needs unwinding (C2712). Keeping
		// the std::string and std::vector locals in their own frames lets the
		// SEH guard live in the caller, where the body is just a call.
		// ---------------------------------------------------------------

		__declspec(noinline) void InvokeGetStringInterfaceParameter(
			OgreAbi::StringInterfaceGetParameterFn fn,
			void* stringInterface,
			const std::string& parameter,
			std::string& outValue)
		{
			// Ogre constructs its return value into this empty string. An
			// empty MSVC std::string owns no heap block, so there is nothing
			// to leak by being constructed over.
			std::string returned;
			fn(stringInterface, &returned, parameter);
			outValue = returned;
		}

		__declspec(noinline) bool InvokeGetStringInterfaceParameterNames(
			OgreAbi::StringInterfaceGetParametersFn fn,
			void* stringInterface,
			std::vector<std::string>& outNames)
		{
			const auto* list = static_cast<const OgreAbi::ParameterListLayout*>(fn(stringInterface));
			if (list == nullptr || list->first == nullptr || list->last == nullptr)
			{
				return false;
			}

			// An empty ParamDictionary is legitimate; anything that is not a
			// well-formed, plausibly sized array means these three words are
			// not really a ParameterList and must not be dereferenced.
			if (list->last < list->first)
			{
				return false;
			}

			const std::ptrdiff_t count = list->last - list->first;
			if (count > static_cast<std::ptrdiff_t>(OgreAbi::kMaxParameterDefs))
			{
				return false;
			}

			outNames.reserve(static_cast<size_t>(count));
			for (std::ptrdiff_t i = 0; i < count; ++i)
			{
				outNames.push_back(list->first[i].name);
			}

			return true;
		}

		bool TryGetStringInterfaceTypeNameSeh(void* stringInterface, OgreAbi::GetTypeNameFn fn, std::string& outType)
		{
			outType.clear();
			if (stringInterface == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				const std::string* type = fn(stringInterface);
				if (type == nullptr)
				{
					return false;
				}

				// getType returns a reference to an Ogre-owned string, so this
				// only reads it; ownership never crosses the boundary.
				outType = *type;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] getType crashed object=%p code=0x%08X", stringInterface, GetExceptionCode());
				return false;
			}
		}

		bool TryGetStringInterfaceTypeName(void* stringInterface, OgreAbi::GetTypeNameFn fn, std::string& outType)
		{
			return Seh::CatchCpp("TryGetStringInterfaceTypeName", [&] { return TryGetStringInterfaceTypeNameSeh(stringInterface, fn, outType); }, false);
		}

		bool TrySetStringInterfaceParameterSeh(
			void* stringInterface,
			const std::string& parameter,
			const std::string& value,
			const char* what)
		{
			const auto fn = ResolveStringInterfaceSetParameter();
			if (stringInterface == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				// Ogre returns false for a parameter the concrete emitter or
				// affector type never published, which is exactly the
				// "unknown parameter" answer Lua needs.
				return fn(stringInterface, parameter, value);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault(
					"[EXU::Particle] %s setParameter crashed object=%p parameter=%s value=%s code=0x%08X",
					what,
					stringInterface,
					parameter.c_str(),
					value.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetStringInterfaceParameter(
			void* stringInterface,
			const std::string& parameter,
			const std::string& value,
			const char* what)
		{
			return Seh::CatchCpp("TrySetStringInterfaceParameter", [&] { return TrySetStringInterfaceParameterSeh(stringInterface, parameter, value, what); }, false);
		}

		bool TryGetStringInterfaceParameterSeh(
			void* stringInterface,
			const std::string& parameter,
			std::string& outValue,
			const char* what)
		{
			outValue.clear();
			const auto fn = ResolveStringInterfaceGetParameter();
			if (stringInterface == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				InvokeGetStringInterfaceParameter(fn, stringInterface, parameter, outValue);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault(
					"[EXU::Particle] %s getParameter crashed object=%p parameter=%s code=0x%08X",
					what,
					stringInterface,
					parameter.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TryGetStringInterfaceParameter(
			void* stringInterface,
			const std::string& parameter,
			std::string& outValue,
			const char* what)
		{
			return Seh::CatchCpp("TryGetStringInterfaceParameter", [&] { return TryGetStringInterfaceParameterSeh(stringInterface, parameter, outValue, what); }, false);
		}

		bool TryGetStringInterfaceParameterNamesSeh(
			void* stringInterface,
			std::vector<std::string>& outNames,
			const char* what)
		{
			outNames.clear();
			const auto fn = ResolveStringInterfaceGetParameters();
			if (stringInterface == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				return InvokeGetStringInterfaceParameterNames(fn, stringInterface, outNames);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault(
					"[EXU::Particle] %s getParameters crashed object=%p code=0x%08X",
					what,
					stringInterface,
					GetExceptionCode());
				return false;
			}
		}

		bool TryGetStringInterfaceParameterNames(
			void* stringInterface,
			std::vector<std::string>& outNames,
			const char* what)
		{
			return Seh::CatchCpp("TryGetStringInterfaceParameterNames", [&] { return TryGetStringInterfaceParameterNamesSeh(stringInterface, outNames, what); }, false);
		}

		bool TrySetEmitterEnabledSeh(void* emitter, bool enabled)
		{
			const auto fn = ResolveEmitterSetEnabled();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, enabled);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] emitter setEnabled crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterEnabled(void* emitter, bool enabled)
		{
			return Seh::CatchCpp("TrySetEmitterEnabled", [&] { return TrySetEmitterEnabledSeh(emitter, enabled); }, false);
		}

		bool TrySetEmitterEmissionRateSeh(void* emitter, float rate)
		{
			const auto fn = ResolveEmitterSetEmissionRate();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, rate);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] emitter setEmissionRate crashed emitter=%p rate=%g code=0x%08X", emitter, rate, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterEmissionRate(void* emitter, float rate)
		{
			return Seh::CatchCpp("TrySetEmitterEmissionRate", [&] { return TrySetEmitterEmissionRateSeh(emitter, rate); }, false);
		}

		bool TryGetEmitterEmissionRateSeh(void* emitter, float& outRate)
		{
			outRate = 0.0f;
			const auto fn = ResolveEmitterGetEmissionRate();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				outRate = fn(emitter);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] emitter getEmissionRate crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TryGetEmitterEmissionRate(void* emitter, float& outRate)
		{
			return Seh::CatchCpp("TryGetEmitterEmissionRate", [&] { return TryGetEmitterEmissionRateSeh(emitter, outRate); }, false);
		}

		bool TrySetEmitterDirectionSeh(void* emitter, const BZR::VECTOR_3D& direction)
		{
			const auto fn = ResolveEmitterSetDirection();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, direction);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] emitter setDirection crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterDirection(void* emitter, const BZR::VECTOR_3D& direction)
		{
			return Seh::CatchCpp("TrySetEmitterDirection", [&] { return TrySetEmitterDirectionSeh(emitter, direction); }, false);
		}

		bool TrySetEmitterPositionSeh(void* emitter, const BZR::VECTOR_3D& position)
		{
			const auto fn = ResolveEmitterSetPosition();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, position);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] emitter setPosition crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterPosition(void* emitter, const BZR::VECTOR_3D& position)
		{
			return Seh::CatchCpp("TrySetEmitterPosition", [&] { return TrySetEmitterPositionSeh(emitter, position); }, false);
		}

		bool TrySetEmitterVelocityRangeSeh(void* emitter, float minVelocity, float maxVelocity)
		{
			const auto fn = ResolveEmitterSetVelocityRange();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, minVelocity, maxVelocity);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] emitter setParticleVelocity crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterVelocityRange(void* emitter, float minVelocity, float maxVelocity)
		{
			return Seh::CatchCpp("TrySetEmitterVelocityRange", [&] { return TrySetEmitterVelocityRangeSeh(emitter, minVelocity, maxVelocity); }, false);
		}

		bool TrySetEmitterAngleSeh(void* emitter, float radians)
		{
			const auto fn = ResolveEmitterSetAngle();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, radians);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] emitter setAngle crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterAngle(void* emitter, float radians)
		{
			return Seh::CatchCpp("TrySetEmitterAngle", [&] { return TrySetEmitterAngleSeh(emitter, radians); }, false);
		}

		bool TrySetEmitterTimeToLiveRangeSeh(void* emitter, float minTimeToLive, float maxTimeToLive)
		{
			const auto fn = ResolveEmitterSetTimeToLiveRange();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, minTimeToLive, maxTimeToLive);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] emitter setTimeToLive crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterTimeToLiveRange(void* emitter, float minTimeToLive, float maxTimeToLive)
		{
			return Seh::CatchCpp("TrySetEmitterTimeToLiveRange", [&] { return TrySetEmitterTimeToLiveRangeSeh(emitter, minTimeToLive, maxTimeToLive); }, false);
		}

		bool TrySetEmitterColourRangeSeh(void* emitter, const Ogre::Color& startColor, const Ogre::Color& endColor)
		{
			const auto fn = ResolveEmitterSetColourRange();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, startColor, endColor);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] emitter setColour crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterColourRange(void* emitter, const Ogre::Color& startColor, const Ogre::Color& endColor)
		{
			return Seh::CatchCpp("TrySetEmitterColourRange", [&] { return TrySetEmitterColourRangeSeh(emitter, startColor, endColor); }, false);
		}

		// Undoes any of the three attachments: the system goes back onto its own
		// EXU-owned node, and that node goes back under the scene root.
		bool TryReturnManagedParticleToOwnNodeSeh(void* sceneManager, const std::string& name)
		{
			const auto detachFromParentFn = ResolveMovableObjectDetachFromParent();
			const auto attachObjectFn = ResolveAttachObject();
			void* movableObject = nullptr;
			void* node = nullptr;
			if (detachFromParentFn == nullptr || attachObjectFn == nullptr ||
				!TryGetParticleMovableObject(sceneManager, name, movableObject) ||
				!TryGetManagedParticleSceneNode(sceneManager, name, node))
			{
				return false;
			}

			if (!TryAttachManagedParticleToRoot(sceneManager, name))
			{
				return false;
			}

			__try
			{
				// A bone attachment moved the system off its own node; a camera or
				// object attachment only moved the node, so this is a no-op there.
				// Both entry points are MovableObject's, so they take the re-based
				// pointer, not the ParticleSystem*.
				detachFromParentFn(movableObject);
				attachObjectFn(node, movableObject);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] detach crashed name=%s code=0x%08X", name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryReturnManagedParticleToOwnNode(void* sceneManager, const std::string& name)
		{
			return Seh::CatchCpp("TryReturnManagedParticleToOwnNode", [&] { return TryReturnManagedParticleToOwnNodeSeh(sceneManager, name); }, false);
		}

		bool TrySetParticleSystemNonVisibleUpdateTimeoutSeh(void* sceneManager, const std::string& name, float timeout)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetNonVisibleUpdateTimeout();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, timeout);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogEnvironmentFault("[EXU::Particle] setNonVisibleUpdateTimeout crashed name=%s code=0x%08X", name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemNonVisibleUpdateTimeout(void* sceneManager, const std::string& name, float timeout)
		{
			return Seh::CatchCpp("TrySetParticleSystemNonVisibleUpdateTimeout", [&] { return TrySetParticleSystemNonVisibleUpdateTimeoutSeh(sceneManager, name, timeout); }, false);
		}

		// Mission teardown: the scene these followers point into is gone, so the
		// list must not survive into the next mission's scene manager.
		void ForgetAllParticleCameraFollowers()
		{
			g_particleCameraFollowers.clear();
		}
}
