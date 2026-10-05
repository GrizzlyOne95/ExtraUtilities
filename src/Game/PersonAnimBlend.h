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

// Cross-fades between the clips the native Person animation FSM switches
// (exu.fps.SetTransitionBlend).
//
// Person::Simulate hard-cuts: on an animation-index change its two apply
// helpers (WORLD 0x00680670, first person 0x00680770) disable the old clip,
// enable the new one and restart it at the start-time table; nothing ever
// calls AnimationState::setWeight. PilotFsmIntercept's hook (which already
// wraps every Person::Simulate call) notices the change by comparing the
// render bridge's latched clip name before and after the stock call, and
// keeps the outgoing clip enabled as a "ghost": EXU advances it on its own
// clock at its latched rate and fades it out while the incoming clip fades
// in, the weights always summing to the base weight (1, or the local first
// person exu.fps.SetBaseWeight value), so average-blend skeletons see a plain
// linear blend and cumulative ones the same.
//
// This header is the pure bookkeeping: settings validation, the per-entity
// ghost list and its weight arithmetic, the phase-carry math and the track
// table. The Ogre side lives in PersonAnimBlend.cpp.
//
// No Windows, Ogre, Lua, or engine dependencies: host-testable on Linux.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ExtraUtilities::Lua::PersonAnimBlend
{
	// Longest clip name EXU copies, excluding the terminator (the Ogre
	// animation names in the native table are all far shorter).
	constexpr std::size_t kMaxName = 63;
	// Outgoing clips fading at once on one entity. A fourth switch inside one
	// fade folds the faintest ghost into the newest one.
	constexpr std::size_t kMaxGhosts = 3;
	// Entities fading at once (every Person's WORLD entity plus the local
	// first-person entity). Tracks exist only while a fade runs.
	constexpr std::size_t kMaxTracks = 32;
	// Longest accepted blend, seconds.
	constexpr double kMaxBlendSeconds = 2.0;
	// A ghost at or below this weight is retired.
	constexpr float kRetireWeight = 1.0e-4f;

	// Native animation indices (Person+0x2A8; table order in the executable).
	constexpr std::int32_t kIndexRunForward = 4;
	constexpr std::int32_t kIndexRunRight = 7;
	constexpr std::int32_t kIndexDeath = 8;
	constexpr std::int32_t kIndexCount = 12;

	// ----- Settings (Lua -> hook) -------------------------------------------

	struct Settings
	{
		bool enabled = false;
		// Blend length in seconds (0 = hard cut, i.e. off).
		float time = 0.15f;
		// Line locomotion strides up on run<->run switches (indices 4-7).
		bool phaseCarry = true;
		// Which entities: the local pilot's first-person entity, and the
		// WORLD (third-person) entity of every Person.
		bool firstPerson = true;
		bool world = true;
		// Also fade into death1 (index 8).
		bool death = true;
	};

	inline bool IsValidBlendSeconds(double seconds) noexcept
	{
		return std::isfinite(seconds) && seconds >= 0.0 && seconds <= kMaxBlendSeconds;
	}

	// Whether a switch from oldIndex to newIndex fades at all.
	inline bool ShouldBlend(const Settings& settings, std::int32_t oldIndex, std::int32_t newIndex) noexcept
	{
		if (!settings.enabled || !(settings.time > 0.0f))
		{
			return false;
		}
		if (oldIndex < 0 || oldIndex >= kIndexCount || newIndex < 0 || newIndex >= kIndexCount)
		{
			return false;
		}
		if (newIndex == kIndexDeath && !settings.death)
		{
			return false;
		}
		return true;
	}

	inline bool IsLocomotion(std::int32_t index) noexcept
	{
		return index >= kIndexRunForward && index <= kIndexRunRight;
	}

	// Phase carry applies only between two looping locomotion clips. Never
	// to non-looping clips: kneel/land/jump/death completion is measured on
	// the clip's own time position.
	inline bool ShouldPhaseCarry(
		const Settings& settings,
		std::int32_t oldIndex,
		std::int32_t newIndex,
		bool oldLoops,
		bool newLoops) noexcept
	{
		return settings.phaseCarry && IsLocomotion(oldIndex) && IsLocomotion(newIndex) &&
			oldLoops && newLoops;
	}

	// The incoming clip's time for the outgoing clip's phase:
	// frac(oldTime / oldLength) * newLength. False when any input is unusable.
	inline bool PhaseCarryTime(float oldTime, float oldLength, float newLength, float& outTime) noexcept
	{
		outTime = 0.0f;
		if (!std::isfinite(oldTime) || !std::isfinite(oldLength) || !std::isfinite(newLength) ||
			!(oldLength > 0.0f) || !(newLength > 0.0f))
		{
			return false;
		}
		float phase = oldTime / oldLength;
		phase -= std::floor(phase);
		if (!(phase >= 0.0f) || phase >= 1.0f)
		{
			phase = 0.0f;
		}
		outTime = phase * newLength;
		return true;
	}

	// The ghost's next clip time, mirroring the stock tick: it advances only
	// while time + step stays below the end-time table entry; otherwise it
	// holds. False = hold (no write).
	inline bool GhostNextTime(float time, float rate, float dt, float endTime, float& outTime) noexcept
	{
		outTime = time;
		const float step = dt * rate;
		if (!std::isfinite(step) || !(step > 0.0f) || !std::isfinite(time))
		{
			return false;
		}
		if (std::isfinite(endTime) && !(time + step < endTime))
		{
			return false;
		}
		outTime = time + step;
		return true;
	}

	// ----- One entity's fade ------------------------------------------------

	struct Ghost
	{
		char name[kMaxName + 1]{};
		float weight = 0.0f;
		// Latched clip rate (render bridge +0xBC / +0xD4) and stock end time.
		float rate = 0.0f;
		float endTime = 0.0f;
	};

	inline void CopyName(char (&out)[kMaxName + 1], const char* source) noexcept
	{
		std::size_t i = 0;
		if (source != nullptr)
		{
			for (; i < kMaxName && source[i] != '\0'; ++i)
			{
				out[i] = source[i];
			}
		}
		out[i] = '\0';
	}

	inline bool SameName(const char* a, const char* b) noexcept
	{
		return a != nullptr && b != nullptr && std::strncmp(a, b, kMaxName + 1) == 0;
	}

	// What the Ogre side must do for one ghost that left the list.
	struct Retired
	{
		char name[kMaxName + 1]{};
		// True: disable it and put weight 1 back (EXU's own leftover). False:
		// the engine or a layer owns it now; do not touch it.
		bool disable = false;
	};

	// The ghosts of one entity. Weights are absolute; the FSM's current clip
	// gets base - TotalGhostWeight().
	class GhostList
	{
	public:
		std::size_t Count() const noexcept
		{
			return m_count;
		}

		const Ghost& At(std::size_t index) const noexcept
		{
			return m_ghosts[index];
		}

		Ghost& At(std::size_t index) noexcept
		{
			return m_ghosts[index];
		}

		float TotalWeight() const noexcept
		{
			float total = 0.0f;
			for (std::size_t i = 0; i < m_count; ++i)
			{
				total += m_ghosts[i].weight;
			}
			return total;
		}

		// The current clip's weight for this base.
		float CurrentWeight(float base) const noexcept
		{
			const float current = base - TotalWeight();
			return current > 0.0f ? current : 0.0f;
		}

		int IndexOf(const char* name) const noexcept
		{
			for (std::size_t i = 0; i < m_count; ++i)
			{
				if (SameName(m_ghosts[i].name, name))
				{
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		// Removes a ghost; it is not disabled (the caller says who owns it).
		void RemoveAt(std::size_t index) noexcept
		{
			if (index >= m_count)
			{
				return;
			}
			for (std::size_t i = index + 1; i < m_count; ++i)
			{
				m_ghosts[i - 1] = m_ghosts[i];
			}
			m_ghosts[--m_count] = Ghost{};
		}

		// The FSM switched from oldName to newName. outgoingWeight is the
		// weight the outgoing clip carried (base - ghosts, before the switch).
		// A ghost that is the incoming clip is dropped without a disable (the
		// stock helper just re-enabled it); its weight goes back to the current
		// clip. The outgoing clip becomes the newest ghost; if the list is full
		// the faintest ghost is retired and its weight folded into the newest,
		// so the sum is unchanged. Up to two Retired entries are written.
		std::size_t Switch(
			const char* oldName,
			const char* newName,
			float outgoingWeight,
			float rate,
			float endTime,
			Retired (&out)[2]) noexcept
		{
			std::size_t retired = 0;
			const int revived = IndexOf(newName);
			if (revived >= 0)
			{
				CopyName(out[retired].name, m_ghosts[revived].name);
				out[retired].disable = false;
				++retired;
				RemoveAt(static_cast<std::size_t>(revived));
			}

			// The outgoing clip may already be a ghost only in a malformed
			// sequence; merge rather than list it twice.
			const int existing = IndexOf(oldName);
			if (existing >= 0)
			{
				Ghost& ghost = m_ghosts[existing];
				ghost.weight += outgoingWeight > 0.0f ? outgoingWeight : 0.0f;
				ghost.rate = rate;
				ghost.endTime = endTime;
				return retired;
			}

			float folded = 0.0f;
			if (m_count >= kMaxGhosts)
			{
				std::size_t faintest = 0;
				for (std::size_t i = 1; i < m_count; ++i)
				{
					if (m_ghosts[i].weight < m_ghosts[faintest].weight)
					{
						faintest = i;
					}
				}
				folded = m_ghosts[faintest].weight;
				CopyName(out[retired].name, m_ghosts[faintest].name);
				out[retired].disable = true;
				++retired;
				RemoveAt(faintest);
			}

			Ghost& ghost = m_ghosts[m_count++];
			CopyName(ghost.name, oldName);
			ghost.weight = (outgoingWeight > 0.0f ? outgoingWeight : 0.0f) + folded;
			ghost.rate = rate;
			ghost.endTime = endTime;
			return retired;
		}

		// One tick of fade: the ghosts lose base * dt / seconds in total,
		// shared in proportion to their weights, and never exceed base in
		// total. Ghosts that reach kRetireWeight are retired (disable = true)
		// into out; returns how many.
		std::size_t Fade(float base, float dt, float seconds, Retired (&out)[kMaxGhosts]) noexcept
		{
			const float total = TotalWeight();
			float target = total;
			if (seconds > 0.0f && std::isfinite(dt) && dt > 0.0f)
			{
				target = total - base * dt / seconds;
			}
			else if (!(seconds > 0.0f))
			{
				target = 0.0f;
			}
			if (target > base)
			{
				target = base;
			}
			if (!(target > 0.0f))
			{
				target = 0.0f;
			}

			const float scale = total > 0.0f ? target / total : 0.0f;
			for (std::size_t i = 0; i < m_count; ++i)
			{
				m_ghosts[i].weight *= scale;
			}

			std::size_t retired = 0;
			for (std::size_t i = 0; i < m_count;)
			{
				if (m_ghosts[i].weight <= kRetireWeight)
				{
					CopyName(out[retired].name, m_ghosts[i].name);
					out[retired].disable = true;
					++retired;
					RemoveAt(i);
					continue;
				}
				++i;
			}
			return retired;
		}

		void Clear() noexcept
		{
			*this = GhostList{};
		}

	private:
		Ghost m_ghosts[kMaxGhosts]{};
		std::size_t m_count = 0;
	};

	// ----- Track table --------------------------------------------------------

	enum class Side : std::uint8_t
	{
		World = 0,
		FirstPerson = 1,
	};

	struct Track
	{
		bool used = false;
		// Identity only; never dereferenced by this header. The Ogre side
		// touches an entity only when the render bridge yields that same
		// pointer again in the current hook call.
		const void* person = nullptr;
		const void* entity = nullptr;
		Side side = Side::World;
		std::uint32_t lastTick = 0;
		GhostList ghosts;
	};

	// Tracks exist only while a fade runs. A track whose Person stops
	// simulating (destroyed, removed) is never touched again and is reused
	// when the table is full.
	class TrackTable
	{
	public:
		Track* Find(const void* person, Side side) noexcept
		{
			for (Track& track : m_tracks)
			{
				if (track.used && track.person == person && track.side == side)
				{
					return &track;
				}
			}
			return nullptr;
		}

		// A fresh track; evicts the stalest one (without any Ogre call) when
		// every slot is in use.
		Track& Acquire(const void* person, const void* entity, Side side, std::uint32_t tick) noexcept
		{
			Track* slot = nullptr;
			for (Track& track : m_tracks)
			{
				if (!track.used)
				{
					slot = &track;
					break;
				}
			}
			if (slot == nullptr)
			{
				slot = &m_tracks[0];
				for (Track& track : m_tracks)
				{
					if (static_cast<std::uint32_t>(tick - track.lastTick) >
						static_cast<std::uint32_t>(tick - slot->lastTick))
					{
						slot = &track;
					}
				}
				++m_evictions;
			}
			*slot = Track{};
			slot->used = true;
			slot->person = person;
			slot->entity = entity;
			slot->side = side;
			slot->lastTick = tick;
			return *slot;
		}

		void Release(Track& track) noexcept
		{
			track = Track{};
		}

		std::size_t ActiveCount() const noexcept
		{
			std::size_t count = 0;
			for (const Track& track : m_tracks)
			{
				count += track.used ? 1u : 0u;
			}
			return count;
		}

		std::uint32_t Evictions() const noexcept
		{
			return m_evictions;
		}

		void Reset() noexcept
		{
			*this = TrackTable{};
		}

	private:
		Track m_tracks[kMaxTracks]{};
		std::uint32_t m_evictions = 0;
	};

	// ----- Runtime (PersonAnimBlend.cpp) ------------------------------------

	struct Stats
	{
		bool available = false;
		bool faulted = false;
		std::uint32_t transitions = 0;
		std::uint32_t phaseCarries = 0;
		std::uint32_t activeTracks = 0;
		std::uint32_t evictions = 0;
	};

	// Lua thread.
	void SetSettings(const Settings& settings) noexcept;
	Settings GetSettings() noexcept;
	void GetStats(Stats& outStats) noexcept;
	// The Person::Simulate seam is active and the build qualified.
	bool IsAvailable() noexcept;
	// Off, no tracks, fault latch cleared. At Lua-state attach and from the
	// mission-scoped reset, while the hook cannot run.
	void ResetMissionState() noexcept;

	// What the render bridge held for one entity before the stock call.
	struct BridgeSide
	{
		void* entity = nullptr;
		bool on = false;
		const char* name = nullptr;
		float rate = 0.0f;
	};

	struct PreCall
	{
		bool valid = false;
		std::int32_t index = -1;
		BridgeSide world{};
		BridgeSide firstPerson{};
	};

	// Hook only. Captured before the stock Person::Simulate call; returns
	// false (and does nothing) when the blend is off and nothing is fading.
	bool CapturePre(const void* person, PreCall& out) noexcept;

	// Hook only, after the stock call (and, for the local Person, after the
	// clip-table restore and the first-person layers). isLocal selects the
	// first-person entity and its base weight.
	void ApplyPost(const void* person, const PreCall& pre, float dt, bool isLocal) noexcept;
}
