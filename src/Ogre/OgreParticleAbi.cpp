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

#include "OgreParticleAbi.h"

namespace ExtraUtilities::Lua::Environment
{
		CreateParticleSystemFn ResolveCreateParticleSystem()
		{
			static CreateParticleSystemFn fn = ResolveOgreProc<CreateParticleSystemFn>("?createParticleSystem@SceneManager@Ogre@@UAEPAVParticleSystem@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z");
			return fn;
		}

		DestroyParticleSystemFn ResolveDestroyParticleSystem()
		{
			static DestroyParticleSystemFn fn = ResolveOgreProc<DestroyParticleSystemFn>("?destroyParticleSystem@SceneManager@Ogre@@UAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		GetParticleSystemFn ResolveGetParticleSystem()
		{
			static GetParticleSystemFn fn = ResolveOgreProc<GetParticleSystemFn>("?getParticleSystem@SceneManager@Ogre@@UBEPAVParticleSystem@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		HasParticleSystemFn ResolveHasParticleSystem()
		{
			static HasParticleSystemFn fn = ResolveOgreProc<HasParticleSystemFn>("?hasParticleSystem@SceneManager@Ogre@@UBE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		GetRootSceneNodeFn ResolveGetRootSceneNode()
		{
			static GetRootSceneNodeFn fn = ResolveOgreProc<GetRootSceneNodeFn>("?getRootSceneNode@SceneManager@Ogre@@UAEPAVSceneNode@2@XZ");
			return fn;
		}

		CreateChildSceneNodeFn ResolveCreateChildSceneNode()
		{
			static CreateChildSceneNodeFn fn = ResolveOgreProc<CreateChildSceneNodeFn>("?createChildSceneNode@SceneNode@Ogre@@UAEPAV12@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@ABVVector3@2@ABVQuaternion@2@@Z");
			return fn;
		}

		GetSceneNodeFn ResolveGetSceneNode()
		{
			static GetSceneNodeFn fn = ResolveOgreProc<GetSceneNodeFn>("?getSceneNode@SceneManager@Ogre@@UBEPAVSceneNode@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		HasSceneNodeFn ResolveHasSceneNode()
		{
			static HasSceneNodeFn fn = ResolveOgreProc<HasSceneNodeFn>("?hasSceneNode@SceneManager@Ogre@@UBE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		DestroySceneNodeFn ResolveDestroySceneNode()
		{
			static DestroySceneNodeFn fn = ResolveOgreProc<DestroySceneNodeFn>("?destroySceneNode@SceneManager@Ogre@@UAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		AttachObjectFn ResolveAttachObject()
		{
			static AttachObjectFn fn = ResolveOgreProc<AttachObjectFn>("?attachObject@SceneNode@Ogre@@UAEXPAVMovableObject@2@@Z");
			return fn;
		}

		GetMovableObjectFn ResolveGetMovableObject()
		{
			static GetMovableObjectFn fn = ResolveOgreProc<GetMovableObjectFn>("?getMovableObject@SceneManager@Ogre@@UBEPAVMovableObject@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z");
			return fn;
		}

		HasMovableObjectFn ResolveHasMovableObject()
		{
			static HasMovableObjectFn fn = ResolveOgreProc<HasMovableObjectFn>("?hasMovableObject@SceneManager@Ogre@@UBE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z");
			return fn;
		}

		// The factory type name is an exported Ogre::String living in OgreMain,
		// so we read the engine's own copy rather than spelling it out here.
		const std::string* ResolveParticleSystemFactoryTypeName()
		{
			static const std::string* name = ResolveOgreProc<const std::string*>("?FACTORY_TYPE_NAME@ParticleSystemFactory@Ogre@@2V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@A");
			return name;
		}

		SetNodePositionFn ResolveSetNodePosition()
		{
			static SetNodePositionFn fn = ResolveOgreProc<SetNodePositionFn>("?setPosition@Node@Ogre@@UAEXABVVector3@2@@Z");
			return fn;
		}

		SetSceneNodeDirectionFn ResolveSetSceneNodeDirection()
		{
			static SetSceneNodeDirectionFn fn = ResolveOgreProc<SetSceneNodeDirectionFn>("?setDirection@SceneNode@Ogre@@UAEXABVVector3@2@W4TransformSpace@Node@2@0@Z");
			return fn;
		}

		SetParticleSystemEmittingFn ResolveSetParticleSystemEmitting()
		{
			static SetParticleSystemEmittingFn fn = ResolveOgreProc<SetParticleSystemEmittingFn>("?setEmitting@ParticleSystem@Ogre@@QAEX_N@Z");
			return fn;
		}

		SetParticleSystemSpeedFactorFn ResolveSetParticleSystemSpeedFactor()
		{
			static SetParticleSystemSpeedFactorFn fn = ResolveOgreProc<SetParticleSystemSpeedFactorFn>("?setSpeedFactor@ParticleSystem@Ogre@@QAEXM@Z");
			return fn;
		}

		SetParticleSystemKeepLocalSpaceFn ResolveSetParticleSystemKeepLocalSpace()
		{
			static SetParticleSystemKeepLocalSpaceFn fn = ResolveOgreProc<SetParticleSystemKeepLocalSpaceFn>("?setKeepParticlesInLocalSpace@ParticleSystem@Ogre@@QAEX_N@Z");
			return fn;
		}

		SetParticleSystemMaterialFn ResolveSetParticleSystemMaterial()
		{
			static SetParticleSystemMaterialFn fn = ResolveOgreProc<SetParticleSystemMaterialFn>("?setMaterialName@ParticleSystem@Ogre@@UAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z");
			return fn;
		}

		SetParticleSystemRenderQueueGroupFn ResolveSetParticleSystemRenderQueueGroup()
		{
			static SetParticleSystemRenderQueueGroupFn fn = ResolveOgreProc<SetParticleSystemRenderQueueGroupFn>("?setRenderQueueGroup@ParticleSystem@Ogre@@UAEXE@Z");
			return fn;
		}

		SetParticleSystemParticleQuotaFn ResolveSetParticleSystemParticleQuota()
		{
			static SetParticleSystemParticleQuotaFn fn = ResolveOgreProc<SetParticleSystemParticleQuotaFn>("?setParticleQuota@ParticleSystem@Ogre@@QAEXI@Z");
			return fn;
		}

		SetParticleSystemDefaultDimensionsFn ResolveSetParticleSystemDefaultDimensions()
		{
			static SetParticleSystemDefaultDimensionsFn fn = ResolveOgreProc<SetParticleSystemDefaultDimensionsFn>("?setDefaultDimensions@ParticleSystem@Ogre@@UAEXMM@Z");
			return fn;
		}

		SetMovableObjectVisibleFn ResolveSetMovableObjectVisible()
		{
			static SetMovableObjectVisibleFn fn = ResolveOgreProc<SetMovableObjectVisibleFn>("?setVisible@MovableObject@Ogre@@UAEX_N@Z");
			return fn;
		}

		GetParentSceneNodeFn ResolveGetParentSceneNode()
		{
			static GetParentSceneNodeFn fn = ResolveOgreProc<GetParentSceneNodeFn>("?getParentSceneNode@MovableObject@Ogre@@UBEPAVSceneNode@2@XZ");
			return fn;
		}

		NodeGetParentFn ResolveNodeGetParent()
		{
			static NodeGetParentFn fn = ResolveOgreProc<NodeGetParentFn>("?getParent@Node@Ogre@@UBEPAV12@XZ");
			return fn;
		}

		NodeAddChildFn ResolveNodeAddChild()
		{
			static NodeAddChildFn fn = ResolveOgreProc<NodeAddChildFn>("?addChild@Node@Ogre@@UAEXPAV12@@Z");
			return fn;
		}

		NodeRemoveChildPtrFn ResolveNodeRemoveChildPtr()
		{
			static NodeRemoveChildPtrFn fn = ResolveOgreProc<NodeRemoveChildPtrFn>("?removeChild@Node@Ogre@@UAEPAV12@PAV12@@Z");
			return fn;
		}

		NodeSetInheritOrientationFn ResolveNodeSetInheritOrientation()
		{
			static NodeSetInheritOrientationFn fn = ResolveOgreProc<NodeSetInheritOrientationFn>("?setInheritOrientation@Node@Ogre@@UAEX_N@Z");
			return fn;
		}

		NodeSetInheritScaleFn ResolveNodeSetInheritScale()
		{
			static NodeSetInheritScaleFn fn = ResolveOgreProc<NodeSetInheritScaleFn>("?setInheritScale@Node@Ogre@@UAEX_N@Z");
			return fn;
		}

		CameraGetDerivedPositionFn ResolveCameraGetDerivedPosition()
		{
			static CameraGetDerivedPositionFn fn = ResolveOgreProc<CameraGetDerivedPositionFn>("?getDerivedPosition@Camera@Ogre@@QBEABVVector3@2@XZ");
			return fn;
		}

		MovableObjectDetachFromParentFn ResolveMovableObjectDetachFromParent()
		{
			static MovableObjectDetachFromParentFn fn = ResolveOgreProc<MovableObjectDetachFromParentFn>("?detachFromParent@MovableObject@Ogre@@UAEXXZ");
			return fn;
		}

		EntityHasSkeletonFn ResolveEntityHasSkeleton()
		{
			static EntityHasSkeletonFn fn = ResolveOgreProc<EntityHasSkeletonFn>("?hasSkeleton@Entity@Ogre@@QBE_NXZ");
			return fn;
		}

		EntityAttachObjectToBoneFn ResolveEntityAttachObjectToBone()
		{
			static EntityAttachObjectToBoneFn fn = ResolveOgreProc<EntityAttachObjectToBoneFn>("?attachObjectToBone@Entity@Ogre@@QAEPAVTagPoint@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PAVMovableObject@2@ABVQuaternion@2@ABVVector3@2@@Z");
			return fn;
		}

		GetNumEmittersFn ResolveGetNumEmitters()
		{
			static GetNumEmittersFn fn = ResolveOgreProc<GetNumEmittersFn>("?getNumEmitters@ParticleSystem@Ogre@@QBEGXZ");
			return fn;
		}

		GetEmitterFn ResolveGetEmitter()
		{
			static GetEmitterFn fn = ResolveOgreProc<GetEmitterFn>("?getEmitter@ParticleSystem@Ogre@@QBEPAVParticleEmitter@2@G@Z");
			return fn;
		}

		OgreAbi::ParticleSystemGetNumAffectorsFn ResolveGetNumAffectors()
		{
			static auto fn = ResolveOgreProc<OgreAbi::ParticleSystemGetNumAffectorsFn>(
				OgreAbi::kParticleSystemGetNumAffectors);
			return fn;
		}

		OgreAbi::ParticleSystemGetAffectorFn ResolveGetAffector()
		{
			static auto fn = ResolveOgreProc<OgreAbi::ParticleSystemGetAffectorFn>(
				OgreAbi::kParticleSystemGetAffector);
			return fn;
		}

		// Emitters and affectors both inherit Ogre::StringInterface, so one
		// resolved setParameter/getParameter pair drives every type-specific
		// ParticleFX property for both.
		OgreAbi::StringInterfaceSetParameterFn ResolveStringInterfaceSetParameter()
		{
			static auto fn = ResolveOgreProc<OgreAbi::StringInterfaceSetParameterFn>(
				OgreAbi::kStringInterfaceSetParameter);
			return fn;
		}

		OgreAbi::StringInterfaceGetParameterFn ResolveStringInterfaceGetParameter()
		{
			static auto fn = ResolveOgreProc<OgreAbi::StringInterfaceGetParameterFn>(
				OgreAbi::kStringInterfaceGetParameter);
			return fn;
		}

		OgreAbi::StringInterfaceGetParametersFn ResolveStringInterfaceGetParameters()
		{
			static auto fn = ResolveOgreProc<OgreAbi::StringInterfaceGetParametersFn>(
				OgreAbi::kStringInterfaceGetParameters);
			return fn;
		}

		OgreAbi::GetTypeNameFn ResolveEmitterGetType()
		{
			static auto fn = ResolveOgreProc<OgreAbi::GetTypeNameFn>(OgreAbi::kParticleEmitterGetType);
			return fn;
		}

		OgreAbi::GetTypeNameFn ResolveAffectorGetType()
		{
			static auto fn = ResolveOgreProc<OgreAbi::GetTypeNameFn>(OgreAbi::kParticleAffectorGetType);
			return fn;
		}

		EmitterSetEnabledFn ResolveEmitterSetEnabled()
		{
			static EmitterSetEnabledFn fn = ResolveOgreProc<EmitterSetEnabledFn>("?setEnabled@ParticleEmitter@Ogre@@UAEX_N@Z");
			return fn;
		}

		EmitterSetEmissionRateFn ResolveEmitterSetEmissionRate()
		{
			static EmitterSetEmissionRateFn fn = ResolveOgreProc<EmitterSetEmissionRateFn>("?setEmissionRate@ParticleEmitter@Ogre@@UAEXM@Z");
			return fn;
		}

		EmitterGetEmissionRateFn ResolveEmitterGetEmissionRate()
		{
			static EmitterGetEmissionRateFn fn = ResolveOgreProc<EmitterGetEmissionRateFn>("?getEmissionRate@ParticleEmitter@Ogre@@UBEMXZ");
			return fn;
		}

		EmitterSetDirectionFn ResolveEmitterSetDirection()
		{
			static EmitterSetDirectionFn fn = ResolveOgreProc<EmitterSetDirectionFn>("?setDirection@ParticleEmitter@Ogre@@UAEXABVVector3@2@@Z");
			return fn;
		}

		EmitterSetPositionFn ResolveEmitterSetPosition()
		{
			static EmitterSetPositionFn fn = ResolveOgreProc<EmitterSetPositionFn>("?setPosition@ParticleEmitter@Ogre@@UAEXABVVector3@2@@Z");
			return fn;
		}

		EmitterSetVelocityRangeFn ResolveEmitterSetVelocityRange()
		{
			static EmitterSetVelocityRangeFn fn = ResolveOgreProc<EmitterSetVelocityRangeFn>("?setParticleVelocity@ParticleEmitter@Ogre@@UAEXMM@Z");
			return fn;
		}

		EmitterSetAngleFn ResolveEmitterSetAngle()
		{
			static EmitterSetAngleFn fn = ResolveOgreProc<EmitterSetAngleFn>("?setAngle@ParticleEmitter@Ogre@@UAEXABVRadian@2@@Z");
			return fn;
		}

		EmitterSetTimeToLiveRangeFn ResolveEmitterSetTimeToLiveRange()
		{
			static EmitterSetTimeToLiveRangeFn fn = ResolveOgreProc<EmitterSetTimeToLiveRangeFn>("?setTimeToLive@ParticleEmitter@Ogre@@UAEXMM@Z");
			return fn;
		}

		EmitterSetColourRangeFn ResolveEmitterSetColourRange()
		{
			static EmitterSetColourRangeFn fn = ResolveOgreProc<EmitterSetColourRangeFn>("?setColour@ParticleEmitter@Ogre@@UAEXABVColourValue@2@0@Z");
			return fn;
		}

		SetNonVisibleUpdateTimeoutFn ResolveSetNonVisibleUpdateTimeout()
		{
			static SetNonVisibleUpdateTimeoutFn fn = ResolveOgreProc<SetNonVisibleUpdateTimeoutFn>("?setNonVisibleUpdateTimeout@ParticleSystem@Ogre@@QAEXM@Z");
			return fn;
		}

		MovableObjectGetParentNodeFn ResolveMovableObjectGetParentNode()
		{
			static MovableObjectGetParentNodeFn fn = ResolveOgreProc<MovableObjectGetParentNodeFn>("?getParentNode@MovableObject@Ogre@@UBEPAVNode@2@XZ");
			return fn;
		}

		MovableObjectIsParentTagPointFn ResolveMovableObjectIsParentTagPoint()
		{
			static MovableObjectIsParentTagPointFn fn = ResolveOgreProc<MovableObjectIsParentTagPointFn>("?isParentTagPoint@MovableObject@Ogre@@UBE_NXZ");
			return fn;
		}

		TagPointGetParentEntityFn ResolveTagPointGetParentEntity()
		{
			static TagPointGetParentEntityFn fn = ResolveOgreProc<TagPointGetParentEntityFn>("?getParentEntity@TagPoint@Ogre@@QBEPAVEntity@2@XZ");
			return fn;
		}

		EntityGetSkeletonFn ResolveEntityGetSkeleton()
		{
			static EntityGetSkeletonFn fn = ResolveOgreProc<EntityGetSkeletonFn>("?getSkeleton@Entity@Ogre@@QBEPAVSkeletonInstance@2@XZ");
			return fn;
		}

		SkeletonHasBoneFn ResolveSkeletonHasBone()
		{
			static SkeletonHasBoneFn fn = ResolveOgreProc<SkeletonHasBoneFn>("?hasBone@Skeleton@Ogre@@UBE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}
}
