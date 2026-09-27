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
#include "Util/RuntimeGate.h"
#include "Util/SignatureResolver.h"

#include <Windows.h>

#include <cstdint>
#include <initializer_list>
#include <limits>

#undef ABSOLUTE // win32 name collision

namespace ExtraUtilities
{
	// Reads and writes one value at a fixed engine address (optionally at the
	// end of a pointer chain resolved once, at construction).
	//
	// Restore::ENABLED means: when the DLL unloads, put back the value that was
	// there before EXU's first Write. A scanner that was never written leaves
	// the game alone, so read-only scanners never write, and a value the
	// player changed in the game is not reverted unless a script changed it.
	template <class T>
	class Scanner : public BasicScanner
	{
	private:
		BaseAddress m_baseAddress;
		T* m_address = nullptr;
		Restore m_restoreData;
		T m_originalData{};
		bool m_written = false;

		T* ResolveBase(T* offset) const noexcept
		{
			const auto resolved = CalculateAddress(reinterpret_cast<uintptr_t>(offset), m_baseAddress);
			return reinterpret_cast<T*>(resolved);
		}

		void PrepareFinalAddress(T* finalAddress) noexcept
		{
			// No protection change here: these are data pages, and changing
			// them at load (inside DllMain, for every scanner) made heap and
			// .data pages executable for the life of the DLL.
			m_address = finalAddress;
			if (m_address != nullptr && !SignatureResolver::IsReadableRange(m_address, sizeof(T)))
			{
				m_address = nullptr;
			}
		}

		static bool IsWritableProtection(DWORD protection) noexcept
		{
			switch (protection & 0xFFu)
			{
			case PAGE_READWRITE:
			case PAGE_WRITECOPY:
			case PAGE_EXECUTE_READWRITE:
			case PAGE_EXECUTE_WRITECOPY:
				return (protection & PAGE_GUARD) == 0;
			default:
				return false;
			}
		}

		// Stores value, unprotecting the page only for the duration of the
		// store when it is not already writable.
		bool Store(const T& value) noexcept
		{
			MEMORY_BASIC_INFORMATION mbi{};
			if (VirtualQuery(m_address, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT)
			{
				return false;
			}

			if (IsWritableProtection(mbi.Protect))
			{
				*m_address = value;
				return true;
			}

			DWORD oldProtect = 0;
			if (!VirtualProtect(m_address, sizeof(T), PAGE_READWRITE, &oldProtect))
			{
				return false;
			}
			*m_address = value;
			DWORD ignored = 0;
			VirtualProtect(m_address, sizeof(T), oldProtect, &ignored);
			return true;
		}

	public:
		Scanner(
			T* address,
			Restore restoreData = Restore::ENABLED,
			BaseAddress baseAddress = BaseAddress::ABSOLUTE)
			: m_baseAddress(baseAddress), m_restoreData(restoreData)
		{
			PrepareFinalAddress(ResolveBase(address));
			Register();
		}

		// Traverse a multi-level pointer chain.
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
			Register();
		}

		Scanner(const Scanner&) = delete;
		Scanner& operator=(const Scanner&) = delete;
		Scanner(Scanner&&) = delete;
		Scanner& operator=(Scanner&&) = delete;

		~Scanner()
		{
			RestoreIfWritten();
			Unregister();
		}

		// Puts back the value that preceded EXU's first Write, if this scanner
		// restores and was written. Safe to call more than once.
		void RestoreIfWritten() noexcept override
		{
			if (!m_written || m_restoreData != Restore::ENABLED ||
				m_address == nullptr ||
				!SignatureResolver::IsReadableRange(m_address, sizeof(T)))
			{
				return;
			}

			Store(m_originalData);
			m_written = false;
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

		// Writes only on the qualified executable: on any other build the
		// fixed address belongs to something else.
		void Write(T value) noexcept
		{
			if (!RuntimeGate::IsSupported() ||
				m_address == nullptr ||
				!SignatureResolver::IsReadableRange(m_address, sizeof(T)))
			{
				return;
			}

			const T previous = *m_address;
			if (Store(value) && !m_written)
			{
				m_originalData = previous;
				m_written = true;
			}
		}

		T* Get() const noexcept
		{
			return m_address;
		}
	};
}
