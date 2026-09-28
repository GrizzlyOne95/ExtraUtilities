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

#include "Util/SignatureResolver.h"

// Raw GameObject memory probes: readable-pointer checks, field reads and
// handle validation (a handle is live only when GetHandle(obj) returns it).

namespace ExtraUtilities::Lua::GameObject
{
	namespace Detail
	{
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
			return SignatureResolver::IsReadableRange(pointer, 1);
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

		bool TryGetHandleFromObject(BZR::GameObject* obj, BZR::handle& outHandle)
		{
			outHandle = 0;
			if (obj == nullptr)
			{
				return false;
			}

			__try
			{
				outHandle = BZR::GameObject::GetHandle(obj);
				return outHandle != 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outHandle = 0;
				return false;
			}
		}
	}
}
