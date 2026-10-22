# Deferred local explosion events

2026-10-21. Reusable read-only support for CR's local flora destruction.

## Evidence

**PROVEN (shipping GOG 2.2.301):** existing catalog `Plant.ExplosionBuild`
identifies `ExplosionClass::Build` at 0x004CB7B0. Its entry starts
`55 8B EC 83 EC 10`; these six reloc-free whole instructions are sufficient for
the existing EntryDetour32 trampoline. PE SHA256 is
`8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`.
ReadProcessMemory, without debugger attachment, found the same bytes in live
PID 28744 on 2026-10-21. This check did not install a hook in that other run.

Ghidra inspection of the shipping image corroborated the class's allocation,
instance initialization and returned object. The function is thiscall with a
matrix and optional owner, callee-pop of 8 bytes, and an object-pointer return.
The event detour preserves that return, including when existing callers ignore it.
Cross references include ordnance hit/expiry paths, building/vehicle destruction,
mines and the scripted explosion path. Observing this common construction path
avoids polling objects, health, expiring ordnance pointers or screen effects.
Effects implemented entirely outside normal ExplosionClass builds are out of scope.

**PROVEN (constructor and stock ODF cross-check):** class +0x10 is the packed
8-character name. Class +0x50 is the float destination of the constructor's
ExplosionClass/damageRadius GetFloat call. The key's lower-case FNV-1a hash is
0xB68798A6; inherited defaults are already resolved. This is not an assumed visual
particle size. Stock bullet ground sparks explicitly have radius zero, missiles
5 m, tank-death explosions 20 m, and mortar explosions 50 m. Zero-radius and
purely cosmetic builds remain observable; the consumer chooses which to use.

Private corpus/decompiler output was used as reference only and is not included.
No new executable target was added: the existing qualified catalog and runtime
build gate are reused. These static/data and live-byte findings do not establish
successful live installation or measured runtime event behavior.

## API and lifetime

`SetExplosionEventsEnabled(boolean)` installs the hook only on demand, fails
closed on unsupported builds/foreign preimages, and toggles collection. No Lua,
Ogre operation, allocation, file I/O or network work runs in the hook. It copies
position, name and loaded radius into POD storage only after a successful build.
No game/ordnance pointers escape. Fixed storage holds 512 events; on overflow the
oldest is discarded and a cumulative dropped counter increments. A short SRW lock
protects capture/drain; it is released before executing the original function.

`DrainExplosionEvents(limit=64)` returns up to 64 plain data tables, cumulative
dropped count, and remaining queue size. Lua table allocation happens after the
queue lock is released. Multiple Lua consumers should share a mission service;
draining is destructive. Disabling clears the queue; hook restoration and queue
cleanup are explicit at Lua-state close, even if another consumer pins EXU.

StaticGeometry info now adds `totalMilliseconds`: QPC-based full native create/
replacement time, including input conversion, old object teardown, temporary
entity work, region accounting and build logging. Existing `buildMilliseconds`
still measures Ogre build alone. Neither includes caller Lua table preparation.

## Validation

Release x86 builds; hardening/API/version/address/profile and generated-address
checks pass. The host queue test covers ordering, overwrite-oldest overflow,
sequence, invalid/nonfinite data, zero-radius preservation and clearing; compiled
and run with MSVC /W4 /WX. Linux's existing host test glob discovers the new test.

**Live smoke pending by owner instruction.** The isolated CR `florafx` fixture
is ready under `C:/BZRCoop/flora-local-20261021/`; no game was launched for this
feature and no installed EXU was replaced. Confirm actual callback values and
rendered removal, warm replacement cost, full save/load and supported backends
before release. Steam and Proton/Wine runtime lanes are unverified.
