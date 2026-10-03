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
		// Threading: g_book is the Lua thread's. g_tracker, g_blendEntity,
		// g_blendMode and g_averageLoggedEntity are the hook's (the
		// Person::Simulate thread). They meet only through the two sequence
		// locks and the fault latch. ResetMissionState touches both sides,
		// and runs only while the hook cannot (see the header).
		LayerBook g_book;
		Seqlock<LayerSet> g_published;
		Seqlock<ResultSet> g_results;
		std::atomic<bool> g_faulted{ false };

		Tracker g_tracker;
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

		// ClearLayer contract: disabled, weight 1, time 0 (if it still exists).
		void DisableRemoved(void* entity, const char* name) noexcept
		{
			void* const state = LookupState(entity, name);
			if (state == nullptr)
			{
				return;
			}
			if (!GameObject::Detail::TrySetAnimationEnabled(state, false) ||
				!GameObject::Detail::TrySetAnimationWeight(state, 1.0f) ||
				!GameObject::Detail::TrySetAnimationTimePosition(state, 0.0f))
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

		// One layer on one tick. Returns false only on an Ogre fault.
		bool ApplyLayer(
			void* entity,
			float dt,
			const char* engineClip,
			const LayerSpec& spec,
			TrackedLayer& tracked,
			LayerResult& result) noexcept
		{
			result.time = tracked.time;
			if (IsEngineOwned(spec.name, engineClip))
			{
				// Advanced by Person::Simulate this tick; leave enable, weight,
				// loop and time alone, and hold EXU's clock where it is.
				result.reason = Reason::EngineOwned;
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

			tracked.time = AdvanceTime(tracked.time, dt, spec.speed, length, spec.loop);

			// Re-asserted every tick: the stock apply helpers disable the old
			// FSM clip by name, and model setup may reset states, so a layer
			// that shares a name with an FSM clip, or a new entity, comes back
			// here. Setting the time from EXU's clock (rather than addTime)
			// also carries the layer across a first-person entity change.
			if (!GameObject::Detail::TrySetAnimationEnabled(state, true) ||
				!GameObject::Detail::TrySetAnimationLoop(state, spec.loop) ||
				!GameObject::Detail::TrySetAnimationWeight(state, spec.weight) ||
				!GameObject::Detail::TrySetAnimationTimePosition(state, tracked.time))
			{
				LatchFault("apply", spec.name);
				return false;
			}

			result.reason = Reason::Active;
			result.time = tracked.time;
			result.length = length;
			WarnAverageOnce(entity, spec.name);
			return true;
		}
	}

	Error SetLayer(const char* name, std::size_t length, const LayerOptions& options) noexcept
	{
		return PublishIfOk(g_book.Set(name, length, options));
	}

	Error SetLayerSpeed(const char* name, std::size_t length, double speed) noexcept
	{
		return PublishIfOk(g_book.SetSpeed(name, length, speed));
	}

	Error SetLayerWeight(const char* name, std::size_t length, double weight) noexcept
	{
		return PublishIfOk(g_book.SetWeight(name, length, weight));
	}

	Error ClearLayer(const char* name, std::size_t length) noexcept
	{
		return PublishIfOk(g_book.Clear(name, length));
	}

	void ClearLayers() noexcept
	{
		g_book.ClearAll();
		g_published.Publish(g_book.Layers());
	}

	std::size_t GetLayers(LayerReport (&out)[kMaxLayers]) noexcept
	{
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
			report.result.id = report.spec.id;
			report.result.reason = Reason::Pending;
			report.result.time = report.spec.seekSerial != 0 ? report.spec.seekTime : 0.0f;

			if (haveResults)
			{
				for (std::size_t r = 0; r < results.count && r < kMaxLayers; ++r)
				{
					if (results.layers[r].id == report.spec.id)
					{
						report.result = results.layers[r];
						break;
					}
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
		g_book.ClearAll();
		g_published.Publish(LayerSet{});
		g_results.Publish(ResultSet{});
		g_faulted.store(false, std::memory_order_release);
		g_tracker.Reset();
		g_blendEntity = nullptr;
		g_blendMode = BlendMode::Unknown;
		g_averageLoggedEntity = nullptr;
	}

	void ApplyLocal(void* firstPersonEntity, float dt, const char* engineClip) noexcept
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
		if (published.count == 0 && g_tracker.Count() == 0)
		{
			return;
		}

		if (firstPersonEntity == nullptr)
		{
			// Nothing to drive. Reconciliation waits too, so a layer cleared
			// meanwhile is still disabled on the next tick that has an entity.
			ResultSet results{};
			results.count = published.count;
			for (std::uint32_t i = 0; i < published.count && i < kMaxLayers; ++i)
			{
				results.layers[i].id = published.layers[i].id;
				results.layers[i].reason = Reason::NoFirstPersonEntity;
			}
			g_results.Publish(results);
			return;
		}

		Reconciliation reconciliation{};
		g_tracker.Reconcile(published, reconciliation);

		// Cleared layers must not stay frozen enabled on the current entity.
		for (std::uint32_t i = 0; i < reconciliation.removedCount; ++i)
		{
			DisableRemoved(firstPersonEntity, reconciliation.removed[i]);
			if (g_faulted.load(std::memory_order_relaxed))
			{
				return;
			}
		}

		RefreshBlendMode(firstPersonEntity);

		const float safeDt = (std::isfinite(dt) && dt > 0.0f) ? dt : 0.0f;
		ResultSet results{};
		results.count = published.count;
		for (std::uint32_t i = 0; i < published.count && i < kMaxLayers; ++i)
		{
			const LayerSpec& spec = published.layers[i];
			TrackedLayer& tracked = g_tracker.At(reconciliation.trackedIndex[i]);
			LayerResult& result = results.layers[i];
			result.id = spec.id;
			result.blendMode = g_blendMode;
			if (!ApplyLayer(firstPersonEntity, safeDt, engineClip, spec, tracked, result))
			{
				return;
			}
		}
		g_results.Publish(results);
	}
}
