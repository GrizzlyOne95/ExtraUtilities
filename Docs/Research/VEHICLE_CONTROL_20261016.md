# Mission-scoped vehicle control prototype (2026-10-16)

This implementation lives entirely in EXU at the owner's request. It needs no
OpenShim export, patch, or configuration change. It is opt-in: querying support
does not install a hook; the first accepted claim installs one until EXU teardown.

## What v1 implements

- `HasVehicleControl()`
- `ClaimVehicleControl(handle)`
- `SetVehicleControls(handle, controls)`
- `GetVehicleControlState(handle)`
- `ReleaseVehicleControl(handle)`
- `ResetVehicleControls()`

The native driver replaces the claimed object's stock UnitProcess update with
cached script inputs. Unclaimed objects execute the original function through a
trampoline. Suppressing the entire selected process update prevents its task from
recalculating steering or deciding to fire after the script chooses otherwise.
This retains the attached process, native hover physics, weapon implementations,
ammo/cooldowns and ordinary object lifecycle.

The initial supported object is an ordinary `HoverCraft` with a UnitProcess-derived
AI process whose Execute slot is the stock UnitProcess implementation. The local
player, non-hovercraft subclasses, non-unit AI, foreign Execute implementations,
stale handles and network sessions are rejected. This is deliberately narrower
than “any object with an AI pointer.” Failure returns `false, reason`; the getter
returns `nil, reason`. Support failure does not disable unrelated EXU features.

## Lua usage

```lua
local exu = require("exu")

-- Use a known AI hovercraft handle. Claim from mission code once the unit exists.
local ok, reason = exu.ClaimVehicleControl(enemy)
assert(ok, reason)

-- Refresh from Update. Each table replaces the complete held command.
ok, reason = exu.SetVehicleControls(enemy, {
    throttle = 0.8, strafe = -0.5, steer = 0.15, pitch = 0.0,
    turbo = false, jump = true, fire = false, special = false,
    weaponMask = 3, timeout = 0.5,
})

local s = exu.GetVehicleControlState(enemy)
-- s.processStateName / s.taskStateName are separate state machines.
-- s.requested, s.lastApplied, s.inputs and s.applications expose application timing.

exu.ReleaseVehicleControl(enemy)
```

See `examples/vehicle_control.lua` for a bounded three-second driving/jump helper.
It accepts an existing unit and does not spawn one or replace a mission.

Float axes must be finite and within [-1,1]. Turbo/jump/fire/special require Lua
booleans. `weaponMask` is an optional five-bit integer; omission preserves current
carrier selection. Other omitted fields become neutral/false. Unknown keys,
including unsupported `aimPoint` or raw `curState`, raise before any state change.
This differs from the old `SetAiTaskState`, which silently ignores unknown keys
and edits task factors rather than final driving input.

Claims start with a neutral command and a 0.5 simulation-second lease. Repeated
claims are idempotent but do not renew that lease. `SetVehicleControls` renews it,
with an optional timeout of 0.01..5 simulation seconds. When it expires, the next
native update clears held movement, removes the claim and resumes stock AI. A
paused simulation does not consume the lease. A backwards/invalid simulation
clock invalidates the command. Re-claim explicitly after expiry or loading a save.

Release clears held movement and returns to the retained stock task/order on its
next update. Long scripted sequences can leave stock task timers overdue; issue a
fresh stock order when appropriate. Weapon selection persists until explicitly
changed or stock AI selects again. Claims are not serialized. Reset before saving
when the mission requires an exact save boundary, and re-claim deliberately after
loading. Teardown clears claims and restores/removes the detour, including when
EXU remains loaded across Lua states.

## Qualified native boundary

Target: x86 GOG Redux 2.2.301, EXE SHA-256
`8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`.
Addresses/signatures are in `exu.json` and the build profile, not feature literals.

