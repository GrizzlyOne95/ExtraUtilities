# Extra Utilities local first-person animation facade

`exu.fps` is a convenience layer over `exu.animation` for the current local
pilot/viewmodel target. It does not create a second animation runtime, resolver,
clock, or Ogre integration path.

Every mutating/read operation is equivalent to calling the matching
`exu.animation` function with:

```lua
exu.animation.TargetLocalFirstPerson()
```

That means resolver behavior, failure handling, animation metadata, playback
semantics, and future standalone EXU support remain owned by the animation
target layer rather than being duplicated here.

## API

Check whether the local first-person entity is resolvable right now:

```lua
if exu.fps.IsAvailable() then
    -- A live local first-person target exists for this operation.
end
```

Availability is dynamic. It may be false while the player is not in a pilot
view, during scene transitions, or when no supported resolver backend is
available.

Inspect capabilities/status:

```lua
local caps = exu.fps.GetCapabilities()
print(caps.localFirstPersonTarget)
print(caps.pilotStateInspection)
print(caps.pilotFsmIntercept)
print(caps.pilotAnimationOverrides)
print(caps.firstPersonStatus)
```

This is the same capability table returned by
`exu.animation.GetCapabilities()`. `pilotStateInspection=true` reports that
the read-only native Person snapshot API is compiled in. It is distinct from
`localFirstPersonTarget`, which reports whether the presentation target backend
is available. `pilotFsmIntercept=true` means the verified observe-only
`Person::Simulate` entry detour is active. `pilotAnimationOverrides` reports
whether this build can apply any non-stock pilot animation policy; it is
`false` because the policy layer currently represents stock pass-through only.

## Read-only pilot FSM state

`exu.fps.GetPilotState()` reads the current local on-foot `Person` directly
from Redux and returns a one-operation snapshot. The snapshot call itself does
not depend on the interception seam, cache the `Person*`, or modify
multiplayer/gameplay state.

```lua
local pilot = exu.fps.GetPilotState()
if pilot then
    print(pilot.state)
    print(pilot.nativeState)
    print(pilot.grounded)
    print(pilot.sniperSelected)
    print(pilot.animationIndex, pilot.animationName)
end
```

The semantic crouch states correspond to the verified native
`Person+0x228` FSM:

| Native | Semantic state | Meaning |
| ---: | --- | --- |
| 0 | `standing` | base state; also owns ordinary locomotion and airborne animation selection |
| 1 | `enteringCrouch` | `stand2Kneel` transition |
| 2 | `crouched` | sniper/crouch hold |
| 3 | `exitingCrouch` | `kneel2stand` transition |

Additional raw evidence is exposed for diagnostics: `animationIndex`
(`Person+0x2A8`), `animationHandle` (`+0x2AC`), the grounded flag from
`*(Person+0x230)+0x114 & 0x80`, and the Carrier selected-weapon mask.

`sniperSelected` scans every selected live Carrier weapon using the same
native class path used by `Person::Simulate`: weapon `+0x08` →
WeaponClass signature `+0x0C`; `0x534E4950` is `SNIP`. The
`selectedWeaponSlot/signature/ODF` fields describe the first selected slot
that currently contains a live weapon, while `sniperSelected` considers all
selected live slots.

Only animation indices whose clip identities are already proven are given a
name: 0 `stand2Kneel`, 1 `kneel2stand`, 2 `idle`, 3
`fireRecoilSniper`, 10 `landParachute`, and 11 `jump`. Other indices
still return the raw `animationIndex` but leave `animationName=nil` rather
than guessing the directional locomotion mapping.

Convenience probes are derived from that same native snapshot:

```lua
local crouched = exu.fps.IsCrouched()
local grounded = exu.fps.IsGrounded()
local sniper = exu.fps.IsSniperSelected()
```

They return `nil` when no readable local on-foot `Person` exists.
`IsCrouched()` is intentionally strict: only native state 2 is true; states
1 and 3 are transitions.


## Observe-only Person::Simulate interception seam

EXU now has a verified x86 function-entry detour at the qualified Redux
2.2.301 `Person::Simulate` entry. The first ten stock bytes are complete,
relocation-free prologue instructions; EXU copies them into an executable
trampoline and resumes at the first untouched instruction.

This work chunk is deliberately **observe-only**. The hook performs:

