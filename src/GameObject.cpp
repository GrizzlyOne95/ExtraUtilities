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

#include "GameObject.h"

#include "LuaHelpers.h"
#include "Ogre.h"
#include "OgreMaterialShim.h"

#include <Windows.h>

#include <cmath>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <exception>
#include <lua.hpp>

#include <string>
#include <unordered_set>
#include <vector>

namespace ExtraUtilities::Lua::GameObject
{
	namespace
	{
		struct MsvcPmd
		{
			int mdisp;
			int pdisp;
			int vdisp;
		};

		struct MsvcRttiTypeDescriptor
		{
			void* pVFTable;
			void* spare;
			char name[1];
		};

		struct MsvcRttiBaseClassDescriptor
		{
			MsvcRttiTypeDescriptor* pTypeDescriptor;
			uint32_t numContainedBases;
			MsvcPmd where;
			uint32_t attributes;
		};

		struct MsvcRttiClassHierarchyDescriptor
		{
			uint32_t signature;
			uint32_t attributes;
			uint32_t numBaseClasses;
			MsvcRttiBaseClassDescriptor** pBaseClassArray;
		};

		struct MsvcRttiCompleteObjectLocator
		{
			uint32_t signature;
			uint32_t offset;
			uint32_t cdOffset;
			MsvcRttiTypeDescriptor* pTypeDescriptor;
			MsvcRttiClassHierarchyDescriptor* pClassDescriptor;
		};

		struct MaterialPassHandle
		{
			::Ogre::Material* material = nullptr;
			::Ogre::Technique* technique = nullptr;
			::Ogre::Pass* pass = nullptr;
		};

		struct PolymorphicObjectInfo
		{
			uint32_t offset = 0;
			void* object = nullptr;
			void* vtable = nullptr;
			std::string rawTypeName;
			std::string typeName;
			std::vector<std::string> hierarchy;
		};

		struct ScannedFieldInfo
		{
			uint32_t offset = 0;
			uint32_t rawValue = 0;
			int32_t intValue = 0;
			bool hasFloatValue = false;
			float floatValue = 0.0f;
			bool hasPointer = false;
			void* pointer = nullptr;
			std::string pointerRawTypeName;
			std::string pointerTypeName;
			std::vector<std::string> pointerHierarchy;
			bool hasHandle = false;
			BZR::handle handleValue = 0;
			BZR::GameObject* handleObject = nullptr;
		};

		void PushStringArray(lua_State* L, const std::vector<std::string>& values);

		void LogMaterialDebug(const char* fmt, ...)
		{
			char message[1024];
			va_list args;
			va_start(args, fmt);
			vsnprintf_s(message, sizeof(message), _TRUNCATE, fmt, args);
			va_end(args);

			OutputDebugStringA(message);
			OutputDebugStringA("\n");

			if (FILE* file = fopen("exu_material_debug.log", "a"))
			{
				fprintf(file, "%s\n", message);
				fclose(file);
			}
		}

		std::string NormalizeMsvcTypeName(const char* rawName)
		{
			if (rawName == nullptr || rawName[0] == '\0')
			{
				return {};
			}

			std::string name(rawName);
			auto trimPrefixAndSuffix = [&](const char* prefix) -> std::string
				{
					const size_t prefixLen = strlen(prefix);
					if (name.rfind(prefix, 0) != 0)
					{
						return {};
					}

					const size_t suffix = name.find("@@", prefixLen);
					if (suffix == std::string::npos || suffix <= prefixLen)
					{
						return {};
					}

					return name.substr(prefixLen, suffix - prefixLen);
				};

			std::string trimmed = trimPrefixAndSuffix(".?AV");
			if (!trimmed.empty())
			{
				return trimmed;
			}

			trimmed = trimPrefixAndSuffix(".?AU");
			if (!trimmed.empty())
			{
				return trimmed;
			}

			return name;
		}

		bool TryGetPolymorphicMetadata(
			void* object,
			void*& outVtable,
			const char*& outRawTypeName,
			MsvcRttiClassHierarchyDescriptor*& outClassDescriptor)
		{
			outVtable = nullptr;
			outRawTypeName = nullptr;
			outClassDescriptor = nullptr;

			if (object == nullptr)
			{
				return false;
			}

			__try
			{
				void** vtable = *reinterpret_cast<void***>(object);
				if (vtable == nullptr)
				{
					return false;
				}

				auto* col = *(reinterpret_cast<MsvcRttiCompleteObjectLocator**>(vtable) - 1);
				if (col == nullptr || col->pTypeDescriptor == nullptr)
				{
					return false;
				}

				const char* rawName = col->pTypeDescriptor->name;
				if (rawName == nullptr || rawName[0] == '\0')
				{
					return false;
				}

				outVtable = vtable;
				outRawTypeName = rawName;
				outClassDescriptor = col->pClassDescriptor;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outVtable = nullptr;
				outRawTypeName = nullptr;
				outClassDescriptor = nullptr;
				return false;
			}
		}

		bool TryGetBaseClassRawName(
			MsvcRttiClassHierarchyDescriptor* classDescriptor,
			uint32_t index,
			const char*& outRawTypeName)
		{
			outRawTypeName = nullptr;

			if (classDescriptor == nullptr || classDescriptor->pBaseClassArray == nullptr)
			{
				return false;
			}

			__try
			{
				auto* baseDescriptor = classDescriptor->pBaseClassArray[index];
				if (baseDescriptor == nullptr || baseDescriptor->pTypeDescriptor == nullptr)
				{
					return false;
				}

				const char* rawTypeName = baseDescriptor->pTypeDescriptor->name;
				if (rawTypeName == nullptr || rawTypeName[0] == '\0')
				{
					return false;
				}

				outRawTypeName = rawTypeName;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outRawTypeName = nullptr;
				return false;
			}
		}

		std::vector<std::string> BuildHierarchyNames(MsvcRttiClassHierarchyDescriptor* classDescriptor)
		{
			std::vector<std::string> result;
			if (classDescriptor == nullptr)
			{
				return result;
			}

			uint32_t count = classDescriptor->numBaseClasses;
			if (count > 64)
			{
				count = 64;
			}

			std::unordered_set<std::string> seenNames;
			for (uint32_t i = 0; i < count; ++i)
			{
				const char* baseRawName = nullptr;
				if (!TryGetBaseClassRawName(classDescriptor, i, baseRawName))
				{
					continue;
				}

				std::string prettyName = NormalizeMsvcTypeName(baseRawName);
				if (prettyName.empty())
				{
					prettyName = baseRawName;
				}

				if (seenNames.insert(prettyName).second)
				{
					result.push_back(prettyName);
				}
			}

			return result;
		}

		bool TryReadPointerField(void* base, uint32_t offset, void*& outPointer)
		{
			outPointer = nullptr;

			if (base == nullptr)
			{
				return false;
			}

			__try
			{
				outPointer = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(base) + offset);
				return outPointer != nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outPointer = nullptr;
				return false;
			}
		}

