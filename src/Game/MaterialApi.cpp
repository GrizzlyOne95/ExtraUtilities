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

#include "GameObjectInternal.h"
#include "Game/TextureWindowMath.h"

#include <algorithm>
#include <cmath>

// Material Lua bindings: sub-entity materials, clone/exists, pass colours and
// texture-unit animation. The Ogre wrappers are in Ogre/OgreMaterialRuntime.cpp.

namespace ExtraUtilities::Lua::GameObject
{
	namespace
	{
		bool ReadOptionalPassColorField(
			lua_State* L,
			int tableIndex,
			const char* fieldName,
			::Ogre::ColourValue& outColor)
		{
			lua_getfield(L, tableIndex, fieldName);
			if (lua_isnil(L, -1))
			{
				lua_pop(L, 1);
				return false;
			}

			outColor = ToMaterialColor(CheckColorOrSingles(L, -1));
			lua_pop(L, 1);
			return true;
		}

		// Returns a pointer into the Lua stack (or a literal), so no C++ object
		// is live if the check raises.
		const char* CheckOptionalResourceGroup(lua_State* L, int idx)
		{
			if (lua_isnoneornil(L, idx))
			{
				return "General";
			}

			return luaL_checkstring(L, idx);
		}
	}

	int GetNumSubEntities(lua_State* L)
	{
		return GetSubEntityCount(L);
	}

	int GetSubEntityCount(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		uint32_t count = 0;
		if (!TryGetNumSubEntities(entity, count))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushinteger(L, count);
		return 1;
	}

	int GetMaterialName(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		void* subEntity = nullptr;
		if (!lua_isnoneornil(L, 2))
		{
			subEntity = GetSubEntity(L, entity, 2);
		}
		else
		{
			subEntity = GetFirstSubEntity(entity);
			if (subEntity == nullptr)
			{
				lua_pushnil(L);
				return 1;
			}
		}

		std::string materialName;
		if (!TryGetMaterialName(subEntity, materialName))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushstring(L, materialName.c_str());
		return 1;
	}

	int GetSubEntityMaterial(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		void* subEntity = GetSubEntity(L, entity, 2);

		std::string materialName;
		if (!TryGetMaterialName(subEntity, materialName))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushstring(L, materialName.c_str());
		return 1;
	}

	int SetMaterialName(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		const char* const materialName = luaL_checkstring(L, 2);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		if (lua_type(L, 3) == LUA_TNUMBER)
		{
			void* subEntity = GetSubEntity(L, entity, 3);
			const char* const resourceGroup = CheckOptionalResourceGroup(L, 4);
			TrySetMaterialNameSubEntity(subEntity, materialName, resourceGroup);
			return 0;
		}

		const char* const resourceGroup = CheckOptionalResourceGroup(L, 3);
		TrySetMaterialNameEntity(entity, materialName, resourceGroup);
		return 0;
	}

	int SetEntityMaterial(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		const char* const materialName = luaL_checkstring(L, 2);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		const char* const resourceGroup = CheckOptionalResourceGroup(L, 3);
		TrySetMaterialNameEntity(entity, materialName, resourceGroup);
		return 0;
	}

	int SetSubEntityMaterial(lua_State* L)
	{
		const EntityTarget h = CheckEntityTarget(L, 1);
		luaL_checkinteger(L, 2);
		const char* const materialName = luaL_checkstring(L, 3);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		void* subEntity = GetSubEntity(L, entity, 2);
		const char* const resourceGroup = CheckOptionalResourceGroup(L, 4);
		TrySetMaterialNameSubEntity(subEntity, materialName, resourceGroup);
		return 0;
	}

	int MaterialExists(lua_State* L)
	{
		const std::string_view materialNameArg = luaL_checkstring(L, 1);
		const std::string_view resourceGroupArg = CheckOptionalResourceGroup(L, 2);
		const std::string materialName(materialNameArg);
		const std::string resourceGroup(resourceGroupArg);
		::Ogre::Material* material = nullptr;
		lua_pushboolean(L, TryResolveMaterial(materialName, resourceGroup, material) ? 1 : 0);
		return 1;
	}

