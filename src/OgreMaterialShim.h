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

#include <Windows.h>

#include <string>

namespace Ogre
{
	using String = std::string;

	class ColourValue
	{
	public:
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 1.0f;
	};

	class Radian
	{
	public:
		explicit Radian(float value = 0.0f)
			: mRad(value) {}

	private:
		float mRad = 0.0f;
	};

	template<typename T>
	class SharedPtr
	{
	public:
		T* pRep = nullptr;
		void* pInfo = nullptr;

		T* getPointer() const
		{
			return pRep;
		}

		bool isNull() const
		{
			return pRep == nullptr;
		}
	};

	class Pass;
	class Technique;
	class Material;
	class MaterialManager;
	class TextureUnitState;
	class SubEntity;
	class Entity;

	enum TextureType
	{
		TEX_TYPE_2D = 2
	};

	namespace Detail
	{
		template<typename T>
		inline T ResolveProc(const char* name)
		{
			static HMODULE ogreMain = GetModuleHandleA("OgreMain.dll");
			if (ogreMain == nullptr)
			{
				return nullptr;
			}

			return reinterpret_cast<T>(GetProcAddress(ogreMain, name));
		}
	}

	inline MaterialManager* GetMaterialManagerSingletonPtr()
	{
		using Fn = MaterialManager * (__cdecl*)();
		static Fn fn = Detail::ResolveProc<Fn>("?getSingletonPtr@MaterialManager@Ogre@@SAPAV12@XZ");
		return fn == nullptr ? nullptr : fn();
	}

	inline SharedPtr<Material> GetMaterialByName(
		MaterialManager* manager,
		const String& name,
		const String& groupName)
	{
		using Fn = void(__thiscall*)(MaterialManager*, SharedPtr<Material>*, const String&, const String&);
		static Fn fn = Detail::ResolveProc<Fn>("?getByName@MaterialManager@Ogre@@QAE?AV?$SharedPtr@VMaterial@Ogre@@@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z");
		if (fn == nullptr || manager == nullptr)
		{
			return {};
		}

		SharedPtr<Material> result{};
		fn(manager, &result, name, groupName);
		return result;
	}

	inline SharedPtr<Material> CloneMaterial(
		const Material* material,
		const String& newName,
		bool changeGroup,
		const String& newGroup)
	{
		using Fn = void(__thiscall*)(const Material*, SharedPtr<Material>*, const String&, bool, const String&);
		static Fn fn = Detail::ResolveProc<Fn>("?clone@Material@Ogre@@QBE?AV?$SharedPtr@VMaterial@Ogre@@@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@_N0@Z");
		if (fn == nullptr || material == nullptr)
		{
			return {};
		}

		SharedPtr<Material> result{};
		fn(material, &result, newName, changeGroup, newGroup);
		return result;
	}

	inline SharedPtr<Material> GetSubEntityMaterial(void* subEntity)
	{
		using Fn = const SharedPtr<Material>&(__thiscall*)(void*);
		static Fn fn = Detail::ResolveProc<Fn>("?getMaterial@SubEntity@Ogre@@UBEABV?$SharedPtr@VMaterial@Ogre@@@2@XZ");
		return fn == nullptr || subEntity == nullptr ? SharedPtr<Material>{} : fn(subEntity);
	}

	inline bool SetEntityMaterial(void* entity, const SharedPtr<Material>& material)
	{
		using Fn = void(__thiscall*)(void*, const SharedPtr<Material>&);
		static Fn fn = Detail::ResolveProc<Fn>("?setMaterial@Entity@Ogre@@QAEXABV?$SharedPtr@VMaterial@Ogre@@@2@@Z");
		if (fn == nullptr || entity == nullptr || material.isNull())
		{
			return false;
		}

		fn(entity, material);
		return true;
	}

	inline bool SetSubEntityMaterial(void* subEntity, const SharedPtr<Material>& material)
	{
		using Fn = void(__thiscall*)(void*, const SharedPtr<Material>&);
		static Fn fn = Detail::ResolveProc<Fn>("?setMaterial@SubEntity@Ogre@@QAEXABV?$SharedPtr@VMaterial@Ogre@@@2@@Z");
		if (fn == nullptr || subEntity == nullptr || material.isNull())
		{
			return false;
		}

		fn(subEntity, material);
		return true;
	}

