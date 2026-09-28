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

#include "OgreSceneRuntime.h"

#include "bzr.h"
#include "Ogre/Ogre.h"
#include "Ogre/OgreStringInterfaceShim.h"

#include <cstdint>
#include <string>

// Ogre ABI for particle systems: the SceneManager, SceneNode, ParticleSystem,
// ParticleEmitter, ParticleAffector, Camera and Entity entry points the
// particle runtime calls, resolved from OgreMain.dll by mangled name.

namespace ExtraUtilities::Lua::Environment
{
		constexpr int kOgreTransformSpaceLocal = 0;

		using CreateParticleSystemFn = void*(__thiscall*)(void*, const std::string&, const std::string&);
		using DestroyParticleSystemFn = void(__thiscall*)(void*, const std::string&);
		using GetParticleSystemFn = void*(__thiscall*)(void*, const std::string&);
		using HasParticleSystemFn = bool(__thiscall*)(void*, const std::string&);
		using GetRootSceneNodeFn = void*(__thiscall*)(void*);
		using CreateChildSceneNodeFn = void*(__thiscall*)(void*, const std::string&, const BZR::VECTOR_3D&, const OgreQuaternionValue&);
		using GetSceneNodeFn = void*(__thiscall*)(void*, const std::string&);
		using HasSceneNodeFn = bool(__thiscall*)(void*, const std::string&);
		using DestroySceneNodeFn = void(__thiscall*)(void*, const std::string&);
		using AttachObjectFn = void(__thiscall*)(void*, void*);
		using GetMovableObjectFn = void*(__thiscall*)(void*, const std::string&, const std::string&);
		using HasMovableObjectFn = bool(__thiscall*)(void*, const std::string&, const std::string&);
		using SetNodePositionFn = void(__thiscall*)(void*, const BZR::VECTOR_3D&);
		using SetSceneNodeDirectionFn = void(__thiscall*)(void*, const BZR::VECTOR_3D&, int, const BZR::VECTOR_3D&);
		using SetParticleSystemEmittingFn = void(__thiscall*)(void*, bool);
		using SetParticleSystemSpeedFactorFn = void(__thiscall*)(void*, float);
		using SetParticleSystemKeepLocalSpaceFn = void(__thiscall*)(void*, bool);
		using SetParticleSystemMaterialFn = void(__thiscall*)(void*, const std::string&, const std::string&);
		using SetParticleSystemRenderQueueGroupFn = void(__thiscall*)(void*, uint8_t);
		using SetParticleSystemParticleQuotaFn = void(__thiscall*)(void*, uint32_t);
		using SetParticleSystemDefaultDimensionsFn = void(__thiscall*)(void*, float, float);
		using SetMovableObjectVisibleFn = void(__thiscall*)(void*, bool);
		CreateParticleSystemFn ResolveCreateParticleSystem();
		DestroyParticleSystemFn ResolveDestroyParticleSystem();
		GetParticleSystemFn ResolveGetParticleSystem();
		HasParticleSystemFn ResolveHasParticleSystem();
		GetRootSceneNodeFn ResolveGetRootSceneNode();
		CreateChildSceneNodeFn ResolveCreateChildSceneNode();
		GetSceneNodeFn ResolveGetSceneNode();
		HasSceneNodeFn ResolveHasSceneNode();
		DestroySceneNodeFn ResolveDestroySceneNode();
		AttachObjectFn ResolveAttachObject();
		GetMovableObjectFn ResolveGetMovableObject();
		HasMovableObjectFn ResolveHasMovableObject();
		const std::string* ResolveParticleSystemFactoryTypeName();
		SetNodePositionFn ResolveSetNodePosition();
		SetSceneNodeDirectionFn ResolveSetSceneNodeDirection();
		SetParticleSystemEmittingFn ResolveSetParticleSystemEmitting();
		SetParticleSystemSpeedFactorFn ResolveSetParticleSystemSpeedFactor();
		SetParticleSystemKeepLocalSpaceFn ResolveSetParticleSystemKeepLocalSpace();
		SetParticleSystemMaterialFn ResolveSetParticleSystemMaterial();
		SetParticleSystemRenderQueueGroupFn ResolveSetParticleSystemRenderQueueGroup();
		SetParticleSystemParticleQuotaFn ResolveSetParticleSystemParticleQuota();
		SetParticleSystemDefaultDimensionsFn ResolveSetParticleSystemDefaultDimensions();
		SetMovableObjectVisibleFn ResolveSetMovableObjectVisible();

