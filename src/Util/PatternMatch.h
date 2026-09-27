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

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

// Byte-pattern matching shared by SignatureResolver (patch/hook targets) and
// BuildValidation (the runtime build gate). Kept free of Windows so tests/host
// can check the exact matching and fail-closed rules the game relies on.
//
// Two pattern encodings are in use:
//   - int arrays, where a negative entry is a wildcard;
//   - byte + mask arrays, where a zero mask byte is a wildcard.
namespace ExtraUtilities::PatternMatch
{
	// Whether `pattern` matches at the start of `data`.
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

	// Number of offsets in `data` where `pattern` matches, counting at most
	// `stopAfter` (so a uniqueness check can stop at 2).
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

	// First match of an int pattern, or nullptr.
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
			if (PatternMatches(data + offset, dataSize - offset, pattern.data(), N))
			{
				return data + offset;
			}
		}

		return nullptr;
	}

	inline bool MaskedPatternMatches(
		const uint8_t* data,
		const uint8_t* pattern,
		const uint8_t* mask,
		size_t patternSize) noexcept
	{
		for (size_t i = 0; i < patternSize; ++i)
		{
			if (mask[i] != 0 && data[i] != pattern[i])
			{
				return false;
			}
		}
		return true;
	}

	// Address (baseAddress + offset) of the first masked match, or 0.
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
			if (MaskedPatternMatches(data + offset, pattern, mask, patternSize))
			{
				return baseAddress + offset;
			}
		}

		return 0;
	}

	// Address of the only masked match, or 0 when there is none or more than
	// one. An ambiguous signature must never pick one of its matches.
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
			if (!MaskedPatternMatches(data + offset, pattern, mask, patternSize))
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
