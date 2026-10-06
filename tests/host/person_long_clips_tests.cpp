/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 */

#include "HostTest.h"
#include "Game/PersonLongClipsCore.h"

#include <cstdint>
#include <cstring>
#include <limits>

namespace
{
	using namespace ExtraUtilities::Lua::PersonLongClips;
	using HostTest::Expect;

	constexpr std::uint32_t kOtherBits = 0x3F800000u; // 1.0f, a policy-written value

	float StockEnd()
	{
		float value = 0.0f;
		std::memcpy(&value, &kStockEndBits, sizeof(value));
		return value;
	}

	void TestStockEndBits()
	{
		Expect(StockEnd() > 0.9669f && StockEnd() < 0.9671f, "stock end bits are 0.967");
		Expect(kRaisedEnd > 1000.0f, "the raised end is past any clip");
	}

	void TestRuns()
	{
		Settings runs{};
		runs.runs = true;
		for (std::int32_t index = kFirstRunIndex; index <= kLastRunIndex; ++index)
		{
			Expect(ShouldRaise(runs, index, kStockEndBits, false), "every run index is raised");
		}
		Expect(!ShouldRaise(Settings{}, 4, kStockEndBits, false), "off: runs stay stock");
		Expect(!ShouldRaise(runs, 4, kOtherBits, false), "a rewritten entry is left to its owner");
		Expect(!ShouldRaise(runs, kIdleIndex, kStockEndBits, true), "runs alone does not touch idle");
	}

	void TestIdle()
	{
		Settings idle{};
		idle.idle = true;
		Expect(ShouldRaise(idle, kIdleIndex, kStockEndBits, true), "a long idle is raised");
		Expect(!ShouldRaise(idle, kIdleIndex, kStockEndBits, false), "a short (stock) idle stays stock");
		Expect(!ShouldRaise(idle, 4, kStockEndBits, true), "idle alone does not touch runs");
	}

	void TestOtherIndicesNeverRaised()
	{
		Settings both{};
		both.runs = both.idle = true;
		// kneel transitions, crouch idle, death and the air clips drive the FSM
		for (std::int32_t index : { 0, 1, 3, 8, 9, 10, 11, -1, 12 })
		{
			Expect(!ShouldRaise(both, index, kStockEndBits, true), "non-loop index never raised");
		}
	}

	void TestLongClip()
	{
		const float end = StockEnd();
		Expect(!IsLongClip(0.9f, end), "0.9 s fits");
		Expect(!IsLongClip(end, end), "exactly the end time fits");
		Expect(IsLongClip(1.0f, end), "1.0 s is long");
		Expect(IsLongClip(4.958f, end), "the rhino idle is long");
		Expect(!IsLongClip(std::numeric_limits<float>::quiet_NaN(), end), "NaN length is not long");
	}

	void TestEnteredIdle()
	{
		Expect(EnteredIdle(4, kIdleIndex), "run -> idle");
		Expect(EnteredIdle(-1, kIdleIndex), "first sample idle");
		Expect(!EnteredIdle(kIdleIndex, kIdleIndex), "staying idle");
		Expect(!EnteredIdle(kIdleIndex, 4), "idle -> run");
	}

	void TestCache()
	{
		LongIdleCache<2> cache;
		int a = 0, b = 0, c = 0;
		bool isLong = false;
		Expect(!cache.Find(&a, isLong), "empty cache misses");
		cache.Put(&a, true);
		cache.Put(&b, false);
		Expect(cache.Find(&a, isLong) && isLong, "a is long");
		Expect(cache.Find(&b, isLong) && !isLong, "b is short");
		cache.Put(&a, false);
		Expect(cache.Find(&a, isLong) && !isLong && cache.Count() == 2, "refresh in place");
		cache.Put(&c, true);
		Expect(cache.Count() == 2 && cache.Find(&c, isLong) && isLong, "full cache replaces a slot");
		cache.Put(nullptr, true);
		Expect(!cache.Find(nullptr, isLong), "null entity is never cached");
		cache.Clear();
		Expect(cache.Count() == 0 && !cache.Find(&c, isLong), "clear empties");
	}
}

int main()
{
	TestStockEndBits();
	TestRuns();
	TestIdle();
	TestOtherIndicesNeverRaised();
	TestLongClip();
	TestEnteredIdle();
	TestCache();
	return HostTest::Finish("person long clips");
}
