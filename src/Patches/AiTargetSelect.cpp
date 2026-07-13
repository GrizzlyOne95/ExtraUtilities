/* Copyright (C) 2023-2026 VTrider
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

// AI target-selection callback.
//
// UnitProcess target acquisition is a virtual: OffensiveProcess::
// ChooseAttackTarget(float* rangeLimit) -> GameObject*, overridden only by
// ScoutProcess in the wingman family. Instead of detouring instructions we
// swap the vtable slot (+0xE4, slot 57) of the five wingman-family process
// vtables. Slot index, vtable addresses, and both implementation addresses
// were validated against the live GOG exe via RTTI + slot diff on 2026-07-12
// (see BZR-OpenShim reverse_engineering notes); everything is re-verified at
// install time and a mismatch aborts the patch for that slot.

#include "AiTargetSelect.h"

#include "BZR.h"
#include "LuaHelpers.h"
#include "LuaState.h"
#include "Util/Logging.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace ExtraUtilities::Patch::AiTargetSelect
{
	namespace
	{
		using ChooseAttackTargetFn = BZR::GameObject*(__thiscall*)(void* process, float* rangeLimit);
		using VectorMagnitudeFn = float(__cdecl*)(const float* vector);

		// Validated 2026-07-12 against GOG battlezone98redux.exe (image base 0x400000).
		constexpr uintptr_t kSlotByteOffset = 0xE4; // vtable slot 57
		constexpr uintptr_t kSharedImplAddr = 0x00583500; // OffensiveProcess::ChooseAttackTarget
		constexpr uintptr_t kScoutImplAddr  = 0x00614020; // ScoutProcess::ChooseAttackTarget

		// Offset of the searching GameObject inside the process, taken from the
		// verified disassembly of both implementations (mov edx,[ecx+0x34]).
		constexpr uintptr_t kProcessOwnerOffset = 0x34;
		constexpr uintptr_t kVectorMagnitudeAddr = 0x00462070;

		// These are the six distance evaluations inside the two stock target
		// searches. Replacing only their call destinations lets us adjust the
		// comparison metric without replaying the engine's eligibility filters,
		// category precedence, range search, or result assignment.
		struct ScoreCallPatch
		{
			uintptr_t callSite;
			uint8_t lane;
			std::array<uint8_t, 5> original = {};
			bool patched = false;
		};

		ScoreCallPatch g_scoreCalls[] = {
			{ 0x004634A5, 0 }, { 0x00463593, 1 }, { 0x00463670, 2 },
			{ 0x00463A46, 0 }, { 0x00463B34, 1 }, { 0x00463C11, 2 },
		};

		BZR::GameObject* __fastcall HookShared(void* process, void* edx, float* rangeLimit);
		BZR::GameObject* __fastcall HookScout(void* process, void* edx, float* rangeLimit);

		struct SlotPatch
		{
			const char* rttiName;
			uintptr_t vftableVa;
			uintptr_t expectedImpl;
			void* hookFn;
			uintptr_t original;
			bool patched;
		};

		SlotPatch g_slots[] = {
			{ ".?AVWingmanProcess@@",    0x0088A6EC, kSharedImplAddr, reinterpret_cast<void*>(&HookShared), 0, false },
			{ ".?AVRocketTankProcess@@", 0x0088A5C0, kSharedImplAddr, reinterpret_cast<void*>(&HookShared), 0, false },
			{ ".?AVTankProcess@@",       0x0088AB9C, kSharedImplAddr, reinterpret_cast<void*>(&HookShared), 0, false },
			{ ".?AVBomberProcess@@",     0x0088B178, kSharedImplAddr, reinterpret_cast<void*>(&HookShared), 0, false },
			{ ".?AVScoutProcess@@",      0x0088AF98, kScoutImplAddr,  reinterpret_cast<void*>(&HookScout),  0, false },
		};

		bool g_installAttempted = false;
		bool g_installSucceeded = false;
		bool g_inCallback = false;
		bool g_scoreInstallAttempted = false;
		bool g_scoreInstallSucceeded = false;
		bool g_inScoreCallback = false;
		int g_scoreSearchDepth = 0;

		bool TryGetHandleForObject(BZR::GameObject* object, BZR::handle* outHandle);

		float DispatchScore(const float* vector,
		                    BZR::GameObject* owner,
		                    BZR::GameObject* candidate,
		                    int lane,
		                    float maxRangeSq)
		{
			const float baseDistanceSq = reinterpret_cast<VectorMagnitudeFn>(
				kVectorMagnitudeAddr)(vector);
			if (!scoreDispatchEnabled || g_scoreSearchDepth <= 0 || g_inScoreCallback ||
				!std::isfinite(baseDistanceSq) ||
				owner == nullptr || candidate == nullptr)
			{
				return baseDistanceSq;
			}
			// Preserve the stock hard range gates. Lane 2 additionally has the
			// engine's explicit 70 m eligibility cap before its final comparison.
			if ((std::isfinite(maxRangeSq) && baseDistanceSq > maxRangeSq) ||
				(lane == 2 && baseDistanceSq > 4900.0f))
			{
				return baseDistanceSq;
			}
			const float baseDistance = std::sqrt((std::max)(0.0f, baseDistanceSq));

			lua_State* L = Lua::state;
			if (L == nullptr)
			{
				return baseDistanceSq;
			}

			BZR::handle ownerHandle = 0;
			BZR::handle candidateHandle = 0;
			if (!TryGetHandleForObject(owner, &ownerHandle) ||
				!TryGetHandleForObject(candidate, &candidateHandle))
			{
				return baseDistanceSq;
			}

			StackGuard guard(L);
			lua_getglobal(L, "exu");
			if (!lua_istable(L, -1))
			{
				return baseDistanceSq;
			}
			lua_getfield(L, -1, "AiTargetScore");
			if (!lua_isfunction(L, -1))
			{
				return baseDistanceSq;
			}

			lua_pushlightuserdata(L, reinterpret_cast<void*>(ownerHandle));
			lua_pushlightuserdata(L, reinterpret_cast<void*>(candidateHandle));
			lua_pushnumber(L, baseDistance);
			lua_pushinteger(L, lane);

			g_inScoreCallback = true;
			const int status = lua_pcall(L, 4, 1, 0);
			g_inScoreCallback = false;
			if (status != 0)
			{
				LuaCheckStatus(status, L, "Extra Utilities AiTargetScore error:\n%s");
				return baseDistanceSq;
			}

			if (lua_isboolean(L, -1) && !lua_toboolean(L, -1))
			{
				return FLT_MAX;
			}
			if (!lua_isnumber(L, -1))
			{
				return baseDistanceSq;
			}

			const double adjusted = lua_tonumber(L, -1);
			if (!std::isfinite(adjusted))
			{
				return baseDistanceSq;
			}
			const double adjustedMeters = (std::max)(0.0, (std::min)(adjusted, std::sqrt(static_cast<double>(FLT_MAX))));
			return static_cast<float>(adjustedMeters * adjustedMeters);
		}

		// At each patched call site EBP is still the stock target-search frame:
		// [ebp+8] is the searching GameObject and [ebp-0x60] is the candidate.
		// The original vector argument remains on the caller's stack for its
		// post-call `add esp,4`, so each stub returns without consuming it.
		static void __declspec(naked) ScoreLane0Stub()
		{
			__asm
			{
				mov eax, dword ptr [ebp+0x0C]
				push dword ptr [eax]
				push 0
				push dword ptr [ebp-0x60]
				push dword ptr [ebp+8]
				mov eax, dword ptr [esp+0x14]
				push eax
				call DispatchScore
				add esp, 0x14
				ret
			}
		}

		static void __declspec(naked) ScoreLane1Stub()
		{
			__asm
			{
				mov eax, dword ptr [ebp+0x0C]
				push dword ptr [eax]
				push 1
				push dword ptr [ebp-0x60]
				push dword ptr [ebp+8]
				mov eax, dword ptr [esp+0x14]
				push eax
				call DispatchScore
				add esp, 0x14
				ret
			}
		}

		static void __declspec(naked) ScoreLane2Stub()
		{
			__asm
			{
				mov eax, dword ptr [ebp+0x0C]
				push dword ptr [eax]
				push 2
				push dword ptr [ebp-0x60]
				push dword ptr [ebp+8]
				mov eax, dword ptr [esp+0x14]
				push eax
				call DispatchScore
				add esp, 0x14
				ret
			}
		}

		void* ScoreStubForLane(uint8_t lane)
		{
			switch (lane)
			{
			case 0: return reinterpret_cast<void*>(&ScoreLane0Stub);
			case 1: return reinterpret_cast<void*>(&ScoreLane1Stub);
			default: return reinterpret_cast<void*>(&ScoreLane2Stub);
			}
		}

		bool WriteRelativeCall(ScoreCallPatch& patch, void* destination)
		{
			auto* site = reinterpret_cast<uint8_t*>(patch.callSite);
			__try
			{
				if (site[0] != 0xE8)
				{
					return false;
				}
				int32_t existingRel = 0;
				std::memcpy(&existingRel, site + 1, sizeof(existingRel));
				const uintptr_t existingTarget = patch.callSite + 5 + existingRel;
				if (existingTarget != kVectorMagnitudeAddr)
				{
					return false;
				}
				std::memcpy(patch.original.data(), site, patch.original.size());
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}

			const intptr_t displacement = reinterpret_cast<uintptr_t>(destination) - (patch.callSite + 5);
			if (displacement < INT32_MIN || displacement > INT32_MAX)
			{
				return false;
			}
			const int32_t relative = static_cast<int32_t>(displacement);
			DWORD oldProtect = 0;
			if (!VirtualProtect(site, patch.original.size(), PAGE_EXECUTE_READWRITE, &oldProtect))
			{
				return false;
			}
			std::memcpy(site + 1, &relative, sizeof(relative));
			FlushInstructionCache(GetCurrentProcess(), site, patch.original.size());
			DWORD restored = 0;
			VirtualProtect(site, patch.original.size(), oldProtect, &restored);
			patch.patched = true;
			return true;
		}

		void RestoreScoreCalls()
		{
			for (ScoreCallPatch& patch : g_scoreCalls)
			{
				if (!patch.patched)
				{
					continue;
				}
				auto* site = reinterpret_cast<uint8_t*>(patch.callSite);
				DWORD oldProtect = 0;
				if (VirtualProtect(site, patch.original.size(), PAGE_EXECUTE_READWRITE, &oldProtect))
				{
					std::memcpy(site, patch.original.data(), patch.original.size());
					FlushInstructionCache(GetCurrentProcess(), site, patch.original.size());
					DWORD restored = 0;
					VirtualProtect(site, patch.original.size(), oldProtect, &restored);
				}
				patch.patched = false;
			}
		}

		bool InstallScoreCalls()
		{
			if (g_scoreInstallAttempted)
			{
				return g_scoreInstallSucceeded;
			}
			g_scoreInstallAttempted = true;
			int installed = 0;
			for (ScoreCallPatch& patch : g_scoreCalls)
			{
				if (WriteRelativeCall(patch, ScoreStubForLane(patch.lane)))
				{
					++installed;
				}
				else
				{
					RestoreScoreCalls();
					break;
				}
			}
			g_scoreInstallSucceeded = installed == static_cast<int>(std::size(g_scoreCalls));
			Logging::LogMessage("[EXU::AiTargetSelect] installed %d/%d native candidate-score call hooks",
				installed, static_cast<int>(std::size(g_scoreCalls)));
			return g_scoreInstallSucceeded;
		}

		bool TryReadRttiName(uintptr_t vftableVa, char* buffer, size_t bufferLen)
		{
			__try
			{
				const uintptr_t col = *reinterpret_cast<const uintptr_t*>(vftableVa - 4);
				const uintptr_t typeDescriptor = *reinterpret_cast<const uintptr_t*>(col + 0x0C);
				const char* name = reinterpret_cast<const char*>(typeDescriptor + 8);
				for (size_t i = 0; i + 1 < bufferLen; ++i)
				{
					buffer[i] = name[i];
					if (name[i] == '\0')
					{
						return true;
					}
				}
				buffer[bufferLen - 1] = '\0';
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool TryReadSlot(uintptr_t slotVa, uintptr_t& outValue)
		{
			__try
			{
				outValue = *reinterpret_cast<const uintptr_t*>(slotVa);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool WriteSlot(uintptr_t slotVa, uintptr_t value)
		{
			DWORD oldProtect = 0;
			if (!VirtualProtect(reinterpret_cast<void*>(slotVa), sizeof(uintptr_t), PAGE_READWRITE, &oldProtect))
			{
				return false;
			}
			*reinterpret_cast<uintptr_t*>(slotVa) = value;
			DWORD restored = 0;
			VirtualProtect(reinterpret_cast<void*>(slotVa), sizeof(uintptr_t), oldProtect, &restored);
			return true;
		}

		bool TryGetProcessOwner(void* process, BZR::GameObject** outOwner)
		{
			__try
			{
				*outOwner = *reinterpret_cast<BZR::GameObject**>(
					reinterpret_cast<uint8_t*>(process) + kProcessOwnerOffset);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool TryGetHandleForObject(BZR::GameObject* object, BZR::handle* outHandle)
		{
			__try
			{
				*outHandle = BZR::GameObject::GetHandle(object);
				return *outHandle != 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// A handle returned from Lua is only accepted when the object it maps to
		// round-trips back to the same handle; anything else is ignored.
		bool TryResolveHandle(BZR::handle h, BZR::GameObject** outObject)
		{
			__try
			{
				BZR::GameObject* object = BZR::GameObject::GetObj(h);
				if (object == nullptr)
				{
					return false;
				}
				if (BZR::GameObject::GetHandle(object) != h)
				{
					return false;
				}
				*outObject = object;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool TryGetHorizontalDistanceSq(BZR::GameObject* first,
		                                BZR::GameObject* second,
		                                float& outDistanceSq)
		{
			using GetPositionFn = const float*(__thiscall*)(void* attachable);
			__try
			{
				auto* firstAttachable = reinterpret_cast<uint8_t*>(first) + 0x18;
				auto* secondAttachable = reinterpret_cast<uint8_t*>(second) + 0x18;
				auto** firstVtable = *reinterpret_cast<void***>(firstAttachable);
				auto** secondVtable = *reinterpret_cast<void***>(secondAttachable);
				const auto firstGetPosition = reinterpret_cast<GetPositionFn>(firstVtable[3]);
				const auto secondGetPosition = reinterpret_cast<GetPositionFn>(secondVtable[3]);
				const float* firstPosition = firstGetPosition(firstAttachable);
				const float* secondPosition = secondGetPosition(secondAttachable);
				const float dx = firstPosition[0] - secondPosition[0];
				const float dz = firstPosition[2] - secondPosition[2];
				outDistanceSq = (dx * dx) + (dz * dz);
				return std::isfinite(outDistanceSq);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		BZR::GameObject* Dispatch(void* process, float* rangeLimit, ChooseAttackTargetFn original)
		{
			if (scoreDispatchEnabled)
			{
				++g_scoreSearchDepth;
			}
			BZR::GameObject* candidate = original(process, rangeLimit);
			if (scoreDispatchEnabled)
			{
				--g_scoreSearchDepth;
			}
			if (scoreDispatchEnabled && candidate != nullptr && rangeLimit != nullptr)
			{
				BZR::GameObject* scoringOwner = nullptr;
				float physicalDistanceSq = 0.0f;
				if (TryGetProcessOwner(process, &scoringOwner) && scoringOwner != nullptr &&
					TryGetHorizontalDistanceSq(scoringOwner, candidate, physicalDistanceSq))
				{
					// The search compares adjusted scores, but downstream engine code
					// still receives the winner's real squared distance.
					*rangeLimit = physicalDistanceSq;
				}
			}

			if (!dispatchEnabled || g_inCallback)
			{
				return candidate;
			}

			lua_State* L = Lua::state;
			if (L == nullptr)
			{
				return candidate;
			}

			BZR::GameObject* owner = nullptr;
			BZR::handle ownerHandle = 0;
			if (!TryGetProcessOwner(process, &owner) || owner == nullptr ||
				!TryGetHandleForObject(owner, &ownerHandle))
			{
				return candidate;
			}

			BZR::handle candidateHandle = 0;
			if (candidate != nullptr && !TryGetHandleForObject(candidate, &candidateHandle))
			{
				return candidate;
			}

			StackGuard guard(L);
			lua_getglobal(L, "exu");
			if (!lua_istable(L, -1))
			{
				return candidate;
			}
			lua_getfield(L, -1, "AiTargetSelect");
			if (!lua_isfunction(L, -1))
			{
				return candidate;
			}

			lua_pushlightuserdata(L, reinterpret_cast<void*>(ownerHandle));
			if (candidateHandle != 0)
			{
				lua_pushlightuserdata(L, reinterpret_cast<void*>(candidateHandle));
			}
			else
			{
				lua_pushnil(L);
			}
			if (rangeLimit != nullptr)
			{
				lua_pushnumber(L, *rangeLimit);
			}
			else
			{
				lua_pushnil(L);
			}

			g_inCallback = true;
			const int status = lua_pcall(L, 3, 1, 0);
			g_inCallback = false;
			if (status != 0)
			{
				LuaCheckStatus(status, L, "Extra Utilities AiTargetSelect error:\n%s");
				return candidate;
			}

			BZR::GameObject* result = candidate;
			if (lua_isboolean(L, -1) && !lua_toboolean(L, -1))
			{
				// Explicit veto: report no target this scan.
				result = nullptr;
			}
			else if (lua_isuserdata(L, -1))
			{
				const BZR::handle overrideHandle =
					reinterpret_cast<BZR::handle>(lua_touserdata(L, -1));
				BZR::GameObject* overrideObject = nullptr;
				if (overrideHandle != 0 && TryResolveHandle(overrideHandle, &overrideObject))
				{
					result = overrideObject;
				}
			}

			return result;
		}

		BZR::GameObject* __fastcall HookShared(void* process, void* /*edx*/, float* rangeLimit)
		{
			return Dispatch(process, rangeLimit, reinterpret_cast<ChooseAttackTargetFn>(kSharedImplAddr));
		}

		BZR::GameObject* __fastcall HookScout(void* process, void* /*edx*/, float* rangeLimit)
		{
			return Dispatch(process, rangeLimit, reinterpret_cast<ChooseAttackTargetFn>(kScoutImplAddr));
		}
	}

	// exu.dll is unloaded whenever the mission Lua state closes, so patched
	// slots MUST be restored on detach: a vtable entry pointing into a freed
	// module crashes the next AI target scan, and a later reload would see an
	// unexpected slot value and refuse to re-patch, leaving the dangling
	// pointer permanent.
	void Uninstall()
	{
		dispatchEnabled = false;
		scoreDispatchEnabled = false;
		RestoreScoreCalls();
		for (SlotPatch& slot : g_slots)
		{
			if (!slot.patched || slot.original == 0)
			{
				continue;
			}
			const uintptr_t slotVa = slot.vftableVa + kSlotByteOffset;
			uintptr_t current = 0;
			if (TryReadSlot(slotVa, current) &&
				current == reinterpret_cast<uintptr_t>(slot.hookFn))
			{
				WriteSlot(slotVa, slot.original);
			}
			slot.patched = false;
		}
		g_installAttempted = false;
		g_installSucceeded = false;
		g_scoreInstallAttempted = false;
		g_scoreInstallSucceeded = false;
	}

	namespace
	{
		// Static-destruction guard: FreeLibrary runs static dtors, restoring
		// the game vtables before the module's code pages disappear.
		struct SlotRestoreGuard
		{
			~SlotRestoreGuard() { Uninstall(); }
		};
		SlotRestoreGuard g_slotRestoreGuard;
	}

	bool Install()
	{
		if (g_installAttempted)
		{
			return g_installSucceeded;
		}
		g_installAttempted = true;

		int patchedCount = 0;
		for (SlotPatch& slot : g_slots)
		{
			char rttiName[64] = {};
			if (!TryReadRttiName(slot.vftableVa, rttiName, sizeof(rttiName)) ||
				strcmp(rttiName, slot.rttiName) != 0)
			{
				Logging::LogMessage(
					"[EXU::AiTargetSelect] RTTI mismatch for %s at 0x%08X (got '%s'); slot skipped",
					slot.rttiName, static_cast<unsigned>(slot.vftableVa), rttiName);
				continue;
			}

			const uintptr_t slotVa = slot.vftableVa + kSlotByteOffset;
			uintptr_t current = 0;
			if (!TryReadSlot(slotVa, current))
			{
				Logging::LogMessage(
					"[EXU::AiTargetSelect] slot read failed for %s; slot skipped", slot.rttiName);
				continue;
			}

			if (current == reinterpret_cast<uintptr_t>(slot.hookFn))
			{
				slot.patched = true;
				++patchedCount;
				continue;
			}

			if (current != slot.expectedImpl)
			{
				Logging::LogMessage(
					"[EXU::AiTargetSelect] slot value mismatch for %s (got 0x%08X expected 0x%08X); slot skipped",
					slot.rttiName, static_cast<unsigned>(current), static_cast<unsigned>(slot.expectedImpl));
				continue;
			}

			if (!WriteSlot(slotVa, reinterpret_cast<uintptr_t>(slot.hookFn)))
			{
				Logging::LogMessage(
					"[EXU::AiTargetSelect] VirtualProtect failed for %s; slot skipped", slot.rttiName);
				continue;
			}

			slot.original = current;
			slot.patched = true;
			++patchedCount;
		}

		g_installSucceeded = patchedCount == static_cast<int>(std::size(g_slots));
		Logging::LogMessage(
			"[EXU::AiTargetSelect] installed %d/%d ChooseAttackTarget slot hooks",
			patchedCount, static_cast<int>(std::size(g_slots)));
		return g_installSucceeded;
	}
}

