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

// Mission-scoped policy for the local pilot's native animation FSM.
//
// This layer sits above the verified Person::Simulate seam
// (Game/PilotFsmIntercept.h) and owns *what should happen*; the seam owns *when
// it can happen*. In this version the only representable policy is stock
// pass-through: no slot can hold anything else, so nothing here can write
// native state, change a branch, substitute an animation, or substitute a
// duration. It exists so the first override lands in an ownership layer that is
// already mission-scoped, reset, and observable rather than growing one later.
//
// Deliberately absent until each is proven against the executable:
//   - animation substitution (first writable feature),
//   - transition completion policy (animation / duration / manual),
//   - stock duration values. Only the animation-handle wait in states 1 and 3
//     is proven; no numeric duration constant has been located, so none is
//     exposed here.
//
// No Windows, Ogre, Lua, or engine dependencies: host-testable on Linux.

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ExtraUtilities::Lua::PilotAnimationPolicy
{
	// Policy slots. Four map one-to-one onto the proven native Person+0x228 FSM
	// states. Jump and Land are animation selections made from the standing
	// state (indices 11 and 10); the exact native conditions that select them
	// are not traced, so they have no native-state association yet.
	enum class Slot : std::uint8_t
	{
		Stand = 0,
		EnterCrouch,
		Crouched,
		ExitCrouch,
		Jump,
		Land,
	};

	constexpr std::size_t kSlotCount = 6;

	enum class Mode : std::uint8_t
	{
		Stock = 0,
	};

	// What the seam is told to do for one local Person::Simulate call.
	enum class Decision : std::uint8_t
	{
		PassThrough = 0,
	};

	struct TransitionPolicy
	{
		Mode mode = Mode::Stock;
	};

	struct Policy
	{
		std::array<TransitionPolicy, kSlotCount> slots{};

		constexpr const TransitionPolicy& At(Slot slot) const noexcept
		{
			return slots[static_cast<std::size_t>(slot)];
		}
	};

	constexpr bool IsStockOnly(const Policy& policy) noexcept
	{
		for (std::size_t i = 0; i < kSlotCount; ++i)
		{
			if (policy.slots[i].mode != Mode::Stock)
			{
				return false;
			}
		}
		return true;
	}

	// The active policy is copied by value and reset by assignment, and its
	// default must be constant-initialised: EXU statics live in a DLL that is
	// initialised inside DllMain under the loader lock.
	static_assert(std::is_trivially_copyable<Policy>::value, "Policy must stay trivially copyable");
	static_assert(IsStockOnly(Policy{}), "the default pilot animation policy must be stock");

	inline const char* SlotName(Slot slot) noexcept
	{
		switch (slot)
		{
		case Slot::Stand: return "stand";
		case Slot::EnterCrouch: return "enterCrouch";
		case Slot::Crouched: return "crouched";
		case Slot::ExitCrouch: return "exitCrouch";
		case Slot::Jump: return "jump";
		case Slot::Land: return "land";
		}
		return "unknown";
	}

	inline const char* ModeName(Mode mode) noexcept
	{
		switch (mode)
		{
		case Mode::Stock: return "stock";
		}
		return "unknown";
	}

	inline const char* DecisionName(Decision decision) noexcept
	{
		switch (decision)
		{
		case Decision::PassThrough: return "passThrough";
		}
		return "unknown";
	}

	// Native Person+0x228 value a slot corresponds to, or -1 when the slot is
	// not a distinct native FSM state (Jump, Land).
	constexpr std::int32_t NativeStateForSlot(Slot slot) noexcept
	{
		switch (slot)
		{
		case Slot::Stand: return 0;
		case Slot::EnterCrouch: return 1;
		case Slot::Crouched: return 2;
		case Slot::ExitCrouch: return 3;
		case Slot::Jump:
		case Slot::Land:
			break;
		}
		return -1;
	}

	// Inverse of NativeStateForSlot. Any native state outside the four proven
	// values has no slot and therefore no policy.
	constexpr bool SlotForNativeState(std::uint32_t nativeState, Slot& outSlot) noexcept
	{
		switch (nativeState)
		{
		case 0: outSlot = Slot::Stand; return true;
		case 1: outSlot = Slot::EnterCrouch; return true;
		case 2: outSlot = Slot::Crouched; return true;
		case 3: outSlot = Slot::ExitCrouch; return true;
		default: return false;
		}
	}

	// Resolves the decision for one local Person::Simulate call that is about to
	// run with the given native FSM state.
	//
	// Fails closed by construction: an unmapped native state, or a slot mode this
	// build does not implement, is pass-through. That property must survive when
	// overrides are added, so a corrupted or future-versioned policy can never
	// act on a state it does not understand.
	constexpr Decision Evaluate(const Policy& policy, std::uint32_t nativeState) noexcept
	{
		Slot slot = Slot::Stand;
		if (!SlotForNativeState(nativeState, slot))
		{
			return Decision::PassThrough;
		}

		switch (policy.At(slot).mode)
		{
		case Mode::Stock:
			return Decision::PassThrough;
		}
		return Decision::PassThrough;
	}

	// ----- Mission-scoped active policy (PilotAnimationPolicy.cpp) -----------

	// Restores the stock policy. Called at Lua-state attach and again from the
	// mission-scoped reset when the state closes, so a consumer that keeps the
	// DLL loaded cannot carry a policy into the next mission.
	void ResetMissionState() noexcept;

	// Copy of the policy currently in force.
	Policy GetActive() noexcept;

	// Evaluate() against the policy currently in force.
	Decision EvaluateActive(std::uint32_t nativeState) noexcept;
}
