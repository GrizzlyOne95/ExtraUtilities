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

#include "Util/SignatureResolver.h"

#include <Windows.h>

#include <array>
#include <cstdint>

namespace ExtraUtilities::BuildValidation
{
	// BZR 2.2.301 anchor from exu.json: Camera::Set_View at VA 0x0061D120.
	// Resolve from the module base so this check remains valid even if the image
	// is ever relocated. The E8 rel32 operand is intentionally wildcarded.
	inline bool IsSupportedBzr2301() noexcept
	{
		HMODULE module = GetModuleHandleA(nullptr);
		if (module == nullptr)
		{
			return false;
		}

		constexpr uintptr_t kDefaultImageBase = 0x00400000u;
		constexpr uintptr_t kSetViewAddress = 0x0061D120u;
		constexpr uintptr_t kSetViewRva = kSetViewAddress - kDefaultImageBase;
		constexpr std::array<int, 21> kSetViewSignature = {
			0x55, 0x8B, 0xEC,
			0x8B, 0x45, 0x08,
			0x50,
			0x8B, 0x4D, 0x0C,
			0x51,
			0xE8, -1, -1, -1, -1,
			0x83, 0xC4, 0x08,
			0x5D, 0xC3,
		};

		const auto* candidate = reinterpret_cast<const uint8_t*>(
			reinterpret_cast<uintptr_t>(module) + kSetViewRva);
		if (!SignatureResolver::IsReadableRange(candidate, kSetViewSignature.size()))
		{
			return false;
		}

		return SignatureResolver::FindPattern(
			candidate,
			kSetViewSignature.size(),
			kSetViewSignature) == candidate;
	}
}