The first RE candidate was the AiMission indirect process-dispatch instruction.
The implementation instead detours **UnitProcess::Execute (0x005FA6E0)** at entry.
It is a smaller ABI boundary: ECX=this, no stack arguments; a fastcall adapter
consumes the unused EDX. The first nine bytes are complete, reloc-free instructions
(`push ebp; mov ebp,esp; sub esp,8; mov [ebp-4],ecx`). `EntryDetour32` preserves
these in its trampoline, checks the preimage/build, restores the original site at
teardown and avoids overwriting a foreign patch. API submission additionally
checks that the entry still contains this controller's own transfer.

Other qualified operations are Carrier::SetSelected (0x004D9880), FireSelected
(0x00511FC0), and FireSpecial (0x00608AA0). Selection occurs before firing. Native
weapon callbacks can re-enter mission Lua; command revisions prevent an old tick
from continuing special fire or resurrecting telemetry after release/re-claim.
The carrier is resolved again between native operations.

The HoverCraft constructor at 0x004E9723 assigns primary vtable **0x0087ABE4** at
complete object+0. Its distinct distributed-object vtable **0x0087AB8C** is at
object+0x18. Confusing the two would reject every ordinary hovercraft. The live
process is found at object+0xFC, simulation object at +0x230, carrier at +0x1A0.
Process owner +0x0C and craft +0x34 must both point back to the resolved live
object. The cached process address/vtable are identity tokens, never sources of
unchecked saved pointers. Full handles are revalidated against the arena serial.

Six native input fields begin at simulation+0xC4: four floats (steer, pitch,
strafe, throttle), then two **integer** booleans (turbo, jump). Hovercraft physics
at 0x004EBFD0 consumes them, including jump at +0xD8. The process pointer is kept
attached because native hover physics suppresses controls when it is absent.

Telemetry reads UnitProcess current/pending/saved states at +0x1C/+0x20/+0x24 and
its task pointer at +0x38. Task state fields are read only after RTTI confirms
UnitTask ancestry; RecycleTask is not cast to UnitTask. State names were recovered
from the image's separate debug tables: process ATTACK=7, task ATTACK=5; task 7 is
SLIDE. The prototype exposes inspection, not raw FSM mutation: engine transitions
must run their own entry/exit handlers.

## Timing and limits

Single-player LuaMission::Update calls native AiMission::Update before Lua Update.
Commands submitted from Lua Update therefore normally apply at the next native
AI update. `applications` distinguishes submission from actual application.

Fire/special invoke normal native weapons using their current muzzle transforms.
They do not instantly aim toward a newly supplied target. V1 has no arbitrary
aim-point solver, assisted mode, raw process-state setter, deploy/eject/abandon or
cloak actions, walker/flyer/pilot control, or multiplayer authority/replication.
Existing stock SetTarget/weapon APIs and EXU convergence policy remain available,
but are not a substitute for qualifying aim-before-fire timing. These are later
extensions, not silently accepted fields in the current API.

## Validation

- Release x86 DLL build: passed with warnings treated as errors.
- Host `vehicle_control_tests.cpp`: passed, covering unclaimed/replaced handles,
  process identity, atomic invalid commands, float/boolean/mask bounds, lease
  expiry/pause/rewind, capacity, release/reset, selection/fire order, suppression
  when fire=false, and release/re-claim during a native weapon callback.
- EXU hardening/API parity checks and generated-address/profile checks: passed.
- Build qualification tooling tests: 12 passed.
- Offline catalog qualification against the installed GOG executable: all 167
  signatures matched, including the five new targets, with zero missing,
  relocated or ambiguous entries.
- Read-only settled runtime capture, PID 26596, existing GOG qualification instance
  on 2026-10-16: all five target byte ranges matched their static preimages. The
  process was only read; this DLL was not injected or deployed there. Raw capture
  and qualification/build output are local under ignored `Release/`.

**Not yet live-qualified:** driving/jump response, AI hand-back, firing/aim behavior,
save/load, teardown in a real mission, and interactions with other mods/hooks.
Static/live byte matching and host tests establish the prototype boundary; they
do not establish those gameplay behaviors. Run the bounded single-player helper
before considering a release. Steam/Proton/Wine and multiplayer are unqualified.
