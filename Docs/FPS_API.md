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
print(caps.firstPersonLayers)
print(caps.transitionBlend)
print(caps.firstPersonTrigger)
print(caps.firstPersonParticles)
print(caps.firstPersonStatus)
```

This is the same capability table returned by
`exu.animation.GetCapabilities()`. `pilotStateInspection=true` reports that
the read-only native Person snapshot API is compiled in. It is distinct from
`localFirstPersonTarget`, which reports whether the presentation target backend
is available. `pilotFsmIntercept=true` means the verified
`Person::Simulate` entry detour is active. `pilotAnimationOverrides=true`
means `SetPilotAnimationProfile` can apply non-stock pilot animation overrides
in this session: the seam is active and the native clip tables it rewrites
passed their install-time preimage check. Overrides are single player only
whatever this flag says.

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


## Person::Simulate interception seam

EXU has a verified x86 function-entry detour at the qualified Redux
2.2.301 `Person::Simulate` entry. The first ten stock bytes are complete,
relocation-free prologue instructions; EXU copies them into an executable
trampoline and resumes at the first untouched instruction.

The hook performs:

1. a cheap check that the simulated `Person*` is the current local user;
2. a pre-stock state snapshot for that local Person;
3. a consult of the mission-scoped pilot animation policy (see below);
4. only for that local Person, in single player, under a non-stock profile:
   a snapshot of the native pilot clip-table entries it overrides, and the
   override write (see "Pilot animation overrides");
5. the original `Person::Simulate(person, dt)` trampoline call;
6. the restore of exactly that snapshot, straight after the stock call;
7. a post-stock snapshot and diagnostic counters.

It never writes `Person` fields (`+0x228` state, `+0x2A8` index, `+0x2AC`
handle), never skips a stock branch, and makes no Lua callbacks from inside
`Person::Simulate`. With the stock profile it only observes.

The seam can be qualified in-game without log spam:

```lua
local hook = exu.fps.GetPilotInterceptStatus()
print(hook.installed, hook.active, hook.observeOnly, hook.overridesAvailable)
print(hook.calls, hook.localCalls, hook.overrideCalls)
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

The trace measured the stock crouch transitions at about 1.937 s, and the
static RE (`Docs/Research/PILOT_CROUCH_NATIVE_RE_20261003.md`) explains it:
`endTime / rate = 0.967 / 0.5 = 1.934 s` plus up to one tick, independent of
the clip length. The trace is how an override is verified in game (see
`tests/runtime/pilot_override_check.lua`).

`tests/runtime/pilot_fsm_capture.lua` automates that capture: call its
`Update` from a test mission's `Update`, hop out, and crouch with a sniper
weapon a few times. It prints the capabilities, intercept status, animation
inventory in the vehicle and on foot, the dwell table against the clip
lengths, the local-Simulate-calls-per-Lua-`Update` ratio, and the transition
samples as `[PILOTCAP]` Markdown lines ready for the research note.

## Pilot animation policy and overrides

Above the seam sits a mission-scoped policy that lets a mission own parts of
the native pilot animation FSM. Read the effective profile back with:

```lua
local profile = exu.fps.GetPilotAnimationProfile()
for _, slot in ipairs({ "stand", "enterCrouch", "crouched", "exitCrouch", "jump", "land" }) do
    local p = profile[slot]
    print(slot, p.mode, p.animation, p.completion, p.duration, p.nativeState)
end
```

| Slot | `nativeState` | Native clip (table index) | Notes |
| --- | ---: | --- | --- |
| `stand` | 0 | `idle` (2) | base state |
| `enterCrouch` | 1 | `stand2Kneel` (0) | transition; has `completion` |
| `crouched` | 2 | `fireRecoilSniper` (3) | sniper/crouch hold |
| `exitCrouch` | 3 | `kneel2stand` (1) | transition; has `completion` |
| `jump` | *(none)* | `jump` (11) | selected from state 0; native conditions not traced |
| `land` | *(none)* | `landParachute` (10) | selected from state 0; native conditions not traced |

`jump` and `land` carry no `nativeState` because they are animation
selections made inside the standing state rather than distinct FSM states.
Their substitution is table-driven, so it applies whenever the profile does.

### Setting a profile

