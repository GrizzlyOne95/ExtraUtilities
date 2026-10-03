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

	bool NearLoose(float a, float b)
	{
		return std::fabs(a - b) < 1.0e-3f;
	}

	// The hook's per-tick bookkeeping for one published set, without Ogre:
	// reconcile, then StepLayer every layer against a clip of `length`.
	struct Hook
	{
		Tracker tracker;
		Reconciliation rec{};
		LayerStep steps[kMaxLayers]{};

		void Tick(const LayerSet& set, float dt, float length, bool triggerHeld = false)
		{
			tracker.Reconcile(set, rec);
			for (std::uint32_t i = 0; i < set.count; ++i)
			{
				steps[i] = StepLayer(set.layers[i], tracker.At(rec.trackedIndex[i]), dt, length, triggerHeld);
			}
		}

		TrackedLayer& Layer(std::uint32_t index)
		{
			return tracker.At(rec.trackedIndex[index]);
		}

		// The ResultSet ApplyLocal would publish for `set` after Tick.
		ResultSet Results(const LayerSet& set)
		{
			ResultSet results{};
			results.count = set.count;
			for (std::uint32_t i = 0; i < set.count; ++i)
			{
				const TrackedLayer& tracked = Layer(i);
				LayerResult& result = results.layers[i];
				result.id = set.layers[i].id;
				result.reason = steps[i].reason;
				result.time = tracked.time;
				result.weight = tracked.lastWeight;
				result.effectiveSpeed = tracked.effectiveSpeed;
				result.finished = tracked.finished;
				result.finishedCount = tracked.finishedCount;
				result.playCount = tracked.playCount;
				result.clearSerial = tracked.clearDoneSerial;
			}
			return results;
		}
	};
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

	// ---- Fade validation -------------------------------------------------------
	HostTest::Expect(IsValidFade(0.0) && IsValidFade(0.5) && IsValidFade(kMaxFadeSeconds), "fade 0..max accepted");
	HostTest::Expect(!IsValidFade(-0.1) && !IsValidFade(nan) && !IsValidFade(inf) && !IsValidFade(kMaxFadeSeconds * 2.0),
		"bad fade refused");
	{
		LayerBook book;
		LayerOptions badFade{};
		badFade.hasFadeIn = true;
		badFade.fadeIn = -1.0;
		HostTest::Expect(Set(book, "pose", badFade) == Error::BadFade, "negative fadeIn refused");
		HostTest::Expect(book.Layers().count == 0, "refused fadeIn creates nothing");
		Set(book, "pose");
		HostTest::Expect(book.SetWeight("pose", 4, 0.5, nan) == Error::BadFade, "NaN weight fade refused");
		HostTest::Expect(book.Clear("pose", 4, true, -1.0) == Error::BadFade, "negative clear fade refused");
		HostTest::Expect(book.Layers().count == 1, "refused clear keeps the layer");
		HostTest::Expect(book.SetBase(2.0, 0.0) == Error::BadWeight && book.SetBase(0.5, -1.0) == Error::BadFade,
			"SetBase validates");
	}

	// ---- Weight ramp math ------------------------------------------------------
	{
		WeightRamp ramp{};
		ramp.Start(0.0f, 1.0f, 0.5f);
		HostTest::Expect(!ramp.Done() && ramp.Value() == 0.0f, "ramp starts at from");
		ramp.Step(0.25f);
		HostTest::Expect(Near(ramp.Value(), 0.5f), "ramp is linear");
		ramp.Step(1.0f);
		HostTest::Expect(ramp.Done() && ramp.Value() == 1.0f, "ramp clamps at to");
		ramp.Start(0.3f, 0.8f, 0.0f);
		HostTest::Expect(ramp.Done() && ramp.Value() == 0.8f, "zero-duration ramp is a step");
		ramp.Start(std::numeric_limits<float>::quiet_NaN(), 0.4f, 1.0f);
		HostTest::Expect(ramp.Value() == 0.4f, "NaN from snaps to the target");
		ramp.Start(0.0f, 1.0f, 1.0f);
		ramp.Step(-1.0f);
		ramp.Step(std::numeric_limits<float>::quiet_NaN());
		HostTest::Expect(ramp.Value() == 0.0f, "bad dt does not advance a ramp");
	}

	// ---- Fade-in, weight fade, fade-out clear ---------------------------------
	{
		LayerBook book;
		Hook hook;
		LayerOptions fadeIn{};
		fadeIn.hasWeight = true;
		fadeIn.weight = 1.0;
		fadeIn.hasFadeIn = true;
		fadeIn.fadeIn = 0.5;
		Set(book, "pose", fadeIn);
		hook.Tick(book.Layers(), 0.1f, 1.0f);
		HostTest::Expect(hook.steps[0].action == StepAction::Apply && NearLoose(hook.steps[0].weight, 0.2f),
			"a new layer fades in from 0");
		for (int i = 0; i < 4; ++i)
		{
			hook.Tick(book.Layers(), 0.1f, 1.0f);
		}
		HostTest::Expect(NearLoose(hook.steps[0].weight, 1.0f), "fade-in reaches the target weight");
		hook.Tick(book.Layers(), 0.1f, 1.0f);
		HostTest::Expect(hook.steps[0].weight == 1.0f, "fade-in holds at the target");

		// Re-applying SetLayer without weight or fadeIn does not restart a ramp.
		const std::uint32_t serial = book.Layers().layers[0].weightSerial;
		LayerOptions speedOnly{};
		speedOnly.hasSpeed = true;
		speedOnly.speed = 2.0;
		Set(book, "pose", speedOnly);
		HostTest::Expect(book.Layers().layers[0].weightSerial == serial, "speed-only update keeps the weight ramp");

		HostTest::Expect(book.SetWeight("pose", 4, 0.5, 1.0) == Error::None, "SetWeight with a fade");
		for (int i = 0; i < 5; ++i)
		{
			hook.Tick(book.Layers(), 0.1f, 1.0f);
		}
		HostTest::Expect(NearLoose(hook.steps[0].weight, 0.75f), "weight fade is linear from the applied weight");

		HostTest::Expect(book.Clear("pose", 4, true, 0.2) == Error::None, "fade-out clear accepted");
		HostTest::Expect(book.Layers().count == 1 && book.Layers().layers[0].clearing, "fade-out clear keeps the entry");
		HostTest::Expect(TargetWeight(book.Layers().layers[0]) == 0.0f, "a clearing layer targets weight 0");
		hook.Tick(book.Layers(), 0.1f, 1.0f);
		HostTest::Expect(hook.steps[0].action == StepAction::Apply && NearLoose(hook.steps[0].weight, 0.375f),
			"fade-out ramps from the applied weight");
		ResultSet midway = hook.Results(book.Layers());
		HostTest::Expect(!book.Prune(midway, true), "a clear still fading is not pruned");
		hook.Tick(book.Layers(), 0.1f, 1.0f);
		HostTest::Expect(hook.steps[0].action == StepAction::Disable && hook.steps[0].reason == Reason::Cleared,
			"fade-out end disables the state");
		hook.Tick(book.Layers(), 0.1f, 1.0f);
		HostTest::Expect(hook.steps[0].action == StepAction::Skip && hook.steps[0].reason == Reason::Cleared,
			"a cleared layer is left alone");
		const ResultSet done = hook.Results(book.Layers());
		HostTest::Expect(book.Prune(done, false) && book.Layers().count == 0, "completed clear is pruned");
		HostTest::Expect(std::strcmp(ReasonName(Reason::Cleared), "cleared") == 0, "cleared reason");
	}
	{
		// Without a fade argument the layer's fadeOut applies; allowFade false
		// (no seam) removes at once; reviving cancels a fade-out clear.
		LayerBook book;
		LayerOptions withFadeOut{};
		withFadeOut.hasFadeOut = true;
		withFadeOut.fadeOut = 0.3;
		Set(book, "pose", withFadeOut);
		HostTest::Expect(book.Clear("pose", 4) == Error::None && book.Layers().layers[0].clearing &&
			NearLoose(book.Layers().layers[0].weightFade, 0.3f), "ClearLayer defaults to the layer's fadeOut");
		Set(book, "pose");
		HostTest::Expect(book.Layers().count == 1 && !book.Layers().layers[0].clearing, "SetLayer revives a clearing layer");
		HostTest::Expect(book.Clear("pose", 4, true, 0.0) == Error::None && book.Layers().count == 0,
			"an explicit zero fade removes at once");
		Set(book, "pose", withFadeOut);
		HostTest::Expect(book.Clear("pose", 4, false, 0.0, false) == Error::None && book.Layers().count == 0,
			"no fades allowed: removed at once");

		// A stale Cleared result never drops a revived-then-recleared layer.
		Hook hook;
		Set(book, "pose", withFadeOut);
		book.Clear("pose", 4);
		for (int i = 0; i < 5; ++i)
		{
			hook.Tick(book.Layers(), 0.1f, 1.0f);
		}
		const ResultSet stale = hook.Results(book.Layers());
		Set(book, "pose");
		book.Clear("pose", 4);
		HostTest::Expect(!book.Prune(stale, false) && book.Layers().count == 1, "stale clear result ignored");
		hook.Tick(book.Layers(), 0.1f, 1.0f);
		HostTest::Expect(hook.steps[0].action == StepAction::Apply, "re-cleared layer fades again");
	}

	// ---- One-shots -------------------------------------------------------------
	HostTest::Expect(Near(OneShotFadeStart(1.0f, 1.0f, 0.25f), 0.75f), "end fade starts fadeOut before the end");
	HostTest::Expect(Near(OneShotFadeStart(1.0f, 2.0f, 0.25f), 0.5f), "end fade start scales with speed");
	HostTest::Expect(OneShotFadeStart(1.0f, 1.0f, 0.0f) == 1.0f, "no end fade: the length");
	HostTest::Expect(OneShotFadeStart(1.0f, 1.0f, 5.0f) == 0.0f, "long end fade starts at 0");
	HostTest::Expect(OneShotEndFactor(0.5f, 1.0f, 1.0f, 0.0f) == 1.0f, "no end fade: factor 1");
	HostTest::Expect(OneShotEndFactor(1.0f, 1.0f, 1.0f, 0.0f) == 1.0f, "no end fade holds the last frame");
	HostTest::Expect(Near(OneShotEndFactor(0.875f, 1.0f, 1.0f, 0.25f), 0.5f), "end factor halfway through the fade");
	HostTest::Expect(OneShotEndFactor(1.0f, 1.0f, 1.0f, 0.25f) == 0.0f, "end factor 0 at the end");
	HostTest::Expect(Near(OneShotEndFactor(0.5f, 1.0f, 1.0f, 2.0f), 0.5f), "a fade longer than the clip spans it");
	{
		LayerBook book;
		Hook hook;
		PlayOptions play{};
		play.fadeOut = 0.25;
		HostTest::Expect(book.Play("reload", 6, play) == Error::None, "PlayLayer creates a one-shot");
		const LayerSpec& spec = book.Layers().layers[0];
		HostTest::Expect(spec.oneShot && !spec.loop && spec.clearOnEnd && spec.playCount == 1 && spec.seekSerial == 1,
			"one-shot defaults");

		for (int i = 0; i < 7; ++i)
		{
			hook.Tick(book.Layers(), 0.125f, 1.0f);
		}
		HostTest::Expect(hook.steps[0].action == StepAction::Apply && NearLoose(hook.Layer(0).time, 0.875f) &&
			NearLoose(hook.steps[0].weight, 0.5f), "one-shot end fade");
		HostTest::Expect(!hook.Layer(0).finished && hook.Layer(0).finishedCount == 0, "not finished before the end");
		hook.Tick(book.Layers(), 0.125f, 1.0f);
		HostTest::Expect(hook.steps[0].action == StepAction::Disable && hook.steps[0].reason == Reason::Ended,
			"clearOnEnd one-shot disabled at its end");
		HostTest::Expect(hook.Layer(0).finished && hook.Layer(0).finishedCount == 1, "finishedCount names the play");
		hook.Tick(book.Layers(), 0.125f, 1.0f);
		HostTest::Expect(hook.steps[0].action == StepAction::Skip && hook.steps[0].reason == Reason::Ended,
			"an ended one-shot is left alone");

		const ResultSet ended = hook.Results(book.Layers());
		HostTest::Expect(!book.Prune(ended, false) && book.Layers().count == 1, "ended one-shot kept until a slot is needed");

		// Replay restarts from 0 with a new playCount.
		HostTest::Expect(book.Play("reload", 6, PlayOptions{}) == Error::None, "replay");
		HostTest::Expect(book.Layers().layers[0].playCount == 2, "replay bumps playCount");
		HostTest::Expect(!book.Prune(ended, true) && book.Layers().count == 1, "a stale Ended result never drops a replay");
		hook.Tick(book.Layers(), 0.125f, 1.0f);
		HostTest::Expect(hook.steps[0].action == StepAction::Apply && NearLoose(hook.Layer(0).time, 0.125f) &&
			hook.steps[0].weight == 1.0f, "replay runs again from 0");
		HostTest::Expect(hook.Layer(0).finishedCount == 1 && !hook.Layer(0).finished, "replay is not finished yet");
		for (int i = 0; i < 7; ++i)
		{
			hook.Tick(book.Layers(), 0.125f, 1.0f);
		}
		HostTest::Expect(hook.Layer(0).finishedCount == 2 && hook.steps[0].reason == Reason::Ended, "second play finished");
		const ResultSet ended2 = hook.Results(book.Layers());
		HostTest::Expect(book.Prune(ended2, true) && book.Layers().count == 0, "ended one-shot evicted when a slot is needed");
	}
	{
		// clearOnEnd = false holds the last frame at full weight.
		LayerBook book;
		Hook hook;
		PlayOptions hold{};
		hold.clearOnEnd = false;
		hold.hasSpeed = true;
		hold.speed = 2.0;
		book.Play("ads", 3, hold);
		for (int i = 0; i < 6; ++i)
		{
			hook.Tick(book.Layers(), 0.1f, 1.0f);
		}
		HostTest::Expect(hook.steps[0].action == StepAction::Apply && hook.steps[0].weight == 1.0f &&
			hook.Layer(0).time == 1.0f, "held one-shot stays applied at its last frame");
		HostTest::Expect(hook.Layer(0).finished && hook.Layer(0).finishedCount == 1, "held one-shot reports finished");
		HostTest::Expect(!book.Prune(hook.Results(book.Layers()), true), "held one-shot is never evicted");

		// SetLayer loop=true turns it back into a looping layer.
		LayerOptions loop{};
		loop.hasLoop = true;
		loop.loop = true;
		Set(book, "ads", loop);
		HostTest::Expect(!book.Layers().layers[0].oneShot && book.Layers().layers[0].loop, "loop=true ends one-shot mode");
	}
	{
		// The 8-slot cap: an ended clearOnEnd one-shot makes room.
		LayerBook book;
		Hook hook;
		char name[8] = "layer0";
		for (std::size_t i = 0; i + 1 < kMaxLayers; ++i)
		{
			name[5] = static_cast<char>('0' + i);
			Set(book, name);
		}
		book.Play("reload", 6, PlayOptions{});
		HostTest::Expect(Set(book, "ninth") == Error::Full, "full book refuses a ninth layer");
		hook.Tick(book.Layers(), 2.0f, 1.0f);
		const ResultSet results = hook.Results(book.Layers());
		HostTest::Expect(book.Prune(results, true) && Set(book, "ninth") == Error::None,
			"evicting the ended one-shot frees a slot");
	}

	// ---- Trigger speed ramp ----------------------------------------------------
	HostTest::Expect(Near(SpeedRampRate(3.0f, 0.0f, 0.4f), 7.5f), "spin-up rate covers the span");
	HostTest::Expect(SpeedRampRate(3.0f, 0.0f, 0.0f) == kInstantRate, "zero seconds is instant");
	HostTest::Expect(SpeedRampRate(3.0f, 3.0f, 1.0f) == kInstantRate, "no span is instant");
	HostTest::Expect(StepToward(0.0f, 3.0f, kInstantRate, 0.1f) == 3.0f, "instant rate snaps");
	HostTest::Expect(StepToward(1.0f, 3.0f, 2.0f, 0.0f) == 1.0f, "zero dt holds");
	HostTest::Expect(StepToward(2.9f, 3.0f, 2.0f, 0.1f) == 3.0f, "StepToward never overshoots up");
	HostTest::Expect(StepToward(0.1f, 0.0f, 2.0f, 0.1f) == 0.0f, "StepToward never overshoots down");
	{
		LayerBook book;
		LayerOptions badFire{};
		badFire.hasFire = true;
		badFire.fire.speed = 51.0;
		HostTest::Expect(Set(book, "barrelSpin", badFire) == Error::BadFire, "fire.speed above 50 refused");
		badFire.fire.speed = 3.0;
		badFire.fire.spinUp = -0.1;
		HostTest::Expect(Set(book, "barrelSpin", badFire) == Error::BadFire, "negative spinUp refused");
		badFire.fire.spinUp = 0.0;
		badFire.fire.spinDown = nan;
		HostTest::Expect(Set(book, "barrelSpin", badFire) == Error::BadFire, "NaN spinDown refused");
		HostTest::Expect(book.Layers().count == 0, "refused fire creates nothing");
	}
	{
		// The runtime check's layer: speed 0, fire = { speed 3, spinUp 0.4, spinDown 1.2 }.
		LayerBook book;
		Hook hook;
		LayerOptions spin{};
		spin.hasSpeed = true;
		spin.speed = 0.0;
		spin.hasLoop = true;
		spin.loop = true;
		spin.hasFire = true;
		spin.fire.speed = 3.0;
		spin.fire.spinUp = 0.4;
		spin.fire.spinDown = 1.2;
		HostTest::Expect(Set(book, "barrelSpin", spin) == Error::None, "fire layer accepted");
		const LayerSpec& spec = book.Layers().layers[0];
		HostTest::Expect(spec.fire.enabled && spec.fire.speed == 3.0f && Near(spec.fire.spinUp, 0.4f) &&
			Near(spec.fire.spinDown, 1.2f), "fire spec stored");

		hook.Tick(book.Layers(), 0.1f, 1.0f, false);
		HostTest::Expect(hook.Layer(0).effectiveSpeed == 0.0f && hook.Layer(0).time == 0.0f, "released: parked at base 0");

		hook.Tick(book.Layers(), 0.1f, 1.0f, true);
		HostTest::Expect(NearLoose(hook.Layer(0).effectiveSpeed, 0.75f), "spin-up: 3 rev/s over 0.4 s");
		HostTest::Expect(NearLoose(hook.Layer(0).time, 0.075f), "the clock runs at the ramped speed");
		for (int i = 0; i < 3; ++i)
		{
			hook.Tick(book.Layers(), 0.1f, 1.0f, true);
		}
		HostTest::Expect(NearLoose(hook.Layer(0).effectiveSpeed, 3.0f), "spin-up reaches fire.speed in spinUp");
		hook.Tick(book.Layers(), 0.1f, 1.0f, true);
		HostTest::Expect(hook.Layer(0).effectiveSpeed == 3.0f, "held: stays at fire.speed");

		hook.Tick(book.Layers(), 0.1f, 1.0f, false);
		HostTest::Expect(NearLoose(hook.Layer(0).effectiveSpeed, 2.75f), "spin-down: 3 rev/s over 1.2 s");
		for (int i = 0; i < 5; ++i)
		{
			hook.Tick(book.Layers(), 0.1f, 1.0f, false);
		}
		HostTest::Expect(NearLoose(hook.Layer(0).effectiveSpeed, 1.5f), "spin-down halfway after 0.6 s");
		hook.Tick(book.Layers(), 0.1f, 1.0f, true);
		HostTest::Expect(NearLoose(hook.Layer(0).effectiveSpeed, 2.25f), "re-press spins up from the current speed");
		for (int i = 0; i < 20; ++i)
		{
			hook.Tick(book.Layers(), 0.1f, 1.0f, false);
		}
		HostTest::Expect(hook.Layer(0).effectiveSpeed == 0.0f, "spin-down settles on the base speed");
		const float parked = hook.Layer(0).time;
		hook.Tick(book.Layers(), 0.1f, 1.0f, false);
		HostTest::Expect(hook.Layer(0).time == parked, "spun down: the clock holds");
		HostTest::Expect(hook.Results(book.Layers()).layers[0].effectiveSpeed == 0.0f, "effectiveSpeed reported");

		// A non-zero base speed is where the trigger ramps back to.
		book.SetSpeed("barrelSpin", 10, 1.0);
		hook.Tick(book.Layers(), 0.1f, 1.0f, false);
		HostTest::Expect(NearLoose(hook.Layer(0).effectiveSpeed, 0.1666667f), "released ramps toward the new base speed");
		for (int i = 0; i < 20; ++i)
		{
			hook.Tick(book.Layers(), 0.1f, 1.0f, true);
		}
		HostTest::Expect(hook.Layer(0).effectiveSpeed == 3.0f, "held over a base of 1 still reaches fire.speed");

		// Instant spin-up, then fire = false snaps back to the base speed.
		LayerOptions instant{};
		instant.hasFire = true;
		instant.fire.speed = 5.0;
		Set(book, "barrelSpin", instant);
		hook.Tick(book.Layers(), 0.1f, 1.0f, true);
		HostTest::Expect(hook.Layer(0).effectiveSpeed == 5.0f, "spinUp 0 is instant");
		LayerOptions off{};
		off.hasFire = true;
		off.fire.enabled = false;
		Set(book, "barrelSpin", off);
		HostTest::Expect(!book.Layers().layers[0].fire.enabled, "fire = false removes the drive");
		hook.Tick(book.Layers(), 0.1f, 1.0f, true);
		HostTest::Expect(hook.Layer(0).effectiveSpeed == 1.0f, "without fire the base speed applies even when held");
	}

	// ---- Base (FSM clip) weight ------------------------------------------------
	{
		LayerBook book;
		BaseTracker base;
		BasePlan plan{};
		int entityA = 0;
		int entityB = 0;

		HostTest::Expect(base.Step(book.Layers().base, 0.1f) == 1.0f, "base weight defaults to 1");
		base.Plan(&entityA, "stand", 1.0f, plan);
		HostTest::Expect(!plan.apply && !plan.restore, "base 1 and nothing dirty: no writes");

		book.SetBase(0.0, 0.5);
		float weight = base.Step(book.Layers().base, 0.25f);
		HostTest::Expect(NearLoose(weight, 0.5f), "base weight fades");
		base.Plan(&entityA, "stand", weight, plan);
		HostTest::Expect(plan.apply && std::strcmp(plan.applyName, "stand") == 0 && NearLoose(plan.weight, 0.5f),
			"base weight set on the FSM clip");

		weight = base.Step(book.Layers().base, 0.25f);
		base.Plan(&entityA, "run", weight, plan);
		HostTest::Expect(plan.restore && std::strcmp(plan.restoreName, "stand") == 0, "previous FSM clip restored to 1");
		HostTest::Expect(plan.apply && std::strcmp(plan.applyName, "run") == 0 && plan.weight == 0.0f,
			"new FSM clip gets the base weight");

		book.SetBase(1.0, 0.0);
		weight = base.Step(book.Layers().base, 0.1f);
		base.Plan(&entityA, "run", weight, plan);
		HostTest::Expect(plan.apply && plan.weight == 1.0f, "returning to 1 writes 1 once");
		base.Plan(&entityA, "run", weight, plan);
		HostTest::Expect(!plan.apply && !plan.restore, "then nothing");

		book.SetBase(0.2, 0.0);
		weight = base.Step(book.Layers().base, 0.1f);
		base.Plan(&entityA, "run", weight, plan);
		base.Plan(&entityB, "run", weight, plan);
		HostTest::Expect(!plan.restore && plan.apply, "a new entity never restores on the old one");

		book.ResetAll();
		HostTest::Expect(book.Layers().base.weight == 1.0f && base.Step(book.Layers().base, 0.0f) == 1.0f,
			"mission reset returns the base weight to 1");
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