		int CloneMaterial(lua_State* L)
		{
			const std::string_view sourceMaterialArg = luaL_checkstring(L, 1);
			const std::string_view cloneNameArg = luaL_checkstring(L, 2);
			const std::string_view resourceGroupArg = CheckOptionalResourceGroup(L, 3);
			const std::string sourceMaterial(sourceMaterialArg);
			const std::string cloneName(cloneNameArg);
			const std::string resourceGroup(resourceGroupArg);

			::Ogre::Material* source = nullptr;
			MaterialHandle sourceShared{};
			if (!TryResolveMaterialCpp(sourceMaterial, resourceGroup, source, &sourceShared))
			{
				lua_pushboolean(L, 0);
				return 1;
			}

			::Ogre::Material* clone = nullptr;
			lua_pushboolean(L, TryCloneMaterial(sourceShared, cloneName, resourceGroup, clone) ? 1 : 0);
			return 1;
		}

	int GetMaterialPassColors(lua_State* L)
	{
		const std::string_view materialNameArg = luaL_checkstring(L, 1);
		int techniqueIndex = luaL_optint(L, 2, 0);
		int passIndex = luaL_optint(L, 3, 0);
		const std::string_view resourceGroupArg = CheckOptionalResourceGroup(L, 4);
		const std::string materialName(materialNameArg);
		const std::string resourceGroup(resourceGroupArg);

		MaterialPassHandle handle;
		if (!TryResolveMaterialPass(materialName, resourceGroup, techniqueIndex, passIndex, handle))
		{
			lua_pushnil(L);
			return 1;
		}

		::Ogre::ColourValue ambient{};
		::Ogre::ColourValue diffuse{};
		::Ogre::ColourValue specular{};
		::Ogre::ColourValue emissive{};

		if (!TryGetPassAmbient(handle.pass, ambient)
			|| !TryGetPassDiffuse(handle.pass, diffuse)
			|| !TryGetPassSpecular(handle.pass, specular)
			|| !TryGetPassSelfIllumination(handle.pass, emissive))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_createtable(L, 0, 4);
		PushColor(L, ToExuColor(ambient));
		lua_setfield(L, -2, "ambient");
		PushColor(L, ToExuColor(diffuse));
		lua_setfield(L, -2, "diffuse");
		PushColor(L, ToExuColor(specular));
		lua_setfield(L, -2, "specular");
		PushColor(L, ToExuColor(emissive));
		lua_setfield(L, -2, "emissive");
		return 1;
	}

	int SetMaterialTexture(lua_State* L)
	{
		const std::string_view materialNameArg = luaL_checkstring(L, 1);
		const std::string_view textureNameArg = luaL_checkstring(L, 2);
		int techniqueIndex = luaL_optint(L, 3, 0);
		int passIndex = luaL_optint(L, 4, 0);
		int textureUnitIndex = luaL_optint(L, 5, 0);
		const std::string_view resourceGroupArg = CheckOptionalResourceGroup(L, 6);
		const std::string materialName(materialNameArg);
		const std::string textureName(textureNameArg);
		const std::string resourceGroup(resourceGroupArg);

		MaterialTextureUnitHandle handle;
		if (!TryResolveMaterialTextureUnit(
			materialName,
			resourceGroup,
			techniqueIndex,
			passIndex,
			textureUnitIndex,
			handle))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const bool success = TrySetMaterialTextureName(handle.textureUnit, textureName);

		if (!success)
		{
			LogMaterialDebug(
				"[EXU::Material] SetMaterialTexture failed material=%s texture=%s group=%s technique=%d pass=%d unit=%d",
				materialName.c_str(),
				textureName.c_str(),
				resourceGroup.c_str(),
				techniqueIndex,
				passIndex,
				textureUnitIndex);
		}

		lua_pushboolean(L, success ? 1 : 0);
		return 1;
	}