namespace ExtraUtilities::Lua::Patches
{
	int SetAiTargetSelectEnabled(lua_State* L)
	{
		const bool enabled = lua_toboolean(L, 1) != 0;
		bool ok = true;
		if (enabled)
		{
			ok = Patch::AiTargetSelect::Install();
		}
		Patch::AiTargetSelect::dispatchEnabled = enabled && ok;
		lua_pushboolean(L, (enabled ? (ok ? 1 : 0) : 1));
		return 1;
	}

	int GetAiTargetSelectEnabled(lua_State* L)
	{
		lua_pushboolean(L, Patch::AiTargetSelect::dispatchEnabled ? 1 : 0);
		return 1;
	}

	int SetAiTargetScoringEnabled(lua_State* L)
	{
		const bool enabled = lua_toboolean(L, 1) != 0;
		bool ok = true;
		if (enabled)
		{
			ok = Patch::AiTargetSelect::Install() &&
				 Patch::AiTargetSelect::InstallScoreCalls();
		}
		Patch::AiTargetSelect::scoreDispatchEnabled = enabled && ok;
		lua_pushboolean(L, enabled ? (ok ? 1 : 0) : 1);
		return 1;
	}

	int GetAiTargetScoringEnabled(lua_State* L)
	{
		lua_pushboolean(L, Patch::AiTargetSelect::scoreDispatchEnabled ? 1 : 0);
		return 1;
	}
}
