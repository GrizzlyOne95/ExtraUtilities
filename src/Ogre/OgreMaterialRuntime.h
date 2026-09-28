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

#include "Ogre/Ogre.h"
#include "Ogre/OgreMaterialShim.h"

#include <string>
#include <vector>

// Ogre material runtime: the per-mission material handle cache, the C++
// material, pass and texture-unit wrappers (try/catch) and the SEH shells
// around them. Lua bindings live in Game/MaterialApi.cpp.

namespace ExtraUtilities::Lua::GameObject
{
	namespace Detail
	{
		struct MaterialPassHandle
		{
			::Ogre::Material* material = nullptr;
			::Ogre::Technique* technique = nullptr;
			::Ogre::Pass* pass = nullptr;
		};

		struct MaterialTextureUnitHandle
		{
			::Ogre::Material* material = nullptr;
			::Ogre::Technique* technique = nullptr;
			::Ogre::Pass* pass = nullptr;
			::Ogre::TextureUnitState* textureUnit = nullptr;
		};

		using MaterialHandle = ::Ogre::SharedPtr<::Ogre::Material>;
		void LogMaterialDebug(const char* fmt, ...);
		void LogMaterialFault(const char* fmt, ...);
		void CacheMaterialHandle(const std::string& materialName, const MaterialHandle& material);
		bool TryGetCachedMaterial(const std::string& materialName, MaterialHandle& outMaterial);
		bool TryResolveMaterialCpp(
			const std::string& materialName,
			const std::string& resourceGroup,
			::Ogre::Material*& outMaterial,
			MaterialHandle* outShared = nullptr);
		bool TryResolveMaterial(
			const std::string& materialName,
			const std::string& resourceGroup,
			::Ogre::Material*& outMaterial);
		bool TryCloneMaterial(
			const MaterialHandle& sourceMaterial,
			const std::string& cloneName,
			const std::string& resourceGroup,
			::Ogre::Material*& outMaterial);
		bool TryResolveMaterialPass(
			const std::string& materialName,
			const std::string& resourceGroup,
			int techniqueIndex,
			int passIndex,
			MaterialPassHandle& outHandle);
		bool TryCollectMaterialTintPasses(
			const std::string& materialName,
			const std::string& resourceGroup,
			int passIndex,
			bool includeGlowScheme,
			std::vector<::Ogre::Pass*>& outPasses);
		bool TryResolveMaterialTextureUnit(
			const std::string& materialName,
			const std::string& resourceGroup,
			int techniqueIndex,
			int passIndex,
			int textureUnitIndex,
			MaterialTextureUnitHandle& outHandle);
		bool TrySetMaterialTextureName(::Ogre::TextureUnitState* textureUnit, const std::string& textureName);
		bool TrySetMaterialTextureScroll(::Ogre::TextureUnitState* textureUnit, float u, float v);
		bool TrySetMaterialTextureRotate(::Ogre::TextureUnitState* textureUnit, float radians);
		bool TrySetMaterialTextureScrollAnimation(::Ogre::TextureUnitState* textureUnit, float uSpeed, float vSpeed);
		bool TrySetMaterialTextureRotateAnimation(::Ogre::TextureUnitState* textureUnit, float speed);
		ExtraUtilities::Ogre::Color ToExuColor(const ::Ogre::ColourValue& color);
		::Ogre::ColourValue ToMaterialColor(const ExtraUtilities::Ogre::Color& color);
		bool TryGetPassAmbient(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor);
		bool TryGetPassDiffuse(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor);
		bool TryGetPassSpecular(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor);
		bool TryGetPassSelfIllumination(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor);
		bool TrySetPassAmbient(::Ogre::Pass* pass, const ::Ogre::ColourValue& color);
		bool TrySetPassDiffuse(::Ogre::Pass* pass, const ::Ogre::ColourValue& color);
		bool TrySetPassSpecular(::Ogre::Pass* pass, const ::Ogre::ColourValue& color);
		bool TrySetPassSelfIllumination(::Ogre::Pass* pass, const ::Ogre::ColourValue& color);
	}

	using namespace Detail;
}