		bool TryReadUInt32Field(void* base, uint32_t offset, uint32_t& outValue)
		{
			outValue = 0;

			if (base == nullptr)
			{
				return false;
			}

			__try
			{
				outValue = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(base) + offset);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outValue = 0;
				return false;
			}
		}

		bool TryInterpretFloat(uint32_t rawValue, float& outValue)
		{
			static_assert(sizeof(uint32_t) == sizeof(float), "float scan assumes 32-bit float");
			memcpy(&outValue, &rawValue, sizeof(float));
			return std::isfinite(outValue);
		}

		bool IsReadablePointer(const void* pointer)
		{
			if (pointer == nullptr)
			{
				return false;
			}

			MEMORY_BASIC_INFORMATION mbi{};
			if (VirtualQuery(pointer, &mbi, sizeof(mbi)) == 0)
			{
				return false;
			}

			if (mbi.State != MEM_COMMIT)
			{
				return false;
			}

			if ((mbi.Protect & PAGE_GUARD) != 0 || (mbi.Protect & PAGE_NOACCESS) != 0)
			{
				return false;
			}

			return true;
		}

		bool TryResolveHandleValue(uint32_t rawValue, BZR::handle& outHandle, BZR::GameObject*& outObject)
		{
			outHandle = 0;
			outObject = nullptr;

			if (rawValue == 0)
			{
				return false;
			}

			__try
			{
				BZR::handle candidate = static_cast<BZR::handle>(rawValue);
				BZR::GameObject* obj = BZR::GameObject::GetObj(candidate);
				if (obj == nullptr)
				{
					return false;
				}

				if (BZR::GameObject::GetHandle(obj) != candidate)
				{
					return false;
				}

				outHandle = candidate;
				outObject = obj;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outHandle = 0;
				outObject = nullptr;
				return false;
			}
		}

		uint32_t GetScanBytesArgument(lua_State* L, int index, uint32_t defaultValue)
		{
			if (lua_gettop(L) < index || lua_isnil(L, index))
			{
				return defaultValue;
			}

			lua_Integer requested = luaL_checkinteger(L, index);
			if (requested < 0)
			{
				luaL_argerror(L, index, "scanBytes must be non-negative");
				return defaultValue;
			}

			if (requested > 0x200)
			{
				luaL_argerror(L, index, "scanBytes must be 512 bytes or less");
				return defaultValue;
			}

			return static_cast<uint32_t>(requested);
		}

		bool IsInterestingAiChildType(const std::string& typeName)
		{
			return typeName.find("Task") != std::string::npos ||
				typeName.find("Attack") != std::string::npos ||
				typeName.find("Process") != std::string::npos;
		}

		int GetTaskCandidateScore(const PolymorphicObjectInfo& info)
		{
			int score = 0;
			if (info.typeName.find("Attack") != std::string::npos)
			{
				score += 100;
			}
			if (info.typeName.find("Task") != std::string::npos)
			{
				score += 50;
			}
			if (info.typeName.find("UnitTask") != std::string::npos)
			{
				score += 10;
			}
			return score;
		}

		std::vector<PolymorphicObjectInfo> ScanPolymorphicChildren(void* base, uint32_t scanBytes)
		{
			std::vector<PolymorphicObjectInfo> result;
			if (base == nullptr)
			{
				return result;
			}

			std::unordered_set<void*> seenObjects;
			for (uint32_t offset = 0; offset + sizeof(void*) <= scanBytes; offset += sizeof(void*))
			{
				void* candidate = nullptr;
				if (!TryReadPointerField(base, offset, candidate))
				{
					continue;
				}

				if (candidate == base || !seenObjects.insert(candidate).second)
				{
					continue;
				}

				void* vtable = nullptr;
				const char* rawTypeName = nullptr;
				MsvcRttiClassHierarchyDescriptor* classDescriptor = nullptr;
				if (!TryGetPolymorphicMetadata(candidate, vtable, rawTypeName, classDescriptor))
				{
					continue;
				}

				PolymorphicObjectInfo info;
				info.offset = offset;
				info.object = candidate;
				info.vtable = vtable;
				info.rawTypeName = rawTypeName;
				info.typeName = NormalizeMsvcTypeName(rawTypeName);
				info.hierarchy = BuildHierarchyNames(classDescriptor);

				if (!IsInterestingAiChildType(info.typeName))
				{
					continue;
				}

				result.push_back(std::move(info));
			}

			return result;
		}

		std::vector<ScannedFieldInfo> ScanAlignedFields(void* base, uint32_t scanBytes)
		{
			std::vector<ScannedFieldInfo> result;
			if (base == nullptr)
			{
				return result;
			}

			result.reserve(scanBytes / sizeof(uint32_t));
			for (uint32_t offset = 0; offset + sizeof(uint32_t) <= scanBytes; offset += sizeof(uint32_t))
			{
				uint32_t rawValue = 0;
				if (!TryReadUInt32Field(base, offset, rawValue))
				{
					continue;
				}

				ScannedFieldInfo info;
				info.offset = offset;
				info.rawValue = rawValue;
				info.intValue = static_cast<int32_t>(rawValue);
				info.hasFloatValue = TryInterpretFloat(rawValue, info.floatValue);

				if (rawValue >= 0x10000)
				{
					void* pointerCandidate = reinterpret_cast<void*>(static_cast<uintptr_t>(rawValue));
					if (IsReadablePointer(pointerCandidate))
					{
						info.hasPointer = true;
						info.pointer = pointerCandidate;

						void* vtable = nullptr;
						const char* rawTypeName = nullptr;
						MsvcRttiClassHierarchyDescriptor* classDescriptor = nullptr;
						if (TryGetPolymorphicMetadata(pointerCandidate, vtable, rawTypeName, classDescriptor))
						{
							info.pointerRawTypeName = rawTypeName;
							info.pointerTypeName = NormalizeMsvcTypeName(rawTypeName);
							info.pointerHierarchy = BuildHierarchyNames(classDescriptor);
						}
					}
				}

				BZR::handle handleValue = 0;
				BZR::GameObject* handleObject = nullptr;
				if (TryResolveHandleValue(rawValue, handleValue, handleObject))
				{
					info.hasHandle = true;
					info.handleValue = handleValue;
					info.handleObject = handleObject;
				}

				result.push_back(std::move(info));
			}

			return result;
		}

		void PushPolymorphicObjectInfo(lua_State* L, const PolymorphicObjectInfo& info)
		{
			lua_createtable(L, 0, 6);

			lua_pushinteger(L, info.offset);
			lua_setfield(L, -2, "offset");

			lua_pushlightuserdata(L, info.object);
			lua_setfield(L, -2, "object");

			if (info.vtable != nullptr)
			{
				lua_pushlightuserdata(L, info.vtable);
				lua_setfield(L, -2, "vtable");
			}

			lua_pushstring(L, info.rawTypeName.c_str());
			lua_setfield(L, -2, "rawTypeName");

			lua_pushstring(L, info.typeName.c_str());
			lua_setfield(L, -2, "typeName");

			PushStringArray(L, info.hierarchy);
			lua_setfield(L, -2, "hierarchy");
		}

		void PushScannedFieldInfo(lua_State* L, const ScannedFieldInfo& info)
		{
			char rawHex[11];
			sprintf_s(rawHex, "0x%08X", info.rawValue);

			lua_createtable(L, 0, 11);

			lua_pushinteger(L, info.offset);
			lua_setfield(L, -2, "offset");

			lua_pushinteger(L, static_cast<lua_Integer>(info.rawValue));
			lua_setfield(L, -2, "rawValue");

			lua_pushstring(L, rawHex);
			lua_setfield(L, -2, "rawHex");

			lua_pushinteger(L, static_cast<lua_Integer>(info.intValue));
			lua_setfield(L, -2, "intValue");

			if (info.hasFloatValue)
			{
				lua_pushnumber(L, info.floatValue);
				lua_setfield(L, -2, "floatValue");
			}

			if (info.hasPointer)
			{
				lua_pushlightuserdata(L, info.pointer);
				lua_setfield(L, -2, "pointer");

				if (!info.pointerRawTypeName.empty())
				{
					lua_pushstring(L, info.pointerRawTypeName.c_str());
					lua_setfield(L, -2, "pointerRawTypeName");
				}

				if (!info.pointerTypeName.empty())
				{
					lua_pushstring(L, info.pointerTypeName.c_str());
					lua_setfield(L, -2, "pointerTypeName");
				}

				if (!info.pointerHierarchy.empty())
				{
					PushStringArray(L, info.pointerHierarchy);
					lua_setfield(L, -2, "pointerHierarchy");
				}
			}

			if (info.hasHandle)
			{
				lua_pushinteger(L, static_cast<lua_Integer>(info.handleValue));
				lua_setfield(L, -2, "handle");

				lua_pushlightuserdata(L, info.handleObject);
				lua_setfield(L, -2, "handleObject");
			}
		}

		void PushPolymorphicObjectArray(lua_State* L, const std::vector<PolymorphicObjectInfo>& values)
		{
			lua_createtable(L, static_cast<int>(values.size()), 0);
			for (size_t i = 0; i < values.size(); ++i)
			{
				PushPolymorphicObjectInfo(L, values[i]);
				lua_rawseti(L, -2, static_cast<int>(i + 1));
			}
		}

		void PushScannedFieldArray(lua_State* L, const std::vector<ScannedFieldInfo>& values)
		{
			lua_createtable(L, static_cast<int>(values.size()), 0);
			for (size_t i = 0; i < values.size(); ++i)
			{
				PushScannedFieldInfo(L, values[i]);
				lua_rawseti(L, -2, static_cast<int>(i + 1));
			}
		}

		void PushStringArray(lua_State* L, const std::vector<std::string>& values)
		{
			lua_createtable(L, static_cast<int>(values.size()), 0);
			for (size_t i = 0; i < values.size(); ++i)
			{
				lua_pushstring(L, values[i].c_str());
				lua_rawseti(L, -2, static_cast<int>(i + 1));
			}
		}

		bool TryResolveMaterialCpp(
			const std::string& materialName,
			const std::string& resourceGroup,
			::Ogre::Material*& outMaterial)
		{
			try
			{
				auto* manager = ::Ogre::GetMaterialManagerSingletonPtr();
				if (manager == nullptr)
				{
					outMaterial = nullptr;
					return false;
				}

				auto material = ::Ogre::GetMaterialByName(manager, materialName, resourceGroup);
				outMaterial = material.getPointer();
				return outMaterial != nullptr;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug(
					"[EXU::Material] ResolveMaterial threw material=%s group=%s what=%s",
					materialName.c_str(),
					resourceGroup.c_str(),
					ex.what());
				outMaterial = nullptr;
				return false;
			}
			catch (...)
			{
				LogMaterialDebug(
					"[EXU::Material] ResolveMaterial threw material=%s group=%s",
					materialName.c_str(),
					resourceGroup.c_str());
				outMaterial = nullptr;
				return false;
			}
		}

		bool TryCloneMaterialCpp(
			::Ogre::Material* sourceMaterial,
			const std::string& cloneName,
			const std::string& resourceGroup,
			::Ogre::Material*& outMaterial)
		{
			try
			{
				if (sourceMaterial == nullptr)
				{
					outMaterial = nullptr;
					return false;
				}

				auto clone = ::Ogre::CloneMaterial(sourceMaterial, cloneName, false, resourceGroup);
				outMaterial = clone.getPointer();
				return outMaterial != nullptr;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug(
					"[EXU::Material] CloneMaterial threw clone=%s group=%s what=%s",
					cloneName.c_str(),
					resourceGroup.c_str(),
					ex.what());
				outMaterial = nullptr;
				return false;
			}
			catch (...)
			{
				LogMaterialDebug(
					"[EXU::Material] CloneMaterial threw clone=%s group=%s",
					cloneName.c_str(),
					resourceGroup.c_str());
				outMaterial = nullptr;
				return false;
			}
		}

		bool TryResolveMaterialPassCpp(
			const std::string& materialName,
			const std::string& resourceGroup,
			int techniqueIndex,
			int passIndex,
			MaterialPassHandle& outHandle)
		{
			try
			{
				outHandle = {};

				::Ogre::Material* material = nullptr;
				if (!TryResolveMaterialCpp(materialName, resourceGroup, material))
				{
					return false;
				}

				auto* technique = ::Ogre::GetMaterialTechnique(material, static_cast<unsigned short>(techniqueIndex));
				if (technique == nullptr)
				{
					return false;
				}

				if (passIndex < 0 || passIndex >= static_cast<int>(::Ogre::GetTechniqueNumPasses(technique)))
				{
					return false;
				}

				auto* pass = ::Ogre::GetTechniquePass(technique, static_cast<unsigned short>(passIndex));
				if (pass == nullptr)
				{
					return false;
				}

				outHandle.material = material;
				outHandle.technique = technique;
				outHandle.pass = pass;
				return true;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug(
					"[EXU::Material] ResolveMaterialPass threw material=%s technique=%d pass=%d what=%s",
					materialName.c_str(),
					techniqueIndex,
					passIndex,
					ex.what());
				outHandle = {};
				return false;
			}
			catch (...)
			{
				LogMaterialDebug(
					"[EXU::Material] ResolveMaterialPass threw material=%s technique=%d pass=%d",
					materialName.c_str(),
					techniqueIndex,
					passIndex);
				outHandle = {};
				return false;
			}
		}

		bool TryGetPassAmbientCpp(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			try
			{
				const auto* color = ::Ogre::GetPassAmbient(pass);
				if (color == nullptr)
				{
					outColor = {};
					return false;
				}

				outColor = *color;
				return true;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] GetPassAmbient threw pass=%p what=%s", pass, ex.what());
				outColor = {};
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] GetPassAmbient threw pass=%p", pass);
				outColor = {};
				return false;
			}
		}

		bool TryGetPassDiffuseCpp(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			try
			{
				const auto* color = ::Ogre::GetPassDiffuse(pass);
				if (color == nullptr)
				{
					outColor = {};
					return false;
				}

				outColor = *color;
				return true;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] GetPassDiffuse threw pass=%p what=%s", pass, ex.what());
				outColor = {};
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] GetPassDiffuse threw pass=%p", pass);
				outColor = {};
				return false;
			}
		}

		bool TryGetPassSpecularCpp(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			try
			{
				const auto* color = ::Ogre::GetPassSpecular(pass);
				if (color == nullptr)
				{
					outColor = {};
					return false;
				}

				outColor = *color;
				return true;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] GetPassSpecular threw pass=%p what=%s", pass, ex.what());
				outColor = {};
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] GetPassSpecular threw pass=%p", pass);
				outColor = {};
				return false;
			}
		}

		bool TryGetPassSelfIlluminationCpp(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			try
			{
				const auto* color = ::Ogre::GetPassSelfIllumination(pass);
				if (color == nullptr)
				{
					outColor = {};
					return false;
				}

				outColor = *color;
				return true;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] GetPassSelfIllumination threw pass=%p what=%s", pass, ex.what());
				outColor = {};
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] GetPassSelfIllumination threw pass=%p", pass);
				outColor = {};
				return false;
			}
		}

		bool TrySetPassAmbientCpp(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			try
			{
				return ::Ogre::SetPassAmbient(pass, color);
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] SetPassAmbient threw pass=%p what=%s", pass, ex.what());
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] SetPassAmbient threw pass=%p", pass);
				return false;
			}
		}

		bool TrySetPassDiffuseCpp(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			try
			{
				return ::Ogre::SetPassDiffuse(pass, color);
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] SetPassDiffuse threw pass=%p what=%s", pass, ex.what());
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] SetPassDiffuse threw pass=%p", pass);
				return false;
			}
		}

		bool TrySetPassSpecularCpp(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			try
			{
				return ::Ogre::SetPassSpecular(pass, color);
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] SetPassSpecular threw pass=%p what=%s", pass, ex.what());
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] SetPassSpecular threw pass=%p", pass);
				return false;
			}
		}

		bool TrySetPassSelfIlluminationCpp(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			try
			{
				return ::Ogre::SetPassSelfIllumination(pass, color);
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] SetPassSelfIllumination threw pass=%p what=%s", pass, ex.what());
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] SetPassSelfIllumination threw pass=%p", pass);
				return false;
			}
		}

		bool TryGetNumSubEntities(void* entity, uint32_t& outCount)
		{
			__try
			{
				outCount = Ogre::GetNumSubEntities(entity);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] GetNumSubEntities crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outCount = 0;
				return false;
			}
		}

		bool TryGetSubEntityByIndex(void* entity, uint32_t index, void*& outSubEntity)
		{
			__try
			{
				outSubEntity = Ogre::GetSubEntityByIndex(entity, index);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] GetSubEntity crashed entity=%p index=%u code=0x%08X", entity, index, GetExceptionCode());
				outSubEntity = nullptr;
				return false;
			}
		}

		bool TryGetMaterialName(void* subEntity, std::string& outName)
		{
			__try
			{
				outName = Ogre::GetMaterialNameSubEntity(subEntity);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] GetMaterialName crashed subEntity=%p code=0x%08X", subEntity, GetExceptionCode());
				outName.clear();
				return false;
			}
		}

		bool TrySetMaterialNameEntity(void* entity, const std::string& materialName, const std::string& resourceGroup)
		{
			__try
			{
				Ogre::SetMaterialNameEntity(entity, materialName, resourceGroup);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug(
					"[EXU::Material] SetMaterialNameEntity crashed entity=%p material=%s group=%s code=0x%08X",
					entity,
					materialName.c_str(),
					resourceGroup.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetMaterialNameSubEntity(void* subEntity, const std::string& materialName, const std::string& resourceGroup)
		{
			__try
			{
				Ogre::SetMaterialNameSubEntity(subEntity, materialName, resourceGroup);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug(
					"[EXU::Material] SetMaterialNameSubEntity crashed subEntity=%p material=%s group=%s code=0x%08X",
					subEntity,
					materialName.c_str(),
					resourceGroup.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TryGetVisible(void* entity, bool& outVisible)
		{
			__try
			{
				outVisible = Ogre::GetVisible(entity);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] GetVisible crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outVisible = false;
				return false;
			}
		}

		bool TrySetVisible(void* entity, bool visible)
		{
			__try
			{
				Ogre::SetVisible(entity, visible);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] SetVisible crashed entity=%p visible=%d code=0x%08X", entity, visible ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TryGetCastShadows(void* entity, bool& outCastShadows)
		{
			__try
			{
				outCastShadows = Ogre::GetCastShadows(entity);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] GetCastShadows crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outCastShadows = false;
				return false;
			}
		}

		bool TrySetCastShadows(void* entity, bool castShadows)
		{
			__try
			{
				Ogre::SetCastShadows(entity, castShadows);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] SetCastShadows crashed entity=%p castShadows=%d code=0x%08X", entity, castShadows ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TryGetRenderingDistance(void* entity, float& outDistance)
		{
			__try
			{
				outDistance = Ogre::GetRenderingDistance(entity);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Render] GetRenderingDistance crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outDistance = 0.0f;
				return false;
			}
		}

		bool TrySetRenderingDistance(void* entity, float distance)
		{
			__try
			{
				Ogre::SetRenderingDistance(entity, distance);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Render] SetRenderingDistance crashed entity=%p distance=%g code=0x%08X", entity, distance, GetExceptionCode());
				return false;
			}
		}

		bool TryGetVisibilityFlags(void* entity, uint32_t& outFlags)
		{
			__try
			{
				outFlags = Ogre::GetVisibilityFlags(entity);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Render] GetVisibilityFlags crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outFlags = 0;
				return false;
			}
		}

		bool TrySetVisibilityFlags(void* entity, uint32_t flags)
		{
			__try
			{
				Ogre::SetVisibilityFlags(entity, flags);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Render] SetVisibilityFlags crashed entity=%p flags=0x%08X code=0x%08X", entity, flags, GetExceptionCode());
				return false;
			}
		}

		bool TryGetQueryFlags(void* entity, uint32_t& outFlags)
		{
			__try
			{
				outFlags = Ogre::GetQueryFlags(entity);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Render] GetQueryFlags crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outFlags = 0;
				return false;
			}
		}

		bool TrySetQueryFlags(void* entity, uint32_t flags)
		{
			__try
			{
				Ogre::SetQueryFlags(entity, flags);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Render] SetQueryFlags crashed entity=%p flags=0x%08X code=0x%08X", entity, flags, GetExceptionCode());
				return false;
			}
		}

		bool TryGetRenderQueueGroup(void* renderable, uint8_t& outGroup)
		{
			__try
			{
				outGroup = Ogre::GetRenderQueueGroup(renderable);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Render] GetRenderQueueGroup crashed renderable=%p code=0x%08X", renderable, GetExceptionCode());
				outGroup = 0;
				return false;
			}
		}

		bool TrySetRenderQueueGroup(void* entity, uint8_t group)
		{
			__try
			{
				Ogre::SetRenderQueueGroupMovable(entity, group);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Render] SetRenderQueueGroup crashed entity=%p group=%u code=0x%08X", entity, group, GetExceptionCode());
				return false;
			}
		}

		bool TryHasSkeleton(void* entity, bool& outHasSkeleton)
		{
			__try
			{
				outHasSkeleton = Ogre::HasSkeleton(entity);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Animation] HasSkeleton crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outHasSkeleton = false;
				return false;
			}
		}

		bool TryGetAllAnimationStates(void* entity, void*& outStates)
		{
			__try
			{
				outStates = Ogre::GetAllAnimationStates(entity);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Animation] GetAllAnimationStates crashed entity=%p code=0x%08X", entity, GetExceptionCode());
				outStates = nullptr;
				return false;
			}
		}

		bool TryHasAnimationState(void* animationStates, const std::string& name, bool& outHasAnimation)
		{
			__try
			{
				outHasAnimation = Ogre::HasAnimationState(animationStates, name);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Animation] HasAnimationState crashed states=%p name=%s code=0x%08X", animationStates, name.c_str(), GetExceptionCode());
				outHasAnimation = false;
				return false;
			}
		}

		bool TryGetAnimationState(void* animationStates, const std::string& name, void*& outAnimationState)
		{
			__try
			{
				outAnimationState = Ogre::GetAnimationState(animationStates, name);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Animation] GetAnimationState crashed states=%p name=%s code=0x%08X", animationStates, name.c_str(), GetExceptionCode());
				outAnimationState = nullptr;
				return false;
			}
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

		bool TryGetAnimationLength(void* animationState, float& outValue)
		{
			__try
			{
				outValue = Ogre::GetAnimationLength(animationState);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Animation] GetLength crashed animationState=%p code=0x%08X", animationState, GetExceptionCode());
				outValue = 0.0f;
				return false;
			}
		}

		bool TryGetAnimationTimePosition(void* animationState, float& outValue)
		{
			__try
			{
				outValue = Ogre::GetAnimationTimePosition(animationState);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Animation] GetTimePosition crashed animationState=%p code=0x%08X", animationState, GetExceptionCode());
				outValue = 0.0f;
				return false;
			}
		}

		bool TryGetAnimationWeight(void* animationState, float& outValue)
		{
			__try
			{
				outValue = Ogre::GetAnimationWeight(animationState);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Animation] GetWeight crashed animationState=%p code=0x%08X", animationState, GetExceptionCode());
				outValue = 0.0f;
				return false;
			}
		}

		bool TryGetAnimationLoop(void* animationState, bool& outValue)
		{
			__try
			{
				outValue = Ogre::GetAnimationLoop(animationState);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Animation] GetLoop crashed animationState=%p code=0x%08X", animationState, GetExceptionCode());
				outValue = false;
				return false;
			}
		}

		bool TryGetAnimationEnabled(void* animationState, bool& outValue)
		{
			__try
			{
				outValue = Ogre::GetAnimationEnabled(animationState);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Animation] GetEnabled crashed animationState=%p code=0x%08X", animationState, GetExceptionCode());
				outValue = false;
				return false;
			}
		}

		bool TrySetAnimationEnabled(void* animationState, bool enabled)
		{
			__try
			{
				Ogre::SetAnimationEnabled(animationState, enabled);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Animation] SetEnabled crashed animationState=%p enabled=%d code=0x%08X", animationState, enabled ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetAnimationLoop(void* animationState, bool loop)
		{
			__try
			{
				Ogre::SetAnimationLoop(animationState, loop);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Animation] SetLoop crashed animationState=%p loop=%d code=0x%08X", animationState, loop ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetAnimationWeight(void* animationState, float weight)
		{
			__try
			{
				Ogre::SetAnimationWeight(animationState, weight);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Animation] SetWeight crashed animationState=%p weight=%g code=0x%08X", animationState, weight, GetExceptionCode());
				return false;
			}
		}

		bool TrySetAnimationTimePosition(void* animationState, float timePosition)
		{
			__try
			{
				Ogre::SetAnimationTimePosition(animationState, timePosition);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Animation] SetTimePosition crashed animationState=%p time=%g code=0x%08X", animationState, timePosition, GetExceptionCode());
				return false;
			}
		}

		bool IsRenderableEntityCandidate(void* candidate)
		{
			if (candidate == nullptr)
			{
				return false;
			}

			uint32_t count = 0;
			if (!TryGetNumSubEntities(candidate, count) || count == 0 || count > 64)
			{
				return false;
			}

			void* subEntity = nullptr;
			if (!TryGetSubEntityByIndex(candidate, 0, subEntity) || subEntity == nullptr)
			{
				return false;
			}

			std::string materialName;
			if (!TryGetMaterialName(subEntity, materialName))
			{
				return false;
			}

			return true;
		}

		void* GetRenderableEntity(BZR::GameObject* obj)
		{
			if (obj == nullptr)
			{
				return nullptr;
			}

			void* entity = nullptr;
			__try
			{
				entity = obj->GetOgreEntity();
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] Ogre pointer lookup crashed obj=%p code=0x%08X", obj, GetExceptionCode());
				return nullptr;
			}

			if (IsRenderableEntityCandidate(entity))
			{
				return entity;
			}

			LogMaterialDebug("[EXU::Material] Ogre entity validation failed obj=%p entity=%p", obj, entity);
			return nullptr;
		}

		void* GetRenderableEntity(BZR::handle h)
		{
			return GetRenderableEntity(BZR::GameObject::GetObj(h));
		}

		void* GetLightObject(BZR::GameObject* obj)
		{
			if (obj == nullptr)
			{
				return nullptr;
			}

			void* light = nullptr;
			__try
			{
				light = obj->GetLight();
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Light] Ogre light lookup crashed obj=%p code=0x%08X", obj, GetExceptionCode());
				return nullptr;
			}

			return light;
		}

		void* GetLightObject(BZR::handle h)
		{
			return GetLightObject(BZR::GameObject::GetObj(h));
		}

		bool TryGetLightPowerScale(void* light, float& outValue)
		{
			__try
			{
				outValue = Ogre::GetPowerScale(light);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Light] GetPowerScale crashed light=%p code=0x%08X", light, GetExceptionCode());
				outValue = 0.0f;
				return false;
			}
		}

		bool TrySetLightPowerScale(void* light, float value)
		{
			__try
			{
				Ogre::SetPowerScale(light, value);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Light] SetPowerScale crashed light=%p value=%g code=0x%08X", light, value, GetExceptionCode());
				return false;
			}
		}

		bool TryGetLightPosition(void* light, BZR::VECTOR_3D& outPosition)
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
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Light] GetPosition crashed light=%p code=0x%08X", light, GetExceptionCode());
				outPosition = {};
				return false;
			}
		}

		bool TrySetLightPosition(void* light, const BZR::VECTOR_3D& position)
		{
			__try
			{
				Ogre::SetLightPosition(light, position.x, position.y, position.z);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Light] SetPosition crashed light=%p code=0x%08X", light, GetExceptionCode());
				return false;
			}
		}

		bool TryGetLightDirection(void* light, BZR::VECTOR_3D& outDirection)
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
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Light] GetDirection crashed light=%p code=0x%08X", light, GetExceptionCode());
				outDirection = {};
				return false;
			}
		}

		bool TrySetLightDirection(void* light, const BZR::VECTOR_3D& direction)
		{
			__try
			{
				Ogre::SetDirection(light, direction.x, direction.y, direction.z);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Light] SetDirection crashed light=%p code=0x%08X", light, GetExceptionCode());
				return false;
			}
		}

		bool TrySetLightAttenuation(void* light, float range, float constant, float linear, float quadratic)
		{
			__try
			{
				Ogre::SetAttenuation(light, range, constant, linear, quadratic);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Light] SetAttenuation crashed light=%p code=0x%08X", light, GetExceptionCode());
				return false;
			}
		}

		bool TryResolveMaterial(
			const std::string& materialName,
			const std::string& resourceGroup,
			::Ogre::Material*& outMaterial)
		{
			__try
			{
				return TryResolveMaterialCpp(materialName, resourceGroup, outMaterial);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug(
					"[EXU::Material] ResolveMaterial crashed material=%s group=%s code=0x%08X",
					materialName.c_str(),
					resourceGroup.c_str(),
					GetExceptionCode());
				outMaterial = nullptr;
				return false;
			}
		}

		bool TryCloneMaterial(
			::Ogre::Material* sourceMaterial,
			const std::string& cloneName,
			const std::string& resourceGroup,
			::Ogre::Material*& outMaterial)
		{
			__try
			{
				return TryCloneMaterialCpp(sourceMaterial, cloneName, resourceGroup, outMaterial);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug(
					"[EXU::Material] CloneMaterial crashed clone=%s group=%s code=0x%08X",
					cloneName.c_str(),
					resourceGroup.c_str(),
					GetExceptionCode());
				outMaterial = nullptr;
				return false;
			}
		}

		bool TryResolveMaterialPass(
			const std::string& materialName,
			const std::string& resourceGroup,
			int techniqueIndex,
			int passIndex,
			MaterialPassHandle& outHandle)
		{
			__try
			{
				return TryResolveMaterialPassCpp(materialName, resourceGroup, techniqueIndex, passIndex, outHandle);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug(
					"[EXU::Material] ResolveMaterialPass crashed material=%s technique=%d pass=%d code=0x%08X",
					materialName.c_str(),
					techniqueIndex,
					passIndex,
					GetExceptionCode());
				outHandle = {};
				return false;
			}
		}

		ExtraUtilities::Ogre::Color ToExuColor(const ::Ogre::ColourValue& color)
		{
			return { color.r, color.g, color.b, color.a };
		}

		::Ogre::ColourValue ToMaterialColor(const ExtraUtilities::Ogre::Color& color)
		{
			return { color.r, color.g, color.b, color.a };
		}

		bool TryGetPassAmbient(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			__try
			{
				return TryGetPassAmbientCpp(pass, outColor);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] GetPassAmbient crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				outColor = {};
				return false;
			}
		}

		bool TryGetPassDiffuse(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			__try
			{
				return TryGetPassDiffuseCpp(pass, outColor);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] GetPassDiffuse crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				outColor = {};
				return false;
			}
		}

		bool TryGetPassSpecular(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			__try
			{
				return TryGetPassSpecularCpp(pass, outColor);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] GetPassSpecular crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				outColor = {};
				return false;
			}
		}

		bool TryGetPassSelfIllumination(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			__try
			{
				return TryGetPassSelfIlluminationCpp(pass, outColor);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] GetPassSelfIllumination crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				outColor = {};
				return false;
			}
		}

		bool TrySetPassAmbient(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			__try
			{
				return TrySetPassAmbientCpp(pass, color);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] SetPassAmbient crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				return false;
			}
		}

		bool TrySetPassDiffuse(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			__try
			{
				return TrySetPassDiffuseCpp(pass, color);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] SetPassDiffuse crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				return false;
			}
		}

		bool TrySetPassSpecular(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			__try
			{
				return TrySetPassSpecularCpp(pass, color);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] SetPassSpecular crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				return false;
			}
		}

		bool TrySetPassSelfIllumination(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			__try
			{
				return TrySetPassSelfIlluminationCpp(pass, color);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Material] SetPassSelfIllumination crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				return false;
			}
		}

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

		bool TrySetLightDiffuse(void* light, float r, float g, float b)
		{
			__try
			{
				Ogre::SetDiffuseColor(light, r, g, b);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Light] SetDiffuseColor crashed light=%p code=0x%08X", light, GetExceptionCode());
				return false;
			}
		}

		bool TrySetLightSpecular(void* light, float r, float g, float b)
		{
			__try
			{
				Ogre::SetSpecularColor(light, r, g, b);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Light] SetSpecularColor crashed light=%p code=0x%08X", light, GetExceptionCode());
				return false;
			}
		}

		bool TrySetSpotlightRange(void* light, float innerAngle, float outerAngle, float falloff)
		{
			__try
			{
				Ogre::SetSpotlightRange(light, &innerAngle, &outerAngle, falloff);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::Light] SetSpotlightRange crashed light=%p code=0x%08X", light, GetExceptionCode());
				return false;
			}
		}

		bool TrySetAsUser(BZR::GameObject* obj)
		{
			__try
			{
				BZR::GameObject::SetAsUser(obj);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::SetAsUser] crashed obj=%p code=0x%08X", obj, GetExceptionCode());
				return false;
			}
		}

		bool TryGetCommTowerPowerHandle(BZR::GameObject* obj, BZR::handle& outHandle)
		{
			__try
			{
				__asm
				{
					mov eax, [obj]
					mov eax, [eax+0x238]
					mov [outHandle], eax;
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialDebug("[EXU::IsCommTowerPowered] crashed obj=%p code=0x%08X", obj, GetExceptionCode());
				outHandle = 0;
				return false;
			}
		}

		uint32_t CheckSubEntityIndex(lua_State* L, void* entity, int idx)
		{
			int requestedIndex = luaL_checkinteger(L, idx);
			if (requestedIndex < 0)
			{
				luaL_argerror(L, idx, "sub-entity index must be >= 0");
			}

			const auto subEntityIndex = static_cast<uint32_t>(requestedIndex);
			uint32_t numSubEntities = 0;
			if (!TryGetNumSubEntities(entity, numSubEntities))
			{
				luaL_error(L, "Extra Utilities: render entity probe crashed while reading sub-entity count");
			}
			if (subEntityIndex >= numSubEntities)
			{
				luaL_argerror(L, idx, "sub-entity index is out of range");
			}

			return subEntityIndex;
		}

		void* GetFirstSubEntity(void* entity)
		{
			uint32_t count = 0;
			if (!TryGetNumSubEntities(entity, count) || count == 0)
			{
				return nullptr;
			}

			void* subEntity = nullptr;
			if (!TryGetSubEntityByIndex(entity, 0, subEntity))
			{
				return nullptr;
			}

			return subEntity;
		}

		std::string CheckOptionalResourceGroup(lua_State* L, int idx)
		{
			if (lua_isnoneornil(L, idx))
			{
				return "General";
			}

			return luaL_checkstring(L, idx);
		}

		void* GetSubEntity(lua_State* L, void* entity, int idx)
		{
			void* subEntity = nullptr;
			if (!TryGetSubEntityByIndex(entity, CheckSubEntityIndex(L, entity, idx), subEntity))
			{
				luaL_error(L, "Extra Utilities: render entity probe crashed while getting sub-entity");
			}
			return subEntity;
		}
	}

	int SetAsUser(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		TrySetAsUser(obj);
		return 0;
	}

	int IsCommTowerPowered(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);

		lua_getglobal(L, "GetClassLabel");
		lua_pushlightuserdata(L, reinterpret_cast<void*>(h));
		lua_call(L, 1, 1);

		std::string classLabel = luaL_checkstring(L, -1);

		if (classLabel != "commtower")
		{
			luaL_error(L, "Extra Utilities: object is not a comm tower");
			return 0;
		}

		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		BZR::handle powerHandle = 0;
		TryGetCommTowerPowerHandle(obj, powerHandle);
		
		lua_pushboolean(L, powerHandle == 0 ? 0 : 1);

		return 1;
	}

	int GetHandle(lua_State* L)
	{
		auto gameObject = reinterpret_cast<BZR::GameObject*>(CheckHandle(L, 1));
		BZR::handle h = BZR::GameObject::GetHandle(gameObject);
		lua_pushlightuserdata(L, reinterpret_cast<void*>(h));
		return 1;
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
		if (!TryResolveMaterial(sourceMaterial, resourceGroup, source))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		::Ogre::Material* clone = nullptr;
		lua_pushboolean(L, TryCloneMaterial(source, cloneName, resourceGroup, clone) ? 1 : 0);
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

	int SetMaterialPassColors(lua_State* L)
	{
		std::string materialName = luaL_checkstring(L, 1);
		luaL_checktype(L, 2, LUA_TTABLE);
		int techniqueIndex = luaL_optint(L, 3, 0);
		int passIndex = luaL_optint(L, 4, 0);
		std::string resourceGroup = CheckOptionalResourceGroup(L, 5);

		MaterialPassHandle handle;
		if (!TryResolveMaterialPass(materialName, resourceGroup, techniqueIndex, passIndex, handle))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		bool success = true;
		::Ogre::ColourValue color{};

		if (ReadOptionalPassColorField(L, 2, "ambient", color))
		{
			success = TrySetPassAmbient(handle.pass, color) && success;
		}

		if (ReadOptionalPassColorField(L, 2, "diffuse", color))
		{
			success = TrySetPassDiffuse(handle.pass, color) && success;
		}

		if (ReadOptionalPassColorField(L, 2, "specular", color))
		{
			success = TrySetPassSpecular(handle.pass, color) && success;
		}

		if (ReadOptionalPassColorField(L, 2, "emissive", color)
			|| ReadOptionalPassColorField(L, 2, "selfIllumination", color)
			|| ReadOptionalPassColorField(L, 2, "selfillumination", color))
		{
			success = TrySetPassSelfIllumination(handle.pass, color) && success;
		}

		lua_pushboolean(L, success ? 1 : 0);
		return 1;
	}

	int GetEntityVisible(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		bool visible = false;
		if (!TryGetVisible(entity, visible))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushboolean(L, visible ? 1 : 0);
		return 1;
	}

	int GetEntityCastShadows(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		bool castShadows = false;
		if (!TryGetCastShadows(entity, castShadows))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushboolean(L, castShadows ? 1 : 0);
		return 1;
	}

	int GetEntityRenderingDistance(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		float distance = 0.0f;
		if (!TryGetRenderingDistance(entity, distance))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushnumber(L, distance);
		return 1;
	}

	int GetEntityVisibilityFlags(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		uint32_t flags = 0;
		if (!TryGetVisibilityFlags(entity, flags))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushinteger(L, flags);
		return 1;
	}

	int GetEntityQueryFlags(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		uint32_t flags = 0;
		if (!TryGetQueryFlags(entity, flags))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushinteger(L, flags);
		return 1;
	}

	int GetEntityRenderQueueGroup(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		uint8_t group = 0;
		if (!TryGetRenderQueueGroup(entity, group))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushinteger(L, group);
		return 1;
	}

	int SetEntityVisible(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		bool visible = CheckBool(L, 2);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		TrySetVisible(entity, visible);
		return 0;
	}

	int SetEntityCastShadows(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		bool castShadows = CheckBool(L, 2);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		TrySetCastShadows(entity, castShadows);
		return 0;
	}

	int SetEntityRenderingDistance(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float distance = static_cast<float>(luaL_checknumber(L, 2));
		if (!std::isfinite(distance) || distance < 0.0f)
		{
			return luaL_argerror(L, 2, "rendering distance must be a finite non-negative number");
		}

		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		TrySetRenderingDistance(entity, distance);
		return 0;
	}

	int SetEntityVisibilityFlags(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		uint32_t flags = static_cast<uint32_t>(luaL_checkinteger(L, 2));
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		TrySetVisibilityFlags(entity, flags);
		return 0;
	}

	int SetEntityQueryFlags(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		uint32_t flags = static_cast<uint32_t>(luaL_checkinteger(L, 2));
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		TrySetQueryFlags(entity, flags);
		return 0;
	}

	int SetEntityRenderQueueGroup(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		int groupValue = luaL_checkinteger(L, 2);
		if (groupValue < 0 || groupValue > 255)
		{
			return luaL_argerror(L, 2, "render queue group must be in range 0..255");
		}

		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		TrySetRenderQueueGroup(entity, static_cast<uint8_t>(groupValue));
		return 0;
	}

	int SetSubEntityVisible(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		void* subEntity = GetSubEntity(L, entity, 2);
		bool visible = CheckBool(L, 3);
		TrySetVisible(subEntity, visible);
		return 0;
	}

	int SetHeadlightDiffuse(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float r = static_cast<float>(luaL_checknumber(L, 2));
		float g = static_cast<float>(luaL_checknumber(L, 3));
		float b = static_cast<float>(luaL_checknumber(L, 4));

		void* light = GetLightObject(h);

		if (light == nullptr)
		{
			return 0;
		}

		TrySetLightDiffuse(light, r, g, b);

		return 0;
	}

	int SetHeadlightSpecular(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float r = static_cast<float>(luaL_checknumber(L, 2));
		float g = static_cast<float>(luaL_checknumber(L, 3));
		float b = static_cast<float>(luaL_checknumber(L, 4));

		void* light = GetLightObject(h);

		if (light == nullptr)
		{
			return 0;
		}

		TrySetLightSpecular(light, r, g, b);

		return 0;
	}

	int SetHeadlightRange(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float innerAngle = static_cast<float>(luaL_checknumber(L, 2));
		float outerAngle = static_cast<float>(luaL_checknumber(L, 3));
		float falloff = static_cast<float>(luaL_checknumber(L, 4));

		void* light = GetLightObject(h);

		if (light == nullptr)
		{
			return 0;
		}

		TrySetSpotlightRange(light, innerAngle, outerAngle, falloff);

		return 0;
	}

	int SetHeadlightVisible(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		bool visible = CheckBool(L, 2);

		void* light = GetLightObject(h);

		if (light == nullptr)
		{
			return 0;
		}

		TrySetVisible(light, visible);

		return 0;
	}

	int GetLightPowerScale(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		void* light = GetLightObject(h);
		if (light == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		float powerScale = 0.0f;
		if (!TryGetLightPowerScale(light, powerScale))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushnumber(L, powerScale);
		return 1;
	}

	int SetLightPowerScale(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float powerScale = static_cast<float>(luaL_checknumber(L, 2));
		if (!std::isfinite(powerScale))
		{
			return luaL_argerror(L, 2, "power scale must be a finite number");
		}

		void* light = GetLightObject(h);
		if (light == nullptr)
		{
			return 0;
		}

		TrySetLightPowerScale(light, powerScale);
		return 0;
	}

	int GetLightPosition(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		void* light = GetLightObject(h);
		if (light == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		BZR::VECTOR_3D position{};
		if (!TryGetLightPosition(light, position))
		{
			lua_pushnil(L);
			return 1;
		}

		PushVector(L, position);
		return 1;
	}

	int SetLightPosition(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		auto position = CheckVectorOrSingles(L, 2);
		if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
		{
			return luaL_argerror(L, 2, "light position must use finite numeric values");
		}

		void* light = GetLightObject(h);
		if (light == nullptr)
		{
			return 0;
		}

		TrySetLightPosition(light, position);
		return 0;
	}

	int GetLightDirection(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		void* light = GetLightObject(h);
		if (light == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		BZR::VECTOR_3D direction{};
		if (!TryGetLightDirection(light, direction))
		{
			lua_pushnil(L);
			return 1;
		}

		PushVector(L, direction);
		return 1;
	}

	int SetLightDirection(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		auto direction = CheckVectorOrSingles(L, 2);
		if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z))
		{
			return luaL_argerror(L, 2, "light direction must use finite numeric values");
		}

		void* light = GetLightObject(h);
		if (light == nullptr)
		{
			return 0;
		}

		TrySetLightDirection(light, direction);
		return 0;
	}

	int SetLightAttenuation(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float range = static_cast<float>(luaL_checknumber(L, 2));
		float constant = static_cast<float>(luaL_checknumber(L, 3));
		float linear = static_cast<float>(luaL_checknumber(L, 4));
		float quadratic = static_cast<float>(luaL_checknumber(L, 5));
		if (!std::isfinite(range) || !std::isfinite(constant) || !std::isfinite(linear) || !std::isfinite(quadratic))
		{
			return luaL_argerror(L, 2, "light attenuation values must be finite numbers");
		}

		void* light = GetLightObject(h);
		if (light == nullptr)
		{
			return 0;
		}

		TrySetLightAttenuation(light, range, constant, linear, quadratic);
		return 0;
	}

	int HasEntityAnimation(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		std::string animationName = luaL_checkstring(L, 2);
		void* entity = GetRenderableEntity(h);
		lua_pushboolean(L, entity != nullptr && GetNamedAnimationState(entity, animationName) != nullptr);
		return 1;
	}

	int GetEntityAnimationInfo(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		std::string animationName = luaL_checkstring(L, 2);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		void* animationState = GetNamedAnimationState(entity, animationName);
		if (animationState == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		bool enabled = false;
		bool loop = false;
		float weight = 0.0f;
		float timePosition = 0.0f;
		float length = 0.0f;
		if (!TryGetAnimationEnabled(animationState, enabled) ||
			!TryGetAnimationLoop(animationState, loop) ||
			!TryGetAnimationWeight(animationState, weight) ||
			!TryGetAnimationTimePosition(animationState, timePosition) ||
			!TryGetAnimationLength(animationState, length))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_createtable(L, 0, 5);
		lua_pushboolean(L, enabled ? 1 : 0);
		lua_setfield(L, -2, "enabled");
		lua_pushboolean(L, loop ? 1 : 0);
		lua_setfield(L, -2, "loop");
		lua_pushnumber(L, weight);
		lua_setfield(L, -2, "weight");
		lua_pushnumber(L, timePosition);
		lua_setfield(L, -2, "timePosition");
		lua_pushnumber(L, length);
		lua_setfield(L, -2, "length");
		return 1;
	}

	int SetEntityAnimationEnabled(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		std::string animationName = luaL_checkstring(L, 2);
		bool enabled = CheckBool(L, 3);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		if (void* animationState = GetNamedAnimationState(entity, animationName))
		{
			TrySetAnimationEnabled(animationState, enabled);
		}
		return 0;
	}

	int SetEntityAnimationLoop(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		std::string animationName = luaL_checkstring(L, 2);
		bool loop = CheckBool(L, 3);
		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		if (void* animationState = GetNamedAnimationState(entity, animationName))
		{
			TrySetAnimationLoop(animationState, loop);
		}
		return 0;
	}

	int SetEntityAnimationWeight(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		std::string animationName = luaL_checkstring(L, 2);
		float weight = static_cast<float>(luaL_checknumber(L, 3));
		if (!std::isfinite(weight))
		{
			return luaL_argerror(L, 3, "animation weight must be a finite number");
		}

		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		if (void* animationState = GetNamedAnimationState(entity, animationName))
		{
			TrySetAnimationWeight(animationState, weight);
		}
		return 0;
	}

	int SetEntityAnimationTime(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		std::string animationName = luaL_checkstring(L, 2);
		float timePosition = static_cast<float>(luaL_checknumber(L, 3));
		if (!std::isfinite(timePosition))
		{
			return luaL_argerror(L, 3, "animation time must be a finite number");
		}

		void* entity = GetRenderableEntity(h);
		if (entity == nullptr)
		{
			return 0;
		}

		if (void* animationState = GetNamedAnimationState(entity, animationName))
		{
			TrySetAnimationTimePosition(animationState, timePosition);
		}
		return 0;
	}

	int GetMass(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float mass = 0.0f;
		__try
		{
			mass = BZR::GameObject::GetObj(h)->euler.mass;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogMaterialDebug("[EXU::GetMass] crashed handle=%p code=0x%08X", reinterpret_cast<void*>(h), GetExceptionCode());
			lua_pushnil(L);
			return 1;
		}
		lua_pushnumber(L, mass);
		return 1;
	}

	int SetMass(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float mass = static_cast<float>(luaL_checknumber(L, 2));
		if (mass == 0.0f)
		{
			return luaL_argerror(L, 2, "mass must be non-zero");
		}
		__try
		{
			auto obj = BZR::GameObject::GetObj(h);
			obj->euler.mass = mass;
			obj->euler.mass_inv = 1 / mass;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogMaterialDebug("[EXU::SetMass] crashed handle=%p code=0x%08X", reinterpret_cast<void*>(h), GetExceptionCode());
		}
		return 0;
	}

	int GetObj(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		lua_pushlightuserdata(L, obj);
		return 1;
	}

	int GetAiProcess(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		__try
		{
			void* aiProcess = BZR::GameObject::GetObj(h)->aiProcess;
			if (aiProcess == nullptr)
			{
				lua_pushnil(L);
			}
			else
			{
				lua_pushlightuserdata(L, aiProcess);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogMaterialDebug("[EXU::GetAiProcess] crashed handle=%p code=0x%08X", reinterpret_cast<void*>(h), GetExceptionCode());
			lua_pushnil(L);
		}
		return 1;
	}

	int GetAiProcessTypeName(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		void* vtable = nullptr;
		const char* rawTypeName = nullptr;
		MsvcRttiClassHierarchyDescriptor* classDescriptor = nullptr;

		if (TryGetPolymorphicMetadata(obj->aiProcess, vtable, rawTypeName, classDescriptor))
		{
			std::string typeName = NormalizeMsvcTypeName(rawTypeName);
			if (!typeName.empty())
			{
				lua_pushstring(L, typeName.c_str());
			}
			else
			{
				lua_pushnil(L);
			}
		}
		else
		{
			lua_pushnil(L);
		}
		return 1;
	}

	int GetAiProcessInfo(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		uint32_t scanBytes = GetScanBytesArgument(L, 2, 0x100);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		void* aiProcess = obj->aiProcess;
		if (aiProcess == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		void* vtable = nullptr;
		const char* rawTypeName = nullptr;
		MsvcRttiClassHierarchyDescriptor* classDescriptor = nullptr;

		lua_createtable(L, 0, 7);
		lua_pushlightuserdata(L, aiProcess);
		lua_setfield(L, -2, "process");
		lua_pushinteger(L, scanBytes);
		lua_setfield(L, -2, "scanBytes");

		if (TryGetPolymorphicMetadata(aiProcess, vtable, rawTypeName, classDescriptor))
		{
			if (vtable != nullptr)
			{
				lua_pushlightuserdata(L, vtable);
				lua_setfield(L, -2, "vtable");
			}

			std::string typeName = NormalizeMsvcTypeName(rawTypeName);
			lua_pushstring(L, rawTypeName);
			lua_setfield(L, -2, "rawTypeName");

			lua_pushstring(L, typeName.c_str());
			lua_setfield(L, -2, "typeName");

			std::vector<std::string> hierarchy = BuildHierarchyNames(classDescriptor);
			PushStringArray(L, hierarchy);
			lua_setfield(L, -2, "hierarchy");
		}

		std::vector<PolymorphicObjectInfo> children = ScanPolymorphicChildren(aiProcess, scanBytes);
		PushPolymorphicObjectArray(L, children);
		lua_setfield(L, -2, "children");

		std::vector<ScannedFieldInfo> fields = ScanAlignedFields(aiProcess, scanBytes);
		PushScannedFieldArray(L, fields);
		lua_setfield(L, -2, "fields");

		return 1;
	}

	int GetAiTaskInfo(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		uint32_t scanBytes = GetScanBytesArgument(L, 2, 0x100);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		void* aiProcess = obj->aiProcess;
		if (aiProcess == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		std::vector<PolymorphicObjectInfo> children = ScanPolymorphicChildren(aiProcess, scanBytes);
		std::vector<PolymorphicObjectInfo> candidates;
		candidates.reserve(children.size());
		for (const auto& child : children)
		{
			if (child.typeName.find("Task") != std::string::npos ||
				child.typeName.find("Attack") != std::string::npos)
			{
				candidates.push_back(child);
			}
		}

		if (candidates.empty())
		{
			lua_pushnil(L);
			return 1;
		}

		size_t bestIndex = 0;
		int bestScore = GetTaskCandidateScore(candidates[0]);
		for (size_t i = 1; i < candidates.size(); ++i)
		{
			int score = GetTaskCandidateScore(candidates[i]);
			if (score > bestScore)
			{
				bestScore = score;
				bestIndex = i;
			}
		}

		const PolymorphicObjectInfo& selected = candidates[bestIndex];
		PushPolymorphicObjectInfo(L, selected);

		lua_pushinteger(L, scanBytes);
		lua_setfield(L, -2, "scanBytes");

		PushPolymorphicObjectArray(L, candidates);
		lua_setfield(L, -2, "candidates");

		std::vector<PolymorphicObjectInfo> nestedChildren = ScanPolymorphicChildren(selected.object, scanBytes);
		PushPolymorphicObjectArray(L, nestedChildren);
		lua_setfield(L, -2, "children");

		std::vector<ScannedFieldInfo> fields = ScanAlignedFields(selected.object, scanBytes);
		PushScannedFieldArray(L, fields);
		lua_setfield(L, -2, "fields");

		return 1;
	}

	int GetAiTaskFieldScan(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		uint32_t scanBytes = GetScanBytesArgument(L, 2, 0x100);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		void* aiProcess = obj->aiProcess;
		if (aiProcess == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		std::vector<PolymorphicObjectInfo> children = ScanPolymorphicChildren(aiProcess, scanBytes);
		size_t bestIndex = static_cast<size_t>(-1);
		int bestScore = 0;
		for (size_t i = 0; i < children.size(); ++i)
		{
			if (children[i].typeName.find("Task") == std::string::npos &&
				children[i].typeName.find("Attack") == std::string::npos)
			{
				continue;
			}

			int score = GetTaskCandidateScore(children[i]);
			if (bestIndex == static_cast<size_t>(-1) || score > bestScore)
			{
				bestIndex = i;
				bestScore = score;
			}
		}

		if (bestIndex == static_cast<size_t>(-1))
		{
			lua_pushnil(L);
			return 1;
		}

		std::vector<ScannedFieldInfo> fields = ScanAlignedFields(children[bestIndex].object, scanBytes);
		PushScannedFieldArray(L, fields);
		return 1;
	}


	int GetRadarPeriod(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::Scanner* scanner = BZR::GameObject::GetObj(h)->GetScanner();
		if (scanner == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushnumber(L, scanner->period);
		return 1;
	}

	int SetRadarPeriod(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float period = static_cast<float>(luaL_checknumber(L, 2));
		BZR::Scanner* scanner = BZR::GameObject::GetObj(h)->GetScanner();
		if (scanner == nullptr)
		{
			return 0;
		}
		scanner->period = period;
		scanner->sweep = 0.01f; // this causes it to immediately update and skip the current sweep

		return 0;
	}

	int GetRadarRange(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::Scanner* scanner = BZR::GameObject::GetObj(h)->GetScanner();
		if (scanner == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushnumber(L, scanner->range);
		return 1;
	}

	int SetRadarRange(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float range = static_cast<float>(luaL_checknumber(L, 2));
		BZR::Scanner* scanner = BZR::GameObject::GetObj(h)->GetScanner();
		if (scanner == nullptr)
		{
			return 0;
		}
		scanner->range = range;
		return 0;
	}

	int GetVelocJam(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::Jammer* jammer = BZR::GameObject::GetObj(h)->GetJammer();
		if (jammer != nullptr) // sometimes your gameobject may not have a jammer, like if you're a pilot for example
		{
			lua_pushnumber(L, jammer->maxSpeed);
		}
		else
		{
			lua_pushnil(L);
		}
		return 1;
	}

	int SetVelocJam(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float maxSpeed = static_cast<float>(luaL_checknumber(L, 2));
		BZR::Jammer* jammer = BZR::GameObject::GetObj(h)->GetJammer();
		if (jammer != nullptr)
		{
			jammer->maxSpeed = maxSpeed;
		}
		return 0;
	}
}
