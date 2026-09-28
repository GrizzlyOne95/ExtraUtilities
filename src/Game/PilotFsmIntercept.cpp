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
#include "Game/PilotAnimationPolicy.h"
#include "Game/PilotState.h"
#include "Game/PilotTrace.h"
#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "bzr.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
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

		EntryDetour32* g_detour = nullptr;
		PersonSimulateFn g_originalPersonSimulate = nullptr;

		std::atomic<std::uint32_t> g_calls{ 0 };
		std::atomic<std::uint32_t> g_localCalls{ 0 };
		std::atomic<std::uint32_t> g_stateChanges{ 0 };
		std::atomic<std::uint32_t> g_animationChanges{ 0 };
		std::atomic<bool> g_hasLocalSample{ false };
		std::atomic<bool> g_hasPolicyDecision{ false };
		std::atomic<std::uint8_t> g_lastPolicyDecision{
			static_cast<std::uint8_t>(PilotAnimationPolicy::Decision::PassThrough) };
		std::atomic<std::uint32_t> g_lastBeforeState{ 0 };
		std::atomic<std::uint32_t> g_lastAfterState{ 0 };
		std::atomic<std::int32_t> g_lastBeforeAnimation{ -1 };
		std::atomic<std::int32_t> g_lastAfterAnimation{ -1 };
		std::atomic<std::int32_t> g_lastBeforeAnimationHandle{ -1 };
		std::atomic<std::int32_t> g_lastAfterAnimationHandle{ -1 };

		// Opt-in timing trace. Only this hook writes it; Lua starts, stops, and
		// reads it (see PilotTrace.h for the threading contract).
		PilotTrace::Recorder g_trace;

		PilotTrace::Frame ToTraceFrame(const PilotState::Snapshot& snapshot) noexcept
		{
			PilotTrace::Frame frame{};
			frame.nativeState = snapshot.nativeState;
			frame.animationIndex = snapshot.animationIndex;
			frame.animationHandle = snapshot.animationHandle;
			return frame;
		}

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

		// ABI bridge: stock Person::Simulate is __thiscall, so Person* arrives in
		// ECX and dt is the single stack argument. An x86 __fastcall wrapper
		// consumes ECX/EDX as its first two parameters, leaving dt in exactly the
		// same stack slot and using the same callee-pop size (ret 4). The entry
		// push/ret transfer therefore does not need a naked assembly adapter.
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

				// Consulted before stock runs so an override has a defined ordering
				// point relative to the native state it would replace. Only
				// pass-through exists, so nothing below branches on the result.
				g_lastPolicyDecision.store(
					static_cast<std::uint8_t>(PilotAnimationPolicy::EvaluateActive(before.nativeState)),
					std::memory_order_relaxed);
				g_hasPolicyDecision.store(true, std::memory_order_relaxed);
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
					g_trace.Record(dt, ToTraceFrame(before), ToTraceFrame(after));
				}
			}
		}
	}

	bool Install() noexcept
	{
		try
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

			g_detour = raw;
			if (!g_detour->PrepareTrampoline())
			{
				Logging::LogMessage(
					"exu: Person::Simulate interception seam unavailable; entry identity/preimage did not qualify");
				delete g_detour;
				g_detour = nullptr;
				g_originalPersonSimulate = nullptr;
				return false;
			}

			g_originalPersonSimulate =
				g_detour->GetTrampolineAs<PersonSimulateFn>();
			if (g_originalPersonSimulate == nullptr)
			{
				Logging::LogMessage("exu: Person::Simulate interception seam produced no trampoline");
				delete g_detour;
				g_detour = nullptr;
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
		catch (...)
		{
			// luaopen_exu itself is a C ABI entry point, so no C++ exception may
			// escape Init. Avoid allocation-heavy logging on this failure path.
			OutputDebugStringA(
				"ExtraUtilities: exception while installing Person::Simulate interception seam\n");
			if (g_detour != nullptr)
			{
				delete g_detour;
				g_detour = nullptr;
			}
			g_originalPersonSimulate = nullptr;
			return false;
		}
	}

	bool IsInstalled() noexcept
	{
		return g_detour != nullptr && g_originalPersonSimulate != nullptr;
	}

	bool IsActive() noexcept
	{
		return IsInstalled() && g_detour->IsActive();
	}

	void Shutdown() noexcept
	{
		if (g_detour != nullptr)
		{
			delete g_detour;
			g_detour = nullptr;
		}
		g_originalPersonSimulate = nullptr;
		ResetStats();
	}

	void ResetStats() noexcept
	{
		g_calls.store(0, std::memory_order_relaxed);
		g_localCalls.store(0, std::memory_order_relaxed);
		g_stateChanges.store(0, std::memory_order_relaxed);
		g_animationChanges.store(0, std::memory_order_relaxed);
		g_hasLocalSample.store(false, std::memory_order_relaxed);
		g_hasPolicyDecision.store(false, std::memory_order_relaxed);
		g_lastPolicyDecision.store(
			static_cast<std::uint8_t>(PilotAnimationPolicy::Decision::PassThrough),
			std::memory_order_relaxed);
		g_lastBeforeState.store(0, std::memory_order_relaxed);
		g_lastAfterState.store(0, std::memory_order_relaxed);
		g_lastBeforeAnimation.store(-1, std::memory_order_relaxed);
		g_lastAfterAnimation.store(-1, std::memory_order_relaxed);
		g_lastBeforeAnimationHandle.store(-1, std::memory_order_relaxed);
		g_lastAfterAnimationHandle.store(-1, std::memory_order_relaxed);
		g_trace.Reset();
	}

	void GetStats(Stats& outStats) noexcept
	{
		outStats = {};
		outStats.installed = IsInstalled();
		outStats.active = IsActive();
		outStats.observeOnly = true;
		outStats.hasLocalSample = g_hasLocalSample.load(std::memory_order_relaxed);
		outStats.hasPolicyDecision = g_hasPolicyDecision.load(std::memory_order_relaxed);
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
		outStats.lastPolicyDecision = static_cast<PilotAnimationPolicy::Decision>(
			g_lastPolicyDecision.load(std::memory_order_relaxed));
	}

	void StartTrace(bool changesOnly) noexcept
	{
		g_trace.Start(changesOnly);
	}

	void StopTrace() noexcept
	{
		g_trace.Stop();
	}

	bool ReadTrace(PilotTrace::Snapshot& outSnapshot) noexcept
	{
		return g_trace.Read(outSnapshot);
	}
}
