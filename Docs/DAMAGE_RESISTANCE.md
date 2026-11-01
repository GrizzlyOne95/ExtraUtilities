# Native craft damage resistance

`exu.HasNativeDamageResistance()` reports whether EXU qualified and installed
its native hook. EXU owns the hook and needs no OpenShim. The current
implementation is qualified for GOG Redux 2.2.301 x86. Steam, Proton and Wine are unqualified. An unavailable hook returns false;
there is no health-refund fallback.

```lua
assert(exu.HasNativeDamageResistance())
assert(exu.SetUnitDamageMultiplier(craft, 0.75)) -- resist 25%
exu.ClearUnitDamageMultiplier(craft)            -- restore stock damage
```

The setter accepts a live craft handle and a finite multiplier from 0 to 1.
Zero provides immunity; one clears the override. Invalid numbers raise a Lua
argument error; stale handles and unavailable native support return false.
`ClearUnitDamageMultiplier` accepts destroyed handles without dereferencing them.
`ResetUnitDamageMultipliers` clears all registrations.

EXU changes the receiver's effective damage inside `Craft::DamageAlloc`,
after stock difficulty adjustment and before health subtraction and death. The
shared source `DAMAGE` record is unchanged, so one shield cannot reduce the next
victim's explosion damage. Healing and direct `SetCurHealth` edits are unchanged.
This hook covers craft; buildings and persons have separate damage implementations.

EXU validates handle/object round trips and records both the object address
and its full generation-bearing handle; a recycled pool address cannot inherit
resistance. Registrations are simulation-thread, mission-scoped state. EXU clears
them on attaching/closing a mission VM. Scripts must register again on save load
and clear an override when the equipment granting it is removed. The API does
not serialize or replicate registrations; multiplayer scripts must make the
same registrations on the participating peers.

`exu.BulletHit` remains a notification callback whose return values are ignored.
Use it to copy contact coordinates for visual effects; the native multiplier
provides resistance without a Lua call inside each damage dispatch.

## Implementation

`src/Patches/UnitDamage.cpp` installs the hook lazily on the first
`HasNativeDamageResistance` or `SetUnitDamageMultiplier` call, through
`EntryDetour32` at `EngineAddresses::Craft::EffectiveDamageResistanceSite`
(`exu.json`). The site is the `MOVSS xmm0,[0]` that follows the effective-damage
store in `DamageAlloc` (absolute operand, so the trampoline replays it
unchanged). Install is gated by the in-process build gate (`RuntimeGate`), a
24-byte check of the site and its following `COMISS`/`JB`, and the detour's own
8-byte preimage check; any mismatch leaves the hook uninstalled and every API
returning false. The thunk saves flags, integer registers, x87 and XMM state
with `fxsave`, reads the receiver (`[ebp-0xF8] - 0x18`) and damage local
(`[ebp-0xFC]`), and resolves the handle with `GetHandle` under SEH. The
per-object table is `src/Game/UnitDamagePolicy.h` (host tests:
`tests/host/unit_damage_policy_tests.cpp`). The hook is removed when the Lua
state closes. Frame offsets were proven live by OpenShim on 2026-10-07 (GOG
SHA256 `8d71f56c...3377413`); the EXU port has not yet had its own in-game run.