	int SetMaterialTextureScroll(lua_State* L)
	{
		const std::string_view materialNameArg = luaL_checkstring(L, 1);
		float u = static_cast<float>(luaL_checknumber(L, 2));
		float v = static_cast<float>(luaL_checknumber(L, 3));
		int techniqueIndex = luaL_optint(L, 4, 0);
		int passIndex = luaL_optint(L, 5, 0);
		int textureUnitIndex = luaL_optint(L, 6, 0);
		const std::string_view resourceGroupArg = CheckOptionalResourceGroup(L, 7);
		const std::string materialName(materialNameArg);
		const std::string resourceGroup(resourceGroupArg);

		MaterialTextureUnitHandle handle;
		if (!TryResolveMaterialTextureUnit(
			materialName,
			resourceGroup,
			techniqueIndex,
			passIndex,
			textureUnitIndex,
			handle))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const bool success = TrySetMaterialTextureScroll(handle.textureUnit, u, v);
		if (!success)
		{
			LogMaterialDebug(
				"[EXU::Material] SetMaterialTextureScroll failed material=%s group=%s technique=%d pass=%d unit=%d",
				materialName.c_str(),
				resourceGroup.c_str(),
				techniqueIndex,
				passIndex,
				textureUnitIndex);
		}

		lua_pushboolean(L, success ? 1 : 0);
		return 1;
	}

	// exu.SetMaterialTextureScale(material, uScale, vScale[, technique, pass, unit, group])
	// Ogre's TextureUnitState::setTextureScale (about the texture centre; 2 =
	// the texture looks twice as big). Same defaults as SetMaterialTextureScroll.
	int SetMaterialTextureScale(lua_State* L)
	{
		const std::string_view materialNameArg = luaL_checkstring(L, 1);
		const float uScale = static_cast<float>(luaL_checknumber(L, 2));
		const float vScale = static_cast<float>(luaL_checknumber(L, 3));
		const int techniqueIndex = luaL_optint(L, 4, 0);
		const int passIndex = luaL_optint(L, 5, 0);
		const int textureUnitIndex = luaL_optint(L, 6, 0);
		const std::string_view resourceGroupArg = CheckOptionalResourceGroup(L, 7);
		const std::string materialName(materialNameArg);
		const std::string resourceGroup(resourceGroupArg);
		if (!std::isfinite(uScale) || !std::isfinite(vScale) || uScale == 0.0f || vScale == 0.0f)
		{
			return luaL_error(L, "Extra Utilities: texture scale must be finite and non-zero");
		}

		MaterialTextureUnitHandle handle;
		if (!TryResolveMaterialTextureUnit(materialName, resourceGroup, techniqueIndex, passIndex, textureUnitIndex, handle))
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		lua_pushboolean(L, TrySetMaterialTextureScale(handle.textureUnit, uScale, vScale) ? 1 : 0);
		return 1;
	}

	// exu.SetMaterialTextureWindow(material, u0, v0, du, dv[, technique, pass, unit, group])
	// Sets scale and scroll so the unit samples the window [u0, u0 + du] x
	// [v0, v0 + dv] across the mesh's UV 0..1, like an overlay panel's
	// uv_coords (a negative extent mirrors). technique -1 applies it to every
	// technique of the material, so one call covers the DX11 and DX9
	// techniques. Rotation is left alone (set it to 0 for an exact window).
	// Returns the number of texture units changed.
	int SetMaterialTextureWindow(lua_State* L)
	{
		const std::string_view materialNameArg = luaL_checkstring(L, 1);
		const float u0 = static_cast<float>(luaL_checknumber(L, 2));
		const float v0 = static_cast<float>(luaL_checknumber(L, 3));
		const float du = static_cast<float>(luaL_checknumber(L, 4));
		const float dv = static_cast<float>(luaL_checknumber(L, 5));
		const int techniqueArg = luaL_optint(L, 6, 0);
		const int passIndex = luaL_optint(L, 7, 0);
		const int textureUnitIndex = luaL_optint(L, 8, 0);
		const std::string_view resourceGroupArg = CheckOptionalResourceGroup(L, 9);
		const std::string materialName(materialNameArg);
		const std::string resourceGroup(resourceGroupArg);

		TextureWindowMath::Transform transform;
		if (!TextureWindowMath::FromWindow(u0, v0, du, dv, transform))
		{
			return luaL_error(L, "Extra Utilities: texture window must be finite with a non-zero extent");
		}

		int firstTechnique = techniqueArg;
		int lastTechnique = techniqueArg;
		if (techniqueArg < 0)
		{
			int count = 0;
			if (!TryGetMaterialTechniqueCount(materialName, resourceGroup, count) || count <= 0)
			{
				lua_pushinteger(L, 0);
				return 1;
			}
			firstTechnique = 0;
			lastTechnique = (std::min)(count, 64) - 1;
		}
		int changed = 0;
		for (int technique = firstTechnique; technique <= lastTechnique; ++technique)
		{
			MaterialTextureUnitHandle handle;
			if (!TryResolveMaterialTextureUnit(materialName, resourceGroup, technique, passIndex, textureUnitIndex, handle))
			{
				continue;    // this technique has no such pass/unit
			}
			if (TrySetMaterialTextureScale(handle.textureUnit, transform.scaleU, transform.scaleV) &&
				TrySetMaterialTextureScroll(handle.textureUnit, transform.scrollU, transform.scrollV))
			{
				++changed;
			}
		}
		lua_pushinteger(L, changed);
		return 1;
	}

