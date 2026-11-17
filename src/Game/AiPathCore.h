// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#pragma once

// Engine-independent parts of exu.GetAiPath / exu.GetAiState: state-name
// lookup and the path snapshot copy. Nothing here touches the game, so the
// tests in tests/host run it against fake buffers. The native reads (RTTI
// gates, SEH) live in AiInspection.cpp.

#include "Game/VehicleControlPolicy.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ExtraUtilities::AiPath
{
	// A plan longer than this is cut and reported as truncated.
	constexpr std::size_t kMaxPoints = 512;
	// A larger count is treated as a corrupt AiPath, not a long route.
	constexpr std::int32_t kSaneCount = 65536;

	// UnitProcess::State 0-17 only. Subclass processes (Scavenger, Rig, ...)
	// have their own enums, so callers gate on the exact class name first.
	inline const char* UnitProcessStateName(int state) noexcept
	{
		return state >= 0 && state < 18 ? VehicleControl::ProcessStateName(state) : nullptr;
	}

	// UnitTask::UnitTaskState 0-13 (GOTO = 2, STUCK = 3, FOLLOW = 4). Not the
	// stock stateText table, which labels a GOTO task "DONE".
	inline const char* UnitTaskStateName(int state) noexcept
	{
		return state >= 0 && state < 14 ? VehicleControl::TaskStateName(state) : nullptr;
	}

	struct PathSnapshot
	{
		std::int32_t count = 0;    // points in the native plan
		std::int32_t copied = 0;   // points in xz (<= kMaxPoints)
		std::uint32_t planPoint = 0; // native 0-based current point
		bool truncated = false;
		float xz[kMaxPoints * 2] = {};
	};

	// The one validity rule for a native AiPath: a points array and a plausible
	// count. GetAiPath and GetAiState's planIndex both use it, so a plan that
	// GetAiPath reports as nil never yields a planIndex.
	inline bool PlanLooksValid(std::int32_t count, const void* points) noexcept
	{
		return points != nullptr && count > 0 && count <= kSaneCount;
	}

	// Copies the {x, z} float pairs of an AiPath (count at +4, array at +8).
	// Plain memory reads: the caller wraps this in SEH. Fails on a null array,
	// an empty or implausible count, or a non-finite coordinate.
	inline bool CopyPath(std::int32_t count, const float* points, std::uint32_t planPoint, PathSnapshot& out) noexcept
	{
		out = PathSnapshot{};
		if (!PlanLooksValid(count, points)) return false;
		const std::int32_t take = count > static_cast<std::int32_t>(kMaxPoints) ? static_cast<std::int32_t>(kMaxPoints) : count;
		std::memcpy(out.xz, points, static_cast<std::size_t>(take) * 2 * sizeof(float));
		for (std::int32_t i = 0; i < take * 2; ++i)
			if (!std::isfinite(out.xz[i])) { out = PathSnapshot{}; return false; }
		out.count = count;
		out.copied = take;
		out.planPoint = planPoint;
		out.truncated = count > take;
		return true;
	}

	// 1-based Lua index of the current plan point, or 0 when the native index
	// is outside the plan.
	inline int LuaPlanIndex(std::uint32_t planPoint, std::int32_t count) noexcept
	{
		return count > 0 && planPoint < static_cast<std::uint32_t>(count) ? static_cast<int>(planPoint) + 1 : 0;
	}

	// Lua `index` for a snapshot: absent (0) when the native index is outside
	// the plan or past the points kept after truncation.
	inline int SnapshotLuaIndex(const PathSnapshot& s) noexcept
	{
		const int index = LuaPlanIndex(s.planPoint, s.count);
		return index > 0 && index <= s.copied ? index : 0;
	}

	// ---- Terrain pathing grid (exu.GetPathCell / GetPathRegion / IsPathReachable) ----

	// Native bounds ints; max values are exclusive.
	struct PathGridBounds
	{
		std::int32_t minX = 0;
		std::int32_t maxX = 0;
		std::int32_t minZ = 0;
		std::int32_t maxZ = 0;
	};

	// floor(v * scale) as an int, or false for a non-finite or absurdly large value.
	inline bool ScaledFloor(float v, float scale, std::int32_t& out) noexcept
	{
		const double r = std::floor(static_cast<double>(v) * static_cast<double>(scale));
		if (!std::isfinite(r) || r < -1.0e9 || r > 1.0e9) return false;
		out = static_cast<std::int32_t>(r);
		return true;
	}

	// Byte index of cell (cx, cz) in the row-major grid, or false when outside
	// [min, max) on either axis (or the bounds are empty).
	inline bool PathCellIndex(const PathGridBounds& b, std::int32_t cx, std::int32_t cz, std::size_t& out) noexcept
	{
		if (b.maxX <= b.minX || b.maxZ <= b.minZ) return false;
		if (cx < b.minX || cx >= b.maxX || cz < b.minZ || cz >= b.maxZ) return false;
		const std::int64_t width = static_cast<std::int64_t>(b.maxX) - b.minX;
		out = static_cast<std::size_t>((static_cast<std::int64_t>(cz) - b.minZ) * width + (cx - b.minX));
		return true;
	}

	// World x/z (native, no flip) to a grid byte index. gridScale is cells per metre.
	inline bool WorldToPathIndex(const PathGridBounds& b, float gridScale, float x, float z, std::size_t& out) noexcept
	{
		if (!(gridScale > 0.0f)) return false;
		std::int32_t cx = 0, cz = 0;
		return ScaledFloor(x, gridScale, cx) && ScaledFloor(z, gridScale, cz) && PathCellIndex(b, cx, cz, out);
	}

	// Two points are reachable when both regions are nonzero and equal.
	inline bool RegionsReachable(std::uint8_t a, std::uint8_t b) noexcept
	{
		return a != 0 && a == b;
	}

	// FindPath plan type byte (+0x10): 3 is the bad/fallback path.
	constexpr std::uint8_t kBadPathType = 3;
	inline bool PlanTypeOk(std::uint8_t type) noexcept { return type != kBadPathType; }

	// ---- AI strategic grid (exu.GetAiGridCell) ----

	constexpr float kAiCellSize = 80.0f;
	constexpr std::int32_t kMaxTeams = 16;

	struct AiGridCoord
	{
		std::int32_t row = 0;
		std::int32_t col = 0;
		float minX = 0.0f;
		float minZ = 0.0f;
		float maxX = 0.0f;
		float maxZ = 0.0f;
	};

	// Stock DrawPriorities: col = floor((x - ox) / 80), row = floor((z - oz) / 80).
	// Fails outside [0, rows) x [0, cols).
	inline bool WorldToAiCell(float x, float z, float ox, float oz, std::int32_t rows, std::int32_t cols, AiGridCoord& out) noexcept
	{
		std::int32_t col = 0, row = 0;
		if (!ScaledFloor(x - ox, 1.0f / kAiCellSize, col) || !ScaledFloor(z - oz, 1.0f / kAiCellSize, row)) return false;
		if (rows <= 0 || cols <= 0 || row < 0 || row >= rows || col < 0 || col >= cols) return false;
		out.row = row;
		out.col = col;
		out.minX = static_cast<float>(col) * kAiCellSize + ox;
		out.minZ = static_cast<float>(row) * kAiCellSize + oz;
		out.maxX = out.minX + kAiCellSize;
		out.maxZ = out.minZ + kAiCellSize;
		return true;
	}

	// priority <= -100000 marks an unused cell.
	inline bool AiCellUsed(float priority) noexcept { return priority > -100000.0f; }
}