	inline Technique* GetMaterialTechnique(Material* material, unsigned short index)
	{
		using Fn = Technique * (__thiscall*)(Material*, unsigned short);
		static Fn fn = Detail::ResolveProc<Fn>("?getTechnique@Material@Ogre@@QAEPAVTechnique@2@G@Z");
		return fn == nullptr || material == nullptr ? nullptr : fn(material, index);
	}

	inline unsigned short GetTechniqueNumPasses(const Technique* technique)
	{
		using Fn = unsigned short(__thiscall*)(const Technique*);
		static Fn fn = Detail::ResolveProc<Fn>("?getNumPasses@Technique@Ogre@@QBEGXZ");
		return fn == nullptr || technique == nullptr ? 0 : fn(technique);
	}

	inline Pass* GetTechniquePass(Technique* technique, unsigned short index)
	{
		using Fn = Pass * (__thiscall*)(Technique*, unsigned short);
		static Fn fn = Detail::ResolveProc<Fn>("?getPass@Technique@Ogre@@QAEPAVPass@2@G@Z");
		return fn == nullptr || technique == nullptr ? nullptr : fn(technique, index);
	}

	inline unsigned short GetPassNumTextureUnitStates(const Pass* pass)
	{
		using Fn = unsigned short(__thiscall*)(const Pass*);
		static Fn fn = Detail::ResolveProc<Fn>("?getNumTextureUnitStates@Pass@Ogre@@QBEGXZ");
		return fn == nullptr || pass == nullptr ? 0 : fn(pass);
	}

	inline TextureUnitState* GetPassTextureUnitState(Pass* pass, unsigned short index)
	{
		using Fn = TextureUnitState * (__thiscall*)(Pass*, unsigned short);
		static Fn fn = Detail::ResolveProc<Fn>("?getTextureUnitState@Pass@Ogre@@QAEPAVTextureUnitState@2@G@Z");
		return fn == nullptr || pass == nullptr ? nullptr : fn(pass, index);
	}

	inline bool SetTextureUnitStateTextureName(TextureUnitState* textureUnitState, const String& name, TextureType type = TEX_TYPE_2D)
	{
		using Fn = void(__thiscall*)(TextureUnitState*, const String&, TextureType);
		static Fn fn = Detail::ResolveProc<Fn>("?setTextureName@TextureUnitState@Ogre@@QAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@W4TextureType@2@@Z");
		if (fn == nullptr || textureUnitState == nullptr)
		{
			return false;
		}

		fn(textureUnitState, name, type);
		return true;
	}

	inline bool SetTextureUnitStateTextureScroll(TextureUnitState* textureUnitState, float u, float v)
	{
		using Fn = void(__thiscall*)(TextureUnitState*, float, float);
		static Fn fn = Detail::ResolveProc<Fn>("?setTextureScroll@TextureUnitState@Ogre@@QAEXMM@Z");
		if (fn == nullptr || textureUnitState == nullptr)
		{
			return false;
		}

		fn(textureUnitState, u, v);
		return true;
	}

	inline bool SetTextureUnitStateTextureRotate(TextureUnitState* textureUnitState, float radians)
	{
		using Fn = void(__thiscall*)(TextureUnitState*, const Radian&);
		static Fn fn = Detail::ResolveProc<Fn>("?setTextureRotate@TextureUnitState@Ogre@@QAEXABVRadian@2@@Z");
		if (fn == nullptr || textureUnitState == nullptr)
		{
			return false;
		}

		const Radian angle(radians);
		fn(textureUnitState, angle);
		return true;
	}

