/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 */

#include "HostTest.h"
#include "Game/PersonAnimBlend.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

namespace
{
	using namespace ExtraUtilities::Lua::PersonAnimBlend;
	using HostTest::Expect;

	bool Near(float a, float b, float tolerance = 1.0e-4f)
	{
		return std::fabs(a - b) < tolerance;
	}

	Settings On()
	{
		Settings settings{};
		settings.enabled = true;
		return settings;
	}

	void TestSettings()
	{
		const Settings defaults{};
		Expect(!defaults.enabled, "blend is off by default");
		Expect(Near(defaults.time, 0.15f), "default blend time is 0.15 s");
		Expect(IsValidBlendSeconds(0.0) && IsValidBlendSeconds(2.0), "0 and 2 s are valid");
		Expect(!IsValidBlendSeconds(-0.01) && !IsValidBlendSeconds(2.01), "out-of-range blend times are refused");
		Expect(!IsValidBlendSeconds(std::numeric_limits<double>::quiet_NaN()), "NaN blend time is refused");

		Expect(!ShouldBlend(defaults, 2, 4), "off: no blend");
		Settings on = On();
		Expect(ShouldBlend(on, 2, 4), "idle -> runForward blends");
		Expect(ShouldBlend(on, 4, 8), "death blends by default");
		on.death = false;
		Expect(!ShouldBlend(on, 4, 8), "death = false: hard cut into death1");
		Expect(ShouldBlend(on, 8, 2), "death = false only affects switches into death1");
		on.time = 0.0f;
		Expect(!ShouldBlend(on, 2, 4), "time 0 is a hard cut");
		Expect(!ShouldBlend(On(), -1, 4) && !ShouldBlend(On(), 2, 12), "out-of-table indices never blend");
	}

	void TestPhaseCarry()
	{
		const Settings on = On();
		Expect(ShouldPhaseCarry(on, 4, 6, true, true), "runForward -> runLeft carries phase");
		Expect(!ShouldPhaseCarry(on, 2, 4, true, true), "idle -> run never seeks");
		Expect(!ShouldPhaseCarry(on, 4, 11, true, true), "run -> jump never seeks");
		Expect(!ShouldPhaseCarry(on, 4, 5, true, false), "a non-looping incoming clip is never seeked");
		Settings off = on;
		off.phaseCarry = false;
		Expect(!ShouldPhaseCarry(off, 4, 5, true, true), "phaseCarry = false");

		float t = -1.0f;
		Expect(PhaseCarryTime(0.4f, 0.8f, 1.0f, t) && Near(t, 0.5f), "half phase maps to half the new length");
		Expect(PhaseCarryTime(1.2f, 0.8f, 0.8f, t) && Near(t, 0.4f), "time past the length wraps");
		Expect(!PhaseCarryTime(0.4f, 0.0f, 1.0f, t), "zero old length is unusable");
		Expect(!PhaseCarryTime(std::numeric_limits<float>::infinity(), 0.8f, 1.0f, t), "infinite time is unusable");
	}

	void TestGhostClock()
	{
		float next = 0.0f;
		Expect(GhostNextTime(0.5f, 0.75f, 0.1f, 0.967f, next) && Near(next, 0.575f), "ghost advances at its rate");
		Expect(!GhostNextTime(0.9f, 1.0f, 0.1f, 0.967f, next) && Near(next, 0.9f), "ghost holds at the stock end gate");
		Expect(GhostNextTime(0.9f, 1.0f, 0.1f, std::numeric_limits<float>::infinity(), next), "no end gate = advance");
		Expect(!GhostNextTime(0.5f, 0.0f, 0.1f, 1.0f, next), "rate 0 holds");
	}

	void TestSwitchAndFade()
	{
		GhostList list;
		Retired retired[2]{};
		// idle -> runForward with base 1: the ghost takes all the weight.
		std::size_t count = list.Switch("idle", "runForward", 1.0f, 0.5f, 0.967f, retired);
		Expect(count == 0, "first switch retires nothing");
		Expect(list.Count() == 1 && Near(list.At(0).weight, 1.0f), "outgoing clip starts at full weight");
		Expect(Near(list.CurrentWeight(1.0f), 0.0f), "incoming clip starts at 0");

		Retired faded[kMaxGhosts]{};
		count = list.Fade(1.0f, 0.05f, 0.15f, faded);
		Expect(count == 0 && Near(list.At(0).weight, 2.0f / 3.0f), "one third of the fade per 0.05 s tick");
		Expect(Near(list.TotalWeight() + list.CurrentWeight(1.0f), 1.0f), "weights sum to the base mid-fade");
		list.Fade(1.0f, 0.05f, 0.15f, faded);
		count = list.Fade(1.0f, 0.05f, 0.15f, faded);
		Expect(count == 1 && faded[0].disable && std::strcmp(faded[0].name, "idle") == 0,
			"a faded-out ghost is retired with a disable");
		Expect(list.Count() == 0 && Near(list.CurrentWeight(1.0f), 1.0f), "fade complete: current at base");
	}

