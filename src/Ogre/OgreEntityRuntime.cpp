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

#include "OgreEntityRuntime.h"
#include "OgreMaterialRuntime.h"

#include "Ogre/Ogre.h"
#include "Util/SehGuard.h"

#include <Windows.h>

namespace ExtraUtilities::Lua::GameObject
{
	namespace
	{
		bool TryHasSkeletonSeh(void* entity, bool& outHasSkeleton)
		{
			__try
			{
				outHasSkeleton = Ogre::HasSkeleton(entity);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Animation] HasSkeleton crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outHasSkeleton = false;
				return false;
			}
		}

		bool TryHasSkeleton(void* entity, bool& outHasSkeleton)
		{
			return Seh::CatchCpp("TryHasSkeleton", [&] { return TryHasSkeletonSeh(entity, outHasSkeleton); }, [&] { outHasSkeleton = false; return false; });
		}

		bool TryGetAllAnimationStatesSeh(void* entity, void*& outStates)
		{
			__try
			{
				outStates = Ogre::GetAllAnimationStates(entity);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Animation] GetAllAnimationStates crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outStates = nullptr;
				return false;
			}
		}

		bool TryGetAllAnimationStates(void* entity, void*& outStates)
		{
			return Seh::CatchCpp("TryGetAllAnimationStates", [&] { return TryGetAllAnimationStatesSeh(entity, outStates); }, [&] { outStates = nullptr; return false; });
		}

		bool TryHasAnimationStateSeh(void* animationStates, const std::string& name, bool& outHasAnimation)
		{
			__try
			{
				outHasAnimation = Ogre::HasAnimationState(animationStates, name);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Animation] HasAnimationState crashed states=%p name=%s code=0x%08X", animationStates, name.c_str(), GetExceptionCode());
				outHasAnimation = false;
				return false;
			}
		}

		bool TryHasAnimationState(void* animationStates, const std::string& name, bool& outHasAnimation)
		{
			return Seh::CatchCpp("TryHasAnimationState", [&] { return TryHasAnimationStateSeh(animationStates, name, outHasAnimation); }, [&] { outHasAnimation = false; return false; });
		}

		bool TryGetAnimationStateSeh(void* animationStates, const std::string& name, void*& outAnimationState)
		{
			__try
			{
				outAnimationState = Ogre::GetAnimationState(animationStates, name);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Animation] GetAnimationState crashed states=%p name=%s code=0x%08X", animationStates, name.c_str(), GetExceptionCode());
				outAnimationState = nullptr;
				return false;
			}
		}

		bool TryGetAnimationState(void* animationStates, const std::string& name, void*& outAnimationState)
		{
			return Seh::CatchCpp("TryGetAnimationState", [&] { return TryGetAnimationStateSeh(animationStates, name, outAnimationState); }, [&] { outAnimationState = nullptr; return false; });
		}
	}

	namespace Detail
	{
		bool TryGetNumSubEntitiesSeh(void* entity, uint32_t& outCount)
		{
			__try
			{
				outCount = Ogre::GetNumSubEntities(entity);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] GetNumSubEntities crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outCount = 0;
				return false;
			}
		}

		bool TryGetNumSubEntities(void* entity, uint32_t& outCount)
		{
			return Seh::CatchCpp("TryGetNumSubEntities", [&] { return TryGetNumSubEntitiesSeh(entity, outCount); }, [&] { outCount = 0; return false; });
		}

		bool TryGetSubEntityByIndexSeh(void* entity, uint32_t index, void*& outSubEntity)
		{
			__try
			{
				outSubEntity = Ogre::GetSubEntityByIndex(entity, index);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] GetSubEntity crashed entity=%p index=%u code=0x%08X", entity, index, GetExceptionCode());
				outSubEntity = nullptr;
				return false;
			}
		}

		bool TryGetSubEntityByIndex(void* entity, uint32_t index, void*& outSubEntity)
		{
			return Seh::CatchCpp("TryGetSubEntityByIndex", [&] { return TryGetSubEntityByIndexSeh(entity, index, outSubEntity); }, [&] { outSubEntity = nullptr; return false; });
		}

		bool TryGetMaterialNameSeh(void* subEntity, std::string& outName)
		{
			__try
			{
				outName = Ogre::GetMaterialNameSubEntity(subEntity);
				CacheMaterialHandle(outName, ::Ogre::GetSubEntityMaterial(subEntity));
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] GetMaterialName crashed subEntity=%p code=0x%08X", subEntity, GetExceptionCode());
				outName.clear();
				return false;
			}
		}

		bool TryGetMaterialName(void* subEntity, std::string& outName)
		{
			return Seh::CatchCpp("TryGetMaterialName", [&] { return TryGetMaterialNameSeh(subEntity, outName); }, [&] { outName.clear(); return false; });
		}

		bool TrySetMaterialNameEntity(void* entity, const std::string& materialName, const std::string& resourceGroup)
		{
			MaterialHandle cachedMaterial{};
			if (TryGetCachedMaterial(materialName, cachedMaterial))
			{
				bool applied = false;
				Seh::Guard(
					"TrySetMaterialNameEntity(cached)",
					[&] { applied = ::Ogre::SetEntityMaterial(entity, cachedMaterial); },
					[&](unsigned long exceptionCode) {
						LogMaterialFault(
							"[EXU::Material] SetEntityMaterial cached apply crashed entity=%p material=%s code=0x%08X",
							entity,
							materialName.c_str(),
							exceptionCode);
					});
				if (applied)
				{
					return true;
				}
			}

			return Seh::Guard(
				"TrySetMaterialNameEntity",
				[&] { Ogre::SetMaterialNameEntity(entity, materialName, resourceGroup); },
				[&](unsigned long exceptionCode) {
					LogMaterialFault(
						"[EXU::Material] SetMaterialNameEntity crashed entity=%p material=%s group=%s code=0x%08X",
						entity,
						materialName.c_str(),
						resourceGroup.c_str(),
						exceptionCode);
				});
		}

		bool TrySetMaterialNameSubEntity(void* subEntity, const std::string& materialName, const std::string& resourceGroup)
		{
			MaterialHandle cachedMaterial{};
			if (TryGetCachedMaterial(materialName, cachedMaterial))
			{
				bool applied = false;
				Seh::Guard(
					"TrySetMaterialNameSubEntity(cached)",
					[&] { applied = ::Ogre::SetSubEntityMaterial(subEntity, cachedMaterial); },
					[&](unsigned long exceptionCode) {
						LogMaterialFault(
							"[EXU::Material] SetSubEntityMaterial cached apply crashed subEntity=%p material=%s code=0x%08X",
							subEntity,
							materialName.c_str(),
							exceptionCode);
					});
				if (applied)
				{
					return true;
				}
			}

			return Seh::Guard(
				"TrySetMaterialNameSubEntity",
				[&] { Ogre::SetMaterialNameSubEntity(subEntity, materialName, resourceGroup); },
				[&](unsigned long exceptionCode) {
					LogMaterialFault(
						"[EXU::Material] SetMaterialNameSubEntity crashed subEntity=%p material=%s group=%s code=0x%08X",
						subEntity,
						materialName.c_str(),
						resourceGroup.c_str(),
						exceptionCode);
				});
		}

		bool TryGetVisibleSeh(void* entity, bool& outVisible)
		{
			__try
			{
				outVisible = Ogre::GetVisible(entity);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] GetVisible crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outVisible = false;
				return false;
			}
		}

		bool TryGetVisible(void* entity, bool& outVisible)
		{
			return Seh::CatchCpp("TryGetVisible", [&] { return TryGetVisibleSeh(entity, outVisible); }, [&] { outVisible = false; return false; });
		}

		bool TrySetVisibleSeh(void* entity, bool visible)
		{
			__try
			{
				Ogre::SetVisible(entity, visible);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] SetVisible crashed entity=%p visible=%d code=0x%08X", entity, visible ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetVisible(void* entity, bool visible)
		{
			return Seh::CatchCpp("TrySetVisible", [&] { return TrySetVisibleSeh(entity, visible); }, false);
		}

		bool TryGetCastShadowsSeh(void* entity, bool& outCastShadows)
		{
			__try
			{
				outCastShadows = Ogre::GetCastShadows(entity);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] GetCastShadows crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outCastShadows = false;
				return false;
			}
		}

		bool TryGetCastShadows(void* entity, bool& outCastShadows)
		{
			return Seh::CatchCpp("TryGetCastShadows", [&] { return TryGetCastShadowsSeh(entity, outCastShadows); }, [&] { outCastShadows = false; return false; });
		}

		bool TrySetCastShadowsSeh(void* entity, bool castShadows)
		{
			__try
			{
				Ogre::SetCastShadows(entity, castShadows);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] SetCastShadows crashed entity=%p castShadows=%d code=0x%08X", entity, castShadows ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetCastShadows(void* entity, bool castShadows)
		{
			return Seh::CatchCpp("TrySetCastShadows", [&] { return TrySetCastShadowsSeh(entity, castShadows); }, false);
		}

		bool TryGetRenderingDistanceSeh(void* entity, float& outDistance)
		{
			__try
			{
				outDistance = Ogre::GetRenderingDistance(entity);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Render] GetRenderingDistance crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outDistance = 0.0f;
				return false;
			}
		}

		bool TryGetRenderingDistance(void* entity, float& outDistance)
		{
			return Seh::CatchCpp("TryGetRenderingDistance", [&] { return TryGetRenderingDistanceSeh(entity, outDistance); }, [&] { outDistance = 0.0f; return false; });
		}

		bool TrySetRenderingDistanceSeh(void* entity, float distance)
		{
			__try
			{
				Ogre::SetRenderingDistance(entity, distance);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Render] SetRenderingDistance crashed entity=%p distance=%g code=0x%08X", entity, distance, GetExceptionCode());
				return false;
			}
		}

		bool TrySetRenderingDistance(void* entity, float distance)
		{
			return Seh::CatchCpp("TrySetRenderingDistance", [&] { return TrySetRenderingDistanceSeh(entity, distance); }, false);
		}

		bool TryGetVisibilityFlagsSeh(void* entity, uint32_t& outFlags)
		{
			__try
			{
				outFlags = Ogre::GetVisibilityFlags(entity);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Render] GetVisibilityFlags crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outFlags = 0;
				return false;
			}
		}

		bool TryGetVisibilityFlags(void* entity, uint32_t& outFlags)
		{
			return Seh::CatchCpp("TryGetVisibilityFlags", [&] { return TryGetVisibilityFlagsSeh(entity, outFlags); }, [&] { outFlags = 0; return false; });
		}

		bool TrySetVisibilityFlagsSeh(void* entity, uint32_t flags)
		{
			__try
			{
				Ogre::SetVisibilityFlags(entity, flags);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Render] SetVisibilityFlags crashed entity=%p flags=0x%08X code=0x%08X", entity, flags, GetExceptionCode());
				return false;
			}
		}

		bool TrySetVisibilityFlags(void* entity, uint32_t flags)
		{
			return Seh::CatchCpp("TrySetVisibilityFlags", [&] { return TrySetVisibilityFlagsSeh(entity, flags); }, false);
		}

		bool TryGetQueryFlagsSeh(void* entity, uint32_t& outFlags)
		{
			__try
			{
				outFlags = Ogre::GetQueryFlags(entity);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Render] GetQueryFlags crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outFlags = 0;
				return false;
			}
		}

		bool TryGetQueryFlags(void* entity, uint32_t& outFlags)
		{
			return Seh::CatchCpp("TryGetQueryFlags", [&] { return TryGetQueryFlagsSeh(entity, outFlags); }, [&] { outFlags = 0; return false; });
		}

		bool TrySetQueryFlagsSeh(void* entity, uint32_t flags)
		{
			__try
			{
				Ogre::SetQueryFlags(entity, flags);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Render] SetQueryFlags crashed entity=%p flags=0x%08X code=0x%08X", entity, flags, GetExceptionCode());
				return false;
			}
		}

		bool TrySetQueryFlags(void* entity, uint32_t flags)
		{
			return Seh::CatchCpp("TrySetQueryFlags", [&] { return TrySetQueryFlagsSeh(entity, flags); }, false);
		}

		bool TryGetRenderQueueGroupSeh(void* renderable, uint8_t& outGroup)
		{
			__try
			{
				outGroup = Ogre::GetRenderQueueGroup(renderable);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Render] GetRenderQueueGroup crashed renderable=%p code=0x%08X", renderable, GetExceptionCode());
				outGroup = 0;
				return false;
			}
		}

		bool TryGetRenderQueueGroup(void* renderable, uint8_t& outGroup)
		{
			return Seh::CatchCpp("TryGetRenderQueueGroup", [&] { return TryGetRenderQueueGroupSeh(renderable, outGroup); }, [&] { outGroup = 0; return false; });
		}

		bool TrySetRenderQueueGroupSeh(void* entity, uint8_t group)
		{
			__try
			{
				Ogre::SetRenderQueueGroupMovable(entity, group);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Render] SetRenderQueueGroup crashed entity=%p group=%u code=0x%08X", entity, group, GetExceptionCode());
				return false;
			}
		}

		bool TrySetRenderQueueGroup(void* entity, uint8_t group)
		{
			return Seh::CatchCpp("TrySetRenderQueueGroup", [&] { return TrySetRenderQueueGroupSeh(entity, group); }, false);
		}

		void* GetNamedAnimationState(void* entity, const std::string& name)
		{
			bool hasSkeleton = false;
			if (!TryHasSkeleton(entity, hasSkeleton) || !hasSkeleton)
			{
				return nullptr;
			}

			void* animationStates = nullptr;
			if (!TryGetAllAnimationStates(entity, animationStates) || animationStates == nullptr)
			{
				return nullptr;
			}

			bool hasAnimation = false;
			if (!TryHasAnimationState(animationStates, name, hasAnimation) || !hasAnimation)
			{
				return nullptr;
			}

			void* animationState = nullptr;
			if (!TryGetAnimationState(animationStates, name, animationState))
			{
				return nullptr;
			}

			return animationState;
		}

		bool TryGetAnimationLengthSeh(void* animationState, float& outValue)
		{
			__try
			{
				outValue = Ogre::GetAnimationLength(animationState);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Animation] GetLength crashed animationState=%p code=0x%08X", animationState, GetExceptionCode());
				outValue = 0.0f;
				return false;
			}
		}

		bool TryGetAnimationLength(void* animationState, float& outValue)
		{
			return Seh::CatchCpp("TryGetAnimationLength", [&] { return TryGetAnimationLengthSeh(animationState, outValue); }, [&] { outValue = 0.0f; return false; });
		}

		bool TryGetAnimationTimePositionSeh(void* animationState, float& outValue)
		{
			__try
			{
				outValue = Ogre::GetAnimationTimePosition(animationState);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Animation] GetTimePosition crashed animationState=%p code=0x%08X", animationState, GetExceptionCode());
				outValue = 0.0f;
				return false;
			}
		}

		bool TryGetAnimationTimePosition(void* animationState, float& outValue)
		{
			return Seh::CatchCpp("TryGetAnimationTimePosition", [&] { return TryGetAnimationTimePositionSeh(animationState, outValue); }, [&] { outValue = 0.0f; return false; });
		}

		bool TryGetAnimationWeightSeh(void* animationState, float& outValue)
		{
			__try
			{
				outValue = Ogre::GetAnimationWeight(animationState);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Animation] GetWeight crashed animationState=%p code=0x%08X", animationState, GetExceptionCode());
				outValue = 0.0f;
				return false;
			}
		}

		bool TryGetAnimationWeight(void* animationState, float& outValue)
		{
			return Seh::CatchCpp("TryGetAnimationWeight", [&] { return TryGetAnimationWeightSeh(animationState, outValue); }, [&] { outValue = 0.0f; return false; });
		}

		bool TryGetAnimationLoopSeh(void* animationState, bool& outValue)
		{
			__try
			{
				outValue = Ogre::GetAnimationLoop(animationState);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Animation] GetLoop crashed animationState=%p code=0x%08X", animationState, GetExceptionCode());
				outValue = false;
				return false;
			}
		}

		bool TryGetAnimationLoop(void* animationState, bool& outValue)
		{
			return Seh::CatchCpp("TryGetAnimationLoop", [&] { return TryGetAnimationLoopSeh(animationState, outValue); }, [&] { outValue = false; return false; });
		}

		bool TryGetAnimationEnabledSeh(void* animationState, bool& outValue)
		{
			__try
			{
				outValue = Ogre::GetAnimationEnabled(animationState);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Animation] GetEnabled crashed animationState=%p code=0x%08X", animationState, GetExceptionCode());
				outValue = false;
				return false;
			}
		}

		bool TryGetAnimationEnabled(void* animationState, bool& outValue)
		{
			return Seh::CatchCpp("TryGetAnimationEnabled", [&] { return TryGetAnimationEnabledSeh(animationState, outValue); }, [&] { outValue = false; return false; });
		}

		bool TrySetAnimationEnabledSeh(void* animationState, bool enabled)
		{
			__try
			{
				Ogre::SetAnimationEnabled(animationState, enabled);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Animation] SetEnabled crashed animationState=%p enabled=%d code=0x%08X", animationState, enabled ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetAnimationEnabled(void* animationState, bool enabled)
		{
			return Seh::CatchCpp("TrySetAnimationEnabled", [&] { return TrySetAnimationEnabledSeh(animationState, enabled); }, false);
		}

		bool TrySetAnimationLoopSeh(void* animationState, bool loop)
		{
			__try
			{
				Ogre::SetAnimationLoop(animationState, loop);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Animation] SetLoop crashed animationState=%p loop=%d code=0x%08X", animationState, loop ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetAnimationLoop(void* animationState, bool loop)
		{
			return Seh::CatchCpp("TrySetAnimationLoop", [&] { return TrySetAnimationLoopSeh(animationState, loop); }, false);
		}

		bool TrySetAnimationWeightSeh(void* animationState, float weight)
		{
			__try
			{
				Ogre::SetAnimationWeight(animationState, weight);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Animation] SetWeight crashed animationState=%p weight=%g code=0x%08X", animationState, weight, GetExceptionCode());
				return false;
			}
		}

		bool TrySetAnimationWeight(void* animationState, float weight)
		{
			return Seh::CatchCpp("TrySetAnimationWeight", [&] { return TrySetAnimationWeightSeh(animationState, weight); }, false);
		}

		bool TrySetAnimationTimePositionSeh(void* animationState, float timePosition)
		{
			__try
			{
				Ogre::SetAnimationTimePosition(animationState, timePosition);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Animation] SetTimePosition crashed animationState=%p time=%g code=0x%08X", animationState, timePosition, GetExceptionCode());
				return false;
			}
		}

		bool TrySetAnimationTimePosition(void* animationState, float timePosition)
		{
			return Seh::CatchCpp("TrySetAnimationTimePosition", [&] { return TrySetAnimationTimePositionSeh(animationState, timePosition); }, false);
		}

		bool TryGetLightPowerScaleSeh(void* light, float& outValue)
		{
			__try
			{
				outValue = Ogre::GetPowerScale(light);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Light] GetPowerScale crashed light=%p code=0x%08X", light, GetExceptionCode());
				outValue = 0.0f;
				return false;
			}
		}

		bool TryGetLightPowerScale(void* light, float& outValue)
		{
			return Seh::CatchCpp("TryGetLightPowerScale", [&] { return TryGetLightPowerScaleSeh(light, outValue); }, [&] { outValue = 0.0f; return false; });
		}

		bool TrySetLightPowerScaleSeh(void* light, float value)
		{
			__try
			{
				Ogre::SetPowerScale(light, value);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Light] SetPowerScale crashed light=%p value=%g code=0x%08X", light, value, GetExceptionCode());
				return false;
			}
		}

		bool TrySetLightPowerScale(void* light, float value)
		{
			return Seh::CatchCpp("TrySetLightPowerScale", [&] { return TrySetLightPowerScaleSeh(light, value); }, false);
		}

		bool TryGetLightPositionSeh(void* light, BZR::VECTOR_3D& outPosition)
		{
			__try
			{
				if (auto* position = Ogre::GetLightPosition(light))
				{
					outPosition = *position;
					return true;
				}
				return false;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Light] GetPosition crashed light=%p code=0x%08X", light, GetExceptionCode());
				outPosition = {};
				return false;
			}
		}

		bool TryGetLightPosition(void* light, BZR::VECTOR_3D& outPosition)
		{
			return Seh::CatchCpp("TryGetLightPosition", [&] { return TryGetLightPositionSeh(light, outPosition); }, [&] { outPosition = {}; return false; });
		}

		bool TrySetLightPositionSeh(void* light, const BZR::VECTOR_3D& position)
		{
			__try
			{
				Ogre::SetLightPosition(light, position.x, position.y, position.z);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Light] SetPosition crashed light=%p code=0x%08X", light, GetExceptionCode());
				return false;
			}
		}

		bool TrySetLightPosition(void* light, const BZR::VECTOR_3D& position)
		{
			return Seh::CatchCpp("TrySetLightPosition", [&] { return TrySetLightPositionSeh(light, position); }, false);
		}

		bool TryGetLightDirectionSeh(void* light, BZR::VECTOR_3D& outDirection)
		{
			__try
			{
				if (auto* direction = Ogre::GetDirection(light))
				{
					outDirection = *direction;
					return true;
				}
				return false;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Light] GetDirection crashed light=%p code=0x%08X", light, GetExceptionCode());
				outDirection = {};
				return false;
			}
		}

		bool TryGetLightDirection(void* light, BZR::VECTOR_3D& outDirection)
		{
			return Seh::CatchCpp("TryGetLightDirection", [&] { return TryGetLightDirectionSeh(light, outDirection); }, [&] { outDirection = {}; return false; });
		}

		bool TrySetLightDirectionSeh(void* light, const BZR::VECTOR_3D& direction)
		{
			__try
			{
				Ogre::SetDirection(light, direction.x, direction.y, direction.z);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Light] SetDirection crashed light=%p code=0x%08X", light, GetExceptionCode());
				return false;
			}
		}

		bool TrySetLightDirection(void* light, const BZR::VECTOR_3D& direction)
		{
			return Seh::CatchCpp("TrySetLightDirection", [&] { return TrySetLightDirectionSeh(light, direction); }, false);
		}

		bool TrySetLightAttenuationSeh(void* light, float range, float constant, float linear, float quadratic)
		{
			__try
			{
				Ogre::SetAttenuation(light, range, constant, linear, quadratic);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Light] SetAttenuation crashed light=%p code=0x%08X", light, GetExceptionCode());
				return false;
			}
		}

		bool TrySetLightAttenuation(void* light, float range, float constant, float linear, float quadratic)
		{
			return Seh::CatchCpp("TrySetLightAttenuation", [&] { return TrySetLightAttenuationSeh(light, range, constant, linear, quadratic); }, false);
		}

		bool TrySetLightDiffuseSeh(void* light, float r, float g, float b)
		{
			__try
			{
				Ogre::SetDiffuseColor(light, r, g, b);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Light] SetDiffuseColor crashed light=%p code=0x%08X", light, GetExceptionCode());
				return false;
			}
		}

		bool TrySetLightDiffuse(void* light, float r, float g, float b)
		{
			return Seh::CatchCpp("TrySetLightDiffuse", [&] { return TrySetLightDiffuseSeh(light, r, g, b); }, false);
		}

		bool TrySetLightSpecularSeh(void* light, float r, float g, float b)
		{
			__try
			{
				Ogre::SetSpecularColor(light, r, g, b);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Light] SetSpecularColor crashed light=%p code=0x%08X", light, GetExceptionCode());
				return false;
			}
		}

		bool TrySetLightSpecular(void* light, float r, float g, float b)
		{
			return Seh::CatchCpp("TrySetLightSpecular", [&] { return TrySetLightSpecularSeh(light, r, g, b); }, false);
		}

		bool TrySetSpotlightRangeSeh(void* light, float innerAngle, float outerAngle, float falloff)
		{
			__try
			{
				Ogre::SetSpotlightRange(light, &innerAngle, &outerAngle, falloff);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Light] SetSpotlightRange crashed light=%p code=0x%08X", light, GetExceptionCode());
				return false;
			}
		}

		bool TrySetSpotlightRange(void* light, float innerAngle, float outerAngle, float falloff)
		{
			return Seh::CatchCpp("TrySetSpotlightRange", [&] { return TrySetSpotlightRangeSeh(light, innerAngle, outerAngle, falloff); }, false);
		}
	}
}
