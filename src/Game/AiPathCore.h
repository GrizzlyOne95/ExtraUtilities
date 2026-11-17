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
}
