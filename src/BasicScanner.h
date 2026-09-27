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

// Non-template properties and helpers for the scanner class

#pragma once

#include <cstdint>
#include <vector>

#include <Windows.h>

#undef ABSOLUTE

namespace ExtraUtilities
{
	class BasicScanner
	{
	protected:
		static inline uintptr_t m_bzrModuleBase = 0;
		static inline uintptr_t m_ogreMainModuleBase = 0;

		static uintptr_t RefreshModuleBase(const char* moduleName, uintptr_t& cache) noexcept
		{
			cache = reinterpret_cast<uintptr_t>(GetModuleHandleA(moduleName));
			return cache;
		}

		// Every scanner, so closing the Lua state can put back what scripts
		// wrote without waiting for the DLL to unload (it may stay loaded).
		// Constant-initialized, so it outlives every scanner.
		static inline std::vector<BasicScanner*> registry{};

		BasicScanner() = default;
		virtual ~BasicScanner() = default;

		void Register() noexcept
		{
			try
			{
				registry.push_back(this);
			}
			catch (...)
			{
				// Restore then only happens at DLL unload, as before.
			}
		}

		void Unregister() noexcept
		{
			for (size_t i = 0; i < registry.size(); ++i)
			{
				if (registry[i] == this)
				{
					registry.erase(registry.begin() + static_cast<std::ptrdiff_t>(i));
					return;
				}
			}
		}

	public:
		virtual void RestoreIfWritten() noexcept {}

		// Called at Lua-state close.
		static void RestoreAllWritten() noexcept
		{
			for (BasicScanner* scanner : registry)
			{
				if (scanner != nullptr)
				{
					scanner->RestoreIfWritten();
				}
			}
		}

		// Should the scanner restore the original data when the dll exits?
		enum class Restore : uint8_t
		{
			ENABLED,
			DISABLED
		};

		// What base address to use when calculating memory addresses
		enum class BaseAddress : uint8_t
		{
			ABSOLUTE, // use absolute address, for most cases in BZR
			BZR, // use BZR module base, not necessary due to lack of ASLR
			OGRE // use ogre module base, necessary for ogre functions since the dll is loaded in a random location
		};

		// Calculates an address from module offset for use in a hook as opposed to accessing data
		static uintptr_t CalculateAddress(uintptr_t offset, BaseAddress baseAddress)
		{
			using enum BaseAddress;
			switch (baseAddress)
			{
			case ABSOLUTE:
				return offset;
			case BZR:
			{
				auto base = RefreshModuleBase("Battlezone98Redux.exe", m_bzrModuleBase);
				return base != 0 ? base + offset : 0;
			}
			case OGRE:
			{
				auto base = RefreshModuleBase("OgreMain.dll", m_ogreMainModuleBase);
				return base != 0 ? base + offset : 0;
			}
			default:
				return 0;
			}
		}
	};
}
