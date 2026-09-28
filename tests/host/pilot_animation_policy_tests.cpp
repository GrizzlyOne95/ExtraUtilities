/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 */

#include "HostTest.h"
#include "Game/PilotAnimationPolicy.cpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>

int main()
{
	using namespace ExtraUtilities::Lua::PilotAnimationPolicy;

	constexpr Slot kAllSlots[] = {
		Slot::Stand, Slot::EnterCrouch, Slot::Crouched,
		Slot::ExitCrouch, Slot::Jump, Slot::Land,
	};
	HostTest::Expect(sizeof(kAllSlots) / sizeof(kAllSlots[0]) == kSlotCount,
		"every slot is covered by these checks");

	// ---- Default is explicit stock for every slot ---------------------------
	{
		const Policy stock{};
		HostTest::Expect(IsStockOnly(stock), "default policy is stock-only");
		for (const Slot slot : kAllSlots)
		{
			HostTest::Expect(stock.At(slot).mode == Mode::Stock,
				std::string("default ") + SlotName(slot) + " slot is stock");
		}
	}

	// ---- Names are the Lua-visible keys; pin them ---------------------------
	HostTest::Expect(std::strcmp(SlotName(Slot::Stand), "stand") == 0, "stand key");
	HostTest::Expect(std::strcmp(SlotName(Slot::EnterCrouch), "enterCrouch") == 0, "enterCrouch key");
	HostTest::Expect(std::strcmp(SlotName(Slot::Crouched), "crouched") == 0, "crouched key");
	HostTest::Expect(std::strcmp(SlotName(Slot::ExitCrouch), "exitCrouch") == 0, "exitCrouch key");
	HostTest::Expect(std::strcmp(SlotName(Slot::Jump), "jump") == 0, "jump key");
	HostTest::Expect(std::strcmp(SlotName(Slot::Land), "land") == 0, "land key");
	HostTest::Expect(std::strcmp(SlotName(static_cast<Slot>(99)), "unknown") == 0,
		"out-of-range slot name is explicit");
	HostTest::Expect(std::strcmp(ModeName(Mode::Stock), "stock") == 0, "stock mode name");
	HostTest::Expect(std::strcmp(ModeName(static_cast<Mode>(99)), "unknown") == 0,
		"unrecognised mode name is explicit");
	HostTest::Expect(std::strcmp(DecisionName(Decision::PassThrough), "passThrough") == 0,
		"pass-through decision name");

	// ---- Slot <-> native FSM state -----------------------------------------
	HostTest::Expect(NativeStateForSlot(Slot::Stand) == 0, "stand is native state 0");
	HostTest::Expect(NativeStateForSlot(Slot::EnterCrouch) == 1, "enterCrouch is native state 1");
	HostTest::Expect(NativeStateForSlot(Slot::Crouched) == 2, "crouched is native state 2");
	HostTest::Expect(NativeStateForSlot(Slot::ExitCrouch) == 3, "exitCrouch is native state 3");
	HostTest::Expect(NativeStateForSlot(Slot::Jump) == -1,
		"jump is not a distinct native FSM state");
	HostTest::Expect(NativeStateForSlot(Slot::Land) == -1,
		"land is not a distinct native FSM state");

	for (std::uint32_t native = 0; native <= 3; ++native)
	{
		Slot slot = Slot::Land;
		HostTest::Expect(SlotForNativeState(native, slot), "native states 0..3 map to a slot");
		HostTest::Expect(NativeStateForSlot(slot) == static_cast<std::int32_t>(native),
			"native state round-trips through its slot");
	}
	{
		Slot slot = Slot::Jump;
		HostTest::Expect(!SlotForNativeState(4, slot), "unproven native state 4 has no slot");
		HostTest::Expect(!SlotForNativeState(0xFFFFFFFFu, slot), "garbage native state has no slot");
		HostTest::Expect(slot == Slot::Jump, "a failed lookup does not touch the output slot");
	}

	// ---- Evaluation is pass-through everywhere ------------------------------
	for (std::uint32_t native : { 0u, 1u, 2u, 3u, 4u, 7u, 0xFFFFFFFFu })
	{
		HostTest::Expect(Evaluate(Policy{}, native) == Decision::PassThrough,
			"stock policy passes every native state through");
	}

	// A mode this build does not implement (a corrupted or future-versioned
	// policy) must still fail closed, in every slot, for every mapped state.
	for (const Slot slot : kAllSlots)
	{
		Policy odd{};
		odd.slots[static_cast<std::size_t>(slot)].mode = static_cast<Mode>(99);
		HostTest::Expect(!IsStockOnly(odd),
			std::string("an unrecognised ") + SlotName(slot) + " mode is not stock-only");
		for (std::uint32_t native = 0; native <= 3; ++native)
		{
			HostTest::Expect(Evaluate(odd, native) == Decision::PassThrough,
				"an unrecognised mode never produces a non-pass-through decision");
		}
	}

	// ---- Mission-scoped instance -------------------------------------------
	ResetMissionState();
	HostTest::Expect(IsStockOnly(GetActive()), "active policy is stock after reset");
	HostTest::Expect(EvaluateActive(1) == Decision::PassThrough, "active policy passes enterCrouch through");
	HostTest::Expect(EvaluateActive(3) == Decision::PassThrough, "active policy passes exitCrouch through");
	HostTest::Expect(EvaluateActive(0xFFFFFFFFu) == Decision::PassThrough,
		"active policy passes an unmapped state through");

	// Nothing can store a non-stock policy through the public API yet, so write
	// the (file-local, visible because this test includes the .cpp) active
	// instance directly to prove the real reset restores the default over it and
	// that a mission boundary cannot leak a policy into the next mission.
	{
		Policy odd{};
		odd.slots[static_cast<std::size_t>(Slot::Crouched)].mode = static_cast<Mode>(99);
		HostTest::Expect(!SetActive(odd), "SetActive refuses a mode this build cannot apply");
		HostTest::Expect(IsStockOnly(GetActive()), "a refused policy leaves the active policy stock");
		g_active.Publish(odd);
	}
	HostTest::Expect(!IsStockOnly(GetActive()), "precondition: active policy was made non-stock");
	HostTest::Expect(EvaluateActive(2) == Decision::PassThrough,
		"an unrecognised active mode still passes through");
	ResetMissionState();
	HostTest::Expect(IsStockOnly(GetActive()), "ResetMissionState restores the stock policy");

	// ---- Planned modes are representable but not applicable ---------------
	HostTest::Expect(std::strcmp(ModeName(Mode::Substitute), "substitute") == 0, "substitute mode name");
	HostTest::Expect(std::strcmp(CompletionName(CompletionMode::Stock), "stock") == 0, "stock completion name");
	HostTest::Expect(std::strcmp(CompletionName(CompletionMode::Animation), "animation") == 0,
		"animation completion name");
	HostTest::Expect(std::strcmp(CompletionName(CompletionMode::Duration), "duration") == 0,
		"duration completion name");
	HostTest::Expect(std::strcmp(CompletionName(CompletionMode::Manual), "manual") == 0,
		"manual completion name");
	HostTest::Expect(std::strcmp(CompletionName(static_cast<CompletionMode>(99)), "unknown") == 0,
		"unrecognised completion name is explicit");
	HostTest::Expect(SlotHasCompletion(Slot::EnterCrouch) && SlotHasCompletion(Slot::ExitCrouch),
		"transition slots have completion");
	for (const Slot slot : { Slot::Stand, Slot::Crouched, Slot::Jump, Slot::Land })
	{
		HostTest::Expect(!SlotHasCompletion(slot),
			std::string(SlotName(slot)) + " has no completion");
	}

	HostTest::Expect(!HasOverrideSupport(kBuildSupport),
		"this build supports no override (capability pilotAnimationOverrides stays false)");

	{
		Policy substitute{};
		substitute.slots[static_cast<std::size_t>(Slot::EnterCrouch)].mode = Mode::Substitute;
		std::strcpy(substitute.slots[static_cast<std::size_t>(Slot::EnterCrouch)].animation, "myKneel");
		HostTest::Expect(!IsStockOnly(substitute), "a substitute slot is not stock-only");
		HostTest::Expect(!IsSupported(substitute, kBuildSupport), "substitute is unsupported by this build");
		HostTest::Expect(!SetActive(substitute), "SetActive refuses an unsupported substitute");

		Support crouchOnly{};
		crouchOnly.substituteSlots = SlotBit(Slot::EnterCrouch);
		HostTest::Expect(IsSupported(substitute, crouchOnly), "support is per slot (allowed slot)");
		Policy other = substitute;
		other.slots[static_cast<std::size_t>(Slot::Jump)].mode = Mode::Substitute;
		HostTest::Expect(!IsSupported(other, crouchOnly), "support is per slot (other slot refused)");

		for (std::uint32_t native = 0; native <= 3; ++native)
		{
			HostTest::Expect(Evaluate(substitute, native) == Decision::PassThrough,
				"substitute is not actionable yet: evaluation passes through");
		}
	}
	{
		Policy completion{};
		completion.slots[static_cast<std::size_t>(Slot::ExitCrouch)].completion = CompletionMode::Manual;
		HostTest::Expect(!IsStockOnly(completion), "a non-stock completion is not stock-only");
		HostTest::Expect(!IsSupported(completion, kBuildSupport), "manual completion is unsupported");

		Support manual{};
		manual.completionModes = CompletionBit(CompletionMode::Manual);
		HostTest::Expect(IsSupported(completion, manual), "completion support is per mode");

		Policy onStand{};
		onStand.slots[static_cast<std::size_t>(Slot::Stand)].completion = CompletionMode::Manual;
		HostTest::Expect(!IsSupported(onStand, manual), "completion on a non-transition slot is never supported");

		Policy garbage{};
		garbage.slots[static_cast<std::size_t>(Slot::EnterCrouch)].completion = static_cast<CompletionMode>(99);
		Support everything{};
		everything.substituteSlots = 0xFF;
		everything.completionModes = 0xFF;
		HostTest::Expect(!IsSupported(garbage, everything), "an unrecognised completion is never supported");
	}

	// ---- Publication ---------------------------------------------------------
	{
		PolicyPublisher publisher;
		Policy read{};
		read.slots[0].mode = static_cast<Mode>(99);
		HostTest::Expect(publisher.TryRead(read) && IsStockOnly(read), "a new publisher holds stock");

		Policy published{};
		published.slots[static_cast<std::size_t>(Slot::Land)].mode = Mode::Substitute;
		std::strcpy(published.slots[static_cast<std::size_t>(Slot::Land)].animation, "flare");
		publisher.Publish(published);
		HostTest::Expect(publisher.TryRead(read), "published policy is readable");
		HostTest::Expect(read.At(Slot::Land).mode == Mode::Substitute &&
			std::strcmp(read.At(Slot::Land).animation, "flare") == 0,
			"reader sees the whole published policy");
	}

	return HostTest::Finish("pilot animation policy");
}
