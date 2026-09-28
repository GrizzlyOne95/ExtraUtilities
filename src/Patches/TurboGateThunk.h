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

#pragma once

// The naked hook GlobalTurbo installs at Turbo.TurboPatchBegin. It lives in a
// header so tests/hardening_smoke.cpp can run it in a synthetic engine frame
// and check the contract below. MSVC x86 only (inline asm).

namespace ExtraUtilities::Patch::TurboGate
{
	// Called once per unit per decision, before the turbo compares run. Defined
	// by GlobalTurbo.cpp; the smoke test supplies its own. Called with an empty
	// x87 stack, as the cdecl ABI requires.
	void __cdecl OnTurboDecisionBegin(void* gameObject) noexcept;

	// Installed as `call dword ptr [&target]` (FF 15, 6 bytes) over
	//
	//   0x00601C92  8B 45 90   mov  eax, [ebp-0x70]   ; eax = unit controls
	//   0x00601C95  D9 58 08   fstp dword ptr [eax+8] ; store clamp result
	//
	// in the hovercraft AI steering function (0x00601730). The return address
	// is 0x00601C98, the instruction after both.
	//
	// Entry contract (verified by disassembling the GOG 2.2.301 executable):
	// - ebp is that function's frame. [ebp-0x70] holds the controls pointer
	//   and [ebp-0x68] the UnitTask `this` (stored at 0x00601743); +0x10 of the
	//   UnitTask is the owning GameObject*, which the function dereferences
	//   itself at 0x00601749.
	// - x87: ST0 holds the float the clamp helper at 0x00447ED0 returned
	//   (it ends in `fld`); nothing else is on the x87 stack, because the
	//   previous clamp result was popped at 0x00601C57.
	// - eax is dead (overwritten by the stolen `mov`). EFLAGS are dead (next
	//   read is the comiss at 0x00601CA0 that sets them). xmm0 is reloaded at
	//   0x00601C9B, and xmm1-xmm7 are not read again before the function
	//   returns, so no XMM register is live across the hook; MSVC's x86 ABI
	//   also treats them as caller-saved.
	// - Nothing branches into 0x00601C98-0x00601CC2, so every path to the
	//   compares passes this hook first.
	//
	// What the thunk does:
	// 1. Runs the two stolen instructions first. The fstp pops the live ST0,
	//    so the C++ callback is entered with an empty x87 stack. The previous
	//    thunk called C++ with ST0 still live, which breaks the ABI.
	// 2. Saves EFLAGS and all general registers, reads the GameObject* from
	//    the frame (not from whatever the clamp helper left in eax), and calls
	//    the cdecl callback with it.
	// 3. Restores everything and returns. On exit every register equals what
	//    the stolen instructions alone would leave: eax = [ebp-0x70], all other
	//    general registers, EFLAGS and esp unchanged, x87 stack empty.
	static void __declspec(naked) TurboDecisionBeginThunk()
	{
		__asm
		{
			// Stolen instructions, x87 stack emptied here.
			mov eax, [ebp - 0x70]
			fstp dword ptr [eax + 0x08]

			pushfd
			pushad

			mov ecx, [ebp - 0x68]
			push dword ptr [ecx + 0x10]
			call OnTurboDecisionBegin
			add esp, 0x04

			popad
			popfd
			ret
		}
	}
}
