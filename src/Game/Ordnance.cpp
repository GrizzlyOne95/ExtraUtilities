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

#include "LuaHelpers.h"
#include "Ordnance.h"
#include "Util/SignatureResolver.h"

#include <lua.hpp>

#include <Windows.h>

#include <cstring>

namespace ExtraUtilities::Lua::Ordnance
{
	namespace
	{
		// Ordnance lives for seconds and scripts receive raw Ordnance* values
		// (from BuildOrdnance and the BulletInit/BulletHit callbacks). Before
		// anything is dereferenced the pointer must be readable and its vtable
		// must belong to a class in the executable whose RTTI hierarchy
		// contains Ordnance. A freed round whose memory was reused by something
		// else is rejected; one reused by another round is still an Ordnance,
		// so the reads stay type-correct.
		struct RttiCompleteObjectLocator
		{
			uint32_t signature;
			uint32_t offset;
			uint32_t cdOffset;
			const void* typeDescriptor;
			const void* classDescriptor;
		};

		struct RttiClassHierarchyDescriptor
		{
			uint32_t signature;
			uint32_t attributes;
			uint32_t numBaseClasses;
			const void* const* baseClassArray;
		};

		bool IsInExecutableImage(const void* address, size_t length) noexcept
		{
			static uintptr_t imageStart = 0;
			static uintptr_t imageEnd = 0;
			if (imageStart == 0)
			{
				const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
				const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
				const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
				imageStart = reinterpret_cast<uintptr_t>(base);
				imageEnd = imageStart + nt->OptionalHeader.SizeOfImage;
			}

			const uintptr_t start = reinterpret_cast<uintptr_t>(address);
			return start >= imageStart && start < imageEnd && length <= imageEnd - start;
		}

		bool TypeDescriptorNameIs(const void* typeDescriptor, const char* mangledName) noexcept
		{
			// TypeDescriptor layout: pVFTable, spare, char name[].
			const char* name = reinterpret_cast<const char*>(typeDescriptor) + 8;
			const size_t length = std::strlen(mangledName) + 1;
			return IsInExecutableImage(name, length) && std::memcmp(name, mangledName, length) == 0;
		}