1. a cheap check that the simulated `Person*` is the current local user;
2. a pre-stock state snapshot for that local Person;
3. a consult of the mission-scoped pilot animation policy (see below), whose
   only possible answer is pass-through;
4. the original `Person::Simulate(person, dt)` trampoline call;
5. a post-stock snapshot and diagnostic counters.

There are no FSM writes, animation substitutions, duration overrides, skipped
stock branches, or Lua callbacks from inside `Person::Simulate`.

The seam can be qualified in-game without log spam:

```lua
local hook = exu.fps.GetPilotInterceptStatus()
print(hook.installed, hook.active, hook.observeOnly)
print(hook.calls, hook.localCalls)
print(hook.stateChanges, hook.animationChanges)
print(hook.beforeState, hook.afterState)
print(hook.policyDecision)
```

`calls` includes every native Person object that reaches the shared function;
`localCalls` increments only when that exact `Person*` is also the current
`p_userObject`. Thus AI/remote Person simulation remains on the stock path
without being mistaken for the local pilot.

The entry patch uses a byte-verified preimage and the normal EXU
`BasicPatch` lifecycle. If the qualified prologue is not present or another
module already owns that exact entry, the seam fails closed and
`pilotFsmIntercept` remains false. Mission/Lua-state teardown restores the
entry through the same foreign-overwrite-safe patch path as other EXU native
patches.

## Pilot FSM timing trace (read-only)

The seam can also record an opt-in timing trace of local `Person::Simulate`
calls. It exists to **measure** the stock crouch transitions before anything
tries to change them: how long native states 1 and 3 last, whether that
matches the clip lengths, and when the animation handle returns to `-1`
relative to the state change. It writes nothing to the game.

```lua
exu.fps.StartPilotTrace()          -- or StartPilotTrace({ changesOnly = true })
-- ... select and deselect a sniper weapon a few times ...
local trace = exu.fps.GetPilotTrace()
exu.fps.StopPilotTrace()

for name, d in pairs(trace.dwell) do
    if d.count > 0 then
        print(name, d.count, d.min, d.mean, d.max, d.lastCalls)
    end
end
for _, s in ipairs(trace.samples) do
    print(s.call, s.dt, s.time,
        s.beforeNativeState, s.afterNativeState,
        s.beforeAnimationIndex, s.afterAnimationIndex,
        s.beforeAnimationHandle, s.afterAnimationHandle)
end
```

Semantics:

- **Off by default** and off again in every new mission/Lua state. Starting
  discards earlier data; stopping keeps it readable. `StartPilotTrace`
  returns whether the seam is active, i.e. whether samples can arrive at all.
  Unknown option keys are an error.
- **Samples** are a ring of the newest 256 local calls (`capacity`), oldest
  first. `call` numbers local calls since the trace started, so gaps show
  where `changesOnly` skipped calls. `dt` is the raw `Person::Simulate`
  argument; `time` is the trace clock (sum of finite, positive `dt`) at the
  end of that call. `GetPilotTrace(limit)` returns only the newest `limit`
  samples.
- **Dwell** is kept separately from the ring, so it survives wrapping. For
  each native state 0-3 it is the sum of `dt` over the calls that *started*
  in that state, up to and including the call that left it, so it is exact to
  one simulation step. Only visits whose entry and exit were both observed
  count; a visit already in progress at start, or interrupted (for example by
  entering a vehicle), is dropped rather than reported short.
- The dwell clock is `Person::Simulate`'s `dt`, not wall time or
  `GetTime()`. If the two ever disagree, that disagreement is itself a
  finding.
- The hook is the only writer. Lua reads through a sequence lock, and
  `GetPilotTrace` returns `nil` rather than a torn snapshot in the unlikely
  case the hook rewrote the data during every read attempt. Whether
  `Person::Simulate` runs on the Lua thread is still unproven, so the trace
  does not assume it.
- Cost when off is one relaxed atomic load per local call, after the
  snapshots the seam already takes.

What this does **not** do: it does not establish stock durations by itself.
Record a dated capture under `Docs/Research/` (several cycles, both
transitions, and the matching `exu.fps.GetInfo("stand2Kneel").length` /
`"kneel2stand"`) before treating any number as the stock behavior.

## Pilot animation policy (stock only)

