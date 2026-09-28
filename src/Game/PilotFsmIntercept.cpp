/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#include "Game/PilotFsmIntercept.h"

#include "EntryDetour32.h"
#include "Game/PilotState.h"
#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "bzr.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>

namespace ExtraUtilities::Lua::PilotFsmIntercept
{
	namespace
	{
		using PersonSimulateFn = void(__thiscall*)(void* person, float dt);

		constexpr std::size_t kPersonSimulateDetourLength = 10;

		// push ebp; mov ebp,esp; push -1; push 0x0084C1D6
		//
		// These are complete, reloc-free instructions. The trampoline resumes at
		// the following "mov eax,fs:[0]" SEH-frame instruction. Identity is also
		// catalogued in exu.json as PersonRuntime.PersonSimulate.
		constexpr std::array<std::uint8_t, kPersonSimulateDetourLength>
			kExpectedPersonSimulateEntry = {
				0x55, 0x8B, 0xEC, 0x6A, 0xFF,
				0x68, 0xD6, 0xC1, 0x84, 0x00
			};

		std::unique_ptr<EntryDetour32> g_detour;
		PersonSimulateFn g_originalPersonSimulate = nullptr;

		std::atomic<std::uint32_t> g_calls{ 0 };
		std::atomic<std::uint32_t> g_localCalls{ 0 };
		std::atomic<std::uint32_t> g_stateChanges{ 0 };
		std::atomic<std::uint32_t> g_animationChanges{ 0 };
		std::atomic<bool> g_hasLocalSample{ false };
		std::atomic<std::uint32_t> g_lastBeforeState{ 0 };
		std::atomic<std::uint32_t> g_lastAfterState{ 0 };
		std::atomic<std::int32_t> g_lastBeforeAnimation{ -1 };
		std::atomic<std::int32_t> g_lastAfterAnimation{ -1 };
		std::atomic<std::int32_t> g_lastBeforeAnimationHandle{ -1 };
		std::atomic<std::int32_t> g_lastAfterAnimationHandle{ -1 };

		void RecordLocalPair(
			const PilotState::Snapshot& before,
			const PilotState::Snapshot& after) noexcept
		{
			g_hasLocalSample.store(true, std::memory_order_relaxed);
			g_lastBeforeState.store(before.nativeState, std::memory_order_relaxed);
			g_lastAfterState.store(after.nativeState, std::memory_order_relaxed);
			g_lastBeforeAnimation.store(before.animationIndex, std::memory_order_relaxed);
			g_lastAfterAnimation.store(after.animationIndex, std::memory_order_relaxed);
			g_lastBeforeAnimationHandle.store(before.animationHandle, std::memory_order_relaxed);
			g_lastAfterAnimationHandle.store(after.animationHandle, std::memory_order_relaxed);

			if (before.nativeState != after.nativeState)
			{
				g_stateChanges.fetch_add(1, std::memory_order_relaxed);
			}
			if (before.animationIndex != after.animationIndex ||
				before.animationHandle != after.animationHandle)
			{
				g_animationChanges.fetch_add(1, std::memory_order_relaxed);
			}
		}

		void __fastcall PersonSimulateObserveHook(
			void* person,
			void* /*edx*/,
			float dt) noexcept
		{
			g_calls.fetch_add(1, std::memory_order_relaxed);

			PilotState::Snapshot before{};
			const bool isLocal = PilotState::CaptureIfCurrent(person, before);
			if (isLocal)
			{
				g_localCalls.fetch_add(1, std::memory_order_relaxed);
			}

			// Install prepares the trampoline and publishes this pointer before
			// activating the entry patch, so null cannot occur on a valid live
			// detour. Keep the check fail-closed for defensive teardown edges.
			const PersonSimulateFn original = g_originalPersonSimulate;
			if (original == nullptr)
			{
				return;
			}

			// Observe-only seam: stock behavior always executes unchanged.
			original(person, dt);

			if (isLocal)
			{
				PilotState::Snapshot after{};
				if (PilotState::CaptureIfCurrent(person, after))
				{
					RecordLocalPair(before, after);
				}
			}
		}
	}