	inline bool SetTextureUnitStateScrollAnimation(TextureUnitState* textureUnitState, float uSpeed, float vSpeed)
	{
		using Fn = void(__thiscall*)(TextureUnitState*, float, float);
		static Fn fn = Detail::ResolveProc<Fn>("?setScrollAnimation@TextureUnitState@Ogre@@QAEXMM@Z");
		if (fn == nullptr || textureUnitState == nullptr)
		{
			return false;
		}

		fn(textureUnitState, uSpeed, vSpeed);
		return true;
	}

	inline bool SetTextureUnitStateRotateAnimation(TextureUnitState* textureUnitState, float speed)
	{
		using Fn = void(__thiscall*)(TextureUnitState*, float);
		static Fn fn = Detail::ResolveProc<Fn>("?setRotateAnimation@TextureUnitState@Ogre@@QAEXM@Z");
		if (fn == nullptr || textureUnitState == nullptr)
		{
			return false;
		}

		fn(textureUnitState, speed);
		return true;
	}

	inline const ColourValue* GetPassAmbient(const Pass* pass)
	{
		using Fn = const ColourValue& (__thiscall*)(const Pass*);
		static Fn fn = Detail::ResolveProc<Fn>("?getAmbient@Pass@Ogre@@QBEABVColourValue@2@XZ");
		return fn == nullptr || pass == nullptr ? nullptr : &fn(pass);
	}

	inline const ColourValue* GetPassDiffuse(const Pass* pass)
	{
		using Fn = const ColourValue& (__thiscall*)(const Pass*);
		static Fn fn = Detail::ResolveProc<Fn>("?getDiffuse@Pass@Ogre@@QBEABVColourValue@2@XZ");
		return fn == nullptr || pass == nullptr ? nullptr : &fn(pass);
	}

	inline const ColourValue* GetPassSpecular(const Pass* pass)
	{
		using Fn = const ColourValue& (__thiscall*)(const Pass*);
		static Fn fn = Detail::ResolveProc<Fn>("?getSpecular@Pass@Ogre@@QBEABVColourValue@2@XZ");
		return fn == nullptr || pass == nullptr ? nullptr : &fn(pass);
	}

	inline const ColourValue* GetPassSelfIllumination(const Pass* pass)
	{
		using Fn = const ColourValue& (__thiscall*)(const Pass*);
		static Fn fn = Detail::ResolveProc<Fn>("?getSelfIllumination@Pass@Ogre@@QBEABVColourValue@2@XZ");
		return fn == nullptr || pass == nullptr ? nullptr : &fn(pass);
	}

	inline bool SetPassAmbient(Pass* pass, const ColourValue& color)
	{
		using Fn = void(__thiscall*)(Pass*, const ColourValue&);
		static Fn fn = Detail::ResolveProc<Fn>("?setAmbient@Pass@Ogre@@QAEXABVColourValue@2@@Z");
		if (fn == nullptr || pass == nullptr)
		{
			return false;
		}

		fn(pass, color);
		return true;
	}

	inline bool SetPassDiffuse(Pass* pass, const ColourValue& color)
	{
		using Fn = void(__thiscall*)(Pass*, const ColourValue&);
		static Fn fn = Detail::ResolveProc<Fn>("?setDiffuse@Pass@Ogre@@QAEXABVColourValue@2@@Z");
		if (fn == nullptr || pass == nullptr)
		{
			return false;
		}

		fn(pass, color);
		return true;
	}

	inline bool SetPassSpecular(Pass* pass, const ColourValue& color)
	{
		using Fn = void(__thiscall*)(Pass*, const ColourValue&);
		static Fn fn = Detail::ResolveProc<Fn>("?setSpecular@Pass@Ogre@@QAEXABVColourValue@2@@Z");
		if (fn == nullptr || pass == nullptr)
		{
			return false;
		}

		fn(pass, color);
		return true;
	}

	inline bool SetPassSelfIllumination(Pass* pass, const ColourValue& color)
	{
		using Fn = void(__thiscall*)(Pass*, const ColourValue&);
		static Fn fn = Detail::ResolveProc<Fn>("?setSelfIllumination@Pass@Ogre@@QAEXABVColourValue@2@@Z");
		if (fn == nullptr || pass == nullptr)
		{
			return false;
		}

		fn(pass, color);
		return true;
	}
}