		bool IsOrdnanceObjectSeh(const void* object) noexcept
		{
			__try
			{
				const void* const* vftable = *reinterpret_cast<const void* const* const*>(object);
				if (!IsInExecutableImage(vftable - 1, sizeof(void*)))
				{
					return false;
				}

				const auto* locator = reinterpret_cast<const RttiCompleteObjectLocator*>(vftable[-1]);
				if (!IsInExecutableImage(locator, sizeof(*locator)) || locator->signature != 0)
				{
					return false;
				}

				const auto* hierarchy = reinterpret_cast<const RttiClassHierarchyDescriptor*>(locator->classDescriptor);
				if (!IsInExecutableImage(hierarchy, sizeof(*hierarchy)) ||
					hierarchy->numBaseClasses == 0 || hierarchy->numBaseClasses > 32 ||
					!IsInExecutableImage(hierarchy->baseClassArray, hierarchy->numBaseClasses * sizeof(void*)))
				{
					return false;
				}

				for (uint32_t i = 0; i < hierarchy->numBaseClasses; ++i)
				{
					// A BaseClassDescriptor starts with its TypeDescriptor pointer.
					const void* baseClass = hierarchy->baseClassArray[i];
					if (!IsInExecutableImage(baseClass, sizeof(void*)))
					{
						return false;
					}
					if (TypeDescriptorNameIs(*reinterpret_cast<const void* const*>(baseClass), ".?AVOrdnance@@"))
					{
						return true;
					}
				}
				return false;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// Returns nullptr for a pointer that is not (or no longer) a live
		// Ordnance; raises only for a non-pointer argument.
		BZR::Ordnance* CheckOrdnance(lua_State* L, int index)
		{
			if (!lua_islightuserdata(L, index))
			{
				luaL_typerror(L, index, "Ordnance");
			}

			auto* ord = static_cast<BZR::Ordnance*>(lua_touserdata(L, index));
			if (ord == nullptr ||
				!SignatureResolver::IsReadableRange(ord, sizeof(BZR::Ordnance)) ||
				!IsOrdnanceObjectSeh(ord))
			{
				return nullptr;
			}
			return ord;
		}

		// Plain-data snapshot of one attribute, read under SEH so a round that
		// is freed between the identity check and the read fails closed.
		struct OrdnanceAttributeValue
		{
			char odf[17]{};
			BZR::MAT_3D matrix{};
			BZR::VECTOR_3D vector{};
			float number = 0.0f;
			BZR::GameObject* owner = nullptr;
		};

		bool TryReadOrdnanceAttribute(const BZR::Ordnance* ord, AttributeCode code, OrdnanceAttributeValue& out) noexcept
		{
			__try
			{
				switch (code)
				{
				case ODF:
					std::memcpy(out.odf, ord->ordnanceClass->odf, sizeof(ord->ordnanceClass->odf));
					out.odf[sizeof(out.odf) - 1] = '\0';
					return true;
				case TRANSFORM:
					out.matrix = ord->obj->transform;
					return true;
				case INIT_TRANSFORM:
					out.matrix = ord->initMat;
					return true;
				case OWNER:
					out.owner = ord->owner != nullptr ? ord->owner->owner : nullptr;
					return true;
				case INIT_TIME:
					out.number = ord->initTime;
					return true;
				case VELOCITY:
					out.vector = ord->euler.v;
					return true;
				case LIFE_TIME:
					out.number = ord->lifeTime;
					return true;
				default:
					return false;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// Engine ODF names are char[16] and carry the ".odf" suffix.
		size_t OdfStemLength(const char* odf) noexcept
		{
			size_t length = strnlen(odf, 16);
			if (length >= 4 && _strnicmp(odf + length - 4, ".odf", 4) == 0)
			{
				length -= 4;
			}
			return length;
		}

		// Looked up on every call rather than cached: the class list grows as a
		// mission loads ODFs, so a snapshot taken on the first call missed
		// classes loaded later, and it held class pointers past the mission
		// that owned them.
		BZR::OrdnanceClass* FindOrdnanceClass(const char* name, size_t nameLength) noexcept
		{
			__try
			{
				auto* const* const* vector =
					reinterpret_cast<BZR::OrdnanceClass* const* const*>(BZR::OrdnanceClass::OrdnanceClassList);
				BZR::OrdnanceClass* const* begin = vector[0];
				BZR::OrdnanceClass* const* end = vector[1];
				if (begin == nullptr || end < begin || end - begin > 0x10000)
				{
					return nullptr;
				}

				for (BZR::OrdnanceClass* const* it = begin; it != end; ++it)
				{
					BZR::OrdnanceClass* ordnanceClass = *it;
					if (ordnanceClass == nullptr)
					{
						continue;
					}
					if (OdfStemLength(ordnanceClass->odf) == nameLength &&
						_strnicmp(ordnanceClass->odf, name, nameLength) == 0)
					{
						return ordnanceClass;
					}
				}
				return nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}
	}

	int BuildOrdnance(lua_State* L)
	{
		if (!RuntimeGate::IsSupported())
		{
			return PushUnsupportedBuild(L);
		}

		size_t nameLength = 0;
		const char* requestedOrd = luaL_checklstring(L, 1, &nameLength);
		BZR::Mat3 matrix = CheckMatrix(L, 2);
		BZR::handle ownerHandle = CheckHandle(L, 3);

		BZR::OrdnanceClass* classToBuild = FindOrdnanceClass(requestedOrd, nameLength);
		if (classToBuild == nullptr)
		{
			return luaL_argerror(L, 1, lua_pushfstring(L, "could not find ordnance class for %s", requestedOrd));
		}

		BZR::GameObject* ownerObj = BZR::GameObject::GetObj(ownerHandle);
		if (ownerObj == nullptr)
		{
			return luaL_argerror(L, 3, "owner handle does not refer to a live object");
		}

		BZR::Ordnance* ord = BZR::OrdnanceClass::Build(classToBuild, &matrix, ownerObj->obj);

		lua_pushlightuserdata(L, ord);

		return 1;
	}

	int GetOrdnanceAttribute(lua_State* L)
	{
		if (!RuntimeGate::IsSupported())
		{
			return PushUnsupportedBuild(L);
		}

		BZR::Ordnance* ord = CheckOrdnance(L, 1);
		const lua_Integer rawCode = luaL_checkinteger(L, 2);
		if (rawCode < ODF || rawCode > LIFE_TIME)
		{
			return luaL_argerror(L, 2, "Invalid ordnance attribute code");
		}
		const AttributeCode code = static_cast<AttributeCode>(rawCode);

		// A round that has expired (or was never an Ordnance) reads as nil.
		OrdnanceAttributeValue value{};
		if (ord == nullptr || !TryReadOrdnanceAttribute(ord, code, value))
		{
			lua_pushnil(L);
			return 1;
		}

		switch (code)
		{
		case ODF:
			lua_pushstring(L, value.odf);
			break;
		case TRANSFORM:
		case INIT_TRANSFORM:
			PushMatrix(L, value.matrix);
			break;
		case OWNER:
			if (BZR::GameObject::IsLiveArenaObject(value.owner))
			{
				lua_pushlightuserdata(L, reinterpret_cast<void*>(BZR::GameObject::GetHandle(value.owner)));
			}
			else
			{
				lua_pushnil(L);
			}
			break;
		case INIT_TIME:
		case LIFE_TIME:
			lua_pushnumber(L, value.number);
			break;
		case VELOCITY:
			PushVector(L, value.vector);
			break;
		}

		return 1;
	}

	int GetCoeffBallistic(lua_State* L)
	{
		lua_pushnumber(L, coeffBallistic.Read());
		return 1;
	}

	int SetCoeffBallistic(lua_State* L)
	{
		float newCoeff = static_cast<float>(luaL_checknumber(L, 1));
		coeffBallistic.Write(newCoeff);
		return 0;
	}
}