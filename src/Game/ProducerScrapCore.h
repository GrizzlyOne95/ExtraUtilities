// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Engine-independent checks for the Producer + scrap bindings (ProducerScrap.cpp):
// argument validation, scrap clamping and the 8-character ODF decode. The tests
// in tests/host run these without the game.

#include <cmath>
#include <cstdint>

namespace ExtraUtilities::ProducerScrapCore
{
	// Larger values are rejected as script mistakes, not clamped.
	constexpr double kMaxBuildSeconds = 1.0e6;
	constexpr double kMaxScrapCapacity = 1.0e6;
	// A longer AI path is treated as corrupt.
	constexpr std::int32_t kMaxPathPoints = 4096;

	// A build timer the engine can count down: finite and in [0, kMaxBuildSeconds].
	inline bool IsValidBuildSeconds(double seconds) noexcept
	{
		return std::isfinite(seconds) && seconds >= 0.0 && seconds <= kMaxBuildSeconds;
	}

	// A scrap capacity: finite and in [1, kMaxScrapCapacity], truncated toward zero.
	// Zero is refused because Scavenger's ammo gauge divides by it.
	inline bool TryScrapCapacity(double value, std::int32_t& out) noexcept
	{
		if (!std::isfinite(value) || value < 1.0 || value > kMaxScrapCapacity) return false;
		out = static_cast<std::int32_t>(value);
		return true;
	}

	// The held amount clamped to [0, max], as Scavenger load clamps it. A NaN is refused.
	inline bool TryClampScrap(double value, std::int32_t max, std::int32_t& out) noexcept
	{
		if (std::isnan(value) || max < 0) return false;
		if (value <= 0.0) { out = 0; return true; }
		if (value >= static_cast<double>(max)) { out = max; return true; }
		out = static_cast<std::int32_t>(value);
		return true;
	}

	// The 8-character name at GameObjectClass +0x30 (not NUL-terminated when full),
	// masked to 7-bit ASCII like BuildEvents. False for an empty name.
	inline bool DecodeOdf8(const unsigned char* name, char (&out)[9]) noexcept
	{
		int n = 0;
		for (; n < 8; ++n)
		{
			const char c = static_cast<char>(name[n] & 0x7f);
			if (c == 0) break;
			out[n] = c;
		}
		out[n] = 0;
		return n > 0;
	}

	// The index of an AI path's last point, or -1 for a count outside 1..kMaxPathPoints.
	inline std::int32_t PathDestinationIndex(std::int32_t count) noexcept
	{
		return count >= 1 && count <= kMaxPathPoints ? count - 1 : -1;
	}
}
