/* Copyright (C) 2026 GrizzlyOne95
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

#include "BasicPatch.h"
#include "EntryDetour32.h"
#include "Hook.h"
#include "InlinePatch.h"
#include "LuaCppBarrier.h"
#include "Patches/TurboGateThunk.h"
#include "Scanner.h"
#include "bzr.h"
#include "Util/BuildValidation.h"
#include "Util/SehGuard.h"
#include "Util/SignatureResolver.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace ExtraUtilities;

// Synthetic engine frame for the per-unit turbo hook (TurboGateThunk.h). The
// driver builds the frame the hovercraft AI function has at the hook site,
// leaves a float live in ST0, fills every register with a sentinel, and calls
// the thunk the way the installed FF 15 does.
static float g_probeControls[8] = {};
static unsigned char g_probeOwner[16] = {};
static void* g_probeTask[5] = { nullptr, nullptr, nullptr, nullptr, g_probeOwner }; // +0x10 = owner
static float g_probeSt0 = 0.625f;
static const void* g_probeThunkTarget = reinterpret_cast<const void*>(&ExtraUtilities::Patch::TurboGate::TurboDecisionBeginThunk);

static void* g_probeSeenObject = nullptr;
static uint16_t g_probeSeenTagWord = 0;
static int g_probeCalls = 0;

static uint32_t g_probeEax = 0, g_probeEbx = 0, g_probeEcx = 0, g_probeEdx = 0, g_probeEsi = 0, g_probeEdi = 0;
static uint32_t g_probeFrameDepth = 0;
static uint8_t g_probeCarry = 0;
static unsigned char g_probeEnvAfter[28] = {};

void __cdecl ExtraUtilities::Patch::TurboGate::OnTurboDecisionBegin(void* gameObject) noexcept
{
	// fnstenv masks x87 exceptions; fldenv puts the environment back.
	unsigned char env[28];
	__asm
	{
		fnstenv env
		fldenv env
	}
	g_probeSeenTagWord = static_cast<uint16_t>(env[8] | (env[9] << 8)); // 0xFFFF: x87 stack empty
	g_probeSeenObject = gameObject;
	++g_probeCalls;

	// A cdecl callee may clobber eax, ecx, edx and EFLAGS.
	__asm
	{
		mov eax, 0xDEAD0001
		mov ecx, 0xDEAD0002
		mov edx, 0xDEAD0003
		clc
	}
}

static void __declspec(naked) DriveTurboThunk()
{
	__asm
	{
		pushad
		push ebp
		mov ebp, esp
		sub esp, 0x80

		lea eax, g_probeControls
		mov [ebp - 0x70], eax
		lea eax, g_probeTask
		mov [ebp - 0x68], eax
		fld dword ptr [g_probeSt0]

		mov ebx, 0x0B0B0B0B
		mov ecx, 0x1C1C1C1C
		mov edx, 0x2D2D2D2D
		mov esi, 0x3E3E3E3E
		mov edi, 0x4F4F4F4F
		mov eax, 0x5A5A5A5A
		stc
		call dword ptr [g_probeThunkTarget]

		setc byte ptr [g_probeCarry]
		mov [g_probeEax], eax
		mov [g_probeEbx], ebx
		mov [g_probeEcx], ecx
		mov [g_probeEdx], edx
		mov [g_probeEsi], esi
		mov [g_probeEdi], edi
		mov eax, ebp
		sub eax, esp
		mov [g_probeFrameDepth], eax
		fnstenv g_probeEnvAfter
		fldenv g_probeEnvAfter

		mov esp, ebp
		pop ebp
		popad
		ret
	}
}

// Exception layering (Util/SehGuard.h, LuaCppBarrier.h). A C++ exception must
// pass through an SEH shell untouched and be caught, and destroyed, by the C++
// layer above it; an access violation must still be handled by the shell.
static int g_liveThrownObjects = 0;
static bool g_shellHandlerRan = false;
static volatile uintptr_t g_faultAddress = 0;

namespace
{
	struct CountedError : std::runtime_error
	{
		CountedError() : std::runtime_error("counted failure") { ++g_liveThrownObjects; }
		CountedError(const CountedError& other) : std::runtime_error(other) { ++g_liveThrownObjects; }
		~CountedError() override { --g_liveThrownObjects; }
	};
}

// Conditional on a volatile so the optimiser cannot prove the callers'
// following code unreachable (C4702 under /WX).
static volatile bool g_throwEnabled = true;

static __declspec(noinline) void ThrowCountedError()
{
	if (g_throwEnabled)
	{
		throw CountedError();
	}
}

static __declspec(noinline) void WriteThroughNull()
{
	*reinterpret_cast<volatile int*>(g_faultAddress) = 1;
}

// An SEH shell as the converted Try* helpers are written.
static __declspec(noinline) bool ShellAroundThrow()
{
	__try
	{
		ThrowCountedError();
		return true;
	}
	__except (Seh::Filter(GetExceptionCode()))
	{
		g_shellHandlerRan = true;
		return false;
	}
}

static __declspec(noinline) bool ShellAroundFault()
{
	__try
	{
		WriteThroughNull();
		return true;
	}
	__except (Seh::Filter(GetExceptionCode()))
	{
		g_shellHandlerRan = true;
		return false;
	}
}

static int ThrowingLuaFunction(lua_State*)
{
	ThrowCountedError();
	return 0;
}

static int PlainLuaFunction(lua_State*)
{
	return 3;
}

using DetourProbeFn = int(__cdecl*)();
static DetourProbeFn g_detourProbeOriginal = nullptr;

static int __cdecl DetourProbeHook()
{
	return g_detourProbeOriginal != nullptr ? g_detourProbeOriginal() + 1 : -1;
}

namespace
{
	class BytePatch final : public BasicPatch
	{
	private:
		uint8_t value;

		void DoPatch() override
		{
			if (!CanPatch() || !ValidatePreimage())
			{
				return;
			}

			auto* target = reinterpret_cast<uint8_t*>(m_address);
			DWORD oldProtect{};
			if (!VirtualProtect(target, m_length, PAGE_EXECUTE_READWRITE, &oldProtect))
			{
				return;
			}

			*target = value;
			FlushPatchedRange();
			VirtualProtect(target, m_length, oldProtect, &dummyProtect);
			MarkPatched();
		}

	public:
		BytePatch(uint8_t* address, uint8_t replacement, Status status, std::vector<uint8_t> expected = {})
			: BasicPatch(reinterpret_cast<uintptr_t>(address), 1, status, std::move(expected)),
			  value(replacement)
		{
			if (m_status == Status::ACTIVE)
			{
				DoPatch();
			}
		}
	};

	bool Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			return false;
		}
		return true;
	}
}

int main()
{
	static_assert(!std::is_move_constructible_v<Hook>);
	static_assert(!std::is_move_constructible_v<EntryDetour32>);
	static_assert(!std::is_move_constructible_v<Scanner<int>>);
	// A function pointer must never be accepted as a byte buffer to copy from.
	static_assert(!std::is_constructible_v<InlinePatch, uintptr_t, void (*)(), size_t, BasicPatch::Status>);
	static_assert(!std::is_constructible_v<InlinePatch, uintptr_t, void (__stdcall*)(int), size_t, BasicPatch::Status>);
	static_assert(std::is_constructible_v<InlinePatch, uintptr_t, uintptr_t, BasicPatch::Status, std::vector<uint8_t>>);

	bool ok = true;
	ok &= Check(
		!BuildValidation::IsSupportedBzr2301(),
		"synthetic test executable unexpectedly matched the BZR 2.2.301 runtime signature");
	ok &= Check(
		BuildValidation::GetBzrDistribution() == BuildValidation::BzrDistribution::Unknown,
		"unsupported executable did not fail closed to unknown storefront");
	ok &= Check(!BuildValidation::IsLuaCoreCompatible(), "synthetic test executable matched the Lua dummynode anchor");
	ok &= Check(!BuildValidation::IsSteamBuild(), "unsupported executable was mislabeled as Steam");
	ok &= Check(!BuildValidation::IsGogBuild(), "unsupported executable was mislabeled as GOG");

	auto* patchPage = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!patchPage)
	{
		std::cerr << "VirtualAlloc failed\n";
		return 1;
	}
	patchPage[0] = 0x11;

	BasicPatch::UnloadAllPatches();
	{
		BytePatch patch(patchPage, 0x22, BasicPatch::Status::ACTIVE, { 0x11 });
		ok &= Check(patchPage[0] == 0x11, "patch activated before deferred activation");

		// Synthetic tests intentionally bypass only the executable identity gate;
		// all patch/preimage/lifetime checks remain active.
		BasicPatch::EnableDeferredPatchActivation(false);
		ok &= Check(patchPage[0] == 0x22 && patch.IsActive(), "deferred patch did not activate");

		BasicPatch::UnloadAllPatches();
		ok &= Check(patchPage[0] == 0x11 && !patch.IsActive(), "UnloadAllPatches did not restore bytes");

		patch.SetStatus(BasicPatch::Status::ACTIVE);
		ok &= Check(patchPage[0] == 0x11, "SetStatus reactivated while global activation was disabled");

		BasicPatch::EnableDeferredPatchActivation(false);
		ok &= Check(patchPage[0] == 0x22, "requested active state was not restored on enable");
		patch.SetStatus(false);
		ok &= Check(patchPage[0] == 0x11, "boolean SetStatus(false) did not unload patch");
	}
	BasicPatch::UnloadAllPatches();

	// Function-entry detour: copy a reloc-free instruction into a trampoline,
	// jump to a hook, then resume at the first untouched byte. The synthetic
	// function is "mov eax,42; ret"; the hook calls the trampoline and adds 1.
	auto* detourPage = static_cast<uint8_t*>(
		VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
	if (!detourPage)
	{
		std::cerr << "entry-detour VirtualAlloc failed\n";
		return 1;
	}
	const uint8_t detourOriginalBytes[6] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };
	std::memcpy(detourPage, detourOriginalBytes, sizeof(detourOriginalBytes));
	auto detourProbe = reinterpret_cast<DetourProbeFn>(detourPage);
	ok &= Check(detourProbe() == 42, "synthetic entry-detour baseline function returned the wrong value");
	{
		EntryDetour32 detour(
			reinterpret_cast<uintptr_t>(detourPage),
			reinterpret_cast<const void*>(&DetourProbeHook),
			5,
			BasicPatch::Status::ACTIVE,
			{ 0xB8, 0x2A, 0x00, 0x00, 0x00 });
		ok &= Check(detourProbe() == 42, "entry detour activated before deferred patch activation");

		BasicPatch::EnableDeferredPatchActivation(false);
		g_detourProbeOriginal = detour.GetTrampolineAs<DetourProbeFn>();
		ok &= Check(g_detourProbeOriginal != nullptr, "entry detour did not publish a trampoline");
		ok &= Check(detour.IsActive(), "entry detour did not report active after activation");
		ok &= Check(detourProbe() == 43, "entry detour hook/trampoline did not preserve and extend stock execution");
		ok &= Check(g_detourProbeOriginal() == 42, "entry detour trampoline did not execute the stolen bytes and resume");

		BasicPatch::UnloadAllPatches();
		ok &= Check(!detour.IsActive(), "entry detour stayed active after UnloadAllPatches");
		ok &= Check(detourProbe() == 42, "entry detour teardown did not restore the original entry");
		g_detourProbeOriginal = nullptr;
	}
	BasicPatch::UnloadAllPatches();

	// A patch a mission enabled goes back to its constructed default at Lua-state
	// close, so the next mission's activation does not re-apply it.
	patchPage[0] = 0x11;
	{
		BytePatch cheat(patchPage, 0x22, BasicPatch::Status::INACTIVE, { 0x11 });
		BasicPatch::EnableDeferredPatchActivation(false);
		cheat.SetStatus(true);
		ok &= Check(patchPage[0] == 0x22, "script-enabled patch did not activate");
		BasicPatch::UnloadAllPatches();
		BasicPatch::ResetRequestedStatusesToDefaults();
		BasicPatch::EnableDeferredPatchActivation(false);
		ok &= Check(patchPage[0] == 0x11 && !cheat.IsActive(), "script-enabled patch re-armed after the mission reset");
	}
	BasicPatch::UnloadAllPatches();

	// Another module patching the site after EXU must survive EXU's unload.
	patchPage[0] = 0x11;
	{
		BytePatch patch(patchPage, 0x22, BasicPatch::Status::ACTIVE, { 0x11 });
		BasicPatch::EnableDeferredPatchActivation(false);
		ok &= Check(patchPage[0] == 0x22, "patch did not activate before the foreign-overwrite check");
		patchPage[0] = 0x55;
		BasicPatch::UnloadAllPatches();
		ok &= Check(patchPage[0] == 0x55, "unload overwrote another module's patch with EXU's original");
		ok &= Check(!patch.IsActive(), "patch still reported active after losing its site");
	}
	BasicPatch::UnloadAllPatches();

	patchPage[0] = 0x33;
	{
		BytePatch rejected(patchPage, 0x44, BasicPatch::Status::ACTIVE, { 0x99 });
		BasicPatch::EnableDeferredPatchActivation(false);
		ok &= Check(patchPage[0] == 0x33 && !rejected.IsActive(), "expected-byte mismatch did not fail closed");
	}
	BasicPatch::UnloadAllPatches();

	auto* root = static_cast<uintptr_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	auto* middle = static_cast<uintptr_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	auto* finalValue = static_cast<int*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!root || !middle || !finalValue)
	{
		std::cerr << "pointer-chain VirtualAlloc failed\n";
		return 1;
	}

	*root = reinterpret_cast<uintptr_t>(middle);
	*middle = reinterpret_cast<uintptr_t>(finalValue);
	*finalValue = 7;

	// A scanner that was never written leaves the value alone at destruction,
	// even when the game changed it in the meantime.
	*root = reinterpret_cast<uintptr_t>(middle);
	*middle = reinterpret_cast<uintptr_t>(finalValue);
	*finalValue = 7;
	{
		Scanner<int> readOnly(reinterpret_cast<int*>(root), { 0, 0 }, BasicScanner::Restore::ENABLED);
		*finalValue = 11;
	}
	ok &= Check(*finalValue == 11, "an unwritten scanner restored its load-time value");

	// Closing the Lua state restores written values while the scanner is alive.
	RuntimeGate::SetSupported(true);
	*finalValue = 7;
	{
		Scanner<int> written(reinterpret_cast<int*>(root), { 0, 0 }, BasicScanner::Restore::ENABLED);
		written.Write(21);
		BasicScanner::RestoreAllWritten();
		ok &= Check(*finalValue == 7, "RestoreAllWritten did not restore a written scanner");
		*finalValue = 13;
	}
	ok &= Check(*finalValue == 13, "a scanner restored again at destruction after RestoreAllWritten");
	RuntimeGate::SetSupported(false);
	*finalValue = 7;

	// Scanner::Write is a no-op while the runtime build gate is closed.
	*finalValue = 7;
	{
		Scanner<int> gated(reinterpret_cast<int*>(root), { 0, 0 }, BasicScanner::Restore::DISABLED);
		ok &= Check(!gated.Write(5), "Scanner::Write reported success with the runtime build gate closed");
		ok &= Check(*finalValue == 7, "Scanner wrote with the runtime build gate closed");
	}
	RuntimeGate::SetSupported(true);

	MEMORY_BASIC_INFORMATION before{};
	MEMORY_BASIC_INFORMATION after{};
	VirtualQuery(finalValue, &before, sizeof(before));
	{
		Scanner<int> scanner(reinterpret_cast<int*>(root), { 0, 0 }, BasicScanner::Restore::ENABLED);
		ok &= Check(scanner.Get() == finalValue, "scanner resolved the wrong final pointee");
		ok &= Check(scanner.Read() == 7, "scanner read failed");
		ok &= Check(scanner.Write(9), "Scanner::Write did not report a stored value");
		ok &= Check(*finalValue == 9, "scanner write failed");
	}
	VirtualQuery(finalValue, &after, sizeof(after));
	ok &= Check(*finalValue == 7, "scanner destructor did not restore original value");
	ok &= Check(before.Protect == after.Protect, "scanner restored memory protection to the wrong page/protection");

	// GameObject handle liveness. The arena lives at a fixed address in BZR;
	// reserve the same range here so GetObj runs against real memory.
	{
		using BZR::GameObject;
		const uintptr_t arenaPage = GameObject::kArenaBase & ~static_cast<uintptr_t>(0xFFFF);
		const size_t arenaBytes = (GameObject::kArenaBase - arenaPage) + GameObject::kArenaSlotCount * GameObject::kArenaSlotSize;
		void* arena = VirtualAlloc(reinterpret_cast<void*>(arenaPage), arenaBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (arena == nullptr)
		{
			std::cout << "SKIP: GameObject arena range is not free in this process\n";
		}
		else
		{
			auto slotAddress = [](uint32_t slot) { return GameObject::kArenaBase + slot * GameObject::kArenaSlotSize; };
			auto setSerial = [&](uint32_t slot, uint32_t serial)
			{
				*reinterpret_cast<uint32_t*>(slotAddress(slot) + GameObject::kSerialOffset) = serial;
			};

			setSerial(5, 0x12345);
			const BZR::handle live = (5u << 20) | 0x12345u;

			// Off the qualified executable the arena is not at kArenaBase.
			RuntimeGate::SetSupported(false);
			ok &= Check(GameObject::GetObj(live) == nullptr, "GetObj resolved a handle with the runtime build gate closed");
			ok &= Check(!GameObject::IsLiveArenaObject(reinterpret_cast<void*>(slotAddress(5))), "arena pointer accepted with the runtime build gate closed");
			RuntimeGate::SetSupported(true);
			ok &= Check(GameObject::GetObj(live) == reinterpret_cast<GameObject*>(slotAddress(5)), "live handle did not resolve to its slot");
			ok &= Check(GameObject::GetObj(0) == nullptr, "handle 0 resolved to an object");
			ok &= Check(GameObject::GetObj((5u << 20) | 0x12346u) == nullptr, "stale handle (serial mismatch) resolved to an object");
			ok &= Check(GameObject::GetObj(5u << 20) == nullptr, "serial-0 handle resolved to an object");
			setSerial(5, 0);
			ok &= Check(GameObject::GetObj(live) == nullptr, "handle to a freed slot resolved to an object");
			setSerial(5, 0x54321);
			ok &= Check(GameObject::GetObj(live) == nullptr, "handle to a reused slot resolved to the new object");

			ok &= Check(GameObject::IsLiveArenaObject(reinterpret_cast<void*>(slotAddress(5))), "live slot start rejected");
			ok &= Check(!GameObject::IsLiveArenaObject(reinterpret_cast<void*>(slotAddress(5) + 4)), "mid-slot pointer accepted");
			ok &= Check(!GameObject::IsLiveArenaObject(reinterpret_cast<void*>(slotAddress(6))), "free slot accepted");
			ok &= Check(!GameObject::IsLiveArenaObject(reinterpret_cast<void*>(live)), "a handle value was accepted as an object pointer");
			RuntimeGate::SetSupported(false);
			VirtualFree(arena, 0, MEM_RELEASE);
		}
	}

	// Per-unit turbo hook: runs the stolen instructions, calls the callback
	// with an empty x87 stack and the owner from the frame, and returns with
	// every register as the stolen instructions alone would leave it.
	DriveTurboThunk();
	ok &= Check(g_probeCalls == 1, "turbo thunk did not call its callback exactly once");
	ok &= Check(g_probeControls[2] == 0.625f, "turbo thunk did not run the stolen fstp [eax+8]");
	ok &= Check(g_probeSeenTagWord == 0xFFFF, "turbo callback was entered with a live x87 register");
	ok &= Check(g_probeSeenObject == g_probeOwner, "turbo callback did not receive [[ebp-0x68]+0x10]");
	ok &= Check(g_probeEax == reinterpret_cast<uint32_t>(g_probeControls), "turbo thunk did not leave eax = [ebp-0x70]");
	ok &= Check(g_probeEbx == 0x0B0B0B0Bu && g_probeEcx == 0x1C1C1C1Cu && g_probeEdx == 0x2D2D2D2Du &&
		g_probeEsi == 0x3E3E3E3Eu && g_probeEdi == 0x4F4F4F4Fu, "turbo thunk changed a general register");
	ok &= Check(g_probeCarry == 1, "turbo thunk did not restore EFLAGS");
	ok &= Check(g_probeFrameDepth == 0x80, "turbo thunk unbalanced esp or changed ebp");
	ok &= Check((g_probeEnvAfter[8] | (g_probeEnvAfter[9] << 8)) == 0xFFFF, "turbo thunk left a value on the x87 stack");

	static_assert(Seh::Filter(Seh::kMsvcCppExceptionCode) == EXCEPTION_CONTINUE_SEARCH);
	static_assert(Seh::Filter(EXCEPTION_STACK_OVERFLOW) == EXCEPTION_CONTINUE_SEARCH);
	static_assert(Seh::Filter(EXCEPTION_ACCESS_VIOLATION) == EXCEPTION_EXECUTE_HANDLER);
	static_assert(Seh::Filter(EXCEPTION_INT_DIVIDE_BY_ZERO) == EXCEPTION_EXECUTE_HANDLER);
	{
		// C++ exception: the shell's filter passes it on, CatchCpp catches it,
		// returns the fallback, and the thrown object is destroyed.
		g_shellHandlerRan = false;
		bool reachedCatch = false;
		bool fallbackResult = true;
		try
		{
			fallbackResult = Seh::CatchCpp("smoke", [] { return ShellAroundThrow(); }, [&] { reachedCatch = true; return false; });
		}
		catch (...)
		{
			ok &= Check(false, "C++ exception escaped Seh::CatchCpp");
		}
		ok &= Check(!g_shellHandlerRan, "SEH shell swallowed a C++ exception");
		ok &= Check(reachedCatch && !fallbackResult, "Seh::CatchCpp did not return the fallback after a C++ exception");
		ok &= Check(g_liveThrownObjects == 0, "C++ exception object leaked through the SEH shell");

		// Access violation: still handled by the shell itself.
		g_shellHandlerRan = false;
		const bool faulted = Seh::CatchCpp("smoke", [] { return ShellAroundFault(); }, true);
		ok &= Check(g_shellHandlerRan && !faulted, "SEH shell did not handle an access violation");

		// Seh::Guard: a fault reaches onFault with its code; a C++ exception
		// does not, and is destroyed.
		unsigned long faultCode = 0;
		const bool guardedFault = Seh::Guard("smoke", [] { WriteThroughNull(); }, [&](unsigned long code) { faultCode = code; });
		ok &= Check(!guardedFault && faultCode == EXCEPTION_ACCESS_VIOLATION, "Seh::Guard did not report an access violation");

		bool onFaultRan = false;
		const bool guardedThrow = Seh::Guard("smoke", [] { ThrowCountedError(); }, [&](unsigned long) { onFaultRan = true; });
		ok &= Check(!guardedThrow && !onFaultRan, "Seh::Guard treated a C++ exception as a fault");
		ok &= Check(g_liveThrownObjects == 0, "Seh::Guard leaked a C++ exception object");

		bool ran = false;
		ok &= Check(Seh::Guard("smoke", [&] { ran = true; }) && ran, "Seh::Guard failed a body that completed");

		// Lua boundary: the barrier turns a C++ exception into an error message
		// (raised as a Lua error by its caller) and passes results through.
		char message[512] = {};
		int results = 0;
		lua_State* const noState = nullptr;
		ok &= Check(
			!Lua::Detail::CallCatchingCpp(noState, &ThrowingLuaFunction, results, message, sizeof(message)) &&
				std::string(message).find("counted failure") != std::string::npos,
			"Lua C++ barrier did not catch and describe a C++ exception");
		ok &= Check(g_liveThrownObjects == 0, "Lua C++ barrier leaked a C++ exception object");
		ok &= Check(
			Lua::Detail::CallCatchingCpp(noState, &PlainLuaFunction, results, message, sizeof(message)) && results == 3,
			"Lua C++ barrier changed a binding's result count");
	}

	const std::array<uint8_t, 8> bytes{ 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
	const std::array<uint8_t, 3> pattern{ 0x30, 0x00, 0x50 };
	const std::array<uint8_t, 3> mask{ 1, 0, 1 };
	const uintptr_t found = SignatureResolver::FindMaskedPattern(
		bytes.data(), bytes.size(), reinterpret_cast<uintptr_t>(bytes.data()),
		pattern.data(), mask.data(), pattern.size());
	ok &= Check(found == reinterpret_cast<uintptr_t>(bytes.data() + 2), "masked signature resolver returned wrong match");

	VirtualFree(detourPage, 0, MEM_RELEASE);
	VirtualFree(patchPage, 0, MEM_RELEASE);
	VirtualFree(root, 0, MEM_RELEASE);
	VirtualFree(middle, 0, MEM_RELEASE);
	VirtualFree(finalValue, 0, MEM_RELEASE);

	if (!ok)
	{
		return 1;
	}

	std::cout << "EXU hardening smoke tests passed\n";
	return 0;
}
