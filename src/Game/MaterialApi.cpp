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

		std::string CheckOptionalResourceGroup(lua_State* L, int idx)
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
		BZR::handle h = CheckHandle(L, 1);
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
		BZR::handle h = CheckHandle(L, 1);
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
		BZR::handle h = CheckHandle(L, 1);
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
		BZR::handle h = CheckHandle(L, 1);
		std::string materialName = luaL_checkstring(L, 2);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		if (lua_type(L, 3) == LUA_TNUMBER)
		{
			void* subEntity = GetSubEntity(L, entity, 3);
			std::string resourceGroup = CheckOptionalResourceGroup(L, 4);
			TrySetMaterialNameSubEntity(subEntity, materialName, resourceGroup);
			return 0;
		}

		std::string resourceGroup = CheckOptionalResourceGroup(L, 3);
		TrySetMaterialNameEntity(entity, materialName, resourceGroup);
		return 0;
	}

	int SetEntityMaterial(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		std::string materialName = luaL_checkstring(L, 2);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		std::string resourceGroup = CheckOptionalResourceGroup(L, 3);
		TrySetMaterialNameEntity(entity, materialName, resourceGroup);
		return 0;
	}

	int SetSubEntityMaterial(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		luaL_checkinteger(L, 2);
		std::string materialName = luaL_checkstring(L, 3);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		void* subEntity = GetSubEntity(L, entity, 2);
		std::string resourceGroup = CheckOptionalResourceGroup(L, 4);
		TrySetMaterialNameSubEntity(subEntity, materialName, resourceGroup);
		return 0;
	}

	int MaterialExists(lua_State* L)
	{
		std::string materialName = luaL_checkstring(L, 1);
		std::string resourceGroup = CheckOptionalResourceGroup(L, 2);
		::Ogre::Material* material = nullptr;
		lua_pushboolean(L, TryResolveMaterial(materialName, resourceGroup, material) ? 1 : 0);
		return 1;
	}

		int CloneMaterial(lua_State* L)
		{
			std::string sourceMaterial = luaL_checkstring(L, 1);
			std::string cloneName = luaL_checkstring(L, 2);
			std::string resourceGroup = CheckOptionalResourceGroup(L, 3);

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
		std::string materialName = luaL_checkstring(L, 1);
		int techniqueIndex = luaL_optint(L, 2, 0);
		int passIndex = luaL_optint(L, 3, 0);
		std::string resourceGroup = CheckOptionalResourceGroup(L, 4);

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
		std::string materialName = luaL_checkstring(L, 1);
		std::string textureName = luaL_checkstring(L, 2);
		int techniqueIndex = luaL_optint(L, 3, 0);
		int passIndex = luaL_optint(L, 4, 0);
		int textureUnitIndex = luaL_optint(L, 5, 0);
		std::string resourceGroup = CheckOptionalResourceGroup(L, 6);

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
		std::string materialName = luaL_checkstring(L, 1);
		float u = static_cast<float>(luaL_checknumber(L, 2));
		float v = static_cast<float>(luaL_checknumber(L, 3));
		int techniqueIndex = luaL_optint(L, 4, 0);
		int passIndex = luaL_optint(L, 5, 0);
		int textureUnitIndex = luaL_optint(L, 6, 0);
		std::string resourceGroup = CheckOptionalResourceGroup(L, 7);

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

	int SetMaterialTextureRotate(lua_State* L)
	{
		std::string materialName = luaL_checkstring(L, 1);
		float radians = static_cast<float>(luaL_checknumber(L, 2));
		int techniqueIndex = luaL_optint(L, 3, 0);
		int passIndex = luaL_optint(L, 4, 0);
		int textureUnitIndex = luaL_optint(L, 5, 0);
		std::string resourceGroup = CheckOptionalResourceGroup(L, 6);

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
		std::string materialName = luaL_checkstring(L, 1);
		float uSpeed = static_cast<float>(luaL_checknumber(L, 2));
		float vSpeed = static_cast<float>(luaL_checknumber(L, 3));
		int techniqueIndex = luaL_optint(L, 4, 0);
		int passIndex = luaL_optint(L, 5, 0);
		int textureUnitIndex = luaL_optint(L, 6, 0);
		std::string resourceGroup = CheckOptionalResourceGroup(L, 7);

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
		std::string materialName = luaL_checkstring(L, 1);
		float speed = static_cast<float>(luaL_checknumber(L, 2));
		int techniqueIndex = luaL_optint(L, 3, 0);
		int passIndex = luaL_optint(L, 4, 0);
		int textureUnitIndex = luaL_optint(L, 5, 0);
		std::string resourceGroup = CheckOptionalResourceGroup(L, 6);

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
		std::string materialName = luaL_checkstring(L, 1);
		luaL_checktype(L, 2, LUA_TTABLE);
		int techniqueIndex = luaL_optint(L, 3, 0);
		int passIndex = luaL_optint(L, 4, 0);
		std::string resourceGroup = CheckOptionalResourceGroup(L, 5);
		bool includeGlowScheme = lua_toboolean(L, 6) != 0;

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
		::Ogre::ColourValue color{};

		if (ReadOptionalPassColorField(L, 2, "ambient", color))
		{
			for (auto* pass : passes)
			{
				success = TrySetPassAmbient(pass, color) && success;
			}
		}

		if (ReadOptionalPassColorField(L, 2, "diffuse", color))
		{
			for (auto* pass : passes)
			{
				success = TrySetPassDiffuse(pass, color) && success;
			}
		}

		if (ReadOptionalPassColorField(L, 2, "specular", color))
		{
			for (auto* pass : passes)
			{
				success = TrySetPassSpecular(pass, color) && success;
			}
		}

		if (ReadOptionalPassColorField(L, 2, "emissive", color)
			|| ReadOptionalPassColorField(L, 2, "selfIllumination", color)
			|| ReadOptionalPassColorField(L, 2, "selfillumination", color))
		{
			for (auto* pass : passes)
			{
				success = TrySetPassSelfIllumination(pass, color) && success;
			}
		}

		lua_pushboolean(L, success ? 1 : 0);
		return 1;
	}
}