		using GetParentSceneNodeFn = void*(__thiscall*)(void*);
		using NodeGetParentFn = void*(__thiscall*)(void*);
		using NodeAddChildFn = void(__thiscall*)(void*, void*);
		using NodeRemoveChildPtrFn = void*(__thiscall*)(void*, void*);
		using NodeSetInheritOrientationFn = void(__thiscall*)(void*, bool);
		using NodeSetInheritScaleFn = void(__thiscall*)(void*, bool);
		using CameraGetDerivedPositionFn = const BZR::VECTOR_3D*(__thiscall*)(void*);
		using MovableObjectDetachFromParentFn = void(__thiscall*)(void*);
		using EntityHasSkeletonFn = bool(__thiscall*)(void*);
		using EntityAttachObjectToBoneFn = void*(__thiscall*)(void*, const std::string&, void*, const OgreQuaternionValue&, const BZR::VECTOR_3D&);
		using GetNumEmittersFn = uint16_t(__thiscall*)(void*);
		using GetEmitterFn = void*(__thiscall*)(void*, uint16_t);
		using EmitterSetEnabledFn = void(__thiscall*)(void*, bool);
		using EmitterSetEmissionRateFn = void(__thiscall*)(void*, float);
		using EmitterGetEmissionRateFn = float(__thiscall*)(void*);
		using EmitterSetDirectionFn = void(__thiscall*)(void*, const BZR::VECTOR_3D&);
		using EmitterSetPositionFn = void(__thiscall*)(void*, const BZR::VECTOR_3D&);
		using EmitterSetVelocityRangeFn = void(__thiscall*)(void*, float, float);
		using EmitterSetAngleFn = void(__thiscall*)(void*, const float&);
		using EmitterSetTimeToLiveRangeFn = void(__thiscall*)(void*, float, float);
		using EmitterSetColourRangeFn = void(__thiscall*)(void*, const Ogre::Color&, const Ogre::Color&);
		using SetNonVisibleUpdateTimeoutFn = void(__thiscall*)(void*, float);
		GetParentSceneNodeFn ResolveGetParentSceneNode();
		NodeGetParentFn ResolveNodeGetParent();
		NodeAddChildFn ResolveNodeAddChild();
		NodeRemoveChildPtrFn ResolveNodeRemoveChildPtr();
		NodeSetInheritOrientationFn ResolveNodeSetInheritOrientation();
		NodeSetInheritScaleFn ResolveNodeSetInheritScale();
		CameraGetDerivedPositionFn ResolveCameraGetDerivedPosition();
		MovableObjectDetachFromParentFn ResolveMovableObjectDetachFromParent();
		EntityHasSkeletonFn ResolveEntityHasSkeleton();
		EntityAttachObjectToBoneFn ResolveEntityAttachObjectToBone();
		GetNumEmittersFn ResolveGetNumEmitters();
		GetEmitterFn ResolveGetEmitter();
		OgreAbi::ParticleSystemGetNumAffectorsFn ResolveGetNumAffectors();
		OgreAbi::ParticleSystemGetAffectorFn ResolveGetAffector();
		OgreAbi::StringInterfaceSetParameterFn ResolveStringInterfaceSetParameter();
		OgreAbi::StringInterfaceGetParameterFn ResolveStringInterfaceGetParameter();
		OgreAbi::StringInterfaceGetParametersFn ResolveStringInterfaceGetParameters();
		OgreAbi::GetTypeNameFn ResolveEmitterGetType();
		OgreAbi::GetTypeNameFn ResolveAffectorGetType();
		EmitterSetEnabledFn ResolveEmitterSetEnabled();
		EmitterSetEmissionRateFn ResolveEmitterSetEmissionRate();
		EmitterGetEmissionRateFn ResolveEmitterGetEmissionRate();
		EmitterSetDirectionFn ResolveEmitterSetDirection();
		EmitterSetPositionFn ResolveEmitterSetPosition();
		EmitterSetVelocityRangeFn ResolveEmitterSetVelocityRange();
		EmitterSetAngleFn ResolveEmitterSetAngle();
		EmitterSetTimeToLiveRangeFn ResolveEmitterSetTimeToLiveRange();
		EmitterSetColourRangeFn ResolveEmitterSetColourRange();
		SetNonVisibleUpdateTimeoutFn ResolveSetNonVisibleUpdateTimeout();
}
