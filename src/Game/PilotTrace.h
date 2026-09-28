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

// Read-only timing trace for the local pilot animation FSM.
//
// The Person::Simulate seam feeds one Record() per local call with the native
// FSM fields it captured before and after stock ran. The recorder keeps a
// bounded ring of those calls and, independently of the ring, how long each
// FSM state lasted. That answers "how long do the crouch transitions take"
// without guessing, and survives the ring wrapping.
//
// Pure data: no engine, Windows, Ogre, or Lua access, so tests/host can drive
// it directly. It allocates nothing and never blocks.
//
// Threading: whether Person::Simulate runs on the Lua thread is not proven.
// So exactly one thread (the hook) writes recorded data, Lua only requests
// start/stop through an atomic control word, and Lua reads through a
// sequence lock that reports a torn read instead of returning one.

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ExtraUtilities::Lua::PilotTrace
{
	constexpr std::size_t kCapacity = 256;

	// Native Person+0x228 states 0-3. Anything else is not dwell-tracked.
	constexpr std::uint32_t kDwellStateCount = 4;

	// Native FSM fields for one side (before or after) of a stock call.
	struct Frame
	{
		std::uint32_t nativeState = 0;
		std::int32_t animationIndex = -1;
		std::int32_t animationHandle = -1;
	};

	struct Sample
	{
		// Local calls seen since the trace started, counting this one (1-based).
		std::uint32_t call = 0;
		// dt exactly as passed to Person::Simulate.
		float dt = 0.0f;
		// Sum of valid dt over every local call since the trace started,
		// including this one: the trace clock at the end of this call.
		double time = 0.0;
		Frame before{};
		Frame after{};
	};

	// Time spent in one native state, measured as the sum of dt over the
	// consecutive local calls that started in that state, up to and including
	// the call that left it. Only complete visits count: a visit already in
	// progress when the trace started, or broken by a discontinuity, is not.
	struct Dwell
	{
		std::uint32_t count = 0;
		double last = 0.0;
		double min = 0.0;
		double max = 0.0;
		double total = 0.0;
		std::uint32_t lastCalls = 0;
	};

	struct Snapshot
	{
		bool enabled = false;
		bool changesOnly = false;
		std::uint32_t localCalls = 0;
		std::uint32_t recorded = 0;
		double time = 0.0;
		// Oldest first. Only the first sampleCount entries are meaningful.
		std::size_t sampleCount = 0;
		Sample samples[kCapacity]{};
		Dwell dwell[kDwellStateCount]{};
	};

	inline bool FramesDiffer(const Frame& a, const Frame& b) noexcept
	{
		return a.nativeState != b.nativeState ||
			a.animationIndex != b.animationIndex ||
			a.animationHandle != b.animationHandle;
	}

	class Recorder
	{
	public:
		// Lua side. Starting always discards previous data.
		void Start(bool changesOnly) noexcept
		{
			std::uint32_t current = m_control.load(std::memory_order_relaxed);
			std::uint32_t next = 0;
			do
			{
				const std::uint32_t generation = (current >> kGenerationShift) + 1u;
				next = (generation << kGenerationShift) | kEnabledBit |
					(changesOnly ? kChangesOnlyBit : 0u);
			} while (!m_control.compare_exchange_weak(
				current, next, std::memory_order_acq_rel, std::memory_order_relaxed));
		}

		// Lua side. Keeps the recorded data readable.
		void Stop() noexcept
		{
			m_control.fetch_and(~kEnabledBit, std::memory_order_acq_rel);
		}

		// Lua-state boundary: disabled, and nothing from the old state readable.
		void Reset() noexcept
		{
			std::uint32_t current = m_control.load(std::memory_order_relaxed);
			std::uint32_t next = 0;
			do
			{
				next = ((current >> kGenerationShift) + 1u) << kGenerationShift;
			} while (!m_control.compare_exchange_weak(
				current, next, std::memory_order_acq_rel, std::memory_order_relaxed));
		}

		bool IsEnabled() const noexcept
		{
			return (m_control.load(std::memory_order_relaxed) & kEnabledBit) != 0;
		}

		// Hook side only. One relaxed load when the trace is off.
		void Record(float dt, const Frame& before, const Frame& after) noexcept
		{
			const std::uint32_t control = m_control.load(std::memory_order_acquire);
			if ((control & kEnabledBit) == 0)
			{
				return;
			}

			const std::uint32_t generation = control >> kGenerationShift;
			const std::uint32_t sequence = m_sequence.load(std::memory_order_relaxed);
			m_sequence.store(sequence + 1u, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);

			if (!m_data.valid || m_data.generation != generation)
			{
				m_data = Data{};
				m_data.valid = true;
				m_data.generation = generation;
			}

			RecordLocked(dt, before, after, (control & kChangesOnlyBit) != 0);

			m_sequence.store(sequence + 2u, std::memory_order_release);
		}

		// Lua side. False only if the hook kept rewriting the data during every
		// attempt; the caller should report that rather than guess.
		bool Read(Snapshot& out) const noexcept
		{
			const std::uint32_t control = m_control.load(std::memory_order_acquire);
			const std::uint32_t generation = control >> kGenerationShift;

			for (int attempt = 0; attempt < kReadAttempts; ++attempt)
			{
				const std::uint32_t begin = m_sequence.load(std::memory_order_acquire);
				if ((begin & 1u) != 0)
				{
					continue;
				}

				Fill(out, control, generation);

				std::atomic_thread_fence(std::memory_order_acquire);
				if (m_sequence.load(std::memory_order_relaxed) == begin)
				{
					return true;
				}
			}

			out = Snapshot{};
			return false;
		}

	private:
		static constexpr std::uint32_t kEnabledBit = 1u;
		static constexpr std::uint32_t kChangesOnlyBit = 2u;
		static constexpr std::uint32_t kGenerationShift = 2u;
		static constexpr int kReadAttempts = 64;

		struct Data
		{
			bool valid = false;
			std::uint32_t generation = 0;
			std::uint32_t localCalls = 0;
			std::uint32_t recorded = 0;
			double time = 0.0;
			Sample ring[kCapacity]{};
			Dwell dwell[kDwellStateCount]{};

			// The visit currently being timed.
			bool visitOpen = false;
			bool visitComplete = false;
			std::uint32_t visitState = 0;
			double visitTime = 0.0;
			std::uint32_t visitCalls = 0;
		};

		static float UsableDt(float dt) noexcept
		{
			return std::isfinite(dt) && dt > 0.0f ? dt : 0.0f;
		}

		void RecordLocked(float dt, const Frame& before, const Frame& after, bool changesOnly) noexcept
		{
			Data& d = m_data;
			const double step = UsableDt(dt);
			++d.localCalls;
			d.time += step;

			if (!changesOnly || FramesDiffer(before, after))
			{
				Sample& sample = d.ring[d.recorded % kCapacity];
				sample.call = d.localCalls;
				sample.dt = dt;
				sample.time = d.time;
				sample.before = before;
				sample.after = after;
				++d.recorded;
			}

			// A visit is complete only when we saw the call that entered it.
			if (!d.visitOpen || d.visitState != before.nativeState)
			{
				d.visitOpen = true;
				d.visitComplete = false;
				d.visitState = before.nativeState;
				d.visitTime = 0.0;
				d.visitCalls = 0;
			}

			d.visitTime += step;
			++d.visitCalls;

			if (after.nativeState != before.nativeState)
			{
				if (d.visitComplete && before.nativeState < kDwellStateCount)
				{
					Dwell& dwell = d.dwell[before.nativeState];
					dwell.min = dwell.count == 0 ? d.visitTime : std::fmin(dwell.min, d.visitTime);
					dwell.max = dwell.count == 0 ? d.visitTime : std::fmax(dwell.max, d.visitTime);
					dwell.last = d.visitTime;
					dwell.lastCalls = d.visitCalls;
					dwell.total += d.visitTime;
					++dwell.count;
				}

				d.visitOpen = true;
				d.visitComplete = true;
				d.visitState = after.nativeState;
				d.visitTime = 0.0;
				d.visitCalls = 0;
			}
		}

		void Fill(Snapshot& out, std::uint32_t control, std::uint32_t generation) const noexcept
		{
			out = Snapshot{};
			out.enabled = (control & kEnabledBit) != 0;
			out.changesOnly = (control & kChangesOnlyBit) != 0;

			// Started (or reset) but the hook has not run since: nothing yet.
			if (!m_data.valid || m_data.generation != generation)
			{
				return;
			}

			out.localCalls = m_data.localCalls;
			out.recorded = m_data.recorded;
			out.time = m_data.time;

			const std::uint32_t held = m_data.recorded < kCapacity
				? m_data.recorded
				: static_cast<std::uint32_t>(kCapacity);
			const std::uint32_t first = m_data.recorded - held;
			for (std::uint32_t i = 0; i < held; ++i)
			{
				out.samples[i] = m_data.ring[(first + i) % kCapacity];
			}
			out.sampleCount = held;

			for (std::uint32_t i = 0; i < kDwellStateCount; ++i)
			{
				out.dwell[i] = m_data.dwell[i];
			}
		}

		std::atomic<std::uint32_t> m_control{ 0 };
		std::atomic<std::uint32_t> m_sequence{ 0 };
		Data m_data{};
	};
}
