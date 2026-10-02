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
// it can happen*.
//
// The model can describe the planned overrides (animation substitution, and
// transition completion by animation / duration / manual), but kBuildSupport
// below says this build can apply none of them. The profile validator
// (PilotAnimationProfile.h) rejects anything unsupported and SetActive refuses
// it, so the active policy is still always stock, and even a non-stock policy
// evaluates to pass-through: the seam has no action to take yet.
//
// Stock duration values are deliberately absent. Only the animation-handle wait
// in states 1 and 3 is proven; no numeric duration constant has been located.
//
// No Windows, Ogre, Lua, or engine dependencies: host-testable on Linux.

#include <array>
#include <atomic>
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

	// Which animation a slot plays.
	enum class Mode : std::uint8_t
	{
		Stock = 0,
		// Play a named animation instead of the stock one (handoff step 3).
		Substitute,
	};

	// What ends a transition slot (enterCrouch / exitCrouch). Stock is the
	// native animation-handle wait (handoff step 6).
	enum class CompletionMode : std::uint8_t
	{
		Stock = 0,
		Animation,
		Duration,
		Manual,
	};

	// Longest substitute animation name, excluding the terminator. Fixed so the
	// policy stays trivially copyable and allocation-free for the hook.
	constexpr std::size_t kMaxAnimationName = 63;

	// What the seam is told to do for one local Person::Simulate call.
	enum class Decision : std::uint8_t
	{
		PassThrough = 0,
	};

	struct TransitionPolicy
	{
		Mode mode = Mode::Stock;
		// NUL-terminated; meaningful only for Mode::Substitute.
		char animation[kMaxAnimationName + 1]{};
		CompletionMode completion = CompletionMode::Stock;
		// Seconds; meaningful only for CompletionMode::Duration.
		float duration = 0.0f;
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
			if (policy.slots[i].mode != Mode::Stock ||
				policy.slots[i].completion != CompletionMode::Stock)
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
		case Mode::Substitute: return "substitute";
		}
		return "unknown";
	}

	inline const char* CompletionName(CompletionMode completion) noexcept
	{
		switch (completion)
		{
		case CompletionMode::Stock: return "stock";
		case CompletionMode::Animation: return "animation";
		case CompletionMode::Duration: return "duration";
		case CompletionMode::Manual: return "manual";
		}
		return "unknown";
	}

	// Transition completion only means something for the two native waits.
	constexpr bool SlotHasCompletion(Slot slot) noexcept
	{
		return slot == Slot::EnterCrouch || slot == Slot::ExitCrouch;
	}

	// ----- What this build can apply -----------------------------------------

	constexpr std::uint8_t SlotBit(Slot slot) noexcept
	{
		return static_cast<std::uint8_t>(1u << static_cast<unsigned>(slot));
	}

	constexpr std::uint8_t CompletionBit(CompletionMode completion) noexcept
	{
		return static_cast<std::uint8_t>(1u << static_cast<unsigned>(completion));
	}

	struct Support
	{
		// Slots that may use Mode::Substitute.
		std::uint8_t substituteSlots = 0;
		// Non-stock completion modes the transition slots may use.
		std::uint8_t completionModes = 0;
	};

	constexpr bool HasOverrideSupport(const Support& support) noexcept
	{
		return support.substituteSlots != 0 || support.completionModes != 0;
	}

	// The single source of truth for what the seam can act on. Also drives the
	// Lua capability pilotAnimationOverrides. Widen a bit here only in the same
	// change that makes the seam apply it.
	constexpr Support kBuildSupport{};

	constexpr bool IsSupported(const Policy& policy, const Support& support) noexcept
	{
		for (std::size_t i = 0; i < kSlotCount; ++i)
		{
			const Slot slot = static_cast<Slot>(i);
			const TransitionPolicy& entry = policy.slots[i];
			switch (entry.mode)
			{
			case Mode::Stock:
				break;
			case Mode::Substitute:
				if ((support.substituteSlots & SlotBit(slot)) == 0)
				{
					return false;
				}
				break;
			default:
				return false;
			}

			switch (entry.completion)
			{
			case CompletionMode::Stock:
				break;
			case CompletionMode::Animation:
			case CompletionMode::Duration:
			case CompletionMode::Manual:
				if (!SlotHasCompletion(slot) ||
					(support.completionModes & CompletionBit(entry.completion)) == 0)
				{
					return false;
				}
				break;
			default:
				return false;
			}
		}
		return true;
	}

	static_assert(IsSupported(Policy{}, Support{}), "stock is supported by every build");

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
		case Mode::Substitute:
			// Representable but not actionable: the seam has no substitution
			// yet, and kBuildSupport keeps it out of the active policy.
			return Decision::PassThrough;
		}
		return Decision::PassThrough;
	}

	// ----- Publication between Lua and the hook -----------------------------

	// Lua publishes; the Person::Simulate hook reads. Whether they share a
	// thread is unproven, so a sequence lock hands over whole policies. The
	// reader never blocks: after a bounded number of torn attempts it reports
	// failure and the caller falls back to stock (fail closed).
	class PolicyPublisher
	{
	public:
		// One writer at a time (the Lua thread).
		void Publish(const Policy& policy) noexcept
		{
			const std::uint32_t sequence = m_sequence.load(std::memory_order_relaxed);
			m_sequence.store(sequence + 1u, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			m_policy = policy;
			m_sequence.store(sequence + 2u, std::memory_order_release);
		}

		bool TryRead(Policy& out) const noexcept
		{
			for (int attempt = 0; attempt < kReadAttempts; ++attempt)
			{
				const std::uint32_t begin = m_sequence.load(std::memory_order_acquire);
				if ((begin & 1u) != 0)
				{
					continue;
				}
				out = m_policy;
				std::atomic_thread_fence(std::memory_order_acquire);
				if (m_sequence.load(std::memory_order_relaxed) == begin)
				{
					return true;
				}
			}
			return false;
		}

	private:
		static constexpr int kReadAttempts = 8;

		std::atomic<std::uint32_t> m_sequence{ 0 };
		Policy m_policy{};
	};

	// ----- Mission-scoped active policy (PilotAnimationPolicy.cpp) -----------

	// Restores the stock policy. Called at Lua-state attach and again from the
	// mission-scoped reset when the state closes, so a consumer that keeps the
	// DLL loaded cannot carry a policy into the next mission.
	void ResetMissionState() noexcept;

	// Makes policy active if this build can apply it (IsSupported against
	// kBuildSupport); otherwise leaves the active policy unchanged and returns
	// false. Lua thread only.
	bool SetActive(const Policy& policy) noexcept;

	// Copy of the policy currently in force (stock if it cannot be read).
	Policy GetActive() noexcept;

	// Evaluate() against the policy currently in force.
	Decision EvaluateActive(std::uint32_t nativeState) noexcept;
}