	int SetMaterialTextureRotate(lua_State* L)
	{
		const std::string_view materialNameArg = luaL_checkstring(L, 1);
		float radians = static_cast<float>(luaL_checknumber(L, 2));
		int techniqueIndex = luaL_optint(L, 3, 0);
		int passIndex = luaL_optint(L, 4, 0);
		int textureUnitIndex = luaL_optint(L, 5, 0);
		const std::string_view resourceGroupArg = CheckOptionalResourceGroup(L, 6);
		const std::string materialName(materialNameArg);
		const std::string resourceGroup(resourceGroupArg);

		MaterialTextureUnitHandle handle;
		if (!TryResolveMaterialTextureUnit(
			materialName,
			resourceGroup,
			techniqueIndex,
			passIndex,
			textureUnitIndex,
			handle))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const bool success = TrySetMaterialTextureRotate(handle.textureUnit, radians);
		if (!success)
		{
			LogMaterialDebug(
				"[EXU::Material] SetMaterialTextureRotate failed material=%s group=%s technique=%d pass=%d unit=%d",
				materialName.c_str(),
				resourceGroup.c_str(),
				techniqueIndex,
				passIndex,
				textureUnitIndex);
		}

		lua_pushboolean(L, success ? 1 : 0);
		return 1;
	}

	int SetMaterialTextureScrollAnimation(lua_State* L)
	{
		const std::string_view materialNameArg = luaL_checkstring(L, 1);
		float uSpeed = static_cast<float>(luaL_checknumber(L, 2));
		float vSpeed = static_cast<float>(luaL_checknumber(L, 3));
		int techniqueIndex = luaL_optint(L, 4, 0);
		int passIndex = luaL_optint(L, 5, 0);
		int textureUnitIndex = luaL_optint(L, 6, 0);
		const std::string_view resourceGroupArg = CheckOptionalResourceGroup(L, 7);
		const std::string materialName(materialNameArg);
		const std::string resourceGroup(resourceGroupArg);

		MaterialTextureUnitHandle handle;
		if (!TryResolveMaterialTextureUnit(
			materialName,
			resourceGroup,
			techniqueIndex,
			passIndex,
			textureUnitIndex,
			handle))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const bool success = TrySetMaterialTextureScrollAnimation(handle.textureUnit, uSpeed, vSpeed);
		if (!success)
		{
			LogMaterialDebug(
				"[EXU::Material] SetMaterialTextureScrollAnimation failed material=%s group=%s technique=%d pass=%d unit=%d",
				materialName.c_str(),
				resourceGroup.c_str(),
				techniqueIndex,
				passIndex,
				textureUnitIndex);
		}

		lua_pushboolean(L, success ? 1 : 0);
		return 1;
	}

