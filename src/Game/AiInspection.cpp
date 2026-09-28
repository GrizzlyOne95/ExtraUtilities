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

#include "Util/MsvcRtti.h"

// AI process and task inspection: MSVC RTTI walking, polymorphic child and
// aligned-field scans, and the GetAi*/SetAiTaskState bindings.

namespace ExtraUtilities::Lua::GameObject
{
	namespace
	{
		using MsvcRttiTypeDescriptor = MsvcRtti::TypeDescriptor;
		using MsvcRttiBaseClassDescriptor = MsvcRtti::BaseClassDescriptor;
		using MsvcRttiClassHierarchyDescriptor = MsvcRtti::ClassHierarchyDescriptor;
		using MsvcRttiCompleteObjectLocator = MsvcRtti::CompleteObjectLocator;

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
		std::vector<PolymorphicObjectInfo> ScanPolymorphicChildren(void* base, uint32_t scanBytes);

		bool TryCopyReadableCString(const char* rawName, std::string& outName, size_t maxLength = 256)
		{
			outName.clear();
			if (rawName == nullptr)
			{
				return false;
			}

			__try
			{
				size_t length = 0;
				for (; length < maxLength; ++length)
				{
					const char ch = rawName[length];
					if (ch == '\0')
					{
						break;
					}

					const unsigned char byte = static_cast<unsigned char>(ch);
					if (byte < 0x20 || byte > 0x7e)
					{
						return false;
					}
				}

				if (length == 0 || length >= maxLength)
				{
					return false;
				}

				outName.assign(rawName, length);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outName.clear();
				return false;
			}
		}

		std::string NormalizeMsvcTypeName(const std::string& rawName)
		{
			if (rawName.empty())
			{
				return {};
			}

			const std::string& name = rawName;
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
			std::string& outRawTypeName,
			MsvcRttiClassHierarchyDescriptor*& outClassDescriptor)
		{
			outVtable = nullptr;
			outRawTypeName.clear();
			outClassDescriptor = nullptr;

			if (object == nullptr)
			{
				return false;
			}

			const char* rawName = nullptr;
			MsvcRttiClassHierarchyDescriptor* classDescriptor = nullptr;

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

				rawName = col->pTypeDescriptor->name;
				if (rawName == nullptr || rawName[0] == '\0')
				{
					return false;
				}

				classDescriptor = col->pClassDescriptor;
				if (classDescriptor != nullptr)
				{
					__try
					{
						(void)classDescriptor->numBaseClasses;
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						classDescriptor = nullptr;
					}
				}

				outVtable = vtable;
				outClassDescriptor = classDescriptor;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outVtable = nullptr;
				outRawTypeName.clear();
				outClassDescriptor = nullptr;
				return false;
			}

			if (!TryCopyReadableCString(rawName, outRawTypeName))
			{
				outVtable = nullptr;
				outClassDescriptor = nullptr;
				return false;
			}

			return true;
		}

