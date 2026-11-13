# Lua boundary safety validation, 2026-09-28

PR [#72](https://github.com/GrizzlyOne95/ExtraUtilities/pull/72),
`agent/p1-15-p2-2-lua-boundary-safety`, covers audit P1-15 and P2-2.
The interrupted integration was recovered without discarding either side:
`ef72685` incorporates #70's shared helpers; `e78a04a` incorporates #71's
generated addresses and engine save directory. The latter retains argument
validation before C++ object construction and filters the new save-directory
memory reader through `Seh::Filter`.

## Build and static checks

- Fresh Release x86 solution rebuild: no warnings or errors.
- `HardeningSmoke`: passes, including C++ exception destruction through the SEH
  shell, hardware-fault handling, and the Lua barrier's result/error handling.
- `tests/host/run_msvc.cmd`: all 13 suites pass.
- Hardening validation, both generated-header checks, and all 12 qualification
  tooling tests pass. The hardening validator checks 206 SEH filters and nine
  individually pushed Lua functions on the integrated tree.
- Baseline and candidate DLLs expose the same eight exports. Their registration
  tables contain the same 397 Lua names.
- The inherited longjmp scan leaves five reports: three confuse the native
  `SetOverlayParameter` overload with its Lua overload; two concern `StackGuard`
  objects, which own no heap allocation and whose stack state is restored by
  Lua's protected error path. These are not remaining heap/lock leaks.
- PR CI runs Windows build/hardening/MSVC tests and Linux host checks. The final
  head must be green before merge; the release job is intentionally skipped.

## Windows/GOG comparison

Built main `839acbd` separately, then ran main and candidate `e78a04a` on the
same installed OpenShim stack and GOG executable SHA-256
`8D71F56C1314E69A8AD38F4EEAF20A8FF825965A84CF196E5F77EA4CC3377413`, in the `lcbench` mission using DX11 and the
machine-wide `BZRHarness.ps1` launch lock. Each process was stopped through
`Stop-BZRGame -Id`.

| Result | Main | Candidate |
|---|---|---|
| Existing broad smoke | 113 successful calls, one expected invalid metrics-mode argument error | Same |
| Targeted boundary checks | 8/8 | 8/8 |
| Slot 10 direct save, slot 10 description save, string-path save | All returned true with the engine save-directory paths | Same |
| Renderer / Lua Update errors in completed runs | 0 / 0 | 0 / 0 |
| New crash dumps in completed comparison | 0 | 0 |

The broad smoke outputs match after normalizing timestamps and addresses,
apart from which key appears in a truncated, unordered AI-state table print.
The EXU logs have one intended difference: an unknown particle template is
reported as a caught C++ `Ogre::Exception` instead of a `0xE06D7363` crash. Both
bindings return false and the next Lua call succeeds.

DLL SHA-256 values for this comparison:

- Main: `15340062070AF99EDD8E88775F3EF438D014002BD86CC1DB0816CB33D31593D8`
- Candidate: `CF5581A327F37AB51B48F427AB14CCE5A0E87DD9E56ACA3530AB2BDCA1C1984B`

The installed EXU DLL, mission script, `openshim.ini`, `ogre.cfg`, and original
slot-10 save were restored. The temporary string-path save was removed. The
installed OpenShim load-chain files and `patches.json` hashes did not change.

### Test adjustment and limits

An initial baseline attempt failed: the new probe incorrectly expected
`GetGravity()` to return a number (it returns a vector), and displaying the
inherited test font overlay triggered a DX11 missing-shader error on main.
That attempt produced a crash dump. The probe now uses `GetScreenResolution`,
and the broad test hides its font overlay immediately after exercising its
setters/show call. Both subsequent comparison runs completed cleanly. This is
API/exception validation; it does not establish visual acceptance of that font
overlay. No renderer code was changed.

Windows/Steam and Proton/Wine runtime checks remain unverified for this PR.
Linux CI tests host-side contracts, not the Win32 DLL under Wine. Release
qualification must retain those distinctions.

## Repeating the targeted check

`tests/runtime/lua_boundary_smoke.lua` returns a function accepting the loaded
EXU module. Put it on an isolated test mission's Lua search path, require it,
and invoke it from `Update` after the mission and renderer have initialized:

```lua
local runBoundarySmoke = require("lua_boundary_smoke")
-- Once, during active gameplay:
runBoundarySmoke(exu)
```

It tests seven invalid-argument paths, an unknown Ogre particle template, and
a valid EXU call after every error. Expect eight `[BOUNDARY] PASS` lines and
`[BOUNDARY] DONE passed=8`. Verify the unknown-template log says `threw a C++
exception`. Its SaveGame arguments are all invalid; this targeted fixture
does not write saves. The separate broad save comparison requires backing up
slot 10 and the chosen string-path destination first.

## Explicitly outside this PR

- C++ barriers for native naked-thunk callbacks (the remaining part of H-12).
- C++ objects live across Lua allocation-only failures (`lua_push*` /
  `lua_newtable` out-of-memory paths).
- The separate `PushMatrix` / `PushNativeMatrix` vector-order investigation.
