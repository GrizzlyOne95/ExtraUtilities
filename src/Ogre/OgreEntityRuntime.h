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

#include <cstdint>
#include <string>

// SEH-guarded wrappers over the Ogre::Entity, SubEntity, AnimationState and
// Light calls in Ogre/Ogre.h, used by the entity, light and material APIs.

namespace ExtraUtilities::Lua::GameObject
{
	namespace Detail
	{
		bool TryGetNumSubEntities(void* entity, uint32_t& outCount);
		bool TryGetSubEntityByIndex(void* entity, uint32_t index, void*& outSubEntity);
		bool TryGetMaterialName(void* subEntity, std::string& outName);
		bool TrySetMaterialNameEntity(void* entity, const std::string& materialName, const std::string& resourceGroup);
		bool TrySetMaterialNameSubEntity(void* subEntity, const std::string& materialName, const std::string& resourceGroup);
		bool TryGetVisible(void* entity, bool& outVisible);
		bool TrySetVisible(void* entity, bool visible);
		bool TryGetCastShadows(void* entity, bool& outCastShadows);
		bool TrySetCastShadows(void* entity, bool castShadows);
		bool TryGetRenderingDistance(void* entity, float& outDistance);
		bool TrySetRenderingDistance(void* entity, float distance);
		bool TryGetVisibilityFlags(void* entity, uint32_t& outFlags);
		bool TrySetVisibilityFlags(void* entity, uint32_t flags);
		bool TryGetQueryFlags(void* entity, uint32_t& outFlags);
		bool TrySetQueryFlags(void* entity, uint32_t flags);
		bool TryGetRenderQueueGroup(void* renderable, uint8_t& outGroup);
		bool TrySetRenderQueueGroup(void* entity, uint8_t group);
		void* GetNamedAnimationState(void* entity, const std::string& name);
		bool TryGetAnimationLength(void* animationState, float& outValue);
		bool TryGetAnimationTimePosition(void* animationState, float& outValue);
		bool TryGetAnimationWeight(void* animationState, float& outValue);
		bool TryGetAnimationLoop(void* animationState, bool& outValue);
		bool TryGetAnimationEnabled(void* animationState, bool& outValue);
		bool TrySetAnimationEnabled(void* animationState, bool enabled);
		bool TrySetAnimationLoop(void* animationState, bool loop);
		bool TrySetAnimationWeight(void* animationState, float weight);
		bool TrySetAnimationTimePosition(void* animationState, float timePosition);
		bool TryGetLightPowerScale(void* light, float& outValue);
		bool TrySetLightPowerScale(void* light, float value);
		bool TryGetLightPosition(void* light, BZR::VECTOR_3D& outPosition);
		bool TrySetLightPosition(void* light, const BZR::VECTOR_3D& position);
		bool TryGetLightDirection(void* light, BZR::VECTOR_3D& outDirection);
		bool TrySetLightDirection(void* light, const BZR::VECTOR_3D& direction);
		bool TrySetLightAttenuation(void* light, float range, float constant, float linear, float quadratic);
		bool TrySetLightDiffuse(void* light, float r, float g, float b);
		bool TrySetLightSpecular(void* light, float r, float g, float b);
		bool TrySetSpotlightRange(void* light, float innerAngle, float outerAngle, float falloff);
	}

	using namespace Detail;
}
