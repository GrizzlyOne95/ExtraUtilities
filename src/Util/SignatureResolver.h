/* Copyright (C) 2023-2026 VTrider
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace ExtraUtilities::SignatureResolver
{
	struct SectionView
	{
		const uint8_t* address = nullptr;
		size_t size = 0;
		uintptr_t virtualAddress = 0;
		std::string name;
	};

	inline bool IsReadableProtection(DWORD protection) noexcept
	{
		if ((protection & PAGE_GUARD) != 0 || (protection & PAGE_NOACCESS) != 0)
		{
			return false;
		}

		const DWORD baseProtection = protection & 0xFFu;
		switch (baseProtection)
		{
		case PAGE_READONLY:
		case PAGE_READWRITE:
		case PAGE_WRITECOPY:
		case PAGE_EXECUTE:
		case PAGE_EXECUTE_READ:
		case PAGE_EXECUTE_READWRITE:
		case PAGE_EXECUTE_WRITECOPY:
			return true;
		default:
			return false;
		}
	}

	// Validate an entire address interval, even when it spans multiple adjacent
	// VirtualQuery regions. This matters for PE sections, which are not required
	// to have one protection descriptor covering their full VirtualSize.
	inline bool IsReadableRange(const void* address, size_t length) noexcept
	{
		if (address == nullptr || length == 0)
		{
			return false;
		}

		const uintptr_t start = reinterpret_cast<uintptr_t>(address);
		if (length - 1 > (std::numeric_limits<uintptr_t>::max)() - start)
		{
			return false;
		}
		const uintptr_t end = start + length;

		uintptr_t cursor = start;
		while (cursor < end)
		{
			MEMORY_BASIC_INFORMATION mbi{};
			if (VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)) == 0)
			{
				return false;
			}

			if (mbi.State != MEM_COMMIT || !IsReadableProtection(mbi.Protect))
			{
				return false;
			}

			const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
			if (cursor < regionBase || mbi.RegionSize == 0 ||
				mbi.RegionSize > (std::numeric_limits<uintptr_t>::max)() - regionBase)
			{
				return false;
			}

			const uintptr_t regionEnd = regionBase + mbi.RegionSize;
			if (regionEnd <= cursor)
			{
				return false;
			}

			cursor = regionEnd < end ? regionEnd : end;
		}

		return true;
	}

	inline bool MatchBytes(uintptr_t address, const uint8_t* expected, size_t length) noexcept
	{
		if (expected == nullptr || !IsReadableRange(reinterpret_cast<const void*>(address), length))
		{
			return false;
		}

		// Retain the old Overlay scanner's SEH boundary in addition to the range
		// validation. A target page can theoretically change between VirtualQuery
		// and the comparison in a live process; fail closed instead of crashing.
		__try
		{
			return std::memcmp(reinterpret_cast<const void*>(address), expected, length) == 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	inline bool MatchBytes(uintptr_t address, const std::vector<uint8_t>& expected) noexcept
	{
		return !expected.empty() && MatchBytes(address, expected.data(), expected.size());
	}

	template <size_t N>
	inline bool MatchBytes(uintptr_t address, const std::array<uint8_t, N>& expected) noexcept
	{
		return MatchBytes(address, expected.data(), N);
	}

	inline bool TryGetModuleSection(
		HMODULE module,
		const char* sectionName,
		const uint8_t*& outData,
		size_t& outSize,
		uintptr_t& outAddress) noexcept
	{
		outData = nullptr;
		outSize = 0;
		outAddress = 0;

		if (module == nullptr || sectionName == nullptr)
		{
			return false;
		}

		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
		if (!IsReadableRange(dos, sizeof(IMAGE_DOS_HEADER)) || dos->e_magic != IMAGE_DOS_SIGNATURE)
		{
			return false;
		}

		if (dos->e_lfanew <= 0)
		{
			return false;
		}

		const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(module);
		const uintptr_t ntOffset = static_cast<uintptr_t>(dos->e_lfanew);
		if (ntOffset > (std::numeric_limits<uintptr_t>::max)() - moduleBase)
		{
			return false;
		}

		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(moduleBase + ntOffset);
		if (!IsReadableRange(nt, sizeof(IMAGE_NT_HEADERS)) || nt->Signature != IMAGE_NT_SIGNATURE)
		{
			return false;
		}

		const auto* section = IMAGE_FIRST_SECTION(nt);
		if (!IsReadableRange(section, sizeof(IMAGE_SECTION_HEADER) * nt->FileHeader.NumberOfSections))
		{
			return false;
		}

		for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
		{
			char name[IMAGE_SIZEOF_SHORT_NAME + 1]{};
			std::memcpy(name, section->Name, IMAGE_SIZEOF_SHORT_NAME);
			if (std::strcmp(name, sectionName) != 0)
			{
				continue;
			}

			const size_t size = section->Misc.VirtualSize != 0
				? static_cast<size_t>(section->Misc.VirtualSize)
				: static_cast<size_t>(section->SizeOfRawData);
			if (size == 0 || section->VirtualAddress > (std::numeric_limits<uintptr_t>::max)() - moduleBase)
			{
				return false;
			}

			const auto* data = reinterpret_cast<const uint8_t*>(moduleBase + section->VirtualAddress);
			if (!IsReadableRange(data, size))
			{
				return false;
			}

			outData = data;
			outSize = size;
			outAddress = reinterpret_cast<uintptr_t>(data);
			return true;
		}

		return false;
	}

	inline bool TryGetModuleTextSection(
		HMODULE module,
		const uint8_t*& outData,
		size_t& outSize,
		uintptr_t& outAddress) noexcept
	{
		return TryGetModuleSection(module, ".text", outData, outSize, outAddress);
	}

	inline std::vector<SectionView> GetExecutableSections(HMODULE module)
	{
		std::vector<SectionView> sections;
		if (module == nullptr)
		{
			return sections;
		}

		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
		if (!IsReadableRange(dos, sizeof(IMAGE_DOS_HEADER)) || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
		{
			return sections;
		}

		const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(module);
		const uintptr_t ntOffset = static_cast<uintptr_t>(dos->e_lfanew);
		if (ntOffset > (std::numeric_limits<uintptr_t>::max)() - moduleBase)
		{
			return sections;
		}

		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(moduleBase + ntOffset);
		if (!IsReadableRange(nt, sizeof(IMAGE_NT_HEADERS)) || nt->Signature != IMAGE_NT_SIGNATURE)
		{
			return sections;
		}

		const auto* section = IMAGE_FIRST_SECTION(nt);
		if (!IsReadableRange(section, sizeof(IMAGE_SECTION_HEADER) * nt->FileHeader.NumberOfSections))
		{
			return sections;
		}

		for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
		{
			if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
			{
				continue;
			}

			const size_t size = section->Misc.VirtualSize != 0
				? static_cast<size_t>(section->Misc.VirtualSize)
				: static_cast<size_t>(section->SizeOfRawData);
			if (size == 0 || section->VirtualAddress > (std::numeric_limits<uintptr_t>::max)() - moduleBase)
			{
				continue;
			}

			const auto* address = reinterpret_cast<const uint8_t*>(moduleBase + section->VirtualAddress);
			if (!IsReadableRange(address, size))
			{
				continue;
			}

			char name[IMAGE_SIZEOF_SHORT_NAME + 1]{};
			std::memcpy(name, section->Name, IMAGE_SIZEOF_SHORT_NAME);
			sections.push_back({ address, size, reinterpret_cast<uintptr_t>(address), name });
		}

		return sections;
	}

	inline uintptr_t FindMaskedPattern(
		const uint8_t* data,
		size_t dataSize,
		uintptr_t baseAddress,
		const uint8_t* pattern,
		const uint8_t* mask,
		size_t patternSize) noexcept
	{
		if (data == nullptr || pattern == nullptr || mask == nullptr || patternSize == 0 || dataSize < patternSize)
		{
			return 0;
		}

		for (size_t offset = 0; offset <= dataSize - patternSize; ++offset)
		{
			bool matched = true;
			for (size_t i = 0; i < patternSize; ++i)
			{
				if (mask[i] != 0 && data[offset + i] != pattern[i])
				{
					matched = false;
					break;
				}
			}

			if (matched)
			{
				return baseAddress + offset;
			}
		}

		return 0;
	}

	template <size_t N>
	inline const uint8_t* FindPattern(
		const uint8_t* data,
		size_t dataSize,
		const std::array<int, N>& pattern) noexcept
	{
		if (data == nullptr || dataSize < N)
		{
			return nullptr;
		}

		for (size_t offset = 0; offset <= dataSize - N; ++offset)
		{
			bool matched = true;
			for (size_t i = 0; i < N; ++i)
			{
				const int expected = pattern[i];
				if (expected >= 0 && data[offset + i] != static_cast<uint8_t>(expected))
				{
					matched = false;
					break;
				}
			}

			if (matched)
			{
				return data + offset;
			}
		}

		return nullptr;
	}

	inline uintptr_t FindUniqueMaskedPattern(
		const uint8_t* data,
		size_t dataSize,
		uintptr_t baseAddress,
		const uint8_t* pattern,
		const uint8_t* mask,
		size_t patternSize) noexcept
	{
		if (data == nullptr || pattern == nullptr || mask == nullptr || patternSize == 0 || dataSize < patternSize)
		{
			return 0;
		}

		uintptr_t matchAddress = 0;
		for (size_t offset = 0; offset <= dataSize - patternSize; ++offset)
		{
			bool matched = true;
			for (size_t i = 0; i < patternSize; ++i)
			{
				if (mask[i] != 0 && data[offset + i] != pattern[i])
				{
					matched = false;
					break;
				}
			}

			if (!matched)
			{
				continue;
			}

			if (matchAddress != 0)
			{
				return 0; // Ambiguous signature; fail closed.
			}

			matchAddress = baseAddress + offset;
		}

		return matchAddress;
	}
}
