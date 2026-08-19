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

#include "OpenShimBridge.h"
#include "Util/BzrBuildProfile.generated.h"
#include "Util/SignatureResolver.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace ExtraUtilities::BuildValidation
{
	using BzrDistribution = OpenShimBridge::BzrDistribution;

	namespace Detail
	{
		inline bool PatternMatches(
			const uint8_t* data,
			size_t dataSize,
			const int* pattern,
			size_t patternSize) noexcept
		{
			if (data == nullptr || pattern == nullptr || patternSize == 0 || dataSize < patternSize)
			{
				return false;
			}

			for (size_t index = 0; index < patternSize; ++index)
			{
				const int expected = pattern[index];
				if (expected >= 0 && data[index] != static_cast<uint8_t>(expected))
				{
					return false;
				}
			}

			return true;
		}

		inline size_t CountPatternMatches(
			const uint8_t* data,
			size_t dataSize,
			const int* pattern,
			size_t patternSize,
			size_t stopAfter = (std::numeric_limits<size_t>::max)()) noexcept
		{
			if (data == nullptr || pattern == nullptr || patternSize == 0 || dataSize < patternSize)
			{
				return 0;
			}

			size_t matches = 0;
			for (size_t offset = 0; offset <= dataSize - patternSize; ++offset)
			{
				if (!PatternMatches(data + offset, dataSize - offset, pattern, patternSize))
				{
					continue;
				}

				++matches;
				if (matches >= stopAfter)
				{
					return matches;
				}
			}

			return matches;
		}

		inline size_t CountTextMatches(
			HMODULE module,
			const BzrBuildProfile::AnchorSpec& anchor,
			size_t stopAfter) noexcept
		{
			const uint8_t* text = nullptr;
			size_t textSize = 0;
			uintptr_t textAddress = 0;
			if (!SignatureResolver::TryGetModuleTextSection(module, text, textSize, textAddress))
			{
				return 0;
			}

			return CountPatternMatches(text, textSize, anchor.pattern, anchor.patternSize, stopAfter);
		}

		inline bool ValidatePeIdentity(HMODULE module) noexcept
		{
			if (module == nullptr)
			{
				return false;
			}

			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
			if (!SignatureResolver::IsReadableRange(dos, sizeof(*dos)) ||
				dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
			{
				return false;
			}

			const uintptr_t base = reinterpret_cast<uintptr_t>(module);
			const uintptr_t ntOffset = static_cast<uintptr_t>(dos->e_lfanew);
			if (ntOffset > (std::numeric_limits<uintptr_t>::max)() - base)
			{
				return false;
			}

			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + ntOffset);
			if (!SignatureResolver::IsReadableRange(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE)
			{
				return false;
			}

			return nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
				nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
				static_cast<uintptr_t>(nt->OptionalHeader.ImageBase) == BzrBuildProfile::kImageBase;
		}

		inline bool HasSteamStubBindSection(HMODULE module) noexcept
		{
			if (!ValidatePeIdentity(module))
			{
				return false;
			}

			const auto* base = reinterpret_cast<const uint8_t*>(module);
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
			const WORD sectionCount = nt->FileHeader.NumberOfSections;
			if (sectionCount == 0 || sectionCount > 96)
			{
				return false;
			}

			const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
			const size_t sectionBytes = static_cast<size_t>(sectionCount) * sizeof(IMAGE_SECTION_HEADER);
			if (!SignatureResolver::IsReadableRange(sections, sectionBytes))
			{
				return false;
			}

			static constexpr uint8_t kBindName[IMAGE_SIZEOF_SHORT_NAME] =
				{ '.', 'b', 'i', 'n', 'd', 0, 0, 0 };
			for (WORD index = 0; index < sectionCount; ++index)
			{
				if (std::memcmp(sections[index].Name, kBindName, IMAGE_SIZEOF_SHORT_NAME) == 0)
				{
					return true;
				}
			}

			return false;
		}

		inline bool MatchAnchor(HMODULE module, const BzrBuildProfile::AnchorSpec& anchor) noexcept
		{
			using BzrBuildProfile::AnchorMatchMode;

			switch (anchor.mode)
			{
			case AnchorMatchMode::ExpectedVa:
			{
				if (anchor.expectedVa < BzrBuildProfile::kImageBase)
				{
					return false;
				}

				const uintptr_t rva = anchor.expectedVa - BzrBuildProfile::kImageBase;
				const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(module);
				if (rva > (std::numeric_limits<uintptr_t>::max)() - moduleBase)
				{
					return false;
				}

				const auto* candidate = reinterpret_cast<const uint8_t*>(moduleBase + rva);
				if (!SignatureResolver::IsReadableRange(candidate, anchor.patternSize))
				{
					return false;
				}

				return PatternMatches(candidate, anchor.patternSize, anchor.pattern, anchor.patternSize);
			}
			case AnchorMatchMode::ExecutableContains:
				return CountTextMatches(module, anchor, 1) >= 1;
			case AnchorMatchMode::UniqueExecutable:
				return CountTextMatches(module, anchor, 2) == 1;
			default:
				return false;
			}
		}
	}

	// Fail closed unless all required runtime anchors for the supported 2.2.301
	// build profile still match. The profile is generated from the same JSON used
	// by the offline qualification tool so build identity cannot silently drift.
	inline bool IsSupportedBzr2301() noexcept
	{
		HMODULE module = GetModuleHandleA(nullptr);
		if (!Detail::ValidatePeIdentity(module))
		{
			return false;
		}

		for (const BzrBuildProfile::AnchorSpec& anchor : BzrBuildProfile::kRuntimeAnchors)
		{
			if (anchor.required && !Detail::MatchAnchor(module, anchor))
			{
				return false;
			}
		}

		return true;
	}

	// Prefer OpenShim's qualified result when available. Older/no-OpenShim
	// installations fall back to EXU's own full supported-build qualification
	// before the SteamStub .bind section is considered. This prevents arbitrary
	// non-Steam PE files from being mislabeled as GOG.
	inline BzrDistribution GetBzrDistribution() noexcept
	{
		const BzrDistribution shimDistribution = OpenShimBridge::GetBzrDistribution();
		if (shimDistribution != BzrDistribution::Unknown)
		{
			return shimDistribution;
		}

		if (!IsSupportedBzr2301())
		{
			return BzrDistribution::Unknown;
		}

		return Detail::HasSteamStubBindSection(GetModuleHandleA(nullptr))
			? BzrDistribution::Steam
			: BzrDistribution::GOG;
	}

	inline bool IsSteamBuild() noexcept
	{
		return GetBzrDistribution() == BzrDistribution::Steam;
	}

	inline bool IsGogBuild() noexcept
	{
		return GetBzrDistribution() == BzrDistribution::GOG;
	}
}