```lua
-- Single player only. Check the capability first.
if exu.fps.GetCapabilities().pilotAnimationOverrides then
    exu.fps.SetPilotAnimationProfile({
        enterCrouch = { completion = "duration", duration = 1.0 },  -- kneel in 1 s
        exitCrouch  = { completion = "animation" },                  -- one play of kneel2stand
        crouched    = { mode = "substitute", animation = "aimRifle" },
    })
end

exu.fps.SetPilotAnimationProfile(nil)   -- or {}: back to stock
```

Rules (all validated by `src/Game/PilotAnimationProfile.h`; a violation is a
Lua error and leaves the active profile unchanged):

- Slot keys are exactly the six names above. Fields are `mode`
  (`"stock"`/`"substitute"`), `animation` (1-63 characters; required by, and
  only allowed with, `"substitute"`), `completion`
  (`"stock"`/`"animation"`/`"duration"`/`"manual"`; `enterCrouch` and
  `exitCrouch` only) and `duration` (seconds in (0, 60]; required by, and only
  allowed with, `completion = "duration"`).
- **Unknown keys are an error**, both slot keys and fields inside a slot.
- A profile replaces the whole policy; omitted slots and fields are stock.
- **Single player only**: a non-stock profile errors in multiplayer. It also
  errors when `GetCapabilities().pilotAnimationOverrides` is false. `nil`/`{}`
  is always accepted.
- The profile is **mission-scoped**: stock again when EXU initialises for a
  Lua state and from the mission-scoped reset when that state closes.

### How it is applied

`Person::Simulate` drives every pilot clip from per-index tables in the
executable (clip name, end time, first-person rate, world rate). For the local
Person the seam rewrites the entries of the six slots immediately before the
stock call and restores them immediately after it, so AI/remote Persons and
everything outside that call see the stock tables. Start times and loop flags
stay stock.

- **Both skeletons need the clip.** The engine enables a clip by name on the
  pilot's world (third-person) entity *and* its first-person entity, and an
  unknown name is an uncaught Ogre exception (a crash). EXU checks the name on
  both entities first (cached per Person/entity pair). If either lacks it, that
  slot stays stock and `exu.log` says so once per name.
- **Takes effect at the next apply.** A substitute is used the next time the
  engine selects that slot's clip; the clip already playing keeps its name
  until the FSM moves on, so the engine always disables the clip it actually
  enabled.
- A substitute with stock completion finishes at `min(0.967, clip length)`
  clip-seconds at the stock rate, so a shorter clip still ends.

### Transition completion (`enterCrouch`, `exitCrouch`)

A transition ends on the tick where the first-person clip position plus
`dt * rate` reaches the table's end time, so it lasts `endTime / rate` real
seconds (plus up to one tick). Stock is `0.967 / 0.5`, about **1.934 s**,
whatever the clip length (a 1.0 s clip plays almost whole at half speed). The
rate is latched when the clip starts (the 0->1 or 2->3 update); the end time is
read every tick.

| `completion` | end time | rate (first person and world) | lasts |
| --- | --- | --- | --- |
| `stock` | stock (0.967), or `min(0.967, L)` for a substitute | stock (0.5) | about 1.934 s |
| `animation` | `L` | 1.0 | one play of the clip at authored speed, `L` s |
| `duration` | `min(0.967, L)` for the stock clip, `L` for a substitute | `end / duration` | `duration` s |
| `manual` | `L + 1000` (holds at the last pose) | stock | until `CompleteTransition()` |

`L` is the clip length on the first-person entity. If it cannot be read the
slot keeps stock timing. `duration` is gameplay FSM timing and also sets the
visual speed (that is the point); it is not a separate playback speed.

Manual completion:

```lua
exu.fps.SetPilotAnimationProfile({ enterCrouch = { completion = "manual" } })
-- ... later, while the pilot is kneeling down (native state 1):
if exu.fps.CompleteTransition() then
    -- the next pilot update finishes the transition (end time 0 for one call)
end
```

`CompleteTransition()` returns `true` only when overrides are available, the
session is single player, the local pilot is in native state 1 or 3, and that
transition's completion is `"manual"`. It is one-shot: the next local
`Person::Simulate` call consumes it whatever happens.

