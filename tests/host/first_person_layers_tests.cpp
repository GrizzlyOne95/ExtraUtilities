/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 */

#include "HostTest.h"
#include "Game/FirstPersonLayers.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace
{
	using namespace ExtraUtilities::Lua::FirstPersonLayers;

	Error Set(LayerBook& book, const char* name, const LayerOptions& options = {})
	{
		return book.Set(name, std::strlen(name), options);
	}

	const LayerSpec* Find(const LayerBook& book, const char* name)
	{
		const LayerSet& set = book.Layers();
		for (std::uint32_t i = 0; i < set.count; ++i)
		{
			if (std::strcmp(set.layers[i].name, name) == 0)
			{
				return &set.layers[i];
			}
		}
		return nullptr;
	}

	bool Near(float a, float b)
	{
		return std::fabs(a - b) < 1.0e-5f;
	}
}

int main()
{
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double inf = std::numeric_limits<double>::infinity();

	// ---- Validation ----------------------------------------------------------
	HostTest::Expect(!IsValidName("", 0), "empty name refused");
	HostTest::Expect(!IsValidName(nullptr, 3), "null name refused");
	HostTest::Expect(IsValidName("barrelSpin", 10), "plain name accepted");
	{
		const std::string longest(kMaxLayerName, 'a');
		const std::string tooLong(kMaxLayerName + 1, 'a');
		HostTest::Expect(IsValidName(longest.c_str(), longest.size()), "63-character name accepted");
		HostTest::Expect(!IsValidName(tooLong.c_str(), tooLong.size()), "64-character name refused");
		const char embedded[] = { 'a', '\0', 'b' };
		HostTest::Expect(!IsValidName(embedded, 3), "embedded NUL refused");
	}
	HostTest::Expect(IsValidSpeed(0.0) && IsValidSpeed(3.0) && IsValidSpeed(50.0), "speed 0..50 accepted");
	HostTest::Expect(!IsValidSpeed(-0.5) && !IsValidSpeed(50.01), "speed outside 0..50 refused");
	HostTest::Expect(!IsValidSpeed(nan) && !IsValidSpeed(inf), "non-finite speed refused");
	HostTest::Expect(IsValidWeight(0.0) && IsValidWeight(1.0), "weight 0..1 accepted");
	HostTest::Expect(!IsValidWeight(-0.01) && !IsValidWeight(1.01) && !IsValidWeight(nan), "bad weight refused");
	HostTest::Expect(IsValidTime(0.0) && IsValidTime(12.5), "time >= 0 accepted");
	HostTest::Expect(!IsValidTime(-1.0) && !IsValidTime(inf) && !IsValidTime(nan), "bad time refused");

	// ---- Create with defaults, update keeps unspecified fields ---------------
	{
		LayerBook book;
		HostTest::Expect(Set(book, "barrelSpin") == Error::None, "create with no options");
		const LayerSpec* spin = Find(book, "barrelSpin");
		HostTest::Expect(spin != nullptr, "layer exists after create");
		HostTest::Expect(spin != nullptr && spin->speed == 1.0f && spin->weight == 1.0f && spin->loop,
			"defaults are speed 1, weight 1, loop true");
		HostTest::Expect(spin != nullptr && spin->id != 0 && spin->seekSerial == 0, "new layer has an id and no seek");
		const std::uint32_t id = spin != nullptr ? spin->id : 0;

		LayerOptions speedOnly{};
		speedOnly.hasSpeed = true;
		speedOnly.speed = 0.0;
		HostTest::Expect(Set(book, "barrelSpin", speedOnly) == Error::None, "update speed");
		spin = Find(book, "barrelSpin");
		HostTest::Expect(spin != nullptr && spin->speed == 0.0f && spin->weight == 1.0f && spin->loop,
			"update changes only the given field");
		HostTest::Expect(spin != nullptr && spin->id == id, "update keeps the layer id");
		HostTest::Expect(book.Layers().count == 1, "update does not add a layer");

		LayerOptions timed{};
		timed.hasTime = true;
		timed.time = 0.25;
		HostTest::Expect(Set(book, "barrelSpin", timed) == Error::None, "set time");
		spin = Find(book, "barrelSpin");
		HostTest::Expect(spin != nullptr && spin->seekSerial == 1 && spin->seekTime == 0.25f, "time bumps the seek serial");

		HostTest::Expect(book.SetSpeed("barrelSpin", 10, 3.0) == Error::None, "SetSpeed");
		HostTest::Expect(book.SetWeight("barrelSpin", 10, 0.3) == Error::None, "SetWeight");
		spin = Find(book, "barrelSpin");
		HostTest::Expect(spin != nullptr && spin->speed == 3.0f && Near(spin->weight, 0.3f), "SetSpeed/SetWeight applied");
		HostTest::Expect(book.SetSpeed("nope", 4, 1.0) == Error::NotFound, "SetSpeed on a missing layer");
		HostTest::Expect(book.SetWeight("nope", 4, 1.0) == Error::NotFound, "SetWeight on a missing layer");
		HostTest::Expect(book.SetSpeed("barrelSpin", 10, -1.0) == Error::BadSpeed, "SetSpeed validates");
		HostTest::Expect(book.SetWeight("barrelSpin", 10, 2.0) == Error::BadWeight, "SetWeight validates");
	}

	// ---- Failures change nothing ---------------------------------------------
	{
		LayerBook book;
		LayerOptions bad{};
		bad.hasSpeed = true;
		bad.speed = 2.0;
		bad.hasWeight = true;
		bad.weight = 5.0;
		HostTest::Expect(Set(book, "a", bad) == Error::BadWeight, "invalid weight refused");
		HostTest::Expect(book.Layers().count == 0, "a refused create adds nothing");

		HostTest::Expect(Set(book, "a") == Error::None, "create a");
		LayerOptions badUpdate{};
		badUpdate.hasSpeed = true;
		badUpdate.speed = 3.0;
		badUpdate.hasTime = true;
		badUpdate.time = -1.0;
		HostTest::Expect(Set(book, "a", badUpdate) == Error::BadTime, "invalid time refused");
		const LayerSpec* a = Find(book, "a");
		HostTest::Expect(a != nullptr && a->speed == 1.0f && a->seekSerial == 0, "a refused update changes nothing");
	}

	// ---- Cap, clear, order, new ids ------------------------------------------
	{
		LayerBook book;
		char name[8] = "layer0";
		for (std::size_t i = 0; i < kMaxLayers; ++i)
		{
			name[5] = static_cast<char>('0' + i);
			HostTest::Expect(Set(book, name) == Error::None, std::string("create ") + name);
		}
		HostTest::Expect(Set(book, "ninth") == Error::Full, "ninth layer refused");
		HostTest::Expect(Set(book, "layer3") == Error::None, "updating at the cap is allowed");

		const std::uint32_t oldId = Find(book, "layer3")->id;
		HostTest::Expect(book.Clear("layer3", 6) == Error::None, "clear layer3");
		HostTest::Expect(book.Clear("layer3", 6) == Error::NotFound, "clear twice is NotFound");
		HostTest::Expect(book.Layers().count == kMaxLayers - 1, "clear frees a slot");
		HostTest::Expect(std::strcmp(book.Layers().layers[3].name, "layer4") == 0, "clear keeps creation order");
		HostTest::Expect(Set(book, "layer3") == Error::None, "re-create after clear");
		HostTest::Expect(Find(book, "layer3")->id != oldId, "re-created layer gets a new id");

		book.ClearAll();
		HostTest::Expect(book.Layers().count == 0, "ClearAll empties the book");
		HostTest::Expect(Set(book, "layer0") == Error::None, "create after ClearAll");
		HostTest::Expect(Find(book, "layer0")->id > oldId, "ids keep counting after ClearAll");
	}

	// ---- Engine-owned guard --------------------------------------------------
	HostTest::Expect(IsEngineOwned("runForward", "runForward"), "same name is engine-owned");
	HostTest::Expect(!IsEngineOwned("barrelSpin", "runForward"), "different name is not");
	HostTest::Expect(!IsEngineOwned("barrelSpin", nullptr), "unknown engine clip never fires the guard");
	HostTest::Expect(!IsEngineOwned("barrelSpin", ""), "empty engine clip never fires the guard");
	HostTest::Expect(!IsEngineOwned("runforward", "runForward"), "the guard is case-sensitive like Ogre");

	// ---- Time math -----------------------------------------------------------
	HostTest::Expect(Near(AdvanceTime(0.0f, 0.1f, 3.0f, 1.0f, true), 0.3f), "loop advances by dt * speed");
	HostTest::Expect(Near(AdvanceTime(0.9f, 0.1f, 3.0f, 1.0f, true), 0.2f), "loop wraps past the end");
	HostTest::Expect(Near(AdvanceTime(0.5f, 1.0f, 2.0f, 1.0f, true), 0.5f), "loop wraps several lengths");
	HostTest::Expect(AdvanceTime(0.5f, 0.5f, 1.0f, 1.0f, true) < 1.0f, "loop never reports length itself");
	HostTest::Expect(Near(AdvanceTime(0.4f, 0.1f, 0.0f, 1.0f, true), 0.4f), "speed 0 holds the clock");
	HostTest::Expect(Near(AdvanceTime(0.9f, 0.1f, 3.0f, 1.0f, false), 1.0f), "non-loop clamps at the end");
	HostTest::Expect(Near(AdvanceTime(1.0f, 0.1f, 3.0f, 1.0f, false), 1.0f), "non-loop stays at the end");
	HostTest::Expect(Near(AdvanceTime(3.5f, 0.0f, 0.0f, 1.0f, true), 0.5f), "a seek past the end wraps when looping");
	HostTest::Expect(Near(AdvanceTime(3.5f, 0.0f, 0.0f, 1.0f, false), 1.0f), "a seek past the end clamps when not");
	HostTest::Expect(AdvanceTime(0.5f, 0.1f, 1.0f, 0.0f, true) == 0.0f, "zero length parks at 0");
	HostTest::Expect(AdvanceTime(0.5f, 0.1f, 1.0f, std::numeric_limits<float>::quiet_NaN(), true) == 0.0f,
		"non-finite length parks at 0");
	HostTest::Expect(Near(AdvanceTime(0.5f, -0.1f, 1.0f, 1.0f, true), 0.5f), "negative dt does not rewind");
	HostTest::Expect(Near(AdvanceTime(0.5f, std::numeric_limits<float>::infinity(), 1.0f, 1.0f, true), 0.5f),
		"infinite dt counts as zero");
	HostTest::Expect(Near(AdvanceTime(std::numeric_limits<float>::quiet_NaN(), 0.1f, 1.0f, 1.0f, true), 0.1f),
		"a NaN clock restarts at 0");
	{
		// Long run: a 3 rev/s spin at 60 Hz for ten minutes stays in range.
		float time = 0.0f;
		bool inRange = true;
		for (int tick = 0; tick < 36000; ++tick)
		{
			time = AdvanceTime(time, 1.0f / 60.0f, 3.0f, 1.0f, true);
			inRange = inRange && time >= 0.0f && time < 1.0f;
		}
		HostTest::Expect(inRange, "looping clock stays in [0, length) over a long run");
	}

	// ---- Blend mode mapping --------------------------------------------------
	HostTest::Expect(BlendModeFromOgre(0) == BlendMode::Average, "Ogre 0 is average");
	HostTest::Expect(BlendModeFromOgre(1) == BlendMode::Cumulative, "Ogre 1 is cumulative");
	HostTest::Expect(BlendModeFromOgre(7) == BlendMode::Unknown, "other values are unknown");
	HostTest::Expect(std::strcmp(BlendModeName(BlendMode::Cumulative), "cumulative") == 0, "cumulative name");
	HostTest::Expect(std::strcmp(BlendModeName(BlendMode::Average), "average") == 0, "average name");

	// ---- Reason names are Lua-visible; pin them --------------------------------
	HostTest::Expect(std::strcmp(ReasonName(Reason::Missing), "missing") == 0, "missing reason");
	HostTest::Expect(std::strcmp(ReasonName(Reason::EngineOwned), "engineOwned") == 0, "engineOwned reason");
	HostTest::Expect(std::strcmp(ReasonName(Reason::Pending), "pending") == 0, "pending reason");
	HostTest::Expect(std::strcmp(ReasonName(Reason::NoFirstPersonEntity), "noFirstPersonEntity") == 0,
		"noFirstPersonEntity reason");
	HostTest::Expect(std::strcmp(ReasonName(Reason::Faulted), "faulted") == 0, "faulted reason");
	HostTest::Expect(std::strcmp(ReasonName(Reason::Unavailable), "unavailable") == 0, "unavailable reason");
	HostTest::Expect(std::strcmp(ReasonName(static_cast<Reason>(99)), "unknown") == 0, "out-of-range reason");
	HostTest::Expect(std::strcmp(ErrorMessage(static_cast<Error>(99)), "unknown error") == 0, "out-of-range error");

	// ---- Hook-side reconciliation --------------------------------------------
	{
		LayerBook book;
		Tracker tracker;
		Reconciliation rec{};

		Set(book, "barrelSpin");
		Set(book, "runForward");
		tracker.Reconcile(book.Layers(), rec);
		HostTest::Expect(tracker.Count() == 2 && rec.removedCount == 0, "two layers tracked, nothing removed");
		HostTest::Expect(std::strcmp(tracker.At(rec.trackedIndex[0]).name, "barrelSpin") == 0, "mapping 0");
		HostTest::Expect(std::strcmp(tracker.At(rec.trackedIndex[1]).name, "runForward") == 0, "mapping 1");

		// The hook's clock persists across ticks and Lua updates.
		tracker.At(rec.trackedIndex[0]).time = 0.6f;
		book.SetSpeed("barrelSpin", 10, 3.0);
		tracker.Reconcile(book.Layers(), rec);
		HostTest::Expect(tracker.At(rec.trackedIndex[0]).time == 0.6f, "speed change keeps the clock");

		// An explicit time is applied once.
		LayerOptions timed{};
		timed.hasTime = true;
		timed.time = 0.1;
		Set(book, "barrelSpin", timed);
		tracker.Reconcile(book.Layers(), rec);
		HostTest::Expect(tracker.At(rec.trackedIndex[0]).time == 0.1f, "seek applied");
		tracker.At(rec.trackedIndex[0]).time = 0.7f;
		tracker.Reconcile(book.Layers(), rec);
		HostTest::Expect(tracker.At(rec.trackedIndex[0]).time == 0.7f, "seek applied only once");

		// Clear -> pending disable on the next tick.
		book.Clear("runForward", 10);
		tracker.Reconcile(book.Layers(), rec);
		HostTest::Expect(rec.removedCount == 1 && std::strcmp(rec.removed[0], "runForward") == 0,
			"cleared layer reported for disabling");
		HostTest::Expect(tracker.Count() == 1, "cleared layer forgotten");
		tracker.Reconcile(book.Layers(), rec);
		HostTest::Expect(rec.removedCount == 0, "removal reported once");

		// Clear + re-create between ticks: old disabled, new starts at 0.
		book.Clear("barrelSpin", 10);
		Set(book, "barrelSpin");
		tracker.Reconcile(book.Layers(), rec);
		HostTest::Expect(rec.removedCount == 1 && std::strcmp(rec.removed[0], "barrelSpin") == 0,
			"re-created layer: old instance disabled");
		HostTest::Expect(tracker.Count() == 1 && tracker.At(rec.trackedIndex[0]).time == 0.0f,
			"re-created layer: new clock at 0");

		// A full turnover of eight layers fits.
		book.ClearAll();
		char name[8] = "layer0";
		for (std::size_t i = 0; i < kMaxLayers; ++i)
		{
			name[5] = static_cast<char>('0' + i);
			Set(book, name);
		}
		tracker.Reconcile(book.Layers(), rec);
		book.ClearAll();
		for (std::size_t i = 0; i < kMaxLayers; ++i)
		{
			name[5] = static_cast<char>('a' + i);
			Set(book, name);
		}
		tracker.Reconcile(book.Layers(), rec);
		HostTest::Expect(rec.removedCount == kMaxLayers && tracker.Count() == kMaxLayers,
			"eight removed and eight added in one tick");
		bool mapped = true;
		for (std::uint32_t i = 0; i < book.Layers().count; ++i)
		{
			mapped = mapped && tracker.At(rec.trackedIndex[i]).id == book.Layers().layers[i].id;
		}
		HostTest::Expect(mapped, "every published layer maps to its tracked entry");

		book.ClearAll();
		tracker.Reconcile(book.Layers(), rec);
		HostTest::Expect(rec.removedCount == kMaxLayers && tracker.Count() == 0, "ClearLayers disables everything");

		tracker.Reset();
		HostTest::Expect(tracker.Count() == 0, "reset tracker is empty");
	}

	// ---- Sequence lock round trip ----------------------------------------------
	{
		Seqlock<LayerSet> lock;
		LayerSet empty{};
		HostTest::Expect(lock.TryRead(empty) && empty.count == 0, "fresh lock reads the empty default");
		LayerBook book;
		Set(book, "barrelSpin");
		lock.Publish(book.Layers());
		LayerSet read{};
		HostTest::Expect(lock.TryRead(read) && read.count == 1 && std::strcmp(read.layers[0].name, "barrelSpin") == 0,
			"published set reads back");
	}

	return HostTest::Finish("first-person layer");
}
