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
		using Fn = SharedPtr<Material>(__thiscall*)(MaterialManager*, const String&, const String&);
		static Fn fn = Detail::ResolveProc<Fn>("?getByName@MaterialManager@Ogre@@QAE?AV?$SharedPtr@VMaterial@Ogre@@@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z");
		return fn == nullptr || manager == nullptr ? SharedPtr<Material>{} : fn(manager, name, groupName);
	}

	inline SharedPtr<Material> CloneMaterial(
		const Material* material,
		const String& newName,
		bool changeGroup,
		const String& newGroup)
	{
		using Fn = SharedPtr<Material>(__thiscall*)(const Material*, const String&, bool, const String&);
		static Fn fn = Detail::ResolveProc<Fn>("?clone@Material@Ogre@@QBE?AV?$SharedPtr@VMaterial@Ogre@@@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@_N0@Z");
		return fn == nullptr || material == nullptr ? SharedPtr<Material>{} : fn(material, newName, changeGroup, newGroup);
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
