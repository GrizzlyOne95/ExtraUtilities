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

/*
* Memory Scanner class with automatic
* protection and restoration of the original
* data
*/

#pragma once

#include "BasicScanner.h"
#include "Util/SignatureResolver.h"

#include <Windows.h>

#include <cstdint>
#include <initializer_list>
#include <limits>

#undef ABSOLUTE // win32 name collision

namespace ExtraUtilities
{
	template <class T>
	class Scanner : public BasicScanner
	{
	private:
		BaseAddress m_baseAddress;
		T* m_address = nullptr;
		Restore m_restoreData;
		T m_originalData{};

		DWORD m_oldProtect{};
		bool m_protectionChanged = false;
		static inline DWORD m_dummyProtect{};

		T* ResolveBase(T* offset) const noexcept
		{
			const auto resolved = CalculateAddress(reinterpret_cast<uintptr_t>(offset), m_baseAddress);
			return reinterpret_cast<T*>(resolved);
		}

		bool PrepareFinalAddress(T* finalAddress) noexcept
		{
			m_address = finalAddress;
			if (m_address == nullptr ||
				!SignatureResolver::IsReadableRange(m_address, sizeof(T)))
			{
				m_address = nullptr;
				return false;
			}

			if (!VirtualProtect(m_address, sizeof(T), PAGE_EXECUTE_READWRITE, &m_oldProtect))
			{
				m_address = nullptr;
				return false;
			}

			m_protectionChanged = true;
			m_originalData = *m_address;
			return true;
		}

		void RestoreProtection() noexcept
		{
			if (!m_protectionChanged || m_address == nullptr)
			{
				return;
			}

			VirtualProtect(m_address, sizeof(T), m_oldProtect, &m_dummyProtect);
			m_protectionChanged = false;
		}

	public:
		Scanner(
			T* address,
			Restore restoreData = Restore::ENABLED,
			BaseAddress baseAddress = BaseAddress::ABSOLUTE)
			: m_baseAddress(baseAddress), m_restoreData(restoreData)
		{
			PrepareFinalAddress(ResolveBase(address));
		}

		// Traverse a multi-level pointer chain. Protection is changed only after
		// the final pointee has been resolved, so m_oldProtect always belongs to
		// the same page that the destructor later restores.
		Scanner(
			T* address,
			const std::initializer_list<uint8_t>& offsetsList,
			Restore restoreData = Restore::ENABLED,
			BaseAddress baseAddress = BaseAddress::ABSOLUTE)
			: m_baseAddress(baseAddress), m_restoreData(restoreData)
		{
			uintptr_t resolvedAddress = reinterpret_cast<uintptr_t>(ResolveBase(address));
			for (const uint8_t offset : offsetsList)
			{
				if (resolvedAddress == 0 ||
					!SignatureResolver::IsReadableRange(
						reinterpret_cast<const void*>(resolvedAddress),
						sizeof(uintptr_t)))
				{
					resolvedAddress = 0;
					break;
				}

				const uintptr_t next = *reinterpret_cast<const uintptr_t*>(resolvedAddress);
				if (next == 0 || next > (std::numeric_limits<uintptr_t>::max)() - offset)
				{
					resolvedAddress = 0;
					break;
				}

				resolvedAddress = next + offset;
			}

			PrepareFinalAddress(reinterpret_cast<T*>(resolvedAddress));
		}

		Scanner(const Scanner&) = delete;
		Scanner& operator=(const Scanner&) = delete;
		Scanner(Scanner&&) = delete;
		Scanner& operator=(Scanner&&) = delete;

		~Scanner()
		{
			if (m_address != nullptr &&
				m_restoreData == Restore::ENABLED &&
				SignatureResolver::IsReadableRange(m_address, sizeof(T)))
			{
				*m_address = m_originalData;
			}

			RestoreProtection();
		}

		T Read() const noexcept
		{
			if (m_address == nullptr ||
				!SignatureResolver::IsReadableRange(m_address, sizeof(T)))
			{
				return {};
			}

			return *m_address;
		}

		void Write(T value) noexcept
		{
			if (m_address == nullptr ||
				!SignatureResolver::IsReadableRange(m_address, sizeof(T)))
			{
				return;
			}

			*m_address = value;
		}

		T* Get() const noexcept
		{
			return m_address;
		}
	};
}