		bool TryGetBaseClassRawName(
			MsvcRttiClassHierarchyDescriptor* classDescriptor,
			uint32_t index,
			std::string& outRawTypeName)
		{
			outRawTypeName.clear();

			if (classDescriptor == nullptr)
			{
				return false;
			}

			const char* rawTypeName = nullptr;
			__try
			{
				auto** baseClassArray = classDescriptor->pBaseClassArray;
				if (baseClassArray == nullptr)
				{
					return false;
				}

				auto* baseDescriptor = baseClassArray[index];
				if (baseDescriptor == nullptr || baseDescriptor->pTypeDescriptor == nullptr)
				{
					return false;
				}

				rawTypeName = baseDescriptor->pTypeDescriptor->name;
				if (rawTypeName == nullptr || rawTypeName[0] == '\0')
				{
					return false;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outRawTypeName.clear();
				return false;
			}

			return TryCopyReadableCString(rawTypeName, outRawTypeName);
		}

		bool TryGetBaseClassCount(
			MsvcRttiClassHierarchyDescriptor* classDescriptor,
			uint32_t& outCount)
		{
			outCount = 0;

			if (classDescriptor == nullptr)
			{
				return false;
			}

			__try
			{
				if (classDescriptor->pBaseClassArray == nullptr)
				{
					return false;
				}

				outCount = classDescriptor->numBaseClasses;
				if (outCount > 64)
				{
					outCount = 64;
				}

				return outCount != 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outCount = 0;
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

			uint32_t count = 0;
			if (!TryGetBaseClassCount(classDescriptor, count))
			{
				return result;
			}

			std::unordered_set<std::string> seenNames;
			for (uint32_t i = 0; i < count; ++i)
			{
				std::string baseRawName;
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

		bool TypeMatches(const PolymorphicObjectInfo& info, const char* typeName)
		{
			if (typeName == nullptr || typeName[0] == '\0')
			{
				return false;
			}

			if (info.typeName == typeName)
			{
				return true;
			}

			for (const auto& baseName : info.hierarchy)
			{
				if (baseName == typeName)
				{
					return true;
				}
			}

			return false;
		}

		bool TryGetObjectTypeInfo(void* object, PolymorphicObjectInfo& outInfo)
		{
			outInfo = {};
			if (object == nullptr)
			{
				return false;
			}

			void* vtable = nullptr;
			std::string rawTypeName;
			MsvcRttiClassHierarchyDescriptor* classDescriptor = nullptr;
			if (!TryGetPolymorphicMetadata(object, vtable, rawTypeName, classDescriptor))
			{
				return false;
			}

			outInfo.object = object;
			outInfo.vtable = vtable;
			outInfo.rawTypeName = rawTypeName;
			outInfo.typeName = NormalizeMsvcTypeName(rawTypeName);
			outInfo.hierarchy = BuildHierarchyNames(classDescriptor);
			return true;
		}

		bool TryFindBestAiTask(
			void* aiProcess,
			uint32_t scanBytes,
			PolymorphicObjectInfo& outTask,
			const char* preferredTypeName = nullptr)
		{
			outTask = {};
			if (aiProcess == nullptr)
			{
				return false;
			}

			std::vector<PolymorphicObjectInfo> children = ScanPolymorphicChildren(aiProcess, scanBytes);
			if (children.empty())
			{
				return false;
			}

			size_t bestIndex = static_cast<size_t>(-1);
			int bestScore = 0;
			for (size_t i = 0; i < children.size(); ++i)
			{
				if (children[i].typeName.find("Task") == std::string::npos &&
					children[i].typeName.find("Attack") == std::string::npos)
				{
					continue;
				}

				if (preferredTypeName != nullptr && !TypeMatches(children[i], preferredTypeName))
				{
					continue;
				}

				const int score = GetTaskCandidateScore(children[i]);
				if (bestIndex == static_cast<size_t>(-1) || score > bestScore)
				{
					bestIndex = i;
					bestScore = score;
				}
			}

			if (bestIndex == static_cast<size_t>(-1))
			{
				return false;
			}

			outTask = children[bestIndex];
			return true;
		}

		void PushHandleField(lua_State* L, const char* fieldName, BZR::handle h)
		{
			if (h == 0)
			{
				lua_pushnil(L);
			}
			else
			{
				lua_pushlightuserdata(L, reinterpret_cast<void*>(h));
			}
			lua_setfield(L, -2, fieldName);
		}

		void PushObjectPointerField(lua_State* L, const char* fieldName, const void* pointer)
		{
			if (pointer == nullptr)
			{
				lua_pushnil(L);
			}
			else
			{
				lua_pushlightuserdata(L, const_cast<void*>(pointer));
			}
			lua_setfield(L, -2, fieldName);
		}

		void PushVectorField(lua_State* L, const char* fieldName, const BZR::VECTOR_3D& value)
		{
			PushVector(L, value);
			lua_setfield(L, -2, fieldName);
		}

		bool TryGetOptionalNumberField(lua_State* L, int tableIndex, const char* fieldName, float& outValue)
		{
			lua_getfield(L, tableIndex, fieldName);
			const bool hasValue = !lua_isnil(L, -1);
			if (hasValue)
			{
				outValue = static_cast<float>(luaL_checknumber(L, -1));
			}
			lua_pop(L, 1);
			return hasValue;
		}

		bool TryGetOptionalBooleanField(lua_State* L, int tableIndex, const char* fieldName, bool& outValue)
		{
			lua_getfield(L, tableIndex, fieldName);
			const bool hasValue = !lua_isnil(L, -1);
			if (hasValue)
			{
				outValue = CheckBool(L, -1);
			}
			lua_pop(L, 1);
			return hasValue;
		}

		bool TryGetOptionalVectorField(lua_State* L, int tableIndex, const char* fieldName, BZR::VECTOR_3D& outValue)
		{
			lua_getfield(L, tableIndex, fieldName);
			const bool hasValue = !lua_isnil(L, -1);
			if (hasValue)
			{
				outValue = CheckVectorOrSingles(L, -1);
			}
			lua_pop(L, 1);
			return hasValue;
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
				std::string rawTypeName;
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
						std::string rawTypeName;
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
	}

	namespace Detail
	{
		bool TryGetClassLabelFromLua(lua_State* L, BZR::handle h, std::string& outClassLabel)
		{
			outClassLabel.clear();
			const int top = lua_gettop(L);
			lua_getglobal(L, "GetClassLabel");
			if (!lua_isfunction(L, -1))
			{
				lua_settop(L, top);
				return false;
			}

			lua_pushlightuserdata(L, reinterpret_cast<void*>(h));
			if (lua_pcall(L, 1, 1, 0) != 0)
			{
				lua_settop(L, top);
				return false;
			}

			const char* rawClassLabel = lua_tostring(L, -1);
			if (rawClassLabel != nullptr)
			{
				outClassLabel.assign(rawClassLabel);
			}

			lua_settop(L, top);
			return !outClassLabel.empty();
		}
	}

	int GetAiProcess(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		__try
		{
			BZR::GameObject* obj = BZR::GameObject::GetObj(h);
			void* aiProcess = obj != nullptr ? obj->aiProcess : nullptr;
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
			LogMaterialFault("[EXU::GetAiProcess] crashed handle=%p code=0x%08X", reinterpret_cast<void*>(h), GetExceptionCode());
			lua_pushnil(L);
		}
		return 1;
	}

	int GetAiProcessTypeName(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		void* vtable = nullptr;
		std::string rawTypeName;
		MsvcRttiClassHierarchyDescriptor* classDescriptor = nullptr;

		if (obj != nullptr && TryGetPolymorphicMetadata(obj->aiProcess, vtable, rawTypeName, classDescriptor))
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
		void* aiProcess = obj != nullptr ? obj->aiProcess : nullptr;
		if (aiProcess == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		void* vtable = nullptr;
		std::string rawTypeName;
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
			lua_pushstring(L, rawTypeName.c_str());
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

	int GetAiProcessState(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		void* aiProcess = obj != nullptr ? obj->aiProcess : nullptr;
		if (aiProcess == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		PolymorphicObjectInfo processInfo{};
		if (!TryGetObjectTypeInfo(aiProcess, processInfo))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_createtable(L, 0, 16);
		lua_pushlightuserdata(L, aiProcess);
		lua_setfield(L, -2, "process");
		lua_pushstring(L, processInfo.typeName.c_str());
		lua_setfield(L, -2, "typeName");
		lua_pushstring(L, processInfo.rawTypeName.c_str());
		lua_setfield(L, -2, "rawTypeName");
		PushStringArray(L, processInfo.hierarchy);
		lua_setfield(L, -2, "hierarchy");

		if (TypeMatches(processInfo, "ScavengerProcess"))
		{
			const auto* scav = reinterpret_cast<const ScavengerProcessLayout*>(aiProcess);
			lua_pushinteger(L, scav->curState);
			lua_setfield(L, -2, "curState");
			lua_pushinteger(L, scav->nextState);
			lua_setfield(L, -2, "nextState");
			lua_pushnumber(L, scav->oldHealth);
			lua_setfield(L, -2, "oldHealth");
			PushHandleField(L, "whoHandle", scav->whoHandle);
			BZR::handle craftHandle = 0;
			if (TryGetHandleFromObject(scav->craft, craftHandle))
			{
				PushHandleField(L, "craftHandle", craftHandle);
			}
			else
			{
				lua_pushnil(L);
				lua_setfield(L, -2, "craftHandle");
			}
			PushObjectPointerField(L, "craft", scav->craft);
			PushVectorField(L, "where", scav->where);
			PushVectorField(L, "lastScrap", scav->lastScrap);
			lua_pushnumber(L, scav->waitTime);
			lua_setfield(L, -2, "waitTime");
			lua_pushboolean(L, scav->recycle != 0);
			lua_setfield(L, -2, "recycle");
			lua_pushinteger(L, scav->team);
			lua_setfield(L, -2, "team");
			PushObjectPointerField(L, "escortGoal", scav->escortGoal);
			PushObjectPointerField(L, "myEscorts", scav->myEscorts);
			PushObjectPointerField(L, "task", scav->task);

			void* taskVtable = nullptr;
			std::string taskRawTypeName;
			MsvcRttiClassHierarchyDescriptor* taskClassDescriptor = nullptr;
			if (TryGetPolymorphicMetadata(scav->task, taskVtable, taskRawTypeName, taskClassDescriptor))
			{
				const std::string taskTypeName = NormalizeMsvcTypeName(taskRawTypeName);
				lua_pushstring(L, taskTypeName.c_str());
				lua_setfield(L, -2, "taskTypeName");
			}
		}

		return 1;
	}

	int GetAiTaskInfo(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		uint32_t scanBytes = GetScanBytesArgument(L, 2, 0x100);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		void* aiProcess = obj != nullptr ? obj->aiProcess : nullptr;
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

	int GetAiTaskState(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		const uint32_t scanBytes = GetScanBytesArgument(L, 2, 0x100);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		void* aiProcess = obj != nullptr ? obj->aiProcess : nullptr;
		if (aiProcess == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		PolymorphicObjectInfo taskInfo{};
		if (!TryFindBestAiTask(aiProcess, scanBytes, taskInfo))
		{
			lua_pushnil(L);
			return 1;
		}

		const auto* task = reinterpret_cast<const UnitTaskLayout*>(taskInfo.object);
		lua_createtable(L, 0, 24);
		lua_pushlightuserdata(L, taskInfo.object);
		lua_setfield(L, -2, "task");
		lua_pushstring(L, taskInfo.typeName.c_str());
		lua_setfield(L, -2, "typeName");
		lua_pushstring(L, taskInfo.rawTypeName.c_str());
		lua_setfield(L, -2, "rawTypeName");
		PushStringArray(L, taskInfo.hierarchy);
		lua_setfield(L, -2, "hierarchy");
		lua_pushinteger(L, task->curState);
		lua_setfield(L, -2, "curState");
		lua_pushinteger(L, task->nextState);
		lua_setfield(L, -2, "nextState");
		BZR::handle meHandle = 0;
		if (TryGetHandleFromObject(task->me, meHandle))
		{
			PushHandleField(L, "meHandle", meHandle);
		}
		else
		{
			lua_pushnil(L);
			lua_setfield(L, -2, "meHandle");
		}
		PushObjectPointerField(L, "me", task->me);
		PushHandleField(L, "himHandle", task->himHandle);
		PushObjectPointerField(L, "him", task->him);
		PushVectorField(L, "gotoPoint", task->gotoPoint);
		PushVectorField(L, "goalPoint", task->goalPoint);
		PushVectorField(L, "gotoForce", task->gotoForce);
		PushVectorField(L, "gotoDir", task->gotoDir);
		PushVectorField(L, "lastStuck", task->lastStuck);
		lua_pushnumber(L, task->braccelFactor);
		lua_setfield(L, -2, "braccel");
		lua_pushnumber(L, task->strafeFactor);
		lua_setfield(L, -2, "strafe");
		lua_pushnumber(L, task->steerFactor);
		lua_setfield(L, -2, "steer");
		lua_pushnumber(L, task->omegaFactor);
		lua_setfield(L, -2, "omega");
		lua_pushnumber(L, task->omegaScale);
		lua_setfield(L, -2, "omegaScale");
		lua_pushnumber(L, task->pitch);
		lua_setfield(L, -2, "pitch");
		lua_pushnumber(L, task->nextStuck);
		lua_setfield(L, -2, "nextStuck");
		lua_pushinteger(L, task->stuckState);
		lua_setfield(L, -2, "stuckState");
		lua_pushnumber(L, task->skill);
		lua_setfield(L, -2, "skill");
		lua_pushnumber(L, task->closeSq);
		lua_setfield(L, -2, "closeSq");
		lua_pushnumber(L, task->rangeSq);
		lua_setfield(L, -2, "rangeSq");
		lua_pushnumber(L, task->time);
		lua_setfield(L, -2, "time");
		lua_pushnumber(L, task->shotSpeed);
		lua_setfield(L, -2, "shotSpeed");
		lua_pushnumber(L, task->shotSpeedInv);
		lua_setfield(L, -2, "shotSpeedInv");

		lua_pushboolean(L, Patch::GetUnitTurboOverride(h));
		lua_setfield(L, -2, "turbo");

		return 1;
	}

	namespace
	{
		// Values parsed from the Lua table before anything is resolved or
		// written: every call that can raise happens first, so a bad field can
		// neither leave a task half-written nor leak the C++ objects used to
		// find the task.
		struct AiTaskStateRequest
		{
			bool hasBraccel = false, hasStrafe = false, hasSteer = false, hasOmega = false, hasOmegaScale = false, hasPitch = false;
			float braccel = 0.0f, strafe = 0.0f, steer = 0.0f, omega = 0.0f, omegaScale = 0.0f, pitch = 0.0f;
			bool hasGotoForce = false, hasGotoDir = false;
			BZR::VECTOR_3D gotoForce{}, gotoDir{};
			bool hasTurbo = false, turbo = false;
		};

		// The steering factors are small multipliers in stock AI; anything
		// outside this range is a script bug, and NaN/inf would propagate into
		// the physics integration.
		constexpr float kMaxAiSteeringMagnitude = 1.0e4f;

		bool IsSaneAiScalar(float value)
		{
			return std::isfinite(value) && std::fabs(value) <= kMaxAiSteeringMagnitude;
		}

		bool IsSaneAiVector(const BZR::VECTOR_3D& value)
		{
			return IsSaneAiScalar(value.x) && IsSaneAiScalar(value.y) && IsSaneAiScalar(value.z);
		}

		void ReadAiScalarField(lua_State* L, const char* name, bool& hasValue, float& value)
		{
			hasValue = TryGetOptionalNumberField(L, 2, name, value);
			if (hasValue && !IsSaneAiScalar(value))
			{
				luaL_error(L, "SetAiTaskState field '%s' must be a finite number within +/-%f", name, static_cast<double>(kMaxAiSteeringMagnitude));
			}
		}

		void ReadAiVectorField(lua_State* L, const char* name, bool& hasValue, BZR::VECTOR_3D& value)
		{
			hasValue = TryGetOptionalVectorField(L, 2, name, value);
			if (hasValue && !IsSaneAiVector(value))
			{
				luaL_error(L, "SetAiTaskState field '%s' must have finite components within +/-%f", name, static_cast<double>(kMaxAiSteeringMagnitude));
			}
		}

		// Plain-data write so the SEH frame holds no C++ objects.
		bool TryApplyAiTaskState(UnitTaskLayout* task, const AiTaskStateRequest& request)
		{
			__try
			{
				if (request.hasBraccel) task->braccelFactor = request.braccel;
				if (request.hasStrafe) task->strafeFactor = request.strafe;
				if (request.hasSteer) task->steerFactor = request.steer;
				if (request.hasOmega) task->omegaFactor = request.omega;
				if (request.hasOmegaScale) task->omegaScale = request.omegaScale;
				if (request.hasPitch) task->pitch = request.pitch;
				if (request.hasGotoForce) task->gotoForce = request.gotoForce;
				if (request.hasGotoDir) task->gotoDir = request.gotoDir;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}
	}

	int SetAiTaskState(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		luaL_checktype(L, 2, LUA_TTABLE);

		AiTaskStateRequest request{};
		ReadAiScalarField(L, "braccel", request.hasBraccel, request.braccel);
		ReadAiScalarField(L, "strafe", request.hasStrafe, request.strafe);
		ReadAiScalarField(L, "steer", request.hasSteer, request.steer);
		ReadAiScalarField(L, "omega", request.hasOmega, request.omega);
		ReadAiScalarField(L, "omegaScale", request.hasOmegaScale, request.omegaScale);
		ReadAiScalarField(L, "pitch", request.hasPitch, request.pitch);
		ReadAiVectorField(L, "gotoForce", request.hasGotoForce, request.gotoForce);
		ReadAiVectorField(L, "gotoDir", request.hasGotoDir, request.gotoDir);
		request.hasTurbo = TryGetOptionalBooleanField(L, 2, "turbo", request.turbo);

		// Nothing below raises a Lua error.
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		if (obj == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		void* aiProcess = obj->aiProcess;
		if (aiProcess == nullptr)
		{
			Logging::LogMessage("[EXU::AiTask] SetAiTaskState missing aiProcess handle=%u", static_cast<unsigned>(h));
			lua_pushboolean(L, 0);
			return 1;
		}

		// Only a UnitTask (or a class derived from it) has this layout. In
		// 2.2.301 every stock task class except RecycleTask derives from
		// UnitTask; RecycleTask derives from AiTask directly and writing the
		// UnitTask layout into it corrupted its state machine and wrote past
		// the object.
		const bool wantsTaskWrite = request.hasBraccel || request.hasStrafe || request.hasSteer || request.hasOmega ||
			request.hasOmegaScale || request.hasPitch || request.hasGotoForce || request.hasGotoDir;
		std::string taskTypeName;
		if (wantsTaskWrite)
		{
			PolymorphicObjectInfo taskInfo{};
			if (!TryFindBestAiTask(aiProcess, 0x100, taskInfo, "UnitTask"))
			{
				Logging::LogMessage("[EXU::AiTask] SetAiTaskState no UnitTask found handle=%u", static_cast<unsigned>(h));
				lua_pushboolean(L, 0);
				return 1;
			}

			taskTypeName = taskInfo.typeName;
			if (!TryApplyAiTaskState(reinterpret_cast<UnitTaskLayout*>(taskInfo.object), request))
			{
				Logging::LogMessage("[EXU::AiTask] SetAiTaskState write faulted handle=%u task=%s", static_cast<unsigned>(h), taskTypeName.c_str());
				lua_pushboolean(L, 0);
				return 1;
			}
		}

		if (request.hasTurbo)
		{
			Patch::SetUnitTurboOverride(h, request.turbo);
		}

		if (Logging::IsDebugLoggingEnabled())
		{
			Logging::LogMessage(
				"[EXU::AiTask] SetAiTaskState handle=%u task=%s turbo=%d",
				static_cast<unsigned>(h),
				taskTypeName.empty() ? "-" : taskTypeName.c_str(),
				request.hasTurbo ? (request.turbo ? 1 : 0) : -1);
		}
		lua_pushboolean(L, 1);
		return 1;
	}

	int GetAiTaskFieldScan(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		uint32_t scanBytes = GetScanBytesArgument(L, 2, 0x100);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		void* aiProcess = obj != nullptr ? obj->aiProcess : nullptr;
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

	int GetAiRecycleTaskState(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		const uint32_t scanBytes = GetScanBytesArgument(L, 2, 0x100);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		void* aiProcess = obj != nullptr ? obj->aiProcess : nullptr;
		if (aiProcess == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		PolymorphicObjectInfo taskInfo{};
		if (!TryFindBestAiTask(aiProcess, scanBytes, taskInfo, "RecycleTask"))
		{
			lua_pushnil(L);
			return 1;
		}

		const auto* recycleTask = reinterpret_cast<const RecycleTaskLayout*>(taskInfo.object);
		lua_createtable(L, 0, 20);
		lua_pushlightuserdata(L, taskInfo.object);
		lua_setfield(L, -2, "task");
		lua_pushstring(L, taskInfo.typeName.c_str());
		lua_setfield(L, -2, "typeName");
		lua_pushstring(L, taskInfo.rawTypeName.c_str());
		lua_setfield(L, -2, "rawTypeName");
		PushStringArray(L, taskInfo.hierarchy);
		lua_setfield(L, -2, "hierarchy");
		lua_pushinteger(L, recycleTask->curState);
		lua_setfield(L, -2, "curState");
		lua_pushinteger(L, recycleTask->nextState);
		lua_setfield(L, -2, "nextState");
		lua_pushnumber(L, recycleTask->nextStuck);
		lua_setfield(L, -2, "nextStuck");
		lua_pushinteger(L, recycleTask->stuckState);
		lua_setfield(L, -2, "stuckState");
		BZR::handle meHandle = 0;
		if (TryGetHandleFromObject(recycleTask->me, meHandle))
		{
			PushHandleField(L, "meHandle", meHandle);
		}
		else
		{
			lua_pushnil(L);
			lua_setfield(L, -2, "meHandle");
		}
		PushObjectPointerField(L, "me", recycleTask->me);
		PushObjectPointerField(L, "subtask", recycleTask->subtask);
		void* subtaskVtable = nullptr;
		std::string subtaskRawTypeName;
		MsvcRttiClassHierarchyDescriptor* subtaskClassDescriptor = nullptr;
		if (TryGetPolymorphicMetadata(recycleTask->subtask, subtaskVtable, subtaskRawTypeName, subtaskClassDescriptor))
		{
			const std::string subtaskTypeName = NormalizeMsvcTypeName(subtaskRawTypeName);
			lua_pushstring(L, subtaskTypeName.c_str());
			lua_setfield(L, -2, "subtaskTypeName");
		}
		PushVectorField(L, "lastScrap", recycleTask->lastScrap);
		PushHandleField(L, "scrapHandle", recycleTask->scrapHandle);
		PushHandleField(L, "dropHandle", recycleTask->dropHandle);
		PushVectorField(L, "where", recycleTask->where);
		lua_pushnumber(L, recycleTask->nextCheck);
		lua_setfield(L, -2, "nextCheck");
		PushVectorField(L, "lastRecyclerPos", recycleTask->lastRecyclerPos);
		PushVectorField(L, "lastStuck", recycleTask->lastStuck);
		if (recycleTask->me != nullptr && recycleTask->scrapHandle != 0)
		{
			const float scrapDistance = (recycleTask->me->pos - recycleTask->lastScrap).Length();
			lua_pushnumber(L, scrapDistance);
			lua_setfield(L, -2, "scrapDistance");
		}

		return 1;
	}
}
