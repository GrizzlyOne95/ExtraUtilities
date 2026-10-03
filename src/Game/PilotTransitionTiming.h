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

// Table-override math for the local pilot animation FSM.
//
// Person::Simulate drives every pilot clip from three per-index tables
// (Docs/Research/PILOT_CROUCH_NATIVE_RE_20261003.md):
//
//   endTime[idx]   clip seconds; re-read every tick. The clip is finished on
//                  the tick where timePosition + dt*rate >= endTime.
//   fpRate[idx]    first-person dt multiplier; LATCHED when the clip is
//                  applied (the 0->1 / 2->3 call), not re-read per tick.
//   worldRate[idx] third-person dt multiplier; latched the same way.
//
// So a transition lasts endTime / fpRate real seconds (+ up to one tick). The
// stock crouch clips use 0.967 / 0.5 = 1.934 s. Only the first-person finished
// flag releases the handle that FSM states 1 and 3 wait for.
//
// Ogre clamps a non-looping clip at its length, so an endTime above the clip
// length never completes and the FSM holds. That is what manual completion
// uses; anywhere else it would hang the pilot, so every other result keeps
// endTime <= clip length.
//
// This header turns (stock entry, clip length, completion policy) into the
// values to write for one table index during one local call. No Windows, Ogre,
// Lua, or engine dependencies: host-testable.

#include "Game/PilotAnimationPolicy.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ExtraUtilities::Lua::PilotTransitionTiming
{
	using PilotAnimationPolicy::CompletionMode;
	using PilotAnimationPolicy::Slot;

	// Entries per native table.
	constexpr std::size_t kTableEntries = 12;

	// Native animation index (Person+0x2A8, the table index) each policy slot
	// drives. PROVEN by the name table: 0 stand2Kneel, 1 kneel2stand, 2 idle,
	// 3 fireRecoilSniper, 10 landParachute, 11 jump.
	constexpr std::int32_t TableIndexForSlot(Slot slot) noexcept
	{
		switch (slot)
		{
		case Slot::Stand: return 2;
		case Slot::EnterCrouch: return 0;
		case Slot::Crouched: return 3;
		case Slot::ExitCrouch: return 1;
		case Slot::Jump: return 11;
		case Slot::Land: return 10;
		}
		return -1;
	}

	// The transition slot whose completion governs native FSM state 1 or 3.
	constexpr bool TransitionSlotForNativeState(std::uint32_t nativeState, Slot& outSlot) noexcept
	{
		switch (nativeState)
		{
		case 1: outSlot = Slot::EnterCrouch; return true;
		case 3: outSlot = Slot::ExitCrouch; return true;
		default: return false;
		}
	}

	// Manual completion holds the clip by putting endTime this far past the
	// clip's end. Any positive margin holds; a large one survives dt spikes.
	constexpr float kManualHoldMargin = 1000.0f;

	// The stock values of one table index, as read for this call.
	struct Entry
	{
		float end = 0.0f;
		float fpRate = 0.0f;
		float worldRate = 0.0f;
	};

	struct Input
	{
		Entry stock{};
		// Length of the clip that will play for this index on the first-person
		// entity (the substitute, or the stock clip). <= 0 or non-finite means
		// unknown.
		float clipLength = 0.0f;
		// True when the clip is a substitute rather than the stock name.
		bool substituted = false;
		CompletionMode completion = CompletionMode::Stock;
		// Seconds; only read for CompletionMode::Duration.
		float duration = 0.0f;
		// One-shot exu.fps.CompleteTransition(); only read for Manual.
		bool manualComplete = false;
	};

	struct Result
	{
		// False: write nothing for this index (stock stays in force).
		bool valid = false;
		// True when the values differ from the stock entry and need writing.
		bool changed = false;
		Entry values{};
	};

	inline bool IsPositiveFinite(float value) noexcept
	{
		return std::isfinite(value) && value > 0.0f;
	}

	inline float Min(float a, float b) noexcept
	{
		return a < b ? a : b;
	}

	// Rules (rates are latched at the apply call; endTime is read every tick):
	//
	//   stock      stock clip: untouched. Substitute: endTime = min(stockEnd, L)
	//              so a shorter substitute still finishes; rates stock.
	//   animation  one play of the clip at authored speed: endTime = L,
	//              rates 1.0. endTime = L (not "just under") is safe: the
	//              finish test is pos + dt*rate >= endTime, and the clamped
	//              position reaches L, so the tick that would pass L finishes.
	//   duration   endTime = min(stockEnd, L) for the stock clip (keeps the
	//              stock cut, 0.967 of a 1.0 s clip), L for a substitute (the
	//              whole substitute plays); both rates = endTime / D.
	//   manual     endTime = L + kManualHoldMargin, rates stock: the clip plays
	//              at stock speed, clamps at its end and the FSM holds. With
	//              manualComplete, endTime = 0: the next tick finishes it.
	//
	// Fails closed: a non-finite or non-positive stock entry, an unknown clip
	// length where one is needed, a bad duration, or a non-finite result is
	// invalid, and the caller leaves the stock entry in force.
	inline Result Compute(const Input& input) noexcept
	{
		Result result{};
		const Entry& stock = input.stock;
		if (!IsPositiveFinite(stock.end) || !IsPositiveFinite(stock.fpRate) ||
			!IsPositiveFinite(stock.worldRate))
		{
			return result;
		}

		const bool lengthKnown = IsPositiveFinite(input.clipLength);
		const float length = input.clipLength;
		Entry values = stock;
		bool allowZeroEnd = false;

		switch (input.completion)
		{
		case CompletionMode::Stock:
			if (input.substituted)
			{
				if (!lengthKnown)
				{
					return result;
				}
				values.end = Min(stock.end, length);
			}
			break;

		case CompletionMode::Animation:
			if (!lengthKnown)
			{
				return result;
			}
			values.end = length;
			values.fpRate = 1.0f;
			values.worldRate = 1.0f;
			break;

		case CompletionMode::Duration:
		{
			if (!lengthKnown || !IsPositiveFinite(input.duration))
			{
				return result;
			}
			values.end = input.substituted ? length : Min(stock.end, length);
			const float rate = values.end / input.duration;
			values.fpRate = rate;
			values.worldRate = rate;
			break;
		}

		case CompletionMode::Manual:
			if (input.manualComplete)
			{
				values.end = 0.0f;
				allowZeroEnd = true;
				break;
			}
			if (!lengthKnown)
			{
				return result;
			}
			values.end = length + kManualHoldMargin;
			break;

		default:
			return result;
		}

		const bool endOk = std::isfinite(values.end) &&
			(values.end > 0.0f || (allowZeroEnd && values.end == 0.0f));
		if (!endOk || !IsPositiveFinite(values.fpRate) || !IsPositiveFinite(values.worldRate))
		{
			return result;
		}

		result.valid = true;
		result.values = values;
		result.changed = values.end != stock.end || values.fpRate != stock.fpRate ||
			values.worldRate != stock.worldRate;
		return result;
	}

	// Real seconds a transition lasts under the given values, ignoring the
	// one-tick quantisation. Diagnostic and test helper.
	inline float TransitionSeconds(const Entry& values) noexcept
	{
		return IsPositiveFinite(values.fpRate) ? values.end / values.fpRate : 0.0f;
	}
}
