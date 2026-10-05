/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#include "Game/FirstPersonLayers.h"

#include "Game/PilotFsmIntercept.h"
#include "Ogre/OgreAnimationInventoryBridge.h"
#include "Ogre/OgreEntityRuntime.h"
#include "Util/Logging.h"

#include <Windows.h>

#include <atomic>
#include <string>

namespace ExtraUtilities::Lua::FirstPersonLayers
{
	namespace
	{
		// Threading: g_book is the Lua thread's. g_tracker, g_baseTracker,
		// g_blendEntity, g_blendMode and g_averageLoggedEntity are the hook's
		// (the Person::Simulate thread). They meet only through the two sequence
		// locks and the fault latch. ResetMissionState touches both sides,
		// and runs only while the hook cannot (see the header).
		LayerBook g_book;
		Seqlock<LayerSet> g_published;
		Seqlock<ResultSet> g_results;
		std::atomic<bool> g_faulted{ false };

		Tracker g_tracker;
		BaseTracker g_baseTracker;
		const void* g_blendEntity = nullptr;
		BlendMode g_blendMode = BlendMode::Unknown;
		const void* g_averageLoggedEntity = nullptr;

		// Allocation-free logging guard for the hook: LogMessage builds a path
		// string and can throw.
		template <typename... Args>
		void LogNoThrow(const char* format, Args... args) noexcept
		{
			try
			{
				Logging::LogMessage(format, args...);
			}
			catch (...)
			{
				OutputDebugStringA("ExtraUtilities: first-person layer log line dropped\n");
			}
		}

		Error PublishIfOk(Error error) noexcept
		{
			if (error == Error::None)
			{
				g_published.Publish(g_book.Layers());
			}
			return error;
		}

		void LatchFault(const char* what, const char* name) noexcept
		{
			if (!g_faulted.exchange(true, std::memory_order_acq_rel))
			{
				LogNoThrow(
					"exu: first-person layers disabled for this Lua state; Ogre %s faulted on layer '%s'",
					what,
					name);
			}
		}

