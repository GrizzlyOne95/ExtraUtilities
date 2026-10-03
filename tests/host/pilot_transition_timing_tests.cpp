/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 */

#include "HostTest.h"
#include "Game/PilotTransitionTiming.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace
{
	using namespace ExtraUtilities::Lua::PilotTransitionTiming;
	using ExtraUtilities::Lua::PilotAnimationPolicy::CompletionMode;
	using ExtraUtilities::Lua::PilotAnimationPolicy::Slot;

	// Stock crouch entry (idx 0/1) as dumped from the GOG 2.2.301 image.
	constexpr float kStockEnd = 0.967f;
	constexpr float kStockRate = 0.5f;

	Input CrouchInput(CompletionMode completion, float clipLength, bool substituted)
	{
		Input input{};
		input.stock.end = kStockEnd;
		input.stock.fpRate = kStockRate;
		input.stock.worldRate = kStockRate;
		input.clipLength = clipLength;
		input.substituted = substituted;
		input.completion = completion;
		return input;
	}

	bool Near(float a, float b)
	{
		return std::fabs(a - b) < 1.0e-5f;
	}
}

int main()
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();

	// ---- Slot -> table index -------------------------------------------------
	HostTest::Expect(TableIndexForSlot(Slot::EnterCrouch) == 0, "enterCrouch drives stand2Kneel (0)");
	HostTest::Expect(TableIndexForSlot(Slot::ExitCrouch) == 1, "exitCrouch drives kneel2stand (1)");
	HostTest::Expect(TableIndexForSlot(Slot::Stand) == 2, "stand drives idle (2)");
	HostTest::Expect(TableIndexForSlot(Slot::Crouched) == 3, "crouched drives fireRecoilSniper (3)");
	HostTest::Expect(TableIndexForSlot(Slot::Land) == 10, "land drives landParachute (10)");
	HostTest::Expect(TableIndexForSlot(Slot::Jump) == 11, "jump drives jump (11)");
	HostTest::Expect(TableIndexForSlot(static_cast<Slot>(99)) == -1, "an unknown slot has no index");
	{
		Slot slot = Slot::Stand;
		HostTest::Expect(TransitionSlotForNativeState(1, slot) && slot == Slot::EnterCrouch,
			"state 1 is the enterCrouch transition");
		HostTest::Expect(TransitionSlotForNativeState(3, slot) && slot == Slot::ExitCrouch,
			"state 3 is the exitCrouch transition");
		slot = Slot::Jump;
		HostTest::Expect(!TransitionSlotForNativeState(0, slot) && !TransitionSlotForNativeState(2, slot) &&
			!TransitionSlotForNativeState(7, slot) && slot == Slot::Jump,
			"other states are not transitions and leave the slot alone");
	}

	// ---- Stock arithmetic matches the capture ---------------------------------
	{
		const Entry stock{ kStockEnd, kStockRate, kStockRate };
		HostTest::Expect(Near(TransitionSeconds(stock), 1.934f), "stock crouch transition is 0.967/0.5 = 1.934 s");
	}

	// ---- Stock completion ------------------------------------------------------
	{
		const Result r = Compute(CrouchInput(CompletionMode::Stock, 1.0f, false));
		HostTest::Expect(r.valid && !r.changed, "stock clip with stock completion is untouched");
	}
	{
		const Result r = Compute(CrouchInput(CompletionMode::Stock, 0.5f, true));
		HostTest::Expect(r.valid && r.changed && Near(r.values.end, 0.5f) &&
			r.values.fpRate == kStockRate && r.values.worldRate == kStockRate,
			"a shorter substitute lowers endTime to its length, rates stock");
	}
	{
		const Result r = Compute(CrouchInput(CompletionMode::Stock, 3.0f, true));
		HostTest::Expect(r.valid && !r.changed && r.values.end == kStockEnd,
			"a longer substitute keeps the stock end (stock timing)");
	}
	{
		const Result r = Compute(CrouchInput(CompletionMode::Stock, 0.0f, true));
		HostTest::Expect(!r.valid, "a substitute with an unknown length is refused");
	}

	// ---- Animation completion -------------------------------------------------
	{
		const Result r = Compute(CrouchInput(CompletionMode::Animation, 1.0f, false));
		HostTest::Expect(r.valid && r.changed && Near(r.values.end, 1.0f) &&
			r.values.fpRate == 1.0f && r.values.worldRate == 1.0f,
			"animation: one play at authored speed (end = L, rate 1)");
		HostTest::Expect(Near(TransitionSeconds(r.values), 1.0f), "animation: lasts the clip length");
	}
	{
		const Result r = Compute(CrouchInput(CompletionMode::Animation, 2.5f, true));
		HostTest::Expect(r.valid && Near(r.values.end, 2.5f), "animation: a long substitute plays whole");
	}
	{
		const Result r = Compute(CrouchInput(CompletionMode::Animation, nan, false));
		HostTest::Expect(!r.valid, "animation: unknown length is refused");
	}

	// ---- Duration completion --------------------------------------------------
	{
		Input input = CrouchInput(CompletionMode::Duration, 1.0f, false);
		input.duration = 1.0f;
		const Result r = Compute(input);
		HostTest::Expect(r.valid && r.changed && Near(r.values.end, kStockEnd) &&
			Near(r.values.fpRate, kStockEnd) && Near(r.values.worldRate, kStockEnd),
			"duration 1.0 on the 1.0 s stock clip: end 0.967, rate 0.967");
		HostTest::Expect(Near(TransitionSeconds(r.values), 1.0f), "duration: lasts the requested seconds");
	}
	{
		Input input = CrouchInput(CompletionMode::Duration, 2.0f, true);
		input.duration = 0.5f;
		const Result r = Compute(input);
		HostTest::Expect(r.valid && Near(r.values.end, 2.0f) && Near(r.values.fpRate, 4.0f),
			"duration on a substitute plays the whole clip in D (end = L, rate = L/D)");
		HostTest::Expect(Near(TransitionSeconds(r.values), 0.5f), "duration on a substitute lasts D");
	}
	{
		Input input = CrouchInput(CompletionMode::Duration, 0.4f, false);
		input.duration = 2.0f;
		const Result r = Compute(input);
		HostTest::Expect(r.valid && Near(r.values.end, 0.4f) && Near(r.values.fpRate, 0.2f),
			"duration on a short stock clip ends at its length");
	}
	for (const float bad : { 0.0f, -1.0f, nan, inf })
	{
		Input input = CrouchInput(CompletionMode::Duration, 1.0f, false);
		input.duration = bad;
		HostTest::Expect(!Compute(input).valid, "duration must be finite and > 0");
	}
	{
		Input input = CrouchInput(CompletionMode::Duration, 1.0f, false);
		input.duration = std::numeric_limits<float>::denorm_min();
		HostTest::Expect(!Compute(input).valid, "a rate that overflows to infinity is refused");
	}

	// ---- Manual completion ----------------------------------------------------
	{
		const Result r = Compute(CrouchInput(CompletionMode::Manual, 1.0f, false));
		HostTest::Expect(r.valid && r.changed && r.values.end > 1.0f &&
			Near(r.values.end, 1.0f + kManualHoldMargin) && r.values.fpRate == kStockRate,
			"manual: endTime past the clip length holds; rates stock");
	}
	{
		Input input = CrouchInput(CompletionMode::Manual, 1.0f, false);
		input.manualComplete = true;
		const Result r = Compute(input);
		HostTest::Expect(r.valid && r.values.end == 0.0f && r.values.fpRate == kStockRate,
			"manual + CompleteTransition: endTime 0 finishes on the next tick");
	}
	{
		Input input = CrouchInput(CompletionMode::Manual, 0.0f, false);
		input.manualComplete = true;
		HostTest::Expect(Compute(input).valid, "manual completion needs no clip length");
		input.manualComplete = false;
		HostTest::Expect(!Compute(input).valid, "manual hold needs a clip length");
	}
	{
		Input input = CrouchInput(CompletionMode::Animation, 1.0f, false);
		input.manualComplete = true;
		const Result r = Compute(input);
		HostTest::Expect(r.valid && r.values.end > 0.0f, "the manual flag is ignored outside manual completion");
	}

	// ---- Fails closed on a bad stock entry or completion ----------------------
	{
		Input input = CrouchInput(CompletionMode::Animation, 1.0f, false);
		input.stock.fpRate = 0.0f;
		HostTest::Expect(!Compute(input).valid, "a zero stock rate is refused");
		input = CrouchInput(CompletionMode::Animation, 1.0f, false);
		input.stock.end = nan;
		HostTest::Expect(!Compute(input).valid, "a NaN stock end is refused");
		input = CrouchInput(static_cast<CompletionMode>(99), 1.0f, false);
		HostTest::Expect(!Compute(input).valid, "an unrecognised completion is refused");
	}

	// ---- Non-transition slot (e.g. jump, stock 1.167 / 0.05 / 0.6) -------------
	{
		Input input{};
		input.stock = Entry{ 1.167f, 0.05f, 0.6f };
		input.clipLength = 0.8f;
		input.substituted = true;
		const Result r = Compute(input);
		HostTest::Expect(r.valid && Near(r.values.end, 0.8f) && r.values.fpRate == 0.05f &&
			r.values.worldRate == 0.6f, "a substitute jump keeps both stock rates");
	}

	return HostTest::Finish("pilot transition timing");
}