Above the seam sits a mission-scoped policy that will eventually let a mod
own parts of the pilot animation FSM. **This version contains only the
ownership/configuration layer and its stock default.** It makes no native
writes, changes no branch, substitutes no animation, and substitutes no
duration. Read the effective profile back with:

```lua
local profile = exu.fps.GetPilotAnimationProfile()
for _, slot in ipairs({ "stand", "enterCrouch", "crouched", "exitCrouch", "jump", "land" }) do
    print(slot, profile[slot].mode, profile[slot].nativeState)
end
```

Every slot currently reads `mode = "stock"`, meaning the native
`Person::Simulate` behavior for that slot is untouched:

| Slot | `nativeState` | Notes |
| --- | ---: | --- |
| `stand` | 0 | base state |
| `enterCrouch` | 1 | native `stand2Kneel` transition |
| `crouched` | 2 | native sniper/crouch hold |
| `exitCrouch` | 3 | native `kneel2stand` transition |
| `jump` | *(none)* | animation 11, selected from state 0; native conditions not traced |
| `land` | *(none)* | animation 10, selected from state 0; native conditions not traced |

`jump` and `land` carry no `nativeState` because they are animation
selections made inside the standing state rather than distinct FSM states.

Lifetime and scope:

- The policy is **mission-scoped**. It is restored to stock when EXU initialises
  for a Lua state and again from the mission-scoped reset when that state
  closes, so nothing can carry into the next mission even if a host keeps the
  DLL loaded.
- The read-back needs no runtime gate or local pilot: it describes EXU's own
  configuration, not engine memory, so it works on an unsupported build and
  returns the same stock profile there.
- `GetPilotInterceptStatus().policyDecision` is `"passThrough"` after the
  first intercepted local `Person::Simulate` call and `nil` before it. It is
  how an in-game session can confirm the policy layer is actually being
  consulted on the seam.

Not exposed, deliberately:

- **No setter.** A `SetPilotAnimationProfile` that accepted overrides which
  cannot yet be applied would silently mislead mods, so nothing writable is
  exposed until the first override is real.
- **No stock durations.** Only the animation-handle wait in native states 1
  and 3 is proven; the numeric transition-duration constants have not been
  located. The profile therefore reports no duration, and existing Ogre clip
  lengths must not be read as the FSM's transition timing.

The per-slot `mode` and `nativeState` fields are the stable part of this
diagnostic shape; additional fields will appear as overrides are implemented.

Enumerate all current viewmodel animations:

```lua
local states = exu.fps.ListAnimations()
if states then
    for _, state in ipairs(states) do
        print(state.name, state.length, state.enabled)
    end
end
```

Check or inspect one animation:

```lua
if exu.fps.HasAnimation("stand2Kneel") then
    local info = exu.fps.GetInfo("stand2Kneel")
    if info then
        print(info.timePosition, info.length)
    end
end
```

Control an animation:

```lua
exu.fps.Play("idle", {
    restart = true,
    loop = true,
    weight = 1.0,
})

exu.fps.Seek("idle", 0.25)
exu.fps.Restart("idle")
exu.fps.Stop("idle")
```

## Delegation contract

The facade intentionally contains no separate target-resolution or Ogre state
logic. Internally it prepends the `localFirstPerson` target descriptor and
calls the same implementation used by:

- `exu.animation.List`
- `exu.animation.Has`
- `exu.animation.GetInfo`
- `exu.animation.Play`
- `exu.animation.Stop`
- `exu.animation.Restart`
- `exu.animation.Seek`

As a result, when the standalone EXU first-person resolver is present, the FPS
facade uses it automatically through `exu.animation`. When OpenShim is the
active resolver, the same facade uses OpenShim. Mods do not need resolver-
specific branches.

## Deliberately not included yet

This facade is presentation-only. It does not override Battlezone Redux's
`Person` animation finite-state machine.

Read-only FSM inspection and the stock-only policy read-back are now available,
but the facade still does **not** provide semantic writes such as:

```lua
exu.fps.SetCrouched(true)
exu.fps.SetPilotAnimationProfile({...})
```

Those require overrides to be implemented in the policy layer that now sits on
top of the established interception seam. Playing or seeking an Ogre animation
alone does not stop `Person::Simulate` from choosing another animation on a
later update.

Playback speed and managed animation timing are also intentionally unchanged;
Redux/Ogre remains responsible for native time advancement.