### Diagnostics

`GetPilotInterceptStatus()` reports `overridesAvailable`, `overrideCalls`
(local calls around which tables were rewritten), `observeOnly` (false only
while a non-stock profile is in force in single player), and `policyDecision`
(`"override"` for native states 0-3 under a supported non-stock profile,
`"passThrough"` otherwise; `nil` before the first local call). An unmapped
native state always passes through untouched.

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

## First-person layers (EXU-clocked loops)

`Person::Simulate` advances only the clip its FSM is currently playing
(`addTime(dt * rate[idx])`, see
`Docs/Research/PILOT_CROUCH_NATIVE_RE_20261003.md`). A clip enabled with
`exu.fps.Play` therefore freezes, and the stock apply helpers may disable it
when the FSM changes clips. A *layer* is a clip on the local pilot's
first-person skeleton that EXU keeps enabled and advances itself on every
local pilot tick, whatever the FSM is doing (stand, crouch, run, jump):

```lua
exu.fps.SetLayer("barrelSpin", { speed = 0 })   -- create (speed 0 = parked)
exu.fps.SetLayerSpeed("barrelSpin", 3.0)       -- 3 clip-seconds per second
exu.fps.SetLayerWeight("barrelSpin", 1.0, 0.2) -- ramp to 1 over 0.2 s
exu.fps.ClearLayer("barrelSpin", 0.3)          -- fade out, then disable
exu.fps.ClearLayers()
exu.fps.PlayLayer("reload", { fadeIn = 0.1, fadeOut = 0.2 })  -- one-shot
exu.fps.SetBaseWeight(0.0, 0.25)               -- fade the FSM's clip out
local current, target = exu.fps.GetBaseWeight()
print(exu.fps.IsTriggerHeld())
for _, layer in ipairs(exu.fps.GetLayers()) do
    print(layer.name, layer.effectiveSpeed, layer.weight, layer.time,
        layer.length, layer.active, layer.reason, layer.blendMode)
end
```

`SetLayer(name, options?)` creates or updates. Options:

- `speed` (base clip-seconds per second, 0..50, default 1)
- `weight` (target weight 0..1, default 1)
- `loop` (default true; false clamps at the clip end; true also turns a
  `PlayLayer` one-shot back into an ordinary layer)
