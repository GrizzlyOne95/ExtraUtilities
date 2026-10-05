# Person animation transition cross-fade: native RE (2026-10-05)

Static RE of `battlezone98redux.exe` (GOG Redux 2.2.301, x86, image base `0x400000`) for
`exu.fps.SetTransitionBlend` (`src/Game/PersonAnimBlend.*`). Sources: the Ghidra decompile
corpus (`FUN_0059d340`, `FUN_00680670`, `FUN_00680770`), then confirmed with `pefile` +
`capstone` against the shipped image. Builds on `PILOT_CROUCH_NATIVE_RE_20261003.md`. Not run
in game yet.

## Clip tables (confirmed bytes, 12 entries, writable `.data`)

| Table | Address | Values (index 0..11) |
| --- | --- | --- |
| names | `0x008E8F24` | stand2Kneel, kneel2stand, idle, fireRecoilSniper, runForward, runBackward, runLeft, runRight, death1, idleParachute, landParachute, jump |
| end time | `0x008E8E94` | 0.967 x9, 1.867, 1.167, 1.167 |
| fp rate | `0x008E8EC4` | 0.5 x4, 1.0, 0.75 x6, 0.05 |
| start time | `0x008E8EF4` | 0 x11, 0.2 (jump) |
| loop byte | `0x008E8F54` | 0 0 0 0 1 1 1 1 0 1 0 0 |
| world rate | `0x008E8F60` | as fp rate except jump 0.6 |

So only the four run clips and `idleParachute` loop; `idle` does not.

## Switch site (index change), `Person::Simulate` 0x0059D340

`0x0059E159`-`0x0059E259`: when the selected index differs from `Person+0x2A8` (or the jump
retrigger flag is set) it pushes `(startTime[new], rate[new], loop[new], name[new],
name[Person+0x2A8])` and the render bridge (virtual `+0x2C` on the `Person+0x18` subobject,
the same object as `*(Person+0xF0)`), calls the WORLD helper `0x00680670` (`0x0059E1CB`) and
the first-person helper `0x00680770` (`0x0059E245`), then stores the new index at
`0x0059E259` (`mov [eax+0x2A8], ecx`).

Both helpers are identical apart from offsets:

| | WORLD `0x00680670` | first person `0x00680770` |
| --- | --- | --- |
| latched name (written first) | bridge `+0xB4` | bridge `+0xD0` |
| latched rate | bridge `+0xBC` | bridge `+0xD4` |
| entity | bridge `+0x94` | bridge `+0xC0` |
| entity-present flag (gate) | bridge `+0xB8` | bridge `+0xC4` |
| finished flag (tick) | bridge `+0xD8` | bridge `+0xDC` |

Body: `getAnimationState(old)->setEnabled(false)`, `getAnimationState(new)->setEnabled(true)`,
`setLoop(loop)`, `refreshAvailableAnimationState()`, `setTimePosition(start)`. No weight call
(`AnimationState::setWeight` is not imported by the executable at all).

## Per-tick advance (same function, before the FSM switch)

For each side whose present flag and latched name are set: `state = getAnimationState(name)`,
`step = dt * latchedRate`; if `getTimePosition() + step < endTime[Person+0x2A8]` then
`addTime(step)`, else set the side's finished flag. Only the latched (current) clip advances.
The advance runs before the switch, so on a switch tick the outgoing clip has already been
advanced once.

## Run-direction pick

`0x0059DF6B`: `comiss xmm0, [ebp-0x48C]; jbe 0x0059DFEC` compares `|forward|` against
`|strafe|` (both through `0x00453F80`), no hysteresis. Idle is chosen at speed `<= 0.1`
(`Person+0x11C`). A hysteresis margin would need a mid-function detour with the previous index
in hand; not done (the cross-fade already softens direction flicker).

## What EXU does

Everything runs inside the existing `Person::Simulate` entry detour (PilotFsmIntercept), for
every Person: read `Person+0x2A8` and the bridge fields above before the stock call, read them
again after it. Index changed and latched name changed = switch. Old name, old latched rate and
the stock end time (`0x008E8E94`, read after the clip-table restore) describe the ghost. No new
patch site; the apply helpers are not hooked. Details and the safety rules are in
`Docs/FPS_API.md` ("Transition cross-fade").
