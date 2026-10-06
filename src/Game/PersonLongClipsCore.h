/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#pragma once

// Engine-free decisions of PersonLongClips (exu.animation.SetPersonLongClips),
// kept apart so the host tests can exercise them.
//
// Person::Simulate stops advancing the current clip on the tick where
// timePosition + dt*rate >= endTime[idx] (0x8e8e94), looped or not. Every
// run clip (idx 4-7, looped) and idle (idx 2, not looped) ends at 0.967 s, so
// a longer run freezes mid-stride while the body keeps moving (the creature
// "slides"), and a longer idle stops at 0.967 s. For those indices the end
// time only feeds the 3rd-person "ended" flag (no consumer) and the cockpit
// flag that stops the clip's sound channel; no FSM transition reads it
// (Docs/Research/PERSON_LONG_CLIPS_RE_20261005.md).

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ExtraUtilities::Lua::PersonLongClips
{
	inline constexpr std::int32_t kIdleIndex = 2;
	inline constexpr std::int32_t kFirstRunIndex = 4;  // runForward
	inline constexpr std::int32_t kLastRunIndex = 7;   // runRight

	// 0.967f, the stock end time of idx 2 and 4-7 on the qualified image.
	inline constexpr std::uint32_t kStockEndBits = 0x3F778D50u;
	// Far past any clip: a raised entry never trips the end test.
	inline constexpr float kRaisedEnd = 1.0e6f;

	struct Settings
	{
		bool runs = false;
		bool idle = false;
	};

	inline bool IsRunIndex(std::int32_t index) noexcept
	{
		return index >= kFirstRunIndex && index <= kLastRunIndex;
	}

	// A clip the stock end time cuts short.
	inline bool IsLongClip(float length, float stockEnd) noexcept
	{
		return std::isfinite(length) && std::isfinite(stockEnd) && length > stockEnd + 1.0e-3f;
	}

	// Before the stock call: raise endTime[index] for this call? Only an entry
	// that still holds its stock value is touched (a pilot animation policy
	// that rewrote it owns it). Runs are raised for every Person: a run shorter
	// than the end time wraps before reaching it, so nothing changes for it.
	// Idle only for an entity whose idle was found long (and is looped by us).
	inline bool ShouldRaise(const Settings& settings, std::int32_t index, std::uint32_t currentEndBits,
		bool idleIsLong) noexcept
	{
		if (currentEndBits != kStockEndBits)
		{
			return false;
		}
		if (IsRunIndex(index))
		{
			return settings.runs;
		}
		if (index == kIdleIndex)
		{
			return settings.idle && idleIsLong;
		}
		return false;
	}

	// After the stock call: the Person switched into idle during it (the stock
	// apply helper then set the idle state's loop flag to 0 and its time to 0).
	inline bool EnteredIdle(std::int32_t before, std::int32_t after) noexcept
	{
		return after == kIdleIndex && before != kIdleIndex;
	}

	// Which WORLD entities have an idle longer than the stock end time.
	// Filled when a Person enters idle, read before each call. Entity pointers
	// can be reused after a free; a stale entry only mis-sizes one idle until
	// that entity next enters idle and the entry is refreshed.
	template <std::size_t N>
	class LongIdleCache
	{
	public:
		bool Find(const void* entity, bool& outIsLong) const noexcept
		{
			for (std::size_t i = 0; i < m_count; ++i)
			{
				if (m_entries[i].entity == entity)
				{
					outIsLong = m_entries[i].isLong;
					return true;
				}
			}
			return false;
		}

		void Put(const void* entity, bool isLong) noexcept
		{
			if (entity == nullptr)
			{
				return;
			}
			for (std::size_t i = 0; i < m_count; ++i)
			{
				if (m_entries[i].entity == entity)
				{
					m_entries[i].isLong = isLong;
					return;
				}
			}
			std::size_t slot = m_next;
			if (m_count < N)
			{
				slot = m_count++;
			}
			else
			{
				m_next = (m_next + 1) % N;
			}
			m_entries[slot] = Entry{ entity, isLong };
		}

		void Clear() noexcept
		{
			m_count = 0;
			m_next = 0;
		}

		std::size_t Count() const noexcept { return m_count; }

	private:
		struct Entry
		{
			const void* entity = nullptr;
			bool isLong = false;
		};
		Entry m_entries[N]{};
		std::size_t m_count = 0;
		std::size_t m_next = 0;
	};
}