	void TestSwitchBack()
	{
		GhostList list;
		Retired retired[2]{};
		list.Switch("runForward", "runLeft", 1.0f, 1.0f, 0.967f, retired);
		Retired faded[kMaxGhosts]{};
		list.Fade(1.0f, 0.05f, 0.15f, faded);
		const float currentBefore = list.CurrentWeight(1.0f);
		// The FSM flips straight back: runLeft becomes a ghost, runForward
		// (the old ghost) is current again and must not be disabled.
		const std::size_t count = list.Switch("runLeft", "runForward", currentBefore, 0.75f, 0.967f, retired);
		Expect(count == 1 && !retired[0].disable && std::strcmp(retired[0].name, "runForward") == 0,
			"switching back revives the ghost without a disable");
		Expect(list.Count() == 1 && std::strcmp(list.At(0).name, "runLeft") == 0, "only the new outgoing clip is a ghost");
		Expect(Near(list.At(0).weight, currentBefore), "the outgoing clip keeps the weight it had");
		Expect(Near(list.TotalWeight() + list.CurrentWeight(1.0f), 1.0f), "sum stays at the base");
	}

	void TestOverflowFolds()
	{
		GhostList list;
		Retired retired[2]{};
		Retired faded[kMaxGhosts]{};
		list.Switch("a", "b", 1.0f, 1.0f, 1.0f, retired);
		list.Fade(1.0f, 0.03f, 0.15f, faded);
		list.Switch("b", "c", list.CurrentWeight(1.0f), 1.0f, 1.0f, retired);
		list.Fade(1.0f, 0.03f, 0.15f, faded);
		list.Switch("c", "d", list.CurrentWeight(1.0f), 1.0f, 1.0f, retired);
		list.Fade(1.0f, 0.03f, 0.15f, faded);
		Expect(list.Count() == kMaxGhosts, "three ghosts fit");
		const float before = list.TotalWeight();
		const float outgoing = list.CurrentWeight(1.0f);
		const std::size_t count = list.Switch("d", "e", outgoing, 1.0f, 1.0f, retired);
		Expect(count == 1 && retired[0].disable, "a fourth ghost retires the faintest");
		Expect(list.Count() == kMaxGhosts, "still three ghosts");
		Expect(Near(list.TotalWeight(), before + outgoing), "the retired weight is folded, the sum unchanged");
		Expect(list.IndexOf("d") >= 0, "the newest outgoing clip is listed");
	}

	void TestBaseWeight()
	{
		GhostList list;
		Retired retired[2]{};
		Retired faded[kMaxGhosts]{};
		list.Switch("idle", "runForward", 0.5f, 1.0f, 1.0f, retired);
		Expect(Near(list.CurrentWeight(0.5f), 0.0f), "first-person base 0.5: ghost holds it all");
		list.Fade(0.5f, 0.075f, 0.15f, faded);
		Expect(Near(list.TotalWeight(), 0.25f) && Near(list.CurrentWeight(0.5f), 0.25f), "base-relative fade");
		// The base drops to 0.1 mid-fade: the ghosts are capped to it.
		list.Fade(0.1f, 0.0f, 0.15f, faded);
		Expect(list.TotalWeight() <= 0.1f + 1.0e-5f, "ghosts never exceed the base");
		// Fade with time 0 clears at once.
		const std::size_t count = list.Fade(0.1f, 0.016f, 0.0f, faded);
		Expect(count == 1 && list.Count() == 0, "time 0 retires everything");
	}

	void TestTrackTable()
	{
		TrackTable table;
		int people[kMaxTracks + 1]{};
		int entity = 0;
		for (std::size_t i = 0; i < kMaxTracks; ++i)
		{
			table.Acquire(&people[i], &entity, Side::World, static_cast<std::uint32_t>(i));
		}
		Expect(table.ActiveCount() == kMaxTracks, "table full");
		Expect(table.Find(&people[3], Side::World) != nullptr, "find by person and side");
		Expect(table.Find(&people[3], Side::FirstPerson) == nullptr, "sides are separate");
		Track& fresh = table.Acquire(&people[kMaxTracks], &entity, Side::World, 100u);
		Expect(table.Evictions() == 1, "a full table evicts");
		Expect(table.Find(&people[0], Side::World) == nullptr, "the stalest track was evicted");
		Expect(fresh.person == &people[kMaxTracks] && fresh.ghosts.Count() == 0, "the fresh track is clean");
		table.Release(fresh);
		Expect(table.ActiveCount() == kMaxTracks - 1, "release frees the slot");
	}
}

int main()
{
	TestSettings();
	TestPhaseCarry();
	TestGhostClock();
	TestSwitchAndFade();
	TestSwitchBack();
	TestOverflowFolds();
	TestBaseWeight();
	TestTrackTable();
	return HostTest::Finish("person anim blend");
}