		// The Ogre key buffer. Ogre takes const std::string& (the layout
		// both runtimes share; see OgreAnimationInventoryBridge.cpp). One
		// buffer reserved for the longest name once, then reassigned in place,
		// so steady-state ticks do not allocate.
		// The named state on entity, or null (absent, or the lookup failed).
		// The GameObject::Detail helpers guard faults with SEH and C++ catches.
		void* LookupState(void* entity, const char* name) noexcept
		{
			// Function-local, so it is built on the first hook call rather
			// than in DllMain.
			static std::string key;
			static bool reserved = false;
			try
			{
				if (!reserved)
				{
					key.reserve(kMaxLayerName);
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

		// Disable + reset (the ClearLayer contract) on a state already found.
		bool DisableState(void* state) noexcept
		{
			return GameObject::Detail::TrySetAnimationEnabled(state, false) &&
				GameObject::Detail::TrySetAnimationWeight(state, 1.0f) &&
				GameObject::Detail::TrySetAnimationTimePosition(state, 0.0f);
		}

		// ClearLayer contract: disabled, weight 1, time 0 (if it still exists).
		void DisableRemoved(void* entity, const char* name) noexcept
		{
			void* const state = LookupState(entity, name);
			if (state == nullptr)
			{
				return;
			}
			if (!DisableState(state))
			{
				LatchFault("clear", name);
			}
		}

		void RefreshBlendMode(void* entity) noexcept
		{
			if (entity == g_blendEntity)
			{
				return;
			}
			g_blendEntity = entity;
			g_blendMode = BlendMode::Unknown;
			int mode = -1;
			if (entity != nullptr && OgreAnimationInventory::TryGetSkeletonBlendMode(entity, mode))
			{
				g_blendMode = BlendModeFromOgre(mode);
			}
		}

		void WarnAverageOnce(void* entity, const char* name) noexcept
		{
			if (g_blendMode != BlendMode::Average || g_averageLoggedEntity == entity)
			{
				return;
			}
			g_averageLoggedEntity = entity;
			LogNoThrow(
				"exu: first-person layer '%s' applied to a skeleton with blendmode \"average\": Ogre rescales every enabled clip by 1/total weight once weights sum past 1, so the layer distorts the whole pose; author the skeleton with blendmode \"cumulative\"",
				name);
		}

		// The base (FSM clip) weight for this tick. Runs before the layers so
		// a layer that shares a name with the previous FSM clip still sets its
		// own weight last. Returns false only on an Ogre fault.
		bool ApplyBase(void* entity, float dt, const char* engineClip, const BaseSpec& base, float& outWeight) noexcept
		{
			outWeight = g_baseTracker.Step(base, dt);
			BasePlan plan{};
			g_baseTracker.Plan(entity, engineClip, outWeight, plan);
			if (plan.restore)
			{
				void* const previous = LookupState(entity, plan.restoreName);
				if (previous != nullptr && !GameObject::Detail::TrySetAnimationWeight(previous, 1.0f))
				{
					g_baseTracker.Forget();
					LatchFault("base restore", plan.restoreName);
					return false;
				}
			}
			if (plan.apply)
			{
				void* const current = LookupState(entity, plan.applyName);
				if (current != nullptr && !GameObject::Detail::TrySetAnimationWeight(current, plan.weight))
				{
					g_baseTracker.Forget();
					LatchFault("base weight", plan.applyName);
					return false;
				}
			}
			return true;
		}

		void FillResult(const TrackedLayer& tracked, LayerResult& result) noexcept
		{
			result.time = tracked.time;
			result.weight = tracked.lastWeight;
			result.effectiveSpeed = tracked.effectiveSpeed;
			result.finished = tracked.finished;
			result.finishedCount = tracked.finishedCount;
			result.playCount = tracked.playCount;
			result.clearSerial = tracked.clearDoneSerial;
		}

		// One layer on one tick. Returns false only on an Ogre fault.
		bool ApplyLayer(
			void* entity,
			float dt,
			const char* engineClip,
			bool triggerHeld,
			const LayerSpec& spec,
			TrackedLayer& tracked,
			LayerResult& result) noexcept
		{
			FillResult(tracked, result);
			if (IsEngineOwned(spec.name, engineClip))
			{
				// Advanced by Person::Simulate this tick; leave enable, weight,
				// loop and time alone, and hold EXU's clock, ramps and speed
				// where they are.
				result.reason = Reason::EngineOwned;
				return true;
			}
			if (tracked.ended)
			{
				result.reason = Reason::Ended;
				return true;
			}

			void* const state = LookupState(entity, spec.name);
			if (state == nullptr)
			{
				result.reason = Reason::Missing;
				if (tracked.lastMissingEntity != entity)
				{
					tracked.lastMissingEntity = entity;
					LogNoThrow(
						"exu: first-person layer '%s' is not an animation on the local pilot's first-person skeleton; skipped",
						spec.name);
				}
				return true;
			}
			tracked.lastMissingEntity = nullptr;

			float length = 0.0f;
			if (!GameObject::Detail::TryGetAnimationLength(state, length))
			{
				LatchFault("getLength", spec.name);
				return false;
			}

			const LayerStep step = StepLayer(spec, tracked, dt, length, triggerHeld);
			FillResult(tracked, result);
			result.length = length;
			result.reason = step.reason;

			if (step.action == StepAction::Disable)
			{
				if (!DisableState(state))
				{
					LatchFault("disable", spec.name);
					return false;
				}
				return true;
			}
			if (step.action == StepAction::Skip)
			{
				return true;
			}

			// Re-asserted every tick: the stock apply helpers disable the old
			// FSM clip by name, and model setup may reset states, so a layer
			// that shares a name with an FSM clip, or a new entity, comes back
			// here. Setting the time from EXU's clock (rather than addTime)
			// also carries the layer across a first-person entity change.
			if (!GameObject::Detail::TrySetAnimationEnabled(state, true) ||
				!GameObject::Detail::TrySetAnimationLoop(state, spec.loop) ||
				!GameObject::Detail::TrySetAnimationWeight(state, step.weight) ||
				!GameObject::Detail::TrySetAnimationTimePosition(state, tracked.time))
			{
				LatchFault("apply", spec.name);
				return false;
			}

			WarnAverageOnce(entity, spec.name);
			return true;
		}

		// Drops entries the hook has finished with (LayerBook::Prune) and
		// republishes when anything changed.
		void PruneFromResults(bool evictEnded) noexcept
		{
			ResultSet results{};
			if (g_results.TryRead(results) && g_book.Prune(results, evictEnded))
			{
				g_published.Publish(g_book.Layers());
			}
		}

		// A fade-out clear needs ticks to complete; without the seam (or after
		// a fault) the removal is immediate instead.
		bool FadesAllowed() noexcept
		{
			return PilotFsmIntercept::IsActive() && !g_faulted.load(std::memory_order_acquire);
		}
	}

	Error SetLayer(const char* name, std::size_t length, const LayerOptions& options) noexcept
	{
		PruneFromResults(false);
		Error error = g_book.Set(name, length, options);
		if (error == Error::Full)
		{
			PruneFromResults(true);
			error = g_book.Set(name, length, options);
		}
		return PublishIfOk(error);
	}

	Error SetLayerSpeed(const char* name, std::size_t length, double speed) noexcept
	{
		PruneFromResults(false);
		return PublishIfOk(g_book.SetSpeed(name, length, speed));
	}

	Error SetLayerWeight(const char* name, std::size_t length, double weight, double fadeSeconds) noexcept
	{
		PruneFromResults(false);
		return PublishIfOk(g_book.SetWeight(name, length, weight, fadeSeconds));
	}

	Error ClearLayer(const char* name, std::size_t length, bool hasFade, double fadeSeconds) noexcept
	{
		PruneFromResults(false);
		return PublishIfOk(g_book.Clear(name, length, hasFade, fadeSeconds, FadesAllowed()));
	}

	void ClearLayers() noexcept
	{
		g_book.ClearAll();
		g_published.Publish(g_book.Layers());
	}

	Error PlayLayer(const char* name, std::size_t length, const PlayOptions& options) noexcept
	{
		PruneFromResults(false);
		Error error = g_book.Play(name, length, options);
		if (error == Error::Full)
		{
			PruneFromResults(true);
			error = g_book.Play(name, length, options);
		}
		return PublishIfOk(error);
	}

	Error SetBaseWeight(double weight, double fadeSeconds) noexcept
	{
		return PublishIfOk(g_book.SetBase(weight, fadeSeconds));
	}

	void GetBaseWeight(float& outCurrent, float& outTarget) noexcept
	{
		outTarget = g_book.Layers().base.weight;
		outCurrent = outTarget;
		ResultSet results{};
		if (IsAvailable() && g_results.TryRead(results))
		{
			outCurrent = results.baseWeight;
		}
	}

	std::size_t GetLayers(LayerReport (&out)[kMaxLayers]) noexcept
	{
		PruneFromResults(false);
		const LayerSet& layers = g_book.Layers();
		ResultSet results{};
		const bool haveResults = g_results.TryRead(results);
		const bool available = IsAvailable();
		const bool faulted = g_faulted.load(std::memory_order_acquire);

		for (std::size_t i = 0; i < layers.count; ++i)
		{
			LayerReport& report = out[i];
			report = LayerReport{};
			report.spec = layers.layers[i];
			report.triggerHeld = haveResults && results.triggerHeld;
			report.result.id = report.spec.id;
			report.result.reason = Reason::Pending;
			report.result.time = report.spec.seekSerial != 0 ? report.spec.seekTime : 0.0f;
			report.result.effectiveSpeed = report.spec.speed;

			if (haveResults)
			{
				if (const LayerResult* result = FindResult(results, report.spec.id))
				{
					report.result = *result;
				}
			}
			if (faulted)
			{
				report.result.reason = Reason::Faulted;
			}
			else if (!available)
			{
				report.result.reason = Reason::Unavailable;
			}
		}
		return layers.count;
	}

	bool IsAvailable() noexcept
	{
		return PilotFsmIntercept::IsActive();
	}

	void ResetMissionState() noexcept
	{
		g_book.ResetAll();
		g_published.Publish(g_book.Layers());
		g_results.Publish(ResultSet{});
		g_faulted.store(false, std::memory_order_release);
		g_tracker.Reset();
		g_baseTracker.Reset();
		g_blendEntity = nullptr;
		g_blendMode = BlendMode::Unknown;
		g_averageLoggedEntity = nullptr;
	}

	void ApplyLocal(void* firstPersonEntity, float dt, const char* engineClip, bool triggerHeld) noexcept
	{
		if (g_faulted.load(std::memory_order_acquire))
		{
			return;
		}

		LayerSet published{};
		if (!g_published.TryRead(published))
		{
			// Torn every attempt: try again next tick with nothing changed.
			return;
		}

		const float safeDt = (std::isfinite(dt) && dt > 0.0f) ? dt : 0.0f;
		ResultSet results{};
		results.triggerHeld = triggerHeld;

		// 1. Base weight: ramps on every local tick (with no entity it only
		// ramps), and is set on the FSM's current clip before the layers.
		if (!ApplyBase(firstPersonEntity, safeDt, engineClip, published.base, results.baseWeight))
		{
			return;
		}

		if (published.count == 0 && g_tracker.Count() == 0)
		{
			g_results.Publish(results);
			return;
		}

		if (firstPersonEntity == nullptr)
		{
			// Nothing to drive. Reconciliation, clocks and fades wait too, so
			// a layer cleared meanwhile is still disabled on the next tick
			// that has an entity.
			results.count = published.count;
			for (std::uint32_t i = 0; i < published.count && i < kMaxLayers; ++i)
			{
				results.layers[i].id = published.layers[i].id;
				results.layers[i].reason = Reason::NoFirstPersonEntity;
				results.layers[i].effectiveSpeed = published.layers[i].speed;
			}
			g_results.Publish(results);
			return;
		}

		// 2. Reconcile: new layers, seeks, replays, weight-ramp starts.
		Reconciliation reconciliation{};
		g_tracker.Reconcile(published, reconciliation);

		// 3. Cleared layers must not stay frozen enabled on the current entity.
		for (std::uint32_t i = 0; i < reconciliation.removedCount; ++i)
		{
			DisableRemoved(firstPersonEntity, reconciliation.removed[i]);
			if (g_faulted.load(std::memory_order_relaxed))
			{
				return;
			}
		}

		RefreshBlendMode(firstPersonEntity);

		// 4. Each layer: speed ramp, clock, weight ramp, end fade, finish.
		results.count = published.count;
		for (std::uint32_t i = 0; i < published.count && i < kMaxLayers; ++i)
		{
			const LayerSpec& spec = published.layers[i];
			TrackedLayer& tracked = g_tracker.At(reconciliation.trackedIndex[i]);
			LayerResult& result = results.layers[i];
			result.id = spec.id;
			result.blendMode = g_blendMode;
			if (!ApplyLayer(firstPersonEntity, safeDt, engineClip, triggerHeld, spec, tracked, result))
			{
				return;
			}
		}
		g_results.Publish(results);
	}

	float CurrentBaseWeight() noexcept
	{
		return g_baseTracker.Weight();
	}

	bool IsDrivenLayerName(const char* name) noexcept
	{
		if (name == nullptr || g_faulted.load(std::memory_order_acquire))
		{
			return false;
		}
		for (std::uint32_t i = 0; i < g_tracker.Count(); ++i)
		{
			const TrackedLayer& tracked = g_tracker.At(i);
			if (!tracked.ended && std::strncmp(tracked.name, name, kMaxLayerName + 1) == 0)
			{
				return true;
			}
		}
		return false;
	}
}
