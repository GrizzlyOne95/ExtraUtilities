/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#include "Game/PersonAnimBlend.h"

#include "Game/FirstPersonLayers.h"
#include "Game/PilotFsmIntercept.h"
#include "Ogre/OgreEntityRuntime.h"
#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "bzr.h"

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ExtraUtilities::Lua::PersonAnimBlend
{
	namespace
	{
		// Person and render-bridge layout on the qualified 2.2.301 image
		// (Docs/Research/PERSON_ANIM_CROSSFADE_RE_20261005.md). The bridge
		// pointer and entity offsets are the ones FirstPersonTarget already
		// relies on; the latched name/rate/enable fields are what the apply
		// helpers 0x00680670 (WORLD) and 0x00680770 (first person) write and
		// what the per-tick advance in Person::Simulate reads.
		constexpr std::size_t kPersonRenderBridgeOffset = 0x0F0;
		constexpr std::size_t kPersonAnimIndexOffset = 0x2A8;
		// Removal flags: Person::Simulate removes the object when
		// *(Person+0xF4)+0x14 has 0x200 (death1 finished) or 0x1000000.
		constexpr std::size_t kPersonObjectOffset = 0x0F4;
		constexpr std::size_t kObjectFlagsOffset = 0x014;
		constexpr std::uint32_t kRemoveFlag = 0x200u;
		constexpr std::uint32_t kRemoveAltFlag = 1u << 24;
		constexpr std::uint32_t kRemovalFlags = kRemoveFlag | kRemoveAltFlag;

		constexpr std::size_t kBridgeWorldEntity = 0x094;
		constexpr std::size_t kBridgeWorldName = 0x0B4;
		constexpr std::size_t kBridgeWorldOn = 0x0B8;
		constexpr std::size_t kBridgeWorldRate = 0x0BC;
		constexpr std::size_t kBridgeFirstPersonEntity = 0x0C0;
		constexpr std::size_t kBridgeFirstPersonOn = 0x0C4;
		constexpr std::size_t kBridgeFirstPersonName = 0x0D0;
		constexpr std::size_t kBridgeFirstPersonRate = 0x0D4;

		// Threading: g_luaSettings is the Lua thread's; everything else below
		// the publication is the hook's (the Person::Simulate thread), except
		// the atomics. ResetMissionState runs only while the hook cannot.
		Settings g_luaSettings{};
		FirstPersonLayers::Seqlock<Settings> g_published;
		std::atomic<bool> g_faulted{ false };
		std::atomic<std::uint32_t> g_transitions{ 0 };
		std::atomic<std::uint32_t> g_phaseCarries{ 0 };
		std::atomic<std::uint32_t> g_activeTracks{ 0 };
		std::atomic<std::uint32_t> g_evictions{ 0 };

		TrackTable g_tracks;
		Settings g_hookSettings{};
		std::uint32_t g_tick = 0;

		template <typename... Args>
		void LogNoThrow(const char* format, Args... args) noexcept
		{
			try
			{
				Logging::LogMessage(format, args...);
			}
			catch (...)
			{
				OutputDebugStringA("ExtraUtilities: transition blend log line dropped\n");
			}
		}

		void LatchFault(const char* what, const char* name) noexcept
		{
			if (!g_faulted.exchange(true, std::memory_order_acq_rel))
			{
				LogNoThrow(
					"exu: person transition blend disabled for this Lua state; Ogre %s faulted on '%s'",
					what,
					name != nullptr ? name : "?");
			}
		}

		void PublishTrackCount() noexcept
		{
			g_activeTracks.store(static_cast<std::uint32_t>(g_tracks.ActiveCount()), std::memory_order_relaxed);
			g_evictions.store(g_tracks.Evictions(), std::memory_order_relaxed);
		}

		// ----- Raw engine reads (POD-only SEH shells) ----------------------

		template <typename T>
		T ReadAt(const void* base, std::size_t offset) noexcept
		{
			return *reinterpret_cast<const T*>(reinterpret_cast<const std::uint8_t*>(base) + offset);
		}

		bool ReadBridgeSeh(const void* person, PreCall& out) noexcept
		{
			__try
			{
				const void* const bridge = ReadAt<const void*>(person, kPersonRenderBridgeOffset);
				if (bridge == nullptr)
				{
					return false;
				}
				out.bridge = bridge;
				out.index = ReadAt<std::int32_t>(person, kPersonAnimIndexOffset);
				out.world.entity = ReadAt<void*>(bridge, kBridgeWorldEntity);
				out.world.on = ReadAt<std::int32_t>(bridge, kBridgeWorldOn) != 0;
				out.world.name = ReadAt<const char*>(bridge, kBridgeWorldName);
				out.world.rate = ReadAt<float>(bridge, kBridgeWorldRate);
				out.firstPerson.entity = ReadAt<void*>(bridge, kBridgeFirstPersonEntity);
				out.firstPerson.on = ReadAt<std::int32_t>(bridge, kBridgeFirstPersonOn) != 0;
				out.firstPerson.name = ReadAt<const char*>(bridge, kBridgeFirstPersonName);
				out.firstPerson.rate = ReadAt<float>(bridge, kBridgeFirstPersonRate);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// Bounded copy of an engine/pool string.
		bool CopyNameSeh(const char* source, char (&out)[kMaxName + 1]) noexcept
		{
			out[0] = '\0';
			if (source == nullptr)
			{
				return false;
			}
			__try
			{
				for (std::size_t i = 0; i <= kMaxName; ++i)
				{
					out[i] = source[i];
					if (source[i] == '\0')
					{
						return i != 0;
					}
				}
				out[0] = '\0';
				return false;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				out[0] = '\0';
				return false;
			}
		}

		// The stock end time of a clip index (the table is restored by the
		// time this runs). Unreadable = no end gate.
		float StockEndTimeSeh(std::int32_t index) noexcept
		{
			if (index < 0 || index >= kIndexCount)
			{
				return INFINITY;
			}
			__try
			{
				return BZR::PersonRuntime::animEndTime[index];
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return INFINITY;
			}
		}

		// ----- Ogre -------------------------------------------------------

		// Reserved once, reassigned in place: steady-state ticks do not
		// allocate.
		void* LookupState(void* entity, const char* name) noexcept
		{
			static std::string key;
			static bool reserved = false;
			try
			{
				if (!reserved)
				{
					key.reserve(kMaxName + 1);
					reserved = true;
				}
				key.assign(name);
				return GameObject::Detail::GetNamedAnimationState(entity, key);
			}
			catch (...)
			{
				return nullptr;
			}
		}

		// A ghost EXU leaves behind: disabled, weight 1 (stock code never sets
		// weights, so a leftover weight would stick to the next stock enable).
		bool RetireState(void* entity, const char* name) noexcept
		{
			void* const state = LookupState(entity, name);
			if (state == nullptr)
			{
				return true;
			}
			if (!GameObject::Detail::TrySetAnimationEnabled(state, false) ||
				!GameObject::Detail::TrySetAnimationWeight(state, 1.0f))
			{
				LatchFault("retire", name);
				return false;
			}
			return true;
		}

		bool SetWeight(void* entity, const char* name, float weight) noexcept
		{
			void* const state = LookupState(entity, name);
			if (state == nullptr)
			{
				return true;
			}
			if (!GameObject::Detail::TrySetAnimationWeight(state, weight))
			{
				LatchFault("setWeight", name);
				return false;
			}
			return true;
		}

		bool ApplyRetired(void* entity, const Retired* retired, std::size_t count) noexcept
		{
			for (std::size_t i = 0; i < count; ++i)
			{
				if (retired[i].disable && !RetireState(entity, retired[i].name))
				{
					return false;
				}
			}
			return true;
		}

		// Ghost weights and the current clip's share.
		bool ApplyWeights(void* entity, const GhostList& ghosts, const char* current, float base) noexcept
		{
			for (std::size_t i = 0; i < ghosts.Count(); ++i)
			{
				if (!SetWeight(entity, ghosts.At(i).name, ghosts.At(i).weight))
				{
					return false;
				}
			}
			return SetWeight(entity, current, ghosts.CurrentWeight(base));
		}

		// Ends a fade at once: every ghost retired (except one that is the
		// current clip or a driven layer), the current clip back at base.
		void Flush(Track& track, void* entity, const char* current, float base, bool firstPerson) noexcept
		{
			for (std::size_t i = 0; i < track.ghosts.Count(); ++i)
			{
				const char* name = track.ghosts.At(i).name;
				if (SameName(name, current) || (firstPerson && FirstPersonLayers::IsDrivenLayerName(name)))
				{
					continue;
				}
				if (!RetireState(entity, name))
				{
					break;
				}
			}
			if (current != nullptr && current[0] != '\0')
			{
				SetWeight(entity, current, base);
			}
			g_tracks.Release(track);
		}

		bool StateLoops(void* state) noexcept
		{
			bool loop = false;
			return state != nullptr && GameObject::Detail::TryGetAnimationLoop(state, loop) && loop;
		}

		// Starts (or extends) a fade on the switch tick.
		void BeginFade(
			const void* person,
			Side side,
			Track* track,
			void* entity,
			const char* oldName,
			const char* newName,
			std::int32_t oldIndex,
			std::int32_t newIndex,
			float oldRate,
			float base) noexcept
		{
			void* const outgoing = LookupState(entity, oldName);
			void* const incoming = LookupState(entity, newName);
			if (outgoing == nullptr || incoming == nullptr)
			{
				if (track != nullptr)
				{
					Flush(*track, entity, newName, base, side == Side::FirstPerson);
				}
				return;
			}

			const float outgoingWeight = track != nullptr ? track->ghosts.CurrentWeight(base) : base;
			if (track == nullptr)
			{
				track = &g_tracks.Acquire(person, entity, side, g_tick);
			}
			track->lastTick = g_tick;

			Retired retired[2]{};
			const std::size_t retiredCount = track->ghosts.Switch(
				oldName, newName, outgoingWeight, oldRate, StockEndTimeSeh(oldIndex), retired);
			if (!ApplyRetired(entity, retired, retiredCount))
			{
				g_tracks.Release(*track);
				return;
			}

			// The stock helper disabled it; it stays on as the ghost. Its time
			// is untouched (the helper never seeks the old clip).
			if (!GameObject::Detail::TrySetAnimationEnabled(outgoing, true))
			{
				LatchFault("enable ghost", oldName);
				g_tracks.Release(*track);
				return;
			}

			if (ShouldPhaseCarry(g_hookSettings, oldIndex, newIndex, StateLoops(outgoing), StateLoops(incoming)))
			{
				float oldTime = 0.0f;
				float oldLength = 0.0f;
				float newLength = 0.0f;
				float newTime = 0.0f;
				if (GameObject::Detail::TryGetAnimationTimePosition(outgoing, oldTime) &&
					GameObject::Detail::TryGetAnimationLength(outgoing, oldLength) &&
					GameObject::Detail::TryGetAnimationLength(incoming, newLength) &&
					PhaseCarryTime(oldTime, oldLength, newLength, newTime))
				{
					if (!GameObject::Detail::TrySetAnimationTimePosition(incoming, newTime))
					{
						LatchFault("phase carry", newName);
						g_tracks.Release(*track);
						return;
					}
					g_phaseCarries.fetch_add(1, std::memory_order_relaxed);
				}
			}

			if (!ApplyWeights(entity, track->ghosts, newName, base))
			{
				g_tracks.Release(*track);
				return;
			}
			g_transitions.fetch_add(1, std::memory_order_relaxed);
		}

		// One fade tick (not on a switch tick: the stock call already
		// advanced the clip that just became the ghost).
		void StepFade(Track& track, void* entity, const char* current, float dt, float base, bool firstPerson) noexcept
		{
			track.lastTick = g_tick;

			// Ghosts somebody else owns now leave without a disable.
			for (std::size_t i = 0; i < track.ghosts.Count();)
			{
				const char* name = track.ghosts.At(i).name;
				if (SameName(name, current) || (firstPerson && FirstPersonLayers::IsDrivenLayerName(name)))
				{
					track.ghosts.RemoveAt(i);
					continue;
				}
				++i;
			}

			// Advance each ghost on its own clock, as the stock tick would.
			for (std::size_t i = 0; i < track.ghosts.Count(); ++i)
			{
				const Ghost& ghost = track.ghosts.At(i);
				void* const state = LookupState(entity, ghost.name);
				if (state == nullptr)
				{
					continue;
				}
				float time = 0.0f;
				float next = 0.0f;
				if (!GameObject::Detail::TryGetAnimationTimePosition(state, time))
				{
					LatchFault("getTimePosition", ghost.name);
					g_tracks.Release(track);
					return;
				}
				if (GhostNextTime(time, ghost.rate, dt, ghost.endTime, next) &&
					!GameObject::Detail::TrySetAnimationTimePosition(state, next))
				{
					LatchFault("setTimePosition", ghost.name);
					g_tracks.Release(track);
					return;
				}
			}

			Retired retired[kMaxGhosts]{};
			const std::size_t retiredCount = track.ghosts.Fade(base, dt, g_hookSettings.time, retired);
			if (!ApplyRetired(entity, retired, retiredCount))
			{
				g_tracks.Release(track);
				return;
			}
			if (!ApplyWeights(entity, track.ghosts, current, base))
			{
				g_tracks.Release(track);
				return;
			}
			if (track.ghosts.Count() == 0)
			{
				g_tracks.Release(track);
			}
		}

		void ProcessSide(
			const void* person,
			Side side,
			const PreCall& pre,
			const BridgeSide& before,
			const BridgeSide& after,
			std::int32_t afterIndex,
			float dt,
			float base,
			bool sideEnabled) noexcept
		{
			Track* track = g_tracks.Find(person, side);
			const bool live = after.on && after.entity != nullptr;
			if (track != nullptr && (!live || track->entity != after.entity))
			{
				// The entity went away or was swapped (mesh change, pilot
				// boarded a craft): never touch the old one.
				g_tracks.Release(*track);
				track = nullptr;
			}
			if (!live)
			{
				return;
			}

			char current[kMaxName + 1];
			if (!CopyNameSeh(after.name, current))
			{
				if (track != nullptr)
				{
					g_tracks.Release(*track);
				}
				return;
			}

			const bool firstPerson = side == Side::FirstPerson;
			const bool active = sideEnabled && !g_faulted.load(std::memory_order_relaxed);
			char previous[kMaxName + 1];
			const bool switched = pre.index != afterIndex &&
				before.on && before.entity == after.entity &&
				CopyNameSeh(before.name, previous) && !SameName(previous, current);

			if (switched)
			{
				if (active && ShouldBlend(g_hookSettings, pre.index, afterIndex) &&
					!(firstPerson && FirstPersonLayers::IsDrivenLayerName(previous)))
				{
					BeginFade(person, side, track, after.entity, previous, current,
						pre.index, afterIndex, before.rate, base);
				}
				else if (track != nullptr)
				{
					Flush(*track, after.entity, current, base, firstPerson);
				}
				return;
			}

			if (track == nullptr)
			{
				return;
			}
			if (!active || !(g_hookSettings.time > 0.0f))
			{
				Flush(*track, after.entity, current, base, firstPerson);
				return;
			}
			StepFade(*track, after.entity, current, dt, base, firstPerson);
		}
	}

	void SetSettings(const Settings& settings) noexcept
	{
		g_luaSettings = settings;
		g_published.Publish(g_luaSettings);
	}

	Settings GetSettings() noexcept
	{
		return g_luaSettings;
	}

	void GetStats(Stats& outStats) noexcept
	{
		outStats = Stats{};
		outStats.available = IsAvailable();
		outStats.faulted = g_faulted.load(std::memory_order_acquire);
		outStats.transitions = g_transitions.load(std::memory_order_relaxed);
		outStats.phaseCarries = g_phaseCarries.load(std::memory_order_relaxed);
		outStats.activeTracks = g_activeTracks.load(std::memory_order_relaxed);
		outStats.evictions = g_evictions.load(std::memory_order_relaxed);
	}

	bool IsAvailable() noexcept
	{
		return RuntimeGate::IsSupported() && PilotFsmIntercept::IsActive();
	}

	void ResetMissionState() noexcept
	{
		g_luaSettings = Settings{};
		g_published.Publish(g_luaSettings);
		g_faulted.store(false, std::memory_order_release);
		g_transitions.store(0, std::memory_order_relaxed);
		g_phaseCarries.store(0, std::memory_order_relaxed);
		g_activeTracks.store(0, std::memory_order_relaxed);
		g_evictions.store(0, std::memory_order_relaxed);
		g_tracks.Reset();
		g_hookSettings = Settings{};
		g_tick = 0;
	}

	bool IsRemovalPending(const void* person) noexcept
	{
		if (person == nullptr)
		{
			return true;
		}
		__try
		{
			const void* const object = ReadAt<const void*>(person, kPersonObjectOffset);
			if (object == nullptr)
			{
				return true;
			}
			return (ReadAt<std::uint32_t>(object, kObjectFlagsOffset) & kRemovalFlags) != 0;
		}
		__except (Seh::Filter(GetExceptionCode()))
		{
			return true;
		}
	}

	void ForgetPerson(const void* person) noexcept
	{
		for (Side side : { Side::World, Side::FirstPerson })
		{
			if (Track* track = g_tracks.Find(person, side))
			{
				g_tracks.Release(*track);
			}
		}
		PublishTrackCount();
	}

	bool CapturePre(const void* person, PreCall& out) noexcept
	{
		out = PreCall{};
		if (person == nullptr || g_faulted.load(std::memory_order_acquire))
		{
			return false;
		}
		Settings settings{};
		if (g_published.TryRead(settings))
		{
			g_hookSettings = settings;
		}
		if (!g_hookSettings.enabled && g_activeTracks.load(std::memory_order_relaxed) == 0)
		{
			return false;
		}
		out.valid = ReadBridgeSeh(person, out);
		return out.valid;
	}

	void ApplyPost(const void* person, const PreCall& pre, float dt, bool isLocal) noexcept
	{
		if (!pre.valid || g_faulted.load(std::memory_order_acquire))
		{
			return;
		}
		// A Person that lost or changed its render bridge inside the stock
		// call (removal, model rebuild) is left alone this tick; its tracks
		// are dropped on a later tick by the entity check, never touched.
		PreCall after{};
		if (!ReadBridgeSeh(person, after) || after.bridge != pre.bridge)
		{
			return;
		}
		++g_tick;
		const float safeDt = (std::isfinite(dt) && dt > 0.0f) ? dt : 0.0f;

		ProcessSide(person, Side::World, pre, pre.world, after.world, after.index, safeDt, 1.0f,
			g_hookSettings.world);

		// The first-person entity belongs to the local Person only, and is
		// never processed twice when it is the WORLD entity.
		if (isLocal && after.firstPerson.entity != after.world.entity)
		{
			float base = FirstPersonLayers::CurrentBaseWeight();
			if (!std::isfinite(base) || base < 0.0f)
			{
				base = 0.0f;
			}
			if (base > 1.0f)
			{
				base = 1.0f;
			}
			ProcessSide(person, Side::FirstPerson, pre, pre.firstPerson, after.firstPerson, after.index,
				safeDt, base, g_hookSettings.firstPerson);
		}
		PublishTrackCount();
	}
}
