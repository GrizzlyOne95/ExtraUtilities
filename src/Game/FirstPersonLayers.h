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

// EXU-clocked first-person animation layers (exu.fps.SetLayer & co).
//
// Person::Simulate advances only the clip its FSM is currently playing
// (Docs/Research/PILOT_CROUCH_NATIVE_RE_20261003.md). A layer is a named
// clip on the local pilot's first-person skeleton that EXU keeps enabled and
// advances itself, once per local Person::Simulate call, independently of
// the FSM (e.g. a minigun barrel spin, an ADS pose, a reload one-shot).
//
// On top of the clock, the hook ramps each layer's weight natively (fades),
// plays non-looping one-shots that fade out exactly at their end and remove
// themselves, ramps a layer's speed toward a trigger speed while the fire
// bind is held, and ramps the weight of the FSM's own current clip (the base
// weight), so a cumulative-blend rig can cross-fade from the FSM pose to a
// layer pose.
//
// This header is the pure bookkeeping: validation, the 8-layer cap,
// create/update/clear/play, the hook-side reconciliation of what Lua
// published with what the hook last applied, the engine-owned guard, the
// time/fade/speed math, the base-weight plan, and the lock-free handoff
// between Lua and the hook. The Ogre side lives in FirstPersonLayers.cpp and
// runs inside PilotFsmIntercept's hook.
//
// No Windows, Ogre, Lua, or engine dependencies: host-testable on Linux.

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ExtraUtilities::Lua::FirstPersonLayers
{
	constexpr std::size_t kMaxLayers = 8;
	// Longest layer (Ogre animation) name, excluding the terminator.
	constexpr std::size_t kMaxLayerName = 63;
	// Clip-seconds per second. Negative speeds are refused: Ogre's looping
	// wrap handles them, but a non-looping clip would sit at 0 forever and
	// nothing in the use case needs reverse play.
	constexpr double kMaxSpeed = 50.0;
	// Longest fade, spin-up or spin-down, in seconds. Only a sanity bound so
	// the float narrowing stays finite.
	constexpr double kMaxFadeSeconds = 1.0e6;

	// ----- What Lua asks for ---------------------------------------------------

	// SetLayer's `fire` option: while the local fire bind is held the layer's
	// effective speed ramps linearly toward speed (over spinUp seconds for the
	// whole span from the base speed), and back toward the base speed when it
	// is released (spinDown). 0 seconds = instant.
	struct FireSpec
	{
		bool enabled = false;
		float speed = 0.0f;
		float spinUp = 0.0f;
		float spinDown = 0.0f;
	};

	struct LayerSpec
	{
		// NUL-terminated Ogre animation name on the first-person skeleton.
		char name[kMaxLayerName + 1]{};
		// Unique per creation (never 0 for a live layer). A cleared and
		// re-created layer of the same name is a new layer to the hook: the
		// old one is disabled and reset, the new one starts at time 0.
		std::uint32_t id = 0;
		// The base speed (SetLayerSpeed); `fire` ramps the effective speed.
		float speed = 1.0f;
		// The requested (target) weight. The hook ramps the applied weight
		// toward it; a pending fade-out clear targets 0 instead.
		float weight = 1.0f;
		bool loop = true;
		// Bumped each time Lua sets an explicit time; the hook seeks to
		// seekTime when it sees a serial it has not applied yet.
		std::uint32_t seekSerial = 0;
		float seekTime = 0.0f;
		// Bumped on every weight request; the hook starts a linear ramp from
		// the weight it is applying to TargetWeight() over weightFade seconds.
		std::uint32_t weightSerial = 0;
		float weightFade = 0.0f;
		// SetLayer: the default fade for ClearLayer without fadeSeconds.
		// PlayLayer: the end fade (weight reaches 0 exactly at the clip end).
		float fadeOut = 0.0f;
		// A fade-out ClearLayer in progress: the hook ramps to 0, disables
		// the state and reports Reason::Cleared for this clearSerial; the Lua
		// side then drops the entry.
		bool clearing = false;
		std::uint32_t clearSerial = 0;
		// PlayLayer: a non-looping one-shot. clearOnEnd disables it natively
		// once finished (Reason::Ended) and frees its slot for reuse.
		bool oneShot = false;
		bool clearOnEnd = true;
		// PlayLayer calls on this layer (each restarts it).
		std::uint32_t playCount = 0;
		FireSpec fire{};
	};

	inline float TargetWeight(const LayerSpec& spec) noexcept
	{
		return spec.clearing ? 0.0f : spec.weight;
	}

	// exu.fps.SetBaseWeight: the weight EXU applies to the clip the FSM is
	// currently playing on the first-person entity.
	struct BaseSpec
	{
		float weight = 1.0f;
		float fade = 0.0f;
		std::uint32_t serial = 0;
	};

	struct LayerSet
	{
		LayerSpec layers[kMaxLayers]{};
		std::uint32_t count = 0;
		BaseSpec base{};
	};

	// Copied whole through the sequence lock below, and constant-initialised:
	// EXU statics live in a DLL that is initialised inside DllMain.
	static_assert(std::is_trivially_copyable<LayerSet>::value, "LayerSet must stay trivially copyable");

	enum class Error : std::uint8_t
	{
		None = 0,
		BadName,
		BadSpeed,
		BadWeight,
		BadTime,
		BadFade,
		BadFire,
		Full,
		NotFound,
	};

	inline const char* ErrorMessage(Error error) noexcept
	{
		switch (error)
		{
		case Error::None: return "ok";
		case Error::BadName: return "layer name must be 1..63 characters with no embedded NUL";
		case Error::BadSpeed: return "speed must be a finite number from 0 to 50";
		case Error::BadWeight: return "weight must be a finite number from 0 to 1";
		case Error::BadTime: return "time must be a finite number >= 0";
		case Error::BadFade: return "fade times must be finite numbers of seconds from 0 to 1000000";
		case Error::BadFire: return "fire.speed must be a finite number from 0 to 50 and fire.spinUp/spinDown finite seconds from 0 to 1000000";
		case Error::Full: return "at most 8 first-person layers can exist at once";
		case Error::NotFound: return "no first-person layer has that name";
		}
		return "unknown error";
	}

	inline bool IsValidName(const char* name, std::size_t length) noexcept
	{
		return name != nullptr && length >= 1 && length <= kMaxLayerName &&
			std::memchr(name, '\0', length) == nullptr;
	}

	inline bool IsValidSpeed(double speed) noexcept
	{
		return std::isfinite(speed) && speed >= 0.0 && speed <= kMaxSpeed;
	}

	inline bool IsValidWeight(double weight) noexcept
	{
		return std::isfinite(weight) && weight >= 0.0 && weight <= 1.0;
	}

	inline bool IsValidTime(double time) noexcept
	{
		// Bounded by float range so the narrowing below stays finite.
		return std::isfinite(time) && time >= 0.0 && time <= 1.0e30;
	}

	inline bool IsValidFade(double seconds) noexcept
	{
		return std::isfinite(seconds) && seconds >= 0.0 && seconds <= kMaxFadeSeconds;
	}

	struct FireOptions
	{
		// false: `fire = false`, removes the trigger drive.
		bool enabled = true;
		double speed = 0.0;
		double spinUp = 0.0;
		double spinDown = 0.0;
	};

	// SetLayer's options. A field that is not present keeps the layer's
	// current value (its default on creation: speed 1, weight 1, loop true,
	// time 0, fadeOut 0, no fire). fadeIn is per call: the ramp time from the
	// weight being applied (0 for a new layer) to the target weight.
	struct LayerOptions
	{
		bool hasSpeed = false;
		double speed = 1.0;
		bool hasWeight = false;
		double weight = 1.0;
		bool hasLoop = false;
		bool loop = true;
		bool hasTime = false;
		double time = 0.0;
		bool hasFadeIn = false;
		double fadeIn = 0.0;
		bool hasFadeOut = false;
		double fadeOut = 0.0;
		bool hasFire = false;
		FireOptions fire{};
	};

	// PlayLayer's options. speed and weight are sticky like SetLayer's
	// (defaults 1 on creation); fadeIn, fadeOut and clearOnEnd apply to this
	// play only (defaults 0, 0, true).
	struct PlayOptions
	{
		bool hasSpeed = false;
		double speed = 1.0;
		bool hasWeight = false;
		double weight = 1.0;
		double fadeIn = 0.0;
		double fadeOut = 0.0;
		bool clearOnEnd = true;
	};

	// ----- What the hook reports ---------------------------------------------

	// Why a layer was not applied on the most recent local tick. Active means
	// it was.
	enum class Reason : std::uint8_t
	{
		Active = 0,
		// Published but no local pilot tick has processed it yet (e.g. the
		// player is in a vehicle).
		Pending,
		// The first-person skeleton has no animation of that name.
		Missing,
		// The FSM itself is playing this clip this tick; EXU leaves it alone.
		EngineOwned,
		// The local pilot has no first-person entity this tick.
		NoFirstPersonEntity,
		// An Ogre call faulted; layers are off for the rest of the Lua state.
		Faulted,
		// The Person::Simulate seam is not active, so nothing advances layers.
		Unavailable,
		// A clearOnEnd one-shot finished and was disabled; its slot is reused
		// when a new layer needs one. PlayLayer starts it again.
		Ended,
		// A fade-out clear finished and the state was disabled; the entry is
		// dropped at the next exu.fps layer call.
		Cleared,
	};

	inline const char* ReasonName(Reason reason) noexcept
	{
		switch (reason)
		{
		case Reason::Active: return "active";
		case Reason::Pending: return "pending";
		case Reason::Missing: return "missing";
		case Reason::EngineOwned: return "engineOwned";
		case Reason::NoFirstPersonEntity: return "noFirstPersonEntity";
		case Reason::Faulted: return "faulted";
		case Reason::Unavailable: return "unavailable";
		case Reason::Ended: return "ended";
		case Reason::Cleared: return "cleared";
		}
		return "unknown";
	}

	// Ogre::SkeletonAnimationBlendMode of the first-person skeleton.
	enum class BlendMode : std::uint8_t
	{
		Unknown = 0,
		Average,
		Cumulative,
	};

	inline const char* BlendModeName(BlendMode mode) noexcept
	{
		switch (mode)
		{
		case BlendMode::Unknown: return "unknown";
		case BlendMode::Average: return "average";
		case BlendMode::Cumulative: return "cumulative";
		}
		return "unknown";
	}

	// Ogre's enum values: ANIMBLEND_AVERAGE = 0, ANIMBLEND_CUMULATIVE = 1.
	constexpr BlendMode BlendModeFromOgre(int value) noexcept
	{
		return value == 0 ? BlendMode::Average
			: value == 1 ? BlendMode::Cumulative
			: BlendMode::Unknown;
	}

	struct LayerResult
	{
		std::uint32_t id = 0;
		Reason reason = Reason::Pending;
		BlendMode blendMode = BlendMode::Unknown;
		// EXU's clock for the layer, seconds into the clip.
		float time = 0.0f;
		// Clip length; 0 until the clip has been found.
		float length = 0.0f;
		// The weight applied this tick (ramp x one-shot end fade).
		float weight = 0.0f;
		// The speed the clock advanced at this tick (base or fire-ramped).
		float effectiveSpeed = 0.0f;
		// A non-looping layer whose clock is at the clip end.
		bool finished = false;
		// playCount of the most recent play that ran to its end (0 = none).
		std::uint32_t finishedCount = 0;
		// The playCount the hook has seen.
		std::uint32_t playCount = 0;
		// clearSerial of the fade-out clear the hook completed (0 = none).
		std::uint32_t clearSerial = 0;
	};

	struct ResultSet
	{
		LayerResult layers[kMaxLayers]{};
		std::uint32_t count = 0;
		// The base weight the hook is applying (ramped).
		float baseWeight = 1.0f;
		// The local fire bind, as sampled by the hook this tick.
		bool triggerHeld = false;
	};

	static_assert(std::is_trivially_copyable<ResultSet>::value, "ResultSet must stay trivially copyable");

	inline const LayerResult* FindResult(const ResultSet& results, std::uint32_t id) noexcept
	{
		for (std::uint32_t i = 0; i < results.count && i < kMaxLayers; ++i)
		{
			if (results.layers[i].id == id)
			{
				return &results.layers[i];
			}
		}
		return nullptr;
	}

	// ----- The Lua thread's layer list --------------------------------------

	// Every operation validates everything first and changes nothing on
	// failure.
	class LayerBook
	{
	public:
		Error Set(const char* name, std::size_t length, const LayerOptions& options) noexcept
		{
			if (!IsValidName(name, length))
			{
				return Error::BadName;
			}
			if (options.hasSpeed && !IsValidSpeed(options.speed))
			{
				return Error::BadSpeed;
			}
			if (options.hasWeight && !IsValidWeight(options.weight))
			{
				return Error::BadWeight;
			}
			if (options.hasTime && !IsValidTime(options.time))
			{
				return Error::BadTime;
			}
			if ((options.hasFadeIn && !IsValidFade(options.fadeIn)) ||
				(options.hasFadeOut && !IsValidFade(options.fadeOut)))
			{
				return Error::BadFade;
			}
			if (options.hasFire && options.fire.enabled &&
				(!IsValidSpeed(options.fire.speed) || !IsValidFade(options.fire.spinUp) ||
					!IsValidFade(options.fire.spinDown)))
			{
				return Error::BadFire;
			}

			LayerSpec* layer = Find(name, length);
			const bool created = layer == nullptr;
			if (created)
			{
				layer = Allocate(name, length);
				if (layer == nullptr)
				{
					return Error::Full;
				}
			}

			if (options.hasSpeed)
			{
				layer->speed = static_cast<float>(options.speed);
			}
			if (options.hasWeight)
			{
				layer->weight = static_cast<float>(options.weight);
			}
			if (options.hasLoop)
			{
				layer->loop = options.loop;
				if (options.loop)
				{
					layer->oneShot = false;
				}
			}
			if (options.hasTime)
			{
				layer->seekTime = static_cast<float>(options.time);
				++layer->seekSerial;
			}
			if (options.hasFadeOut)
			{
				layer->fadeOut = static_cast<float>(options.fadeOut);
			}
			if (options.hasFire)
			{
				layer->fire = FireSpec{};
				if (options.fire.enabled)
				{
					layer->fire.enabled = true;
					layer->fire.speed = static_cast<float>(options.fire.speed);
					layer->fire.spinUp = static_cast<float>(options.fire.spinUp);
					layer->fire.spinDown = static_cast<float>(options.fire.spinDown);
				}
			}
			// A new weight, a fade-in, a new layer, or reviving a layer that
			// was fading out: start a ramp to the (new) target.
			if (created || options.hasWeight || options.hasFadeIn || layer->clearing)
			{
				layer->clearing = false;
				RequestWeight(*layer, options.hasFadeIn ? options.fadeIn : 0.0);
			}
			return Error::None;
		}

		Error SetSpeed(const char* name, std::size_t length, double speed) noexcept
		{
			if (!IsValidName(name, length))
			{
				return Error::BadName;
			}
			if (!IsValidSpeed(speed))
			{
				return Error::BadSpeed;
			}
			LayerSpec* layer = Find(name, length);
			if (layer == nullptr)
			{
				return Error::NotFound;
			}
			layer->speed = static_cast<float>(speed);
			return Error::None;
		}

		// Ramps to weight over fadeSeconds (0 = at once). Cancels a pending
		// fade-out clear.
		Error SetWeight(const char* name, std::size_t length, double weight, double fadeSeconds = 0.0) noexcept
		{
			if (!IsValidName(name, length))
			{
				return Error::BadName;
			}
			if (!IsValidWeight(weight))
			{
				return Error::BadWeight;
			}
			if (!IsValidFade(fadeSeconds))
			{
				return Error::BadFade;
			}
			LayerSpec* layer = Find(name, length);
			if (layer == nullptr)
			{
				return Error::NotFound;
			}
			layer->weight = static_cast<float>(weight);
			layer->clearing = false;
			RequestWeight(*layer, fadeSeconds);
			return Error::None;
		}

		// Removes the layer at once (keeping the order of the others), or,
		// with a fade (fadeSeconds when given, else the layer's fadeOut) and
		// allowFade, marks it clearing: the hook fades it to 0, disables it,
		// and the entry is dropped by Prune once the hook reports it.
		Error Clear(
			const char* name,
			std::size_t length,
			bool hasFade = false,
			double fadeSeconds = 0.0,
			bool allowFade = true) noexcept
		{
			if (!IsValidName(name, length))
			{
				return Error::BadName;
			}
			if (hasFade && !IsValidFade(fadeSeconds))
			{
				return Error::BadFade;
			}
			LayerSpec* layer = Find(name, length);
			if (layer == nullptr)
			{
				return Error::NotFound;
			}
			const double fade = hasFade ? fadeSeconds : static_cast<double>(layer->fadeOut);
			if (!allowFade || !(fade > 0.0))
			{
				RemoveAt(static_cast<std::size_t>(layer - m_set.layers));
				return Error::None;
			}
			layer->clearing = true;
			if (++layer->clearSerial == 0)
			{
				layer->clearSerial = 1;
			}
			RequestWeight(*layer, fade);
			return Error::None;
		}

		// Creates or restarts a non-looping one-shot at time 0.
		Error Play(const char* name, std::size_t length, const PlayOptions& options) noexcept
		{
			if (!IsValidName(name, length))
			{
				return Error::BadName;
			}
			if (options.hasSpeed && !IsValidSpeed(options.speed))
			{
				return Error::BadSpeed;
			}
			if (options.hasWeight && !IsValidWeight(options.weight))
			{
				return Error::BadWeight;
			}
			if (!IsValidFade(options.fadeIn) || !IsValidFade(options.fadeOut))
			{
				return Error::BadFade;
			}

			LayerSpec* layer = Find(name, length);
			if (layer == nullptr)
			{
				layer = Allocate(name, length);
				if (layer == nullptr)
				{
					return Error::Full;
				}
			}
			if (options.hasSpeed)
			{
				layer->speed = static_cast<float>(options.speed);
			}
			if (options.hasWeight)
			{
				layer->weight = static_cast<float>(options.weight);
			}
			layer->loop = false;
			layer->oneShot = true;
			layer->clearOnEnd = options.clearOnEnd;
			layer->fadeOut = static_cast<float>(options.fadeOut);
			layer->clearing = false;
			layer->seekTime = 0.0f;
			++layer->seekSerial;
			++layer->playCount;
			RequestWeight(*layer, options.fadeIn);
			return Error::None;
		}

		Error SetBase(double weight, double fadeSeconds) noexcept
		{
			if (!IsValidWeight(weight))
			{
				return Error::BadWeight;
			}
			if (!IsValidFade(fadeSeconds))
			{
				return Error::BadFade;
			}
			m_set.base.weight = static_cast<float>(weight);
			m_set.base.fade = static_cast<float>(fadeSeconds);
			++m_set.base.serial;
			return Error::None;
		}

		// Drops entries the hook has finished with: fade-out clears it
		// completed, and (evictEnded, when a slot is needed) clearOnEnd
		// one-shots that ended. Matched by id and serial, so a stale result
		// never drops a layer Lua has since revived or replayed. Returns
		// whether anything was dropped.
		bool Prune(const ResultSet& results, bool evictEnded) noexcept
		{
			bool changed = false;
			for (std::size_t i = 0; i < m_set.count;)
			{
				const LayerSpec& spec = m_set.layers[i];
				const LayerResult* result = FindResult(results, spec.id);
				const bool cleared = result != nullptr && spec.clearing &&
					result->reason == Reason::Cleared && result->clearSerial == spec.clearSerial;
				const bool ended = evictEnded && result != nullptr && spec.oneShot && spec.clearOnEnd &&
					result->reason == Reason::Ended && result->playCount == spec.playCount;
				if (cleared || ended)
				{
					RemoveAt(i);
					changed = true;
					continue;
				}
				++i;
			}
			return changed;
		}

		// Ids keep counting, so a layer re-created after this is still new
		// to a hook that has not yet seen the clear. Keeps the base weight.
		void ClearAll() noexcept
		{
			const BaseSpec base = m_set.base;
			m_set = LayerSet{};
			m_set.base = base;
		}

		// Mission reset: no layers, base weight 1.
		void ResetAll() noexcept
		{
			const std::uint32_t serial = m_set.base.serial;
			m_set = LayerSet{};
			m_set.base.serial = serial + 1u;
		}

		const LayerSet& Layers() const noexcept
		{
			return m_set;
		}

	private:
		LayerSpec* Find(const char* name, std::size_t length) noexcept
		{
			for (std::size_t i = 0; i < m_set.count; ++i)
			{
				if (std::strlen(m_set.layers[i].name) == length &&
					std::memcmp(m_set.layers[i].name, name, length) == 0)
				{
					return &m_set.layers[i];
				}
			}
			return nullptr;
		}

		LayerSpec* Allocate(const char* name, std::size_t length) noexcept
		{
			if (m_set.count >= kMaxLayers)
			{
				return nullptr;
			}
			LayerSpec* layer = &m_set.layers[m_set.count++];
			*layer = LayerSpec{};
			std::memcpy(layer->name, name, length);
			layer->name[length] = '\0';
			layer->id = NextId();
			return layer;
		}

		void RemoveAt(std::size_t index) noexcept
		{
			for (std::size_t i = index + 1; i < m_set.count; ++i)
			{
				m_set.layers[i - 1] = m_set.layers[i];
			}
			m_set.layers[--m_set.count] = LayerSpec{};
		}

		static void RequestWeight(LayerSpec& layer, double fadeSeconds) noexcept
		{
			layer.weightFade = static_cast<float>(fadeSeconds);
			if (++layer.weightSerial == 0)
			{
				layer.weightSerial = 1;
			}
		}

		std::uint32_t NextId() noexcept
		{
			if (++m_nextId == 0)
			{
				m_nextId = 1;
			}
			return m_nextId;
		}

		LayerSet m_set{};
		std::uint32_t m_nextId = 0;
	};

	// ----- Engine-owned guard ----------------------------------------------

	// True when the layer names the clip the FSM is playing for the local
	// Person this tick. Person::Simulate already advanced that clip by
	// dt * rate and owns its enable/loop, so EXU skips it entirely rather
	// than advancing it twice (its weight is the base weight's business).
	// engineClip is EXU's bounded copy of the name in force (null when
	// unknown: the guard then does not fire).
	inline bool IsEngineOwned(const char* layerName, const char* engineClip) noexcept
	{
		return layerName != nullptr && engineClip != nullptr && engineClip[0] != '\0' &&
			std::strcmp(layerName, engineClip) == 0;
	}

	// ----- Time math -------------------------------------------------------

	// Where a layer's clock lands after dt seconds at speed. Matches what
	// Ogre's AnimationState::setTimePosition does with the result: a looping
	// clip wraps into [0, length), a non-looping one clamps to [0, length].
	// A bad length (unknown, zero, non-finite) parks the clock at 0; bad dt or
	// time inputs count as zero.
	inline float AdvanceTime(float time, float dt, float speed, float length, bool loop) noexcept
	{
		if (!std::isfinite(length) || length <= 0.0f)
		{
			return 0.0f;
		}
		if (!std::isfinite(time) || time < 0.0f)
		{
			time = 0.0f;
		}
		float step = dt * speed;
		if (!std::isfinite(step) || step < 0.0f)
		{
			step = 0.0f;
		}

		float next = time + step;
		if (!std::isfinite(next))
		{
			next = 0.0f;
		}
		if (loop)
		{
			next = std::fmod(next, length);
			if (next < 0.0f)
			{
				next += length;
			}
			// fmod of a value just under a multiple of length can round up to
			// length itself in float.
			if (next >= length)
			{
				next = 0.0f;
			}
			return next;
		}
		return next > length ? length : next;
	}

	// A non-looping clip whose clock has reached the end. Looping clips and
	// unknown lengths never finish.
	inline bool IsFinished(float time, float length, bool loop) noexcept
	{
		return !loop && std::isfinite(length) && length > 0.0f && std::isfinite(time) && time >= length;
	}

	// ----- Fade math -------------------------------------------------------

	// A linear ramp from `from` to `to` over `duration` seconds of hook time.
	// duration 0 is a step: Value() is `to` at once.
	struct WeightRamp
	{
		float from = 0.0f;
		float to = 0.0f;
		float duration = 0.0f;
		float elapsed = 0.0f;

		void Start(float current, float target, float seconds) noexcept
		{
			from = std::isfinite(current) ? current : target;
			to = target;
			duration = (std::isfinite(seconds) && seconds > 0.0f) ? seconds : 0.0f;
			elapsed = 0.0f;
		}

		void Step(float dt) noexcept
		{
			if (duration <= 0.0f || !std::isfinite(dt) || dt <= 0.0f)
			{
				return;
			}
			elapsed = elapsed + dt >= duration ? duration : elapsed + dt;
		}

		bool Done() const noexcept
		{
			return duration <= 0.0f || elapsed >= duration;
		}

		float Value() const noexcept
		{
			if (Done())
			{
				return to;
			}
			return from + (to - from) * (elapsed / duration);
		}
	};

	// Clip time at which a one-shot's end fade starts: fadeOut real seconds
	// before the end at the given speed (so length - fadeOut at speed 1),
	// never before 0. fadeOut 0 (or speed 0) = no end fade: the length.
	inline float OneShotFadeStart(float length, float speed, float fadeOut) noexcept
	{
		if (!std::isfinite(length) || length <= 0.0f)
		{
			return 0.0f;
		}
		if (!(fadeOut > 0.0f) || !(speed > 0.0f) || !std::isfinite(fadeOut * speed))
		{
			return length;
		}
		const float start = length - fadeOut * speed;
		return start > 0.0f ? start : 0.0f;
	}

	// The one-shot end-fade multiplier: 1 until OneShotFadeStart, then linear
	// in the remaining real time, reaching exactly 0 at the clip end. With no
	// end fade (fadeOut 0) it stays 1, so a finished clip without clearOnEnd
	// holds its last frame at full weight.
	inline float OneShotEndFactor(float time, float length, float speed, float fadeOut) noexcept
	{
		if (!(fadeOut > 0.0f) || !std::isfinite(length) || length <= 0.0f)
		{
			return 1.0f;
		}
		const float remaining = length - (std::isfinite(time) ? time : 0.0f);
		if (remaining <= 0.0f)
		{
			return 0.0f;
		}
		if (!(speed > 0.0f))
		{
			return 1.0f;
		}
		// A fade longer than the clip spans the whole clip.
		const float span = fadeOut * speed < length ? fadeOut * speed : length;
		const float factor = remaining / span;
		return factor < 1.0f ? factor : 1.0f;
	}

	// ----- Trigger speed math ----------------------------------------------

	// Ramp rate for StepToward that is "instant".
	constexpr float kInstantRate = -1.0f;

	// Ramp rate (clip-seconds per second, per second) for a spin-up or
	// spin-down that covers the span between the base and fire speeds in
	// `seconds`. kInstantRate for seconds 0 or no span.
	inline float SpeedRampRate(float fireSpeed, float baseSpeed, float seconds) noexcept
	{
		const float span = std::fabs(fireSpeed - baseSpeed);
		if (!(seconds > 0.0f) || !(span > 0.0f) || !std::isfinite(span / seconds))
		{
			return kInstantRate;
		}
		return span / seconds;
	}

	// Moves current toward target by at most rate * dt. A negative or
	// non-finite rate (kInstantRate), or a non-finite current, snaps to the
	// target; rate 0 or bad dt moves nothing.
	inline float StepToward(float current, float target, float rate, float dt) noexcept
	{
		if (!std::isfinite(current) || !std::isfinite(rate) || rate < 0.0f)
		{
			return target;
		}
		if (!std::isfinite(dt) || dt <= 0.0f || rate == 0.0f)
		{
			return current;
		}
		const float step = rate * dt;
		if (current < target)
		{
			return current + step >= target ? target : current + step;
		}
		return current - step <= target ? target : current - step;
	}

	// The speed the layer's clock runs at after this tick: the base speed at
	// once without `fire`; with it, ramped toward fire.speed while held and
	// back toward the base speed when released.
	inline float NextEffectiveSpeed(const LayerSpec& spec, float current, bool triggerHeld, float dt) noexcept
	{
		if (!spec.fire.enabled)
		{
			return spec.speed;
		}
		const float target = triggerHeld ? spec.fire.speed : spec.speed;
		const float rate = SpeedRampRate(
			spec.fire.speed, spec.speed, triggerHeld ? spec.fire.spinUp : spec.fire.spinDown);
		return StepToward(current, target, rate, dt);
	}

	// ----- Hook-side reconciliation ----------------------------------------

	// What the hook remembers about a layer between ticks. Holds no Ogre
	// pointer: the state is looked up by name every tick. lastMissingEntity
	// is compared for identity only (to log "missing" once per entity).
	struct TrackedLayer
	{
		char name[kMaxLayerName + 1]{};
		std::uint32_t id = 0;
		float time = 0.0f;
		std::uint32_t seekSerial = 0;
		const void* lastMissingEntity = nullptr;
		// Weight ramp toward TargetWeight(spec), restarted per weightSerial.
		WeightRamp ramp{};
		std::uint32_t weightSerial = 0;
		// The weight applied on the last tick (0 once disabled).
		float lastWeight = 0.0f;
		float effectiveSpeed = 0.0f;
		std::uint32_t playCount = 0;
		bool finished = false;
		std::uint32_t finishedCount = 0;
		// A clearOnEnd one-shot that finished and was disabled.
		bool ended = false;
		std::uint32_t clearDoneSerial = 0;
	};

	struct Reconciliation
	{
		// Layers the hook applied before that Lua has since cleared (or
		// replaced with a new layer of the same name): disable and reset them.
		char removed[kMaxLayers][kMaxLayerName + 1]{};
		std::uint32_t removedCount = 0;
		// For each published layer, its index in the tracker.
		std::uint32_t trackedIndex[kMaxLayers]{};
	};

	// Hook-thread memory of applied layers, matched to the published set by
	// id. Removals are processed before additions, so kMaxLayers entries
	// always suffice.
	class Tracker
	{
	public:
		void Reconcile(const LayerSet& published, Reconciliation& out) noexcept
		{
			out.removedCount = 0;
			const std::uint32_t publishedCount =
				published.count <= kMaxLayers ? published.count : static_cast<std::uint32_t>(kMaxLayers);

			std::uint32_t kept = 0;
			for (std::uint32_t i = 0; i < m_count; ++i)
			{
				if (IndexOfId(published, publishedCount, m_layers[i].id) < 0)
				{
					std::memcpy(out.removed[out.removedCount++], m_layers[i].name, sizeof(m_layers[i].name));
					continue;
				}
				m_layers[kept++] = m_layers[i];
			}
			for (std::uint32_t i = kept; i < m_count; ++i)
			{
				m_layers[i] = TrackedLayer{};
			}
			m_count = kept;

			for (std::uint32_t p = 0; p < publishedCount; ++p)
			{
				const LayerSpec& spec = published.layers[p];
				std::uint32_t index = m_count;
				for (std::uint32_t i = 0; i < m_count; ++i)
				{
					if (m_layers[i].id == spec.id)
					{
						index = i;
						break;
					}
				}
				if (index == m_count)
				{
					// Fits: m_count only holds ids still in the published set.
					TrackedLayer& added = m_layers[m_count++];
					added = TrackedLayer{};
					std::memcpy(added.name, spec.name, sizeof(added.name));
					added.name[kMaxLayerName] = '\0';
					added.id = spec.id;
					added.effectiveSpeed = spec.speed;
				}

				TrackedLayer& layer = m_layers[index];
				// A replay restarts from the weight actually on screen (which
				// includes the old play's end fade).
				const bool replayed = layer.playCount != spec.playCount;
				if (replayed)
				{
					layer.playCount = spec.playCount;
					layer.ended = false;
					layer.finished = false;
				}
				if (layer.seekSerial != spec.seekSerial)
				{
					layer.seekSerial = spec.seekSerial;
					layer.time = spec.seekTime;
					layer.ended = false;
				}
				if (layer.ended && !(spec.oneShot && spec.clearOnEnd))
				{
					layer.ended = false;
				}
				if (layer.weightSerial != spec.weightSerial)
				{
					layer.weightSerial = spec.weightSerial;
					layer.ramp.Start(
						replayed ? layer.lastWeight : layer.ramp.Value(),
						TargetWeight(spec),
						spec.weightFade);
				}
				out.trackedIndex[p] = index;
			}
		}

		TrackedLayer& At(std::uint32_t index) noexcept
		{
			return m_layers[index];
		}

		std::uint32_t Count() const noexcept
		{
			return m_count;
		}

		void Reset() noexcept
		{
			*this = Tracker{};
		}

	private:
		static int IndexOfId(const LayerSet& set, std::uint32_t count, std::uint32_t id) noexcept
		{
			for (std::uint32_t i = 0; i < count; ++i)
			{
				if (set.layers[i].id == id)
				{
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		TrackedLayer m_layers[kMaxLayers]{};
		std::uint32_t m_count = 0;
	};

	// ----- One layer, one tick (pure) ----------------------------------------

	enum class StepAction : std::uint8_t
	{
		// Enable, set loop, weight and time on the state.
		Apply,
		// Disable the state and reset it (weight 1, time 0): an ended
		// one-shot or a completed fade-out clear.
		Disable,
		// Leave the state alone (already ended or cleared).
		Skip,
	};

	struct LayerStep
	{
		StepAction action = StepAction::Skip;
		Reason reason = Reason::Active;
		float weight = 0.0f;
	};

	// Everything the hook decides for a found, non-engine-owned layer whose
	// clip is `length` long, in order: trigger speed ramp, clock, weight
	// ramp, one-shot end fade, finish edge, clearOnEnd, fade-out clear.
	inline LayerStep StepLayer(
		const LayerSpec& spec,
		TrackedLayer& tracked,
		float dt,
		float length,
		bool triggerHeld) noexcept
	{
		LayerStep out{};
		if (tracked.ended)
		{
			out.reason = Reason::Ended;
			return out;
		}
		if (spec.clearing && spec.clearSerial != 0 && tracked.clearDoneSerial == spec.clearSerial)
		{
			out.reason = Reason::Cleared;
			return out;
		}

		tracked.effectiveSpeed = NextEffectiveSpeed(spec, tracked.effectiveSpeed, triggerHeld, dt);
		tracked.time = AdvanceTime(tracked.time, dt, tracked.effectiveSpeed, length, spec.loop);
		tracked.ramp.Step(dt);

		float weight = tracked.ramp.Value();
		if (spec.oneShot && !spec.loop)
		{
			weight *= OneShotEndFactor(tracked.time, length, tracked.effectiveSpeed, spec.fadeOut);
		}
		weight = weight < 0.0f ? 0.0f : (weight > 1.0f ? 1.0f : weight);

		const bool finished = IsFinished(tracked.time, length, spec.loop);
		if (finished && !tracked.finished && spec.oneShot)
		{
			tracked.finishedCount = spec.playCount;
		}
		tracked.finished = finished;

		if (spec.oneShot && spec.clearOnEnd && finished)
		{
			tracked.ended = true;
			tracked.lastWeight = 0.0f;
			out.action = StepAction::Disable;
			out.reason = Reason::Ended;
			return out;
		}
		if (spec.clearing && tracked.ramp.Done())
		{
			tracked.clearDoneSerial = spec.clearSerial;
			tracked.lastWeight = 0.0f;
			out.action = StepAction::Disable;
			out.reason = Reason::Cleared;
			return out;
		}

		tracked.lastWeight = weight;
		out.action = StepAction::Apply;
		out.reason = Reason::Active;
		out.weight = weight;
		return out;
	}

	// ----- Base (FSM clip) weight ------------------------------------------

	struct BasePlan
	{
		// Put weight 1 back on the previous FSM clip (it is still on this
		// entity; the stock apply helper has disabled it).
		bool restore = false;
		char restoreName[kMaxLayerName + 1]{};
		// Set `weight` on the current FSM clip.
		bool apply = false;
		char applyName[kMaxLayerName + 1]{};
		float weight = 1.0f;
	};

	// Hook-side base weight: ramps toward the published BaseSpec and plans the
	// setWeight calls on the FSM's current clip. Stock code never calls
	// AnimationState::setWeight (the import is absent from the executable),
	// so a weight EXU leaves on an FSM clip would stick: EXU writes 1 back
	// when the FSM moves on, and writes nothing while the base is 1 and
	// nothing is dirty.
	class BaseTracker
	{
	public:
		// Advances the ramp; returns the weight for this tick.
		float Step(const BaseSpec& spec, float dt) noexcept
		{
			if (spec.serial != m_serial)
			{
				m_serial = spec.serial;
				m_ramp.Start(m_ramp.Value(), spec.weight, spec.fade);
			}
			m_ramp.Step(dt);
			return m_ramp.Value();
		}

		float Weight() const noexcept
		{
			return m_ramp.Value();
		}

		// entity is compared for identity only. engineClip may be null.
		void Plan(const void* entity, const char* engineClip, float weight, BasePlan& out) noexcept
		{
			out = BasePlan{};
			if (entity == nullptr)
			{
				return;
			}
			if (entity != m_entity)
			{
				// A new entity has fresh states; never touch the old one.
				m_entity = entity;
				m_clip[0] = '\0';
				m_dirty = false;
			}
			const char* clip = engineClip != nullptr ? engineClip : "";
			if (std::strncmp(m_clip, clip, kMaxLayerName) != 0)
			{
				if (m_dirty && m_clip[0] != '\0')
				{
					out.restore = true;
					std::memcpy(out.restoreName, m_clip, sizeof(m_clip));
				}
				CopyName(m_clip, clip);
				m_dirty = false;
			}
			if (m_clip[0] != '\0' && (weight != 1.0f || m_dirty))
			{
				out.apply = true;
				std::memcpy(out.applyName, m_clip, sizeof(m_clip));
				out.weight = weight;
				m_dirty = weight != 1.0f;
			}
		}

		// After an Ogre fault nothing is dirty any more as far as EXU can tell.
		void Forget() noexcept
		{
			m_entity = nullptr;
			m_clip[0] = '\0';
			m_dirty = false;
		}

		void Reset() noexcept
		{
			*this = BaseTracker{};
		}

	private:
		static void CopyName(char (&out)[kMaxLayerName + 1], const char* source) noexcept
		{
			std::size_t i = 0;
			for (; i < kMaxLayerName && source[i] != '\0'; ++i)
			{
				out[i] = source[i];
			}
			out[i] = '\0';
		}

		WeightRamp m_ramp{ 1.0f, 1.0f, 0.0f, 0.0f };
		std::uint32_t m_serial = 0;
		const void* m_entity = nullptr;
		char m_clip[kMaxLayerName + 1]{};
		bool m_dirty = false;
	};

	// ----- Publication between Lua and the hook ----------------------------

	// The same sequence lock as PilotAnimationPolicy::PolicyPublisher: one
	// writer, a never-blocking reader that gives up after a bounded number of
	// torn attempts. Used both ways: Lua publishes the LayerSet for the hook,
	// the hook publishes the ResultSet for exu.fps.GetLayers.
	template <typename T>
	class Seqlock
	{
		static_assert(std::is_trivially_copyable<T>::value, "Seqlock payloads must be trivially copyable");

	public:
		void Publish(const T& value) noexcept
		{
			const std::uint32_t sequence = m_sequence.load(std::memory_order_relaxed);
			m_sequence.store(sequence + 1u, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			m_value = value;
			m_sequence.store(sequence + 2u, std::memory_order_release);
		}

		bool TryRead(T& out) const noexcept
		{
			for (int attempt = 0; attempt < kReadAttempts; ++attempt)
			{
				const std::uint32_t begin = m_sequence.load(std::memory_order_acquire);
				if ((begin & 1u) != 0)
				{
					continue;
				}
				out = m_value;
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
		T m_value{};
	};

	// ----- Runtime (FirstPersonLayers.cpp) ---------------------------------

	// Lua thread. Each returns Error::None after publishing the new set.
	Error SetLayer(const char* name, std::size_t length, const LayerOptions& options) noexcept;
	Error SetLayerSpeed(const char* name, std::size_t length, double speed) noexcept;
	Error SetLayerWeight(const char* name, std::size_t length, double weight, double fadeSeconds) noexcept;
	// Without hasFade the layer's fadeOut applies. A fade is honoured only
	// while the seam is active and not faulted (otherwise nothing would
	// complete it); the removal is then immediate.
	Error ClearLayer(const char* name, std::size_t length, bool hasFade, double fadeSeconds) noexcept;
	void ClearLayers() noexcept;
	// When all 8 slots are taken, ended clearOnEnd one-shots are evicted
	// first.
	Error PlayLayer(const char* name, std::size_t length, const PlayOptions& options) noexcept;
	Error SetBaseWeight(double weight, double fadeSeconds) noexcept;
	// current: the hook's most recent applied base weight (the target until
	// the hook has reported); target: what Lua asked for.
	void GetBaseWeight(float& outCurrent, float& outTarget) noexcept;

	struct LayerReport
	{
		LayerSpec spec{};
		LayerResult result{};
		bool triggerHeld = false;
	};

	// Lua thread: the current layers in creation order, each with the hook's
	// most recent result for it. Drops completed fade-out clears first.
	// Returns the count.
	std::size_t GetLayers(LayerReport (&out)[kMaxLayers]) noexcept;

	// The capability firstPersonLayers: the Person::Simulate seam is active.
	bool IsAvailable() noexcept;

	// Clears every layer, base weight 1, the hook's memory, and the fault
	// latch. Called at Lua-state attach and from the mission-scoped reset,
	// both while the Person::Simulate hook cannot run.
	void ResetMissionState() noexcept;

	// Hook only: called from PilotFsmIntercept's Person::Simulate hook for
	// the LOCAL Person, after the stock call and the clip-table restore.
	// firstPersonEntity may be null; engineClip is a bounded copy of the clip
	// the FSM is playing (null when unknown); triggerHeld is the local fire
	// bind (PlayerTrigger::IsHeld).
	void ApplyLocal(void* firstPersonEntity, float dt, const char* engineClip, bool triggerHeld) noexcept;

	// Hook only (after ApplyLocal on the same tick): the base weight applied
	// to the FSM's current first-person clip this tick (1 when unset), and
	// whether a layer the hook is driving has this name. Used by the
	// transition cross-fade (PersonAnimBlend), which keeps its weights
	// summing to the base and never touches a clip a layer owns.
	float CurrentBaseWeight() noexcept;
	bool IsDrivenLayerName(const char* name) noexcept;
}
