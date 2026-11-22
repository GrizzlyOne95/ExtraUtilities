// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#pragma once

// Pure value checks and conversions for the lightning / lava / glare Lua
// bindings (EnvironmentHazards.cpp). No engine access, so tests/host can
// cover them.

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ExtraUtilities::EnvironmentHazards
{
	// LightningFeature::PostLoad clamps the TRN bolt count to 16 (Ghidra
	// 0x4fa862 "cmp [0x9b7ca4], 0x10"), the size of its emitter array.
	constexpr int kMaxBolts = 16;
	// BoltSet.sound is a char[16] read with GetINIString(..., 0x10).
	constexpr std::size_t kSoundCapacity = 16;
	// lavaMat is compared to the 4-bit terrain material index (cell >> 12).
	constexpr int kMaxTerrainMaterial = 15;
	// Lightning times are stored as float seconds; this keeps the ms input
	// well inside float precision.
	constexpr double kMaxMilliseconds = 1.0e9;
	// SetGlare amount cap; ColorFade draws at most min(glare, 1) per frame.
	constexpr double kMaxGlare = 1000.0;

	// BoltSet.color is 0xAARRGGBB: PostLoad builds palette colours as
	// 0xFF000000 | r << 16 | g << 8 | b (Ghidra 0x4fa734..0x4fa759).
	constexpr uint32_t PackArgb(uint32_t r, uint32_t g, uint32_t b, uint32_t a) noexcept
	{
		return (a & 0xFFu) << 24 | (r & 0xFFu) << 16 | (g & 0xFFu) << 8 | (b & 0xFFu);
	}

	struct Rgba
	{
		int r = 0;
		int g = 0;
		int b = 0;
		int a = 0;
	};

	constexpr Rgba UnpackArgb(uint32_t argb) noexcept
	{
		return Rgba{ static_cast<int>(argb >> 16 & 0xFFu), static_cast<int>(argb >> 8 & 0xFFu),
			static_cast<int>(argb & 0xFFu), static_cast<int>(argb >> 24 & 0xFFu) };
	}

	// One colour channel: an integer 0..255.
	inline bool ChannelFromNumber(double value, uint32_t& out) noexcept
	{
		if (!std::isfinite(value) || value < 0.0 || value > 255.0 || std::floor(value) != value)
		{
			return false;
		}
		out = static_cast<uint32_t>(value);
		return true;
	}

	inline bool MillisecondsToSeconds(double ms, float& outSeconds) noexcept
	{
		if (!std::isfinite(ms) || ms < 0.0 || ms > kMaxMilliseconds)
		{
			return false;
		}
		outSeconds = static_cast<float>(ms * 0.001);
		return true;
	}

	inline double SecondsToMilliseconds(float seconds) noexcept
	{
		// Rounded to whole microseconds so 150 ms reads back as 150, not 150.0000089.
		return std::round(static_cast<double>(seconds) * 1.0e6) / 1000.0;
	}

	inline bool IsNonNegativeFinite(double value) noexcept
	{
		return std::isfinite(value) && value >= 0.0 && value <= 3.0e38;
	}

	// Fits the inline char[16] with its terminator and has no embedded NUL.
	inline bool IsValidSoundName(const char* text, std::size_t length) noexcept
	{
		if (text == nullptr || length >= kSoundCapacity)
		{
			return false;
		}
		for (std::size_t i = 0; i < length; ++i)
		{
			if (text[i] == '\0')
			{
				return false;
			}
		}
		return true;
	}

	// StartLightning's bolt count: the argument when given (1..16, else -1),
	// otherwise the current BoltSet count, or 1 when that is 0 (no TRN bolts).
	inline int ResolveBoltCount(bool hasArgument, long long requested, int current) noexcept
	{
		if (hasArgument)
		{
			return requested >= 1 && requested <= kMaxBolts ? static_cast<int>(requested) : -1;
		}
		if (current >= 1 && current <= kMaxBolts)
		{
			return current;
		}
		return 1;
	}

	inline bool IsValidTerrainMaterial(long long index) noexcept
	{
		return index >= 0 && index <= kMaxTerrainMaterial;
	}
}
