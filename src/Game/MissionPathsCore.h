// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#pragma once

// Engine-independent parts of the exu mission-path calls (AllPaths,
// BuildPath, AddPathPoint, RemovePath, RemovePathPoint): name checks, Lua index
// normalisation and the {x,z} array edits. Nothing here touches the game, so
// tests/host runs it against plain buffers. The native reads and the engine
// allocator live in MissionPaths.cpp.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ExtraUtilities::MissionPathsCore
{
	// Longest label BuildPath accepts (the engine stores any length; this is
	// only a sanity cap for script input).
	constexpr std::size_t kMaxNameLength = 255;
	// Most points one path may hold through these calls.
	constexpr std::int32_t kMaxPoints = 4096;
	// A larger native count is treated as a corrupt AiPath.
	constexpr std::int32_t kSaneCount = 65536;
	// Most list nodes AllPaths walks before it gives up (corrupt list guard).
	constexpr std::size_t kMaxListNodes = 65536;

	// Non-empty, at most kMaxNameLength bytes, no embedded NUL (len is the Lua
	// string length, so a NUL inside would make the engine see a shorter name).
	inline bool IsValidPathName(const char* name, std::size_t len) noexcept
	{
		return name != nullptr && len > 0 && len <= kMaxNameLength && std::strlen(name) == len;
	}

	inline bool IsFinitePoint(float x, float z) noexcept
	{
		return std::isfinite(x) && std::isfinite(z);
	}

	// AddPathPoint index: 1-based position the new point takes (1..count+1);
	// no index appends. Writes the 0-based slot. False when out of range or
	// the path would exceed kMaxPoints.
	inline bool InsertSlot(std::int32_t count, bool hasPoint, long long point, std::int32_t& slot) noexcept
	{
		if (count < 0 || count >= kMaxPoints)
		{
			return false;
		}
		if (!hasPoint)
		{
			slot = count;
			return true;
		}
		if (point < 1 || point > static_cast<long long>(count) + 1)
		{
			return false;
		}
		slot = static_cast<std::int32_t>(point - 1);
		return true;
	}

	// RemovePathPoint index: 1-based point to remove (1..count); no index
	// removes the last. Writes the 0-based slot. False when out of range or
	// when the path has only one point left (stock GotoTask reads
	// points[count - 1], so a path never goes empty).
	inline bool RemoveSlot(std::int32_t count, bool hasPoint, long long point, std::int32_t& slot) noexcept
	{
		if (count < 2 || count > kSaneCount)
		{
			return false;
		}
		if (!hasPoint)
		{
			slot = count - 1;
			return true;
		}
		if (point < 1 || point > count)
		{
			return false;
		}
		slot = static_cast<std::int32_t>(point - 1);
		return true;
	}

	// Inserts {x,z} at slot into xz, which holds count pairs and has room for
	// at least count + 1. Pairs from slot on move up by one.
	inline void InsertPair(float* xz, std::int32_t count, std::int32_t slot, float x, float z) noexcept
	{
		const std::size_t tail = static_cast<std::size_t>(count - slot) * 2;
		if (tail != 0)
		{
			std::memmove(xz + (slot + 1) * 2, xz + slot * 2, tail * sizeof(float));
		}
		xz[slot * 2] = x;
		xz[slot * 2 + 1] = z;
	}

	// Removes the pair at slot from xz (count pairs). Pairs after it move down;
	// the last pair's storage is left as it was (the block is not shrunk).
	inline void RemovePair(float* xz, std::int32_t count, std::int32_t slot) noexcept
	{
		const std::size_t tail = static_cast<std::size_t>(count - slot - 1) * 2;
		if (tail != 0)
		{
			std::memmove(xz + slot * 2, xz + (slot + 1) * 2, tail * sizeof(float));
		}
	}
}
