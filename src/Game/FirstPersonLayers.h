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

// EXU-clocked looping first-person animation layers (exu.fps.SetLayer & co).
//
// Person::Simulate advances only the clip its FSM is currently playing
// (Docs/Research/PILOT_CROUCH_NATIVE_RE_20261003.md). A layer is a named
// clip on the local pilot's first-person skeleton that EXU keeps enabled and
// advances itself, once per local Person::Simulate call, independently of
// the FSM (e.g. a minigun barrel spin at a scripted speed).
//
// This header is the pure bookkeeping: validation, the 8-layer cap,
// create/update/clear, the hook-side reconciliation of what Lua published
// with what the hook last applied, the engine-owned guard, the time math, and
// the lock-free handoff between Lua and the hook. The Ogre side lives in
// FirstPersonLayers.cpp and runs inside PilotFsmIntercept's hook.
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

	// ----- What Lua asks for ---------------------------------------------------

	struct LayerSpec
	{
		// NUL-terminated Ogre animation name on the first-person skeleton.
		char name[kMaxLayerName + 1]{};
		// Unique per creation (never 0 for a live layer). A cleared and
		// re-created layer of the same name is a new layer to the hook: the
		// old one is disabled and reset, the new one starts at time 0.
		std::uint32_t id = 0;
		float speed = 1.0f;
		float weight = 1.0f;
		bool loop = true;
		// Bumped each time Lua sets an explicit time; the hook seeks to
		// seekTime when it sees a serial it has not applied yet.
		std::uint32_t seekSerial = 0;
		float seekTime = 0.0f;
	};

	struct LayerSet
	{
		LayerSpec layers[kMaxLayers]{};
		std::uint32_t count = 0;
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

	// SetLayer's options. A field that is not present keeps the layer's
	// current value (its default on creation: speed 1, weight 1, loop true,
	// time 0).
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
	};

	// The Lua thread's editable layer list. Every operation validates
	// everything first and changes nothing on failure.
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

			LayerSpec* layer = Find(name, length);
			if (layer == nullptr)
			{
				if (m_set.count >= kMaxLayers)
				{
					return Error::Full;
				}
				layer = &m_set.layers[m_set.count++];
				*layer = LayerSpec{};
				std::memcpy(layer->name, name, length);
				layer->name[length] = '\0';
				layer->id = NextId();
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
			}
			if (options.hasTime)
			{
				layer->seekTime = static_cast<float>(options.time);
				++layer->seekSerial;
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

		Error SetWeight(const char* name, std::size_t length, double weight) noexcept
		{
			if (!IsValidName(name, length))
			{
				return Error::BadName;
			}
			if (!IsValidWeight(weight))
			{
				return Error::BadWeight;
			}
			LayerSpec* layer = Find(name, length);
			if (layer == nullptr)
			{
				return Error::NotFound;
			}
			layer->weight = static_cast<float>(weight);
			return Error::None;
		}

		// Removes the layer, keeping the order of the others.
		Error Clear(const char* name, std::size_t length) noexcept
		{
			if (!IsValidName(name, length))
			{
				return Error::BadName;
			}
			LayerSpec* layer = Find(name, length);
			if (layer == nullptr)
			{
				return Error::NotFound;
			}
			const std::size_t index = static_cast<std::size_t>(layer - m_set.layers);
			for (std::size_t i = index + 1; i < m_set.count; ++i)
			{
				m_set.layers[i - 1] = m_set.layers[i];
			}
			m_set.layers[--m_set.count] = LayerSpec{};
			return Error::None;
		}

		// Ids keep counting, so a layer re-created after this is still new
		// to a hook that has not yet seen the clear.
		void ClearAll() noexcept
		{
			m_set = LayerSet{};
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
	};

	struct ResultSet
	{
		LayerResult layers[kMaxLayers]{};
		std::uint32_t count = 0;
	};

	static_assert(std::is_trivially_copyable<ResultSet>::value, "ResultSet must stay trivially copyable");

	// ----- Engine-owned guard ----------------------------------------------

	// True when the layer names the clip the FSM is playing for the local
	// Person this tick. Person::Simulate already advanced that clip by
	// dt * rate and owns its enable/weight/loop, so EXU skips it entirely
	// rather than advancing it twice. engineClip is EXU's bounded copy of
	// the name in force (null when unknown: the guard then does not fire).
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
				}

				TrackedLayer& layer = m_layers[index];
				if (layer.seekSerial != spec.seekSerial)
				{
					layer.seekSerial = spec.seekSerial;
					layer.time = spec.seekTime;
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
	Error SetLayerWeight(const char* name, std::size_t length, double weight) noexcept;
	Error ClearLayer(const char* name, std::size_t length) noexcept;
	void ClearLayers() noexcept;

	struct LayerReport
	{
		LayerSpec spec{};
		LayerResult result{};
	};

	// Lua thread: the current layers in creation order, each with the hook's
	// most recent result for it. Returns the count.
	std::size_t GetLayers(LayerReport (&out)[kMaxLayers]) noexcept;

	// The capability firstPersonLayers: the Person::Simulate seam is active.
	bool IsAvailable() noexcept;

	// Clears every layer, the hook's memory, and the fault latch. Called at
	// Lua-state attach and from the mission-scoped reset, both while the
	// Person::Simulate hook cannot run.
	void ResetMissionState() noexcept;

	// Hook only: called from PilotFsmIntercept's Person::Simulate hook for
	// the LOCAL Person, after the stock call and the clip-table restore.
	// firstPersonEntity may be null; engineClip is a bounded copy of the clip
	// the FSM is playing (null when unknown).
	void ApplyLocal(void* firstPersonEntity, float dt, const char* engineClip) noexcept;
}
