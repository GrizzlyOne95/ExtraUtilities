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
#include "Game/FirstPersonLayers.h"
#include "Game/FirstPersonTarget.h"
#include "Game/GameObject.h"
#include "Game/PilotAnimationPolicy.h"
#include "Game/PilotState.h"
#include "Game/PilotTrace.h"
#include "Game/PilotTransitionTiming.h"
#include "Game/PlayerTrigger.h"
#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "bzr.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace ExtraUtilities::Lua::PilotFsmIntercept
{
	namespace
	{
		using PersonSimulateFn = void(__thiscall*)(void* person, float dt);

		using PilotAnimationPolicy::CompletionMode;
		using PilotAnimationPolicy::Decision;
		using PilotAnimationPolicy::Mode;
		using PilotAnimationPolicy::Policy;
		using PilotAnimationPolicy::Slot;
		using PilotAnimationPolicy::kSlotCount;

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
		std::atomic<std::uint32_t> g_overrideCalls{ 0 };
		std::atomic<bool> g_hasLocalSample{ false };
		std::atomic<bool> g_hasPolicyDecision{ false };
		std::atomic<std::uint8_t> g_lastPolicyDecision{
			static_cast<std::uint8_t>(Decision::PassThrough) };
		std::atomic<std::uint32_t> g_lastBeforeState{ 0 };
		std::atomic<std::uint32_t> g_lastAfterState{ 0 };
		std::atomic<std::int32_t> g_lastBeforeAnimation{ -1 };
		std::atomic<std::int32_t> g_lastAfterAnimation{ -1 };
		std::atomic<std::int32_t> g_lastBeforeAnimationHandle{ -1 };
		std::atomic<std::int32_t> g_lastAfterAnimationHandle{ -1 };

		// Opt-in timing trace. Only this hook writes it; Lua starts, stops, and
		// reads it (see PilotTrace.h for the threading contract).
		PilotTrace::Recorder g_trace;

		// ----- Native clip-table override seam ---------------------------------
		//
		// Person::Simulate reads six 12-entry tables in writable .data and
		// nothing else reads them (Docs/Research/PILOT_CROUCH_NATIVE_RE_20261003.md).
		// For the local Person the hook overwrites the entries of the six policy
		// slots (names, end times, first-person and world rates) right before
		// the stock call and puts the snapshot back right after it. Start times
		// and loop bytes are never touched.
		//
		// Threading: everything below except g_tablesQualified and
		// g_manualCompleteRequested is touched only by the hook (the Simulate
		// thread). Lua publishes the policy through PolicyPublisher and requests
		// manual completion through the atomic flag.

		// Stock names and value bit patterns of the entries the seam touches, in
		// Slot order (stand, enterCrouch, crouched, exitCrouch, jump, land; table
		// indices 2, 0, 3, 1, 11, 10). Verified against the shipped image.
		constexpr const char* kStockNames[kSlotCount] = {
			"idle", "stand2Kneel", "fireRecoilSniper", "kneel2stand", "jump", "landParachute",
		};

		struct StockBits
		{
			std::uint32_t end;
			std::uint32_t fpRate;
			std::uint32_t worldRate;
		};

		// 0.967 = 0x3F778D50, 1.167 = 0x3F956042, 0.5 = 0x3F000000,
		// 0.75 = 0x3F400000, 0.05 = 0x3D4CCCCD, 0.6 = 0x3F19999A.
		constexpr StockBits kStockBits[kSlotCount] = {
			{ 0x3F778D50u, 0x3F000000u, 0x3F000000u }, // idx 2  idle
			{ 0x3F778D50u, 0x3F000000u, 0x3F000000u }, // idx 0  stand2Kneel
			{ 0x3F778D50u, 0x3F000000u, 0x3F000000u }, // idx 3  fireRecoilSniper
			{ 0x3F778D50u, 0x3F000000u, 0x3F000000u }, // idx 1  kneel2stand
			{ 0x3F956042u, 0x3D4CCCCDu, 0x3F19999Au }, // idx 11 jump
			{ 0x3F956042u, 0x3F400000u, 0x3F400000u }, // idx 10 landParachute
		};

		// The touched entries of the four tables, in Slot order.
		struct TableValues
		{
			float end[kSlotCount];
			float fpRate[kSlotCount];
			float worldRate[kSlotCount];
			const char* name[kSlotCount];
		};

		std::uint32_t FloatBits(float value) noexcept
		{
			std::uint32_t bits = 0;
			std::memcpy(&bits, &value, sizeof(bits));
			return bits;
		}

		bool SameValues(const TableValues& a, const TableValues& b) noexcept
		{
			for (std::size_t i = 0; i < kSlotCount; ++i)
			{
				if (FloatBits(a.end[i]) != FloatBits(b.end[i]) ||
					FloatBits(a.fpRate[i]) != FloatBits(b.fpRate[i]) ||
					FloatBits(a.worldRate[i]) != FloatBits(b.worldRate[i]) ||
					a.name[i] != b.name[i])
				{
					return false;
				}
			}
			return true;
		}

		// POD-only SEH shells: raw .data reads and writes, nothing that throws.
		bool ReadTablesSeh(TableValues& out) noexcept
		{
			__try
			{
				for (std::size_t i = 0; i < kSlotCount; ++i)
				{
					const std::int32_t index =
						PilotTransitionTiming::TableIndexForSlot(static_cast<Slot>(i));
					out.end[i] = BZR::PersonRuntime::animEndTime[index];
					out.fpRate[i] = BZR::PersonRuntime::animFirstPersonRate[index];
					out.worldRate[i] = BZR::PersonRuntime::animWorldRate[index];
					out.name[i] = BZR::PersonRuntime::animName[index];
				}
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool WriteTablesSeh(const TableValues& values) noexcept
		{
			__try
			{
				for (std::size_t i = 0; i < kSlotCount; ++i)
				{
					const std::int32_t index =
						PilotTransitionTiming::TableIndexForSlot(static_cast<Slot>(i));
					BZR::PersonRuntime::animEndTime[index] = values.end[i];
					BZR::PersonRuntime::animFirstPersonRate[index] = values.fpRate[i];
					BZR::PersonRuntime::animWorldRate[index] = values.worldRate[i];
					BZR::PersonRuntime::animName[index] = values.name[i];
				}
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// Bounded compare of an engine string against an expected name.
		bool NameEqualsSeh(const char* engineName, const char* expected) noexcept
		{
			if (engineName == nullptr)
			{
				return false;
			}
			__try
			{
				for (std::size_t i = 0; i < 64; ++i)
				{
					if (engineName[i] != expected[i])
					{
						return false;
					}
					if (expected[i] == '\0')
					{
						return true;
					}
				}
				return false;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool IsNetGameSeh() noexcept
		{
			__try
			{
				return *BZR::Multiplayer::isNetGame;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				// Unreadable: treat as a network session so overrides stand down.
				return true;
			}
		}

		// Set once by Install after the preimage check; cleared at Shutdown, or
		// for the rest of the Lua state if a table write ever faults.
		std::atomic<bool> g_tablesQualified{ false };
		// The verified stock name pointers (executable .rdata strings).
		const char* g_stockNames[kSlotCount]{};

		// One-shot exu.fps.CompleteTransition() request (Lua -> hook).
		std::atomic<bool> g_manualCompleteRequested{ false };

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
				OutputDebugStringA("ExtraUtilities: pilot override log line dropped\n");
			}
		}

		bool QualifyTables() noexcept
		{
			TableValues stock{};
			if (!ReadTablesSeh(stock))
			{
				LogNoThrow("exu: pilot animation overrides unavailable; clip tables could not be read");
				return false;
			}

			for (std::size_t i = 0; i < kSlotCount; ++i)
			{
				const std::int32_t index =
					PilotTransitionTiming::TableIndexForSlot(static_cast<Slot>(i));
				if (FloatBits(stock.end[i]) != kStockBits[i].end ||
					FloatBits(stock.fpRate[i]) != kStockBits[i].fpRate ||
					FloatBits(stock.worldRate[i]) != kStockBits[i].worldRate)
				{
					LogNoThrow(
						"exu: pilot animation overrides unavailable; clip table index %d timing preimage mismatch (end=%08X fp=%08X world=%08X)",
						static_cast<int>(index),
						static_cast<unsigned>(FloatBits(stock.end[i])),
						static_cast<unsigned>(FloatBits(stock.fpRate[i])),
						static_cast<unsigned>(FloatBits(stock.worldRate[i])));
					return false;
				}
				if (!NameEqualsSeh(stock.name[i], kStockNames[i]))
				{
					LogNoThrow(
						"exu: pilot animation overrides unavailable; clip table index %d does not name '%s'",
						static_cast<int>(index),
						kStockNames[i]);
					return false;
				}
			}

			for (std::size_t i = 0; i < kSlotCount; ++i)
			{
				g_stockNames[i] = stock.name[i];
			}
			return true;
		}

		// ----- Substitute name pool -----------------------------------------------
		//
		// The render bridge keeps the raw name pointer of the applied clip and
		// rebuilds a std::string from it every tick, possibly after this DLL has
		// unloaded at the end of the mission. So substitute names live on the
		// process heap, are appended once, and are never freed or overwritten.
		// Bounded: a mission can intern at most kNamePoolCapacity distinct names.

		constexpr std::size_t kNamePoolCapacity = 64;
		const char* g_namePool[kNamePoolCapacity]{};
		bool g_namePoolLogged[kNamePoolCapacity]{};
		std::size_t g_namePoolCount = 0;
		bool g_namePoolFullLogged = false;

		// Pool index of an interned pointer, or kNamePoolCapacity.
		std::size_t PoolIndexOf(const char* pooled) noexcept
		{
			for (std::size_t i = 0; i < g_namePoolCount; ++i)
			{
				if (g_namePool[i] == pooled)
				{
					return i;
				}
			}
			return kNamePoolCapacity;
		}

		const char* InternName(const char* name) noexcept
		{
			for (std::size_t i = 0; i < g_namePoolCount; ++i)
			{
				if (std::strcmp(g_namePool[i], name) == 0)
				{
					return g_namePool[i];
				}
			}
			if (g_namePoolCount >= kNamePoolCapacity)
			{
				if (!g_namePoolFullLogged)
				{
					g_namePoolFullLogged = true;
					LogNoThrow("exu: pilot animation substitute name pool is full (%u names); further substitutes stay stock",
						static_cast<unsigned>(kNamePoolCapacity));
				}
				return nullptr;
			}

			const std::size_t length = std::strlen(name);
			HANDLE heap = GetProcessHeap();
			auto* const copy = heap != nullptr
				? static_cast<char*>(HeapAlloc(heap, 0, length + 1))
				: nullptr;
			if (copy == nullptr)
			{
				return nullptr;
			}
			std::memcpy(copy, name, length + 1);
			g_namePool[g_namePoolCount++] = copy;
			return copy;
		}

		// Interned pointer of each slot's substitute, re-interned only when the
		// policy's name changes.
		const char* g_slotSubstitute[kSlotCount]{};

		const char* SubstituteNameFor(std::size_t slot, const char* wanted) noexcept
		{
			if (g_slotSubstitute[slot] == nullptr || std::strcmp(g_slotSubstitute[slot], wanted) != 0)
			{
				g_slotSubstitute[slot] = InternName(wanted);
			}
			return g_slotSubstitute[slot];
		}

		// ----- Clip validation cache ----------------------------------------------
		//
		// An Ogre name missing from either the WORLD or the first-person
		// skeleton makes Entity::getAnimationState throw inside the stock apply
		// helpers, with no handler: a crash. So no name reaches the table before
		// both entities are known to have it. Results are cached per (Person,
		// WORLD entity, FP entity) and dropped when any of them changes.

		struct ClipCheck
		{
			const char* name = nullptr;
			bool onFirstPerson = false;
			bool onWorld = false;
			float length = 0.0f;
		};

		constexpr std::size_t kClipCheckCapacity = 16;
		ClipCheck g_clipChecks[kClipCheckCapacity]{};
		std::size_t g_clipCheckCount = 0;
		std::size_t g_clipCheckNext = 0;
		const void* g_checkPerson = nullptr;
		void* g_checkWorld = nullptr;
		void* g_checkFirstPerson = nullptr;

		void ClearClipChecks() noexcept
		{
			g_clipCheckCount = 0;
			g_clipCheckNext = 0;
		}

		// Ogre queries allocate (std::string), so they run behind a catch. The
		// GameObject helpers already guard faults with SEH and C++ catches.
		bool ProbeFirstPerson(void* entity, const char* name, float& outLength) noexcept
		{
			outLength = 0.0f;
			try
			{
				GameObject::EntityAnimationInfo info{};
				if (!GameObject::GetAnimationInfo(entity, std::string(name), info))
				{
					return false;
				}
				outLength = info.length;
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		bool ProbeWorld(void* entity, const char* name) noexcept
		{
			try
			{
				return GameObject::HasAnimation(entity, std::string(name));
			}
			catch (...)
			{
				return false;
			}
		}

		const ClipCheck& CheckClip(const char* name) noexcept
		{
			for (std::size_t i = 0; i < g_clipCheckCount; ++i)
			{
				if (g_clipChecks[i].name == name)
				{
					return g_clipChecks[i];
				}
			}

			ClipCheck check{};
			check.name = name;
			if (g_checkFirstPerson != nullptr)
			{
				check.onFirstPerson = ProbeFirstPerson(g_checkFirstPerson, name, check.length);
			}
			if (g_checkWorld != nullptr)
			{
				check.onWorld = g_checkWorld == g_checkFirstPerson
					? check.onFirstPerson
					: ProbeWorld(g_checkWorld, name);
			}

			std::size_t slot = g_clipCheckNext;
			if (g_clipCheckCount < kClipCheckCapacity)
			{
				slot = g_clipCheckCount++;
			}
			else
			{
				g_clipCheckNext = (g_clipCheckNext + 1) % kClipCheckCapacity;
			}
			g_clipChecks[slot] = check;
			return g_clipChecks[slot];
		}

		// Safe to hand to both stock apply helpers.
		bool IsClipUsable(const ClipCheck& check) noexcept
		{
			return check.onFirstPerson && check.onWorld &&
				PilotTransitionTiming::IsPositiveFinite(check.length);
		}

		void LogRejectedOnce(const char* pooled, const char* slotName) noexcept
		{
			const std::size_t index = PoolIndexOf(pooled);
			if (index >= kNamePoolCapacity || g_namePoolLogged[index])
			{
				return;
			}
			g_namePoolLogged[index] = true;
			LogNoThrow(
				"exu: pilot animation substitute '%s' for %s stays stock: the local pilot's WORLD and first-person entities must both have it",
				pooled,
				slotName);
		}

		// ----- Applied-clip record ----------------------------------------------
		//
		// The apply helpers disable names[current index] as the OLD clip. If
		// that entry no longer names the clip that is actually enabled (the
		// policy changed mid-clip, or was reset to stock), the old clip is
		// never disabled and two clips blend. So the hook remembers which name
		// it applied for the local Person's current index and keeps that entry
		// pointing at it until the index changes. A new substitute for the
		// current index therefore takes effect at the next apply of that index.

		const void* g_recordPerson = nullptr;
		std::int32_t g_recordIndex = -1;
		const char* g_recordName = nullptr;

		bool IsStockName(const char* name) noexcept
		{
			for (std::size_t i = 0; i < kSlotCount; ++i)
			{
				if (g_stockNames[i] == name)
				{
					return true;
				}
			}
			return false;
		}

		void ResetOverrideState() noexcept
		{
			ClearClipChecks();
			g_checkPerson = nullptr;
			g_checkWorld = nullptr;
			g_checkFirstPerson = nullptr;
			g_recordPerson = nullptr;
			g_recordIndex = -1;
			g_recordName = nullptr;
			for (std::size_t i = 0; i < kSlotCount; ++i)
			{
				g_slotSubstitute[i] = nullptr;
			}
			g_manualCompleteRequested.store(false, std::memory_order_relaxed);
			// The name pool is deliberately kept: the engine may still hold its
			// pointers, and it never frees them anyway.
		}

		// What one local call writes around the stock call.
		struct TablePlan
		{
			bool write = false;
			TableValues stock{};
			TableValues desired{};
			// Names in force during the call, in Slot order (stock if unwritten).
			const char* inForce[kSlotCount]{};
		};

		void PlanTableOverride(
			const void* person,
			const PilotState::Snapshot& before,
			const Policy& policy,
			Decision decision,
			bool manualRequested,
			TablePlan& plan) noexcept
		{
			plan = {};
			for (std::size_t i = 0; i < kSlotCount; ++i)
			{
				plan.inForce[i] = g_stockNames[i];
			}

			if (!g_tablesQualified.load(std::memory_order_acquire) || IsNetGameSeh())
			{
				return;
			}
			// Fail closed on a native state the policy layer does not map.
			Slot stateSlot = Slot::Stand;
			if (!PilotAnimationPolicy::SlotForNativeState(before.nativeState, stateSlot))
			{
				return;
			}

			if (g_recordPerson != person)
			{
				g_recordPerson = person;
				g_recordIndex = -1;
				g_recordName = nullptr;
			}

			const bool overriding = decision == Decision::Override;
			// A substitute applied earlier is still the current clip.
			const bool recordInForce = g_recordIndex == before.animationIndex &&
				g_recordName != nullptr && !IsStockName(g_recordName);
			if (!overriding && !recordInForce)
			{
				return;
			}

			if (!ReadTablesSeh(plan.stock))
			{
				return;
			}
			plan.desired = plan.stock;

			void* world = nullptr;
			void* firstPerson = nullptr;
			FirstPersonTarget::ReadPersonRenderEntities(person, world, firstPerson);
			if (person != g_checkPerson || world != g_checkWorld || firstPerson != g_checkFirstPerson)
			{
				ClearClipChecks();
				g_checkPerson = person;
				g_checkWorld = world;
				g_checkFirstPerson = firstPerson;
			}

			Slot transitionSlot = Slot::Stand;
			const bool inTransition =
				PilotTransitionTiming::TransitionSlotForNativeState(before.nativeState, transitionSlot);

			for (std::size_t i = 0; i < kSlotCount; ++i)
			{
				const Slot slot = static_cast<Slot>(i);
				const std::int32_t index = PilotTransitionTiming::TableIndexForSlot(slot);
				const PilotAnimationPolicy::TransitionPolicy& entry = policy.slots[i];

				const char* name = plan.stock.name[i];
				if (index == before.animationIndex)
				{
					// The current clip: keep the name that was actually applied.
					if (recordInForce && g_recordName != name && IsClipUsable(CheckClip(g_recordName)))
					{
						name = g_recordName;
					}
				}
				else if (overriding && entry.mode == Mode::Substitute)
				{
					const char* pooled = SubstituteNameFor(i, entry.animation);
					if (pooled != nullptr)
					{
						if (IsClipUsable(CheckClip(pooled)))
						{
							name = pooled;
						}
						else
						{
							LogRejectedOnce(pooled, PilotAnimationPolicy::SlotName(slot));
						}
					}
				}
				plan.desired.name[i] = name;

				// A substitute still in force after the policy went stock keeps
				// substitute-safe timing (end <= its length), or a short clip
				// would never finish and states 1/3 would wait forever.
				const bool substituted = name != g_stockNames[i];
				const CompletionMode completion =
					(overriding && PilotAnimationPolicy::SlotHasCompletion(slot))
					? entry.completion
					: CompletionMode::Stock;
				if (completion == CompletionMode::Stock && !substituted)
				{
					continue;
				}

				PilotTransitionTiming::Input input{};
				input.stock.end = plan.stock.end[i];
				input.stock.fpRate = plan.stock.fpRate[i];
				input.stock.worldRate = plan.stock.worldRate[i];
				const ClipCheck& clip = CheckClip(name);
				input.clipLength = clip.onFirstPerson ? clip.length : 0.0f;
				input.substituted = substituted;
				input.completion = completion;
				input.duration = entry.duration;
				input.manualComplete = manualRequested && inTransition && transitionSlot == slot;

				const PilotTransitionTiming::Result timing = PilotTransitionTiming::Compute(input);
				if (timing.valid)
				{
					plan.desired.end[i] = timing.values.end;
					plan.desired.fpRate[i] = timing.values.fpRate;
					plan.desired.worldRate[i] = timing.values.worldRate;
				}
			}

			plan.write = !SameValues(plan.desired, plan.stock);
			if (plan.write)
			{
				for (std::size_t i = 0; i < kSlotCount; ++i)
				{
					plan.inForce[i] = plan.desired.name[i];
				}
			}
		}

		void DisableOverridesAfterFault(const char* what) noexcept
		{
			if (g_tablesQualified.exchange(false))
			{
				LogNoThrow("exu: pilot animation overrides disabled for this Lua state; clip table %s faulted", what);
			}
		}

		void UpdateAppliedRecord(
			const void* person,
			const PilotState::Snapshot& before,
			const PilotState::Snapshot& after,
			const TablePlan& plan) noexcept
		{
			if (g_recordPerson != person)
			{
				g_recordPerson = person;
				g_recordIndex = -1;
				g_recordName = nullptr;
			}
			if (after.animationIndex == before.animationIndex && g_recordIndex == after.animationIndex)
			{
				return;
			}

			g_recordIndex = after.animationIndex;
			g_recordName = nullptr;
			for (std::size_t i = 0; i < kSlotCount; ++i)
			{
				if (PilotTransitionTiming::TableIndexForSlot(static_cast<Slot>(i)) == after.animationIndex)
				{
					g_recordName = plan.inForce[i];
					break;
				}
			}
		}

		// ----- First-person layers ---------------------------------------------
		//
		// exu.fps.SetLayer clips are advanced here, for the local Person only,
		// after the stock call and the table restore above, so they see the
		// clip the FSM just applied (FirstPersonLayers.h).

		// Entries in each native clip table (bzr.h PersonRuntime).
		constexpr std::int32_t kClipTableEntries = 12;

		// Bounded copy of an engine/pool string. Raw reads only.
		bool CopyClipNameSeh(const char* source, char (&out)[FirstPersonLayers::kMaxLayerName + 1]) noexcept
		{
			out[0] = '\0';
			if (source == nullptr)
			{
				return false;
			}
			__try
			{
				for (std::size_t i = 0; i <= FirstPersonLayers::kMaxLayerName; ++i)
				{
					out[i] = source[i];
					if (source[i] == '\0')
					{
						return true;
					}
				}
				// Longer than any layer name: it cannot match one.
				out[0] = '\0';
				return false;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				out[0] = '\0';
				return false;
			}
		}

		const char* ReadStockClipNameSeh(std::int32_t index) noexcept
		{
			__try
			{
				return BZR::PersonRuntime::animName[index];
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return nullptr;
			}
		}

		// The name of the clip the FSM is playing for the local Person after
		// this call: the name the apply helpers were actually given. For the
		// six policy slots that is the applied-clip record (a substitute
		// stays the current clip until the index changes, even after the
		// policy went stock); for every other index, and whenever the record
		// does not cover the index, it is the stock table entry, which the
		// restore above has just put back.
		void CurrentClipName(
			const PilotState::Snapshot& after,
			char (&out)[FirstPersonLayers::kMaxLayerName + 1]) noexcept
		{
			out[0] = '\0';
			if (g_recordIndex == after.animationIndex && g_recordName != nullptr)
			{
				CopyClipNameSeh(g_recordName, out);
				return;
			}
			if (after.animationIndex >= 0 && after.animationIndex < kClipTableEntries)
			{
				CopyClipNameSeh(ReadStockClipNameSeh(after.animationIndex), out);
			}
		}

		void ApplyFirstPersonLayers(
			const void* person,
			const PilotState::Snapshot& after,
			float dt) noexcept
		{
			void* world = nullptr;
			void* firstPerson = nullptr;
			FirstPersonTarget::ReadPersonRenderEntities(person, world, firstPerson);

			char engineClip[FirstPersonLayers::kMaxLayerName + 1];
			CurrentClipName(after, engineClip);
			// Global player input, sampled here because this is the LOCAL
			// Person's tick (false when the read sites did not qualify).
			const bool triggerHeld = PlayerTrigger::IsHeld();
			FirstPersonLayers::ApplyLocal(
				firstPerson, dt, engineClip[0] != '\0' ? engineClip : nullptr, triggerHeld);
		}

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
		void __fastcall PersonSimulateHook(
			void* person,
			void* /*edx*/,
			float dt) noexcept
		{
			g_calls.fetch_add(1, std::memory_order_relaxed);

			// Install prepares the trampoline and publishes this pointer before
			// activating the entry patch, so null cannot occur on a valid live
			// detour. Keep the check fail-closed for defensive teardown edges,
			// and before any table write so no return can skip the restore.
			const PersonSimulateFn original = g_originalPersonSimulate;
			if (original == nullptr)
			{
				return;
			}

			PilotState::Snapshot before{};
			const bool isLocal = PilotState::CaptureIfCurrent(person, before);
			TablePlan plan{};
			if (isLocal)
			{
				g_localCalls.fetch_add(1, std::memory_order_relaxed);

				// One-shot: consumed by this local call whatever it does.
				const bool manualRequested =
					g_manualCompleteRequested.exchange(false, std::memory_order_acq_rel);

				// Consulted before stock runs: the table writes must be in place
				// for the stock apply that happens inside this call.
				const Policy policy = PilotAnimationPolicy::GetActive();
				const Decision decision = PilotAnimationPolicy::Evaluate(policy, before.nativeState);
				g_lastPolicyDecision.store(static_cast<std::uint8_t>(decision), std::memory_order_relaxed);
				g_hasPolicyDecision.store(true, std::memory_order_relaxed);

				PlanTableOverride(person, before, policy, decision, manualRequested, plan);
				if (plan.write && !WriteTablesSeh(plan.desired))
				{
					// A partial write: put the snapshot back before stock runs.
					WriteTablesSeh(plan.stock);
					DisableOverridesAfterFault("write");
					plan.write = false;
					for (std::size_t i = 0; i < kSlotCount; ++i)
					{
						plan.inForce[i] = plan.stock.name[i];
					}
				}
			}

			// Write -> stock -> restore. Nothing between the write above and the
			// restore below can return early; the stock call is the only code in
			// between.
			original(person, dt);

			if (plan.write)
			{
				if (!WriteTablesSeh(plan.stock))
				{
					DisableOverridesAfterFault("restore");
				}
				g_overrideCalls.fetch_add(1, std::memory_order_relaxed);
			}

			if (isLocal)
			{
				PilotState::Snapshot after{};
				if (PilotState::CaptureIfCurrent(person, after))
				{
					UpdateAppliedRecord(person, before, after, plan);
					RecordLocalPair(before, after);
					g_trace.Record(dt, ToTraceFrame(before), ToTraceFrame(after));
					// Presentation only, so also in multiplayer: no gameplay
					// state and no clip table is written.
					ApplyFirstPersonLayers(person, after, dt);
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
				reinterpret_cast<const void*>(&PersonSimulateHook),
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
				reinterpret_cast<PersonSimulateFn>(g_detour->GetTrampoline());
			if (g_originalPersonSimulate == nullptr)
			{
				Logging::LogMessage("exu: Person::Simulate interception seam produced no trampoline");
				delete g_detour;
				g_detour = nullptr;
				return false;
			}
		}

		// Qualify the clip tables before the hook can see them: until this
		// returns true the hook never writes.
		if (!g_tablesQualified.load(std::memory_order_acquire))
		{
			g_tablesQualified.store(QualifyTables(), std::memory_order_release);
		}

		g_detour->SetStatus(true);
		if (!g_detour->IsActive())
		{
			Logging::LogMessage(
				"exu: Person::Simulate interception seam prepared but entry patch could not activate");
			return false;
		}

			Logging::LogMessage(
				g_tablesQualified.load(std::memory_order_acquire)
					? "exu: Person::Simulate interception seam active (pilot animation overrides qualified; single player only)"
					: "exu: Person::Simulate interception seam active (observe-only; pilot animation overrides unavailable)");
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
			g_tablesQualified.store(false, std::memory_order_release);
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

	bool AreOverridesAvailable() noexcept
	{
		return IsActive() && g_tablesQualified.load(std::memory_order_acquire);
	}

	bool IsNetworkSession() noexcept
	{
		return RuntimeGate::IsSupported() && IsNetGameSeh();
	}

	bool CompleteTransition() noexcept
	{
		if (!AreOverridesAvailable() || IsNetworkSession())
		{
			return false;
		}

		PilotState::Snapshot snapshot{};
		if (!PilotState::Capture(snapshot))
		{
			return false;
		}

		Slot slot = Slot::Stand;
		if (!PilotTransitionTiming::TransitionSlotForNativeState(snapshot.nativeState, slot))
		{
			return false;
		}

		const Policy policy = PilotAnimationPolicy::GetActive();
		if (policy.At(slot).completion != CompletionMode::Manual ||
			!PilotAnimationPolicy::IsSupported(policy, PilotAnimationPolicy::kBuildSupport))
		{
			return false;
		}

		g_manualCompleteRequested.store(true, std::memory_order_release);
		return true;
	}

	void Shutdown() noexcept
	{
		if (g_detour != nullptr)
		{
			delete g_detour;
			g_detour = nullptr;
		}
		g_originalPersonSimulate = nullptr;
		g_tablesQualified.store(false, std::memory_order_release);
		ResetStats();
	}

	void ResetStats() noexcept
	{
		g_calls.store(0, std::memory_order_relaxed);
		g_localCalls.store(0, std::memory_order_relaxed);
		g_stateChanges.store(0, std::memory_order_relaxed);
		g_animationChanges.store(0, std::memory_order_relaxed);
		g_overrideCalls.store(0, std::memory_order_relaxed);
		g_hasLocalSample.store(false, std::memory_order_relaxed);
		g_hasPolicyDecision.store(false, std::memory_order_relaxed);
		g_lastPolicyDecision.store(
			static_cast<std::uint8_t>(Decision::PassThrough),
			std::memory_order_relaxed);
		g_lastBeforeState.store(0, std::memory_order_relaxed);
		g_lastAfterState.store(0, std::memory_order_relaxed);
		g_lastBeforeAnimation.store(-1, std::memory_order_relaxed);
		g_lastAfterAnimation.store(-1, std::memory_order_relaxed);
		g_lastBeforeAnimationHandle.store(-1, std::memory_order_relaxed);
		g_lastAfterAnimationHandle.store(-1, std::memory_order_relaxed);
		ResetOverrideState();
		g_trace.Reset();
	}

	void GetStats(Stats& outStats) noexcept
	{
		outStats = {};
		outStats.installed = IsInstalled();
		outStats.active = IsActive();
		outStats.overridesAvailable = AreOverridesAvailable();
		outStats.observeOnly = !(outStats.overridesAvailable &&
			!IsNetworkSession() &&
			!PilotAnimationPolicy::IsStockOnly(PilotAnimationPolicy::GetActive()));
		outStats.hasLocalSample = g_hasLocalSample.load(std::memory_order_relaxed);
		outStats.hasPolicyDecision = g_hasPolicyDecision.load(std::memory_order_relaxed);
		outStats.calls = g_calls.load(std::memory_order_relaxed);
		outStats.localCalls = g_localCalls.load(std::memory_order_relaxed);
		outStats.stateChanges = g_stateChanges.load(std::memory_order_relaxed);
		outStats.animationChanges = g_animationChanges.load(std::memory_order_relaxed);
		outStats.overrideCalls = g_overrideCalls.load(std::memory_order_relaxed);
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
