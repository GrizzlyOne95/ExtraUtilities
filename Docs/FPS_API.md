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
print(caps.firstPersonTrigger)
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