	bool Install() noexcept
	{
		if (!RuntimeGate::IsSupported())
		{
			return false;
		}

		if (g_detour == nullptr)
		{
			auto* raw = new (std::nothrow) EntryDetour32(
				BZR::PersonRuntime::PersonSimulate,
				reinterpret_cast<const void*>(&PersonSimulateObserveHook),
				kPersonSimulateDetourLength,
				BasicPatch::Status::INACTIVE,
				std::vector<std::uint8_t>(
					kExpectedPersonSimulateEntry.begin(),
					kExpectedPersonSimulateEntry.end()));
			if (raw == nullptr)
			{
				Logging::LogMessage("exu: failed to allocate Person::Simulate interception seam");
				return false;
			}

			g_detour.reset(raw);
			if (!g_detour->PrepareTrampoline())
			{
				Logging::LogMessage(
					"exu: Person::Simulate interception seam unavailable; entry identity/preimage did not qualify");
				g_detour.reset();
				g_originalPersonSimulate = nullptr;
				return false;
			}

			g_originalPersonSimulate =
				g_detour->GetTrampolineAs<PersonSimulateFn>();
			if (g_originalPersonSimulate == nullptr)
			{
				Logging::LogMessage("exu: Person::Simulate interception seam produced no trampoline");
				g_detour.reset();
				return false;
			}
		}

		g_detour->SetStatus(true);
		if (!g_detour->IsActive())
		{
			Logging::LogMessage(
				"exu: Person::Simulate interception seam prepared but entry patch could not activate");
			return false;
		}

		Logging::LogMessage(
			"exu: Person::Simulate interception seam active (observe-only)");
		return true;
	}

	bool IsInstalled() noexcept
	{
		return g_detour != nullptr && g_originalPersonSimulate != nullptr;
	}

	bool IsActive() noexcept
	{
		return IsInstalled() && g_detour->IsActive();
	}

	void ResetStats() noexcept
	{
		g_calls.store(0, std::memory_order_relaxed);
		g_localCalls.store(0, std::memory_order_relaxed);
		g_stateChanges.store(0, std::memory_order_relaxed);
		g_animationChanges.store(0, std::memory_order_relaxed);
		g_hasLocalSample.store(false, std::memory_order_relaxed);
		g_lastBeforeState.store(0, std::memory_order_relaxed);
		g_lastAfterState.store(0, std::memory_order_relaxed);
		g_lastBeforeAnimation.store(-1, std::memory_order_relaxed);
		g_lastAfterAnimation.store(-1, std::memory_order_relaxed);
		g_lastBeforeAnimationHandle.store(-1, std::memory_order_relaxed);
		g_lastAfterAnimationHandle.store(-1, std::memory_order_relaxed);
	}

	void GetStats(Stats& outStats) noexcept
	{
		outStats = {};
		outStats.installed = IsInstalled();
		outStats.active = IsActive();
		outStats.observeOnly = true;
		outStats.hasLocalSample = g_hasLocalSample.load(std::memory_order_relaxed);
		outStats.calls = g_calls.load(std::memory_order_relaxed);
		outStats.localCalls = g_localCalls.load(std::memory_order_relaxed);
		outStats.stateChanges = g_stateChanges.load(std::memory_order_relaxed);
		outStats.animationChanges = g_animationChanges.load(std::memory_order_relaxed);
		outStats.lastBeforeState = g_lastBeforeState.load(std::memory_order_relaxed);
		outStats.lastAfterState = g_lastAfterState.load(std::memory_order_relaxed);
		outStats.lastBeforeAnimation = g_lastBeforeAnimation.load(std::memory_order_relaxed);
		outStats.lastAfterAnimation = g_lastAfterAnimation.load(std::memory_order_relaxed);
		outStats.lastBeforeAnimationHandle =
			g_lastBeforeAnimationHandle.load(std::memory_order_relaxed);
		outStats.lastAfterAnimationHandle =
			g_lastAfterAnimationHandle.load(std::memory_order_relaxed);
	}
}