	int SetMaterialTextureRotateAnimation(lua_State* L)
	{
		const std::string_view materialNameArg = luaL_checkstring(L, 1);
		float speed = static_cast<float>(luaL_checknumber(L, 2));
		int techniqueIndex = luaL_optint(L, 3, 0);
		int passIndex = luaL_optint(L, 4, 0);
		int textureUnitIndex = luaL_optint(L, 5, 0);
		const std::string_view resourceGroupArg = CheckOptionalResourceGroup(L, 6);
		const std::string materialName(materialNameArg);
		const std::string resourceGroup(resourceGroupArg);

		MaterialTextureUnitHandle handle;
		if (!TryResolveMaterialTextureUnit(
			materialName,
			resourceGroup,
			techniqueIndex,
			passIndex,
			textureUnitIndex,
			handle))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const bool success = TrySetMaterialTextureRotateAnimation(handle.textureUnit, speed);
		if (!success)
		{
			LogMaterialDebug(
				"[EXU::Material] SetMaterialTextureRotateAnimation failed material=%s group=%s technique=%d pass=%d unit=%d",
				materialName.c_str(),
				resourceGroup.c_str(),
				techniqueIndex,
				passIndex,
				textureUnitIndex);
		}

		lua_pushboolean(L, success ? 1 : 0);
		return 1;
	}

	int SetMaterialPassColors(lua_State* L)
	{
		const std::string_view materialNameArg = luaL_checkstring(L, 1);
		luaL_checktype(L, 2, LUA_TTABLE);
		int techniqueIndex = luaL_optint(L, 3, 0);
		int passIndex = luaL_optint(L, 4, 0);
		const std::string_view resourceGroupArg = CheckOptionalResourceGroup(L, 5);
		bool includeGlowScheme = lua_toboolean(L, 6) != 0;

		// Every colour field is read (and can raise) before any std::string or
		// std::vector exists, so a bad field cannot leak them.
		::Ogre::ColourValue ambient{};
		::Ogre::ColourValue diffuse{};
		::Ogre::ColourValue specular{};
		::Ogre::ColourValue emissive{};
		const bool hasAmbient = ReadOptionalPassColorField(L, 2, "ambient", ambient);
		const bool hasDiffuse = ReadOptionalPassColorField(L, 2, "diffuse", diffuse);
		const bool hasSpecular = ReadOptionalPassColorField(L, 2, "specular", specular);
		const bool hasEmissive = ReadOptionalPassColorField(L, 2, "emissive", emissive)
			|| ReadOptionalPassColorField(L, 2, "selfIllumination", emissive)
			|| ReadOptionalPassColorField(L, 2, "selfillumination", emissive);

		const std::string materialName(materialNameArg);
		const std::string resourceGroup(resourceGroupArg);

		// techniqueIndex -1 targets every technique (passIndex -1 every pass)
		// so tints survive the viewport scheme swaps done by the lighting
		// modes ("en-" enhanced); the collector skips retro "og-" techniques.
		std::vector<::Ogre::Pass*> passes;
		if (techniqueIndex < 0)
		{
			if (!TryCollectMaterialTintPasses(
				materialName,
				resourceGroup,
				passIndex,
				includeGlowScheme,
				passes))
			{
				lua_pushboolean(L, 0);
				return 1;
			}
		}
		else
		{
			MaterialPassHandle handle;
			if (!TryResolveMaterialPass(materialName, resourceGroup, techniqueIndex, passIndex, handle))
			{
				lua_pushboolean(L, 0);
				return 1;
			}
			passes.push_back(handle.pass);
		}

		bool success = true;
		if (hasAmbient)
		{
			for (auto* pass : passes)
			{
				success = TrySetPassAmbient(pass, ambient) && success;
			}
		}

		if (hasDiffuse)
		{
			for (auto* pass : passes)
			{
				success = TrySetPassDiffuse(pass, diffuse) && success;
			}
		}

		if (hasSpecular)
		{
			for (auto* pass : passes)
			{
				success = TrySetPassSpecular(pass, specular) && success;
			}
		}

		if (hasEmissive)
		{
			for (auto* pass : passes)
			{
				success = TrySetPassSelfIllumination(pass, emissive) && success;
			}
		}

		lua_pushboolean(L, success ? 1 : 0);
		return 1;
	}
}