- `time` (seconds >= 0: seek EXU's clock)
- `fadeIn` (seconds, this call only: ramp from the weight being applied, 0 for
  a new layer, to the target weight; without it a new weight applies at once)
- `fadeOut` (seconds, stored: the default fade of a later `ClearLayer`)
- `fire = { speed = n, spinUp = s?, spinDown = s? }` or `fire = false`: the
  trigger drive. While the local player's fire (or auto-fire) bind is held the
  layer's effective speed ramps linearly toward `fire.speed`; released, it ramps
  back to the base `speed`. `spinUp`/`spinDown` are the seconds for the whole
  span between the two speeds (0 or omitted = instant); a re-press mid-ramp
  continues from the current speed. `false` removes the drive. Unknown keys in
  the table raise.

A key left out keeps the layer's current value. It is strict: an unknown
option key, a wrong type, a value out of range (fades 0..1000000 s), a name
outside 1..63 characters, or a ninth layer raises a Lua error.
`SetLayerSpeed(name, speed)` and `SetLayerWeight(name, weight, fadeSeconds?)`
raise for an unknown name. `ClearLayer(name, fadeSeconds?)` returns whether the
layer existed; with a fade (`fadeSeconds`, else the layer's `fadeOut`) the layer
ramps to 0, is disabled, and drops out of `GetLayers()` (`reason = "cleared"`
meanwhile); with no fade, or when the seam is not active, it is removed at
once. `SetLayer` or `SetLayerWeight` on a layer still fading out revives it.

`PlayLayer(name, options?)` creates or restarts a non-looping one-shot at time
0. Options: `speed`, `weight` (sticky, like `SetLayer`), `fadeIn`, `fadeOut`
(this play: the weight reaches 0 exactly at the clip end), `clearOnEnd`
(default true: disabled when it finishes, `reason = "ended"`, and its slot is
reused when a ninth layer needs one; false holds the last frame). Each call
bumps `playCount`; `finishedCount` is the `playCount` of the latest play that
reached its end, so `layer.finishedCount == layer.playCount` means "this play
is done".

`SetBaseWeight(weight, fadeSeconds?)` ramps the weight EXU applies to the clip
the FSM is currently playing (default 1; EXU puts 1 back on a clip when the FSM
moves on). With a cumulative rig this cross-fades from the FSM pose to a layer
pose (e.g. an ADS hold). `GetBaseWeight()` returns `current, target`. It is
reset to 1 at every mission boundary.

`IsTriggerHeld()` is the local fire/auto-fire bind as the engine last polled
it (global player input; the engine zeroes it while input is not allowed). It
is false when `firstPersonTrigger` is false.

`GetLayers()` returns one table per layer in creation order: `name`, `speed`
(base), `effectiveSpeed` (what the clock ran at on the last tick, base or
fire-ramped), `weight` (applied on the last tick, fades included),
`targetWeight`, `loop`, `oneShot`, `clearing`, `fadeOut`, `finished`,
`playCount`, `finishedCount`, `triggerHeld` (as sampled by the hook), `fire`
(`{ speed, spinUp, spinDown }`, only when set), `time`, `length`, `active`,
`reason` (only when not active: `pending`, `missing`, `engineOwned`,
`noFirstPersonEntity`, `faulted`, `unavailable`, `ended`, `cleared`) and
`blendMode`. All layers are cleared at every mission/Lua-state boundary.

`exu.fps.GetCapabilities().firstPersonLayers` is true when the
`Person::Simulate` seam is active (the same condition as `pilotFsmIntercept`).
`firstPersonTrigger` is true when both `UserProcess::Execute` read sites of the
fire bytes (0x009198C0 `weapon_fire`, 0x009198C1 `weapon_fire_auto`) matched
at install (`Docs/Research/PLAYER_TRIGGER_SIGNAL_RE_20261003.md`); otherwise
`fire` options are accepted but never see the trigger held.

**Presentation-only.** Layers touch nothing but Ogre animation states on the
local first-person entity: no gameplay state, no clip table, no other Person.
They are therefore allowed in multiplayer (unlike `SetPilotAnimationProfile`)
and each client runs its own.

### How it is applied

Inside EXU's `Person::Simulate` hook, for the LOCAL Person only, after the
stock call has returned and after the override seam has restored the native
clip tables:

1. The first-person entity is read from the render bridge (the same
   Person+0xF0 -> +0xC0 read the override seam uses). No Ogre pointer is kept
   between ticks: every state is looked up by name each tick.
2. Layers cleared since the last tick are disabled on that entity, with
   weight 1 and time 0. A tick with no first-person entity (the player is in
   a vehicle) waits, so the clear lands on the next one that has one.
3. Each layer: if its name is the clip the FSM is playing this tick it is
   skipped (`reason = "engineOwned"`, below). If the skeleton has no such
   animation it is skipped (`reason = "missing"`, logged once per name and
   entity). Otherwise EXU ramps the effective speed (the `fire` drive), advances
   its own clock by `dt * effectiveSpeed` (sim seconds; wrapped into the clip
   when looping, clamped when not), steps the weight ramp and any one-shot end
   fade, and sets enabled, loop, weight and time position on the state (or
   disables it: an ended `clearOnEnd` one-shot, a finished fade-out clear).

Before the layers, the base weight is ramped and written to the FSM's current
clip. The fire bind is sampled once per local tick in the same hook.

Enable/loop/weight are re-asserted every tick because the stock apply helpers
disable the old FSM clip by name and model setup can reset states. Time is
EXU's clock written with `setTimePosition`, so a layer carries across a
first-person entity change (hop in/out, respawn) at the time it had.

**Engine-owned guard.** Should a layer share a name with an FSM clip (the
runtime test below uses `runForward`), the FSM has already advanced that clip
by `dt * rate` this tick and owns its enable/weight/loop. EXU then leaves the
state completely alone for that tick and holds the layer's clock. The clip
"the FSM is playing" is the name the apply helpers were actually given for
the Person's current animation index after the call: for the six policy slots
the override seam's applied-clip record (a substitute stays current until the
index changes), for every other index the stock 12-entry name table.

A fault in any Ogre call turns layers off for the rest of the Lua state
(`reason = "faulted"`, logged once).

### Rig contract

- The clip exists on the **first-person** skeleton only (EXU never looks at
  the world entity for layers).
- It keys **only** the bone(s) it drives (e.g. a barrel bone), and the stock
  FSM clips have **no** tracks for those bones.
- The skeleton uses **`blendmode="cumulative"`**. With Ogre's default
  `"average"`, `Skeleton::setAnimationState` rescales every enabled state by
  1/total weight once the weights sum past 1, so a weight-1 layer on top of a
  weight-1 FSM clip halves the whole pose. `GetLayers()` reports the
  skeleton's `blendMode`, and EXU logs once per entity when a layer is applied
  to an `"average"` skeleton (it does not refuse).
- A looping clip is seamless: the last key equals the first (e.g. a full
  360-degree turn).

### Example: minigun barrel

The ISDF Chronicles minigun rig (`issold_cockpit.mesh` -> `issoldfp.skeleton`,
blendmode cumulative) has a `barrelSpin` clip that keys only the
`msfp_spin` bone: 1.0 s is one revolution, so speed is revolutions per
second.

```lua
function Start()
    if exu.fps.GetCapabilities().firstPersonLayers then
        -- Parked; 3 rev/s 0.4 s after the trigger goes down, coasting to a
        -- stop over 1.2 s after release. EXU ramps it natively every tick.
        exu.fps.SetLayer("barrelSpin", { loop = true, speed = 0,
            fire = { speed = 3, spinUp = 0.4, spinDown = 1.2 } })
    end
end
```

A mod that needs its own trigger logic can leave out `fire` and drive
`SetLayerSpeed` from `Update` instead.

`tests/runtime/fp_layer_check.lua` is an in-game check that cycles a layer
through speeds 0 -> 1 -> 3 -> 0 and prints `GetLayers()` each phase.
`tests/runtime/fp_trigger_check.lua` keeps the trigger-driven barrel layer
above alive and prints `IsTriggerHeld()` changes with `effectiveSpeed`.

## Transition cross-fade (FSM clip switches)

The native Person FSM hard-cuts: when the animation index changes, its apply
helpers disable the old clip, enable the new one and restart it at the
start-time table. Nothing in the executable ever sets a weight. An opt-in
cross-fade replaces the cut with a short blend, for every rig with no
asset changes:

```lua
if exu.fps.GetCapabilities().transitionBlend then
    exu.fps.SetTransitionBlend({ time = 0.15 })   -- defaults for everything else
end
exu.fps.SetTransitionBlend({ time = 0.2, phaseCarry = true, fp = true, world = true, death = false })
exu.fps.SetTransitionBlend(nil)                    -- or false: off (stock hard cut)

local b = exu.fps.GetTransitionBlend()
print(b.enabled, b.time, b.transitions, b.phaseCarries, b.activeFades, b.faulted)
```

`SetTransitionBlend(options | nil | false)` returns whether the seam is active.
A table replaces the whole setting: omitted keys take their defaults, and it
turns the blend on unless it has `enabled = false`. Options:

- `time`: blend seconds, 0..2 (default 0.15; 0 = hard cut).
- `phaseCarry` (default true): on a switch between two looping locomotion
  clips (`runForward`/`runBackward`/`runLeft`/`runRight`, indices 4-7) the
  incoming clip starts at the outgoing clip's phase,
  `frac(oldTime / oldLength) * newLength`, so strides line up.
- `fp` (default true): the local pilot's first-person entity.
- `world` (default true): the world (third-person) entity of **every**
  Person, AI and remote pilots included.
- `death` (default true): also fade into `death1`. `false` keeps the hard cut
  into death.

Unknown keys and wrong types raise. It is **off by default** and turned off
again at every mission/Lua-state boundary, so a mod that never calls it sees
the stock behaviour. It is presentation-only (weights and the outgoing clip's
clock), so it also works in multiplayer.

`GetTransitionBlend()` returns the settings plus `available` (the seam is
active), `faulted` (an Ogre call failed; the blend is off for this Lua state),
`transitions` (fades started), `phaseCarries`, `activeFades` (entities fading
now) and `evictions` (fades dropped because 32 entities were already fading).

### How it is applied

The existing `Person::Simulate` seam reads the render bridge's latched clip
before and after every stock call (every Person). When the animation index
changed and the latched clip name differs, the outgoing clip becomes a
*ghost*:

- EXU re-enables it (the stock helper just disabled it) and advances it
  every tick at its latched rate (bridge `+0xBC` world / `+0xD4` first
  person), with the same end gate as the stock tick (it holds once
  `time + dt * rate` reaches the end-time table entry). Looping ghosts wrap.
- Its weight ramps linearly to 0 over `time` while the incoming clip's weight
  ramps up; the weights always sum to the base weight (1 for world entities;
  the `SetBaseWeight` value on the first-person entity), so a stock
  `average`-blend skeleton sees a plain linear blend and never leans towards
  the bind pose. A `cumulative` skeleton blends the same way.
- The incoming clip's time is **never** touched except for the phase carry
  between two looping run clips. Kneel, stand, land, jump and death
  completion is measured on the incoming clip's own time, so the FSM timing
  is exactly stock.
- A ghost that has faded out is disabled and gets weight 1 back (stock code
  never sets weights, so a leftover weight would stick to the next stock
  enable).
- If the FSM switches back to a clip that is still fading out, that ghost is
  dropped without a disable (it is the current clip again) and fades from
  where it was; up to three ghosts per entity fade at once (a fourth switch
  inside one fade folds the faintest into the newest).
- A first-person ghost with the same name as an `exu.fps.SetLayer` layer is
  left to the layer. Layers (and `SetBaseWeight`) keep working on top.
- An entity that disappears or changes (mesh swap, the pilot boards a craft,
  death removal) ends its fade at once **without** any call on the old
  entity: EXU only touches an entity the render bridge yields again in the
  same `Person::Simulate` call. Nothing is cached across ticks except names
  and weights.
- Turning the blend off, or setting `time = 0`, ends every running fade on the
  next tick of its Person (ghosts disabled, current clip back at the base).

### Idle looping and the jump rate (not available)

Two related limits are **not** changed by this and cannot be changed through
`SetPilotAnimationProfile` today: the profile seam rewrites names, end times
and rates of its six slots but never the loop-flag (`0x008E8F54`) or
start-time (`0x008E8EF4`) tables, and only `enterCrouch`/`exitCrouch` have a
`completion` (rate) setting.

- `idle` (index 2) is non-looping with end time 0.967, so a long idle clip
  freezes at 0.967 clip-seconds (about 1.9 s at rate 0.5). Making it loop
  natively needs both its loop byte set and its end time raised past the clip
  length. A first-person-only workaround with the existing API: author the
  long idle as a separate clip (for example `idleLong`) and, while
  `exu.fps.GetPilotState().animationIndex == 2` (the FSM is on `idle`),
  run it as a layer over a faded base:
  `exu.fps.SetLayer("idleLong", { loop = true, fadeIn = 0.2 })` plus
  `exu.fps.SetBaseWeight(0, 0.2)`, and undo both
  (`exu.fps.ClearLayer("idleLong", 0.2)`, `exu.fps.SetBaseWeight(1, 0.2)`)
  when it changes.
- `jump` (index 11) has a first-person rate of 0.05 (world 0.6) and start
  time 0.2, so the first-person view sees an almost frozen pose. There is no
  API for that rate; the same layer workaround applies (`PlayLayer` of a jump
  clip on the jump frame).

Both would need new profile fields (a loop flag and a per-slot rate) and a
fourth qualified table in the seam.

## First-person death view

When the local pilot is sniped, the engine plays `death1` but immediately
switches the camera to a free-eye view of the body, so the first-person clip
is never seen. Opt in to keep the camera on the pilot's POV bone until
`death1` finishes:

```lua
exu.fps.SetDeathCamera("first")                    -- returns whether it is active
exu.fps.SetDeathCamera("first", { probe = true })  -- plus diagnostic lines in exu.log
exu.fps.SetDeathCamera("stock")                    -- or nil: stock behaviour
local d = exu.fps.GetDeathCamera()
print(d.mode, d.patched, d.armed, d.kept, d.forced, d.declined)
```

- It applies only at the moment of a snipe death, and only when the session
  is single player, the user object is a Person with a dedicated
  first-person entity, and the camera is attached to that pilot. Otherwise
  the stock switch runs (`declined` counts these). Ordinary deaths that turn
  the pilot into chunks never take this path.
- When `death1` finishes, the stock code switches to the free-eye camera
  exactly as before and removes the pilot.
- Guard: if the pilot stops being the user object (or leaves `death1`) while
  the camera still follows it, EXU runs the stock camera switch itself
  (`forced`).
- The view follows the pilot's POV bone as `death1` moves it; the camera
  reads that bone from the world skeleton (static RE: `0x0067DAC0`), so the
  world `death1` decides the fall and the first-person `death1` is drawn at
  the camera. `probe = true` logs the entities and bone every 10 pilot ticks
  to confirm this in game.
- Mission-scoped: stock again at every mission boundary. Two call sites are
  redirected only while the mode is `"first"`. RE notes:
  `Docs/Research/DEATH_CAMERA_RE_20261005.md`.

## First-person particles (effects on FP bones)

Attach an EXU-managed particle system (`exu.CreateParticleSystem`) to a bone of
the LOCAL player's first-person entity, so a muzzle flash, smoke wisp or shell
ejector rides the gun through the stock clips and the EXU layers above.

```lua
local attached, reattached = exu.fps.AttachParticleToBone(name, boneName, offset?)
local attachedNow = exu.fps.IsParticleAttached(name)
local hadBinding = exu.fps.DetachParticle(name)
local generation = exu.fps.GetParticleTargetGeneration()
```

- `name`: a system created with `exu.CreateParticleSystem(name, template)`.
- `boneName`: a bone of the first-person skeleton (e.g. a stock `*11GC1`
  muzzle hardpoint, or a mod's own muzzle bone). An unknown bone returns
  `false` without touching the system.
- `offset` (optional): a vector or three numbers, in the BONE's local frame
  (Ogre TagPoint offset). Default `(0, 0, 0)`.
- Returns `attached` (the system rides that bone of the current FP entity) and
  `reattached` (this call made the Ogre attachment: first call, new FP entity,
  or changed bone/offset).

`exu.fps.GetCapabilities().firstPersonParticles` is true when a local
first-person resolver is present and the OgreMain entry points this needs
(`Entity::attachObjectToBone`, `Skeleton::hasBone`,
`MovableObject::getParentNode`/`isParentTagPoint`, `TagPoint::getParentEntity`)
resolved. Like the layers, this is presentation only and acts on the local
player's FP entity, through the same resolver as every other `exu.fps` call
(OpenShim first, then the EXU native read).

### Re-attach rule: call it every Update

`AttachParticleToBone` is idempotent and cheap when nothing changed. It
resolves the current FP entity, asks Ogre for the system's live parent and,
when that is still the TagPoint EXU made on that entity with the same bone and
offset, returns `true, false` without any Ogre write. So the intended pattern is
to call it from `Update` while the effect should be shown:

```lua
local FLASH = "fp_muzzle_flash"

local fpParticles = false

function Start()
    fpParticles = exu.fps.GetCapabilities().firstPersonParticles
    exu.CreateParticleSystem(FLASH, "fx/muzzleflash_fp")
    exu.SetParticleSystemEmitting(FLASH, false)
end

function Update()
    if not fpParticles then return end
    -- asp11GC1: the stock American FP muzzle hardpoint (child of asp21mg1).
    local ok, fresh = exu.fps.AttachParticleToBone(FLASH, "asp11GC1", 0, 0, 0.05)
    if not ok then
        -- No on-foot FP entity (in a vehicle, dead, between missions).
        exu.SetParticleSystemEmitting(FLASH, false)
        return
    end
    if fresh then
        -- New FP entity (respawn, left a vehicle): restore per-attach state here.
    end
    exu.SetParticleSystemEmitting(FLASH, exu.fps.IsTriggerHeld())
end
```

When `false` is returned nothing is changed. If the system was riding an FP
entity that still exists but is no longer the current one (the player boarded
a vehicle), it stays there; that entity is not drawn, so neither is the system,
but turn emission off as above, or call `DetachParticle`, so it does not keep
simulating. `GetParticleTargetGeneration()` increments each time a binding lands
on a different FP entity than the previous binding, for scripts that cache
per-target state.

Emission and look stay with the existing particle API, by name:
`exu.SetParticleSystemEmitting`, `exu.SetParticleSystemVisible`,
`exu.SetParticleEmitterEnabled`, `exu.SetParticleEmitterEmissionRate`,
`exu.SetParticleSystemKeepLocalSpace` (true makes live particles move with the
gun, usually right for a flash; false leaves smoke and casings behind in the
world), etc. `exu.DestroyParticleSystem` and every generic `exu.Attach*` /
`exu.DetachParticleSystem` call drop the first-person binding, so the generic API
can take a system back at any time. Bindings are cleared at mission teardown;
at most 64 names are bound at once (a new name beyond that returns `false`).

`tests/runtime/fp_particle_check.lua` is an in-game check: it keeps a system on
an FP bone every Update, emits while the trigger is held, and prints attach,
re-attach and generation changes.

### Where the FP entity is rendered (why this works)

Established for BZR 2.2.301 (OpenShim
`reverse_engineering/pilot_flashlight_investigation_20260905.md` live probe,
`redux_scene_ui_boundary_20260921.md` group map, and a re-read of the
creation code at `0x0067E6A8`):

- The FP entity (render bridge `+0xC0`) is an ordinary `Ogre::Entity` on its
  own SceneNode directly under `SceneRoot`, a sibling of the world pilot's node
  with a near-identical transform (the aim, yaw and pitch, lives in that node).
  The camera is placed at the FP skeleton's `*POV` bone. So FP bones are in
  WORLD space, and a TagPoint on an FP bone is where the gun is drawn and where
  the player sees it. There is no separate scene, camera or viewport.
- `0x0067E6A8` calls `setCastShadows(false)`, `setRenderQueueGroup([0x008ED6A8])`
  (`= 10`, the opaque world group) and gives the mesh infinite bounds, so the
  entity is never frustum culled.
- Group 10 draws before terrain (group 40). That is the whole explanation for the
  known "first-person view drops `depth_write off` passes" rule: terrain later
  overdraws any fp pixel that did not write depth, except where the rifle's own
  depth protects it. It is not a separate compositing layer.
- A particle system on a TagPoint is queued by `Entity::_updateRenderQueue`
  (only while the FP entity is rendered, so not in third person or in a vehicle)
  but into the particle renderer's own group, 50 by default, after terrain. So
  ordinary additive or alpha particle materials with `depth_write off` show
  normally, and are depth-tested against the rifle and the world. Do not move an
  FP particle system to a group of 40 or below with
  `SetParticleSystemRenderQueueGroup`, or the depth-write rule above applies
  to it too.
- Ogre ticks particle systems before it updates skeletons, so emission follows
  the bone with up to one frame of lag. It is invisible at muzzle-flash scale.

### Lifetime and safety

Ogre's `Entity::_deinitialise` detaches every bone child
(`_notifyAttached(0)`) and frees its TagPoints, so when the FP entity is
destroyed (respawn, mission end) the particle system survives, unparented, and
never points at a freed TagPoint. EXU keeps no Ogre pointer it dereferences
later: the recorded entity and TagPoint are only compared with the system's LIVE
parent on each call, so a new FP entity at a recycled address still re-attaches.
`DetachParticle` and `DestroyParticleSystem` detach through that live parent.
Every Ogre call is SEH-guarded and the bone is checked with `Skeleton::hasBone`
first, because `attachObjectToBone` throws for an unknown bone. (That check now
also guards the generic `exu.AttachParticleSystemToBone`.)

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

The facade's `Play`/`Stop`/`Seek` are presentation-only: playing or seeking an
Ogre animation alone does not stop `Person::Simulate` from choosing another
animation on a later update. Native ownership goes through
`SetPilotAnimationProfile` (above), which changes which clip the FSM plays and
when a crouch transition ends, but not which state it is in. There is still no
semantic state write such as:

```lua
exu.fps.SetCrouched(true)
```

Playback speed of the FSM's own clips is also intentionally unchanged;
Redux/Ogre remains responsible for advancing them. EXU clocks only the
first-person layers above.
