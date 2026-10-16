# Player control input boundary, 2026-10-16

Target: GOG Redux 2.2.301, SHA-256
`8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`.
Ghidra's UserProcess RTTI vtable is at 0x00889FCC. Its Execute slot points to
0x0060A480. This was compared with the 1.5 UserProcess::Execute decompile; all
addresses/offsets below were checked against the actual Redux consumer.

The first nine bytes are complete position-independent instructions:
`55 8B EC 81 EC 1C 02 00 00` (push ebp; mov ebp,esp; sub esp,0x21c).
An EntryDetour32 trampoline can steal them without relocating a branch/call.
The fastcall wrapper receives this in ECX, ignores EDX and calls the original
thiscall trampoline with no stack arguments. It introduces no mid-function
floating-point or register preservation assumption.

The engine copies `this+0x0C` to its current GameObject local. That object's
`+0x230` points to the smoothing/physics input structure, starting at `+0xC4`.
EXU rereads both per invocation and compares the GameObject with the catalogued
`p_userObject`; it never applies a mask to another process/object.

The disabled-input branch at 0x0060A4C7 reads a 16-bit key at 0x0091989C,
memsets 0x38 bytes there, then restores the key. The new buffer anchor includes
both absolute references and the clear length. The existing PlayerTrigger
anchors independently identify primary/auto fire at 0x009198C0/C1.

Offsets relative to USER_CTRL 0x0091989C:

- 4..7 throttle analogue; 8/9 digital throttle.
- 12..15 strafe analogue; 16/17 digital strafe.
- 20..23 steering analogue; 24/25 digital steering; 26 tracking/aim flag.
- 28..31 pitch analogue; 32/33 digital pitch; 34/35 pitch/aim flags.
- 36 primary fire; 37 auto fire; 38 cycle; 39 link; 40 special fire.
- 41..45 weapon selection; 46 turbo; 47 jump; 48 deploy; 49 eject;
  50 abandon; 53 nav beacon; 54 jetpack.
- Key/UI prefix, byte 27 and remaining unnamed padding are untouched.

GAS_CTRL float slots from +0xC4: steer 0, pitch 1, strafe 2, throttle 3,
turbo 4, jump 5, eject 6, deploy 7, abandon 8, native jetpack 10. Slot 9 is
preserved. Clearing the output before processing prevents decaying smoothed
input; clearing masked outputs afterwards also suppresses turbo synthesized
from full throttle. Fire must be filtered before the original function, since
its Carrier calls have already committed shots by its return.

All engine identities are catalogued under PlayerInput. The two new code anchors
match at their expected addresses in the offline qualification report. The
feature still checks its own preimages; optional anchor failure cannot disable
unrelated EXU features. Read/write helpers are bounded POD-only SEH operations.

This is a mission-requested EXU operation, not an always-on stock-game change.
No OpenShim API currently owns this control-mask entry. A foreign entry patch
causes qualification/installation refusal, rather than overwriting the owner.
Network sessions are unsupported until input/network packet ordering is tested.

This note records static executable analysis and host checks, not a native
playthrough. Qualification must test real bound input (including auto fire and
analogue throttle), menu/chat access, a local pilot/craft swap, release, save/load,
mission restart/exit and DLL teardown before claiming runtime acceptance.
