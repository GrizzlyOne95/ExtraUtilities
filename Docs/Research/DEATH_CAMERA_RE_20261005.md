# First-person death view: native RE (2026-10-05)

Static RE of `battlezone98redux.exe` (GOG Redux 2.2.301) for `exu.fps.SetDeathCamera`
(`src/Game/DeathCamera.*`). Decompile corpus first, then bytes confirmed with `pefile` +
`capstone`. Not run in game yet.

## Why death1 is never seen in first person

- A snipe kills the pilot through `0x004AD700` (vtable slot `+0x78`, called from the snipe hit at
  `0x005D6F98`). It clears `Person+0xEC`, deletes the pilot record at `Person+0xFC`, and for the
  user object (`[0x00917AFC]`) runs:
  - `0x004AD831  E8 CA C7 16 00  call 0x0061A000`: camera save (copies the camera block
    `0x008EAAD0`, 0x238 bytes, onto the stack counted by `0x008EAD14`).
  - `0x004AD843  E8 E8 F6 16 00  call 0x0061CF30` (cdecl, one arg `*(Person+0xF0)`, caller
    pops): free-eye mode (`[0x008EAAD8] = 0xB`), clears the attached bridge `[0x008EACB8]`.
- The next `Person::Simulate` sees `Person+0xFC == 0` and selects index 8 (`death1`); the clip
  plays on both entities. When the first-person clip's finished flag (bridge `+0xDC`) is set,
  Simulate sets `*(Person+0xF4)+0x14 |= 0x200`.
- The next Simulate takes the removal branch (flags `& 0x1000200`): for the user object it runs the
  same save + free-eye pair (`0x0059D488`, `0x0059D49A`), then the object's remove virtual.
- Ordinary damage deaths (chunks) never call `0x004AD700`.

## The camera while attached

`0x0061A510` (per frame) follows the bridge from `0x00439E70` (`[0x008EACB8]`). With a POV bone
(bridge `+0xE0`) it builds the view from `0x0067DAC0`, which reads the **WORLD** entity (bridge
`+0x94`) and that bone. So during a kept death the view follows the third-person skeleton's POV
bone as `death1` plays on it, and the first-person entity is drawn at the camera. This is the
static reading; `SetDeathCamera("first", { probe = true })` logs the bridge entities and POV bone
every 10 pilot ticks to confirm it in game.

## What EXU does

Two `InlinePatch` call redirects (preimages above; the rel32 ties each to its site) point at
stubs. The save stub skips both calls only when all hold: mode `"first"`, qualified build, not a
network game, the user object is a Person with a dedicated first-person entity (the native
resolver), camera mode `0` (attached) and `[0x008EACB8] == *(Person+0xF0)` (the camera
follows this pilot). Otherwise both stock calls run. Nothing else changes: the stock removal branch
still switches the camera before removing the Person.

Guard (in the existing `Person::Simulate` hook, for the armed Person only):

- The Person entered Simulate with its removal flags set: stock just switched the camera; disarm
  and never read it again.
- The camera left mode 0 or follows another bridge: disarm.
- The pilot stopped being the user object, or left `death1` after it was applied: the Person is
  alive (its own Simulate call), so EXU runs the stock save + free-eye pair itself, then disarms.

The same removal-flag pre-check now also stops the transition cross-fade from reading a Person
after the call that may remove it.

## Residual risk

If something deletes the dying Person without the removal branch (no Simulate call), the camera
keeps `[0x008EACB8]` pointing at its bridge, exactly as it does for a living pilot removed the
same way; stock handles (or not) that case identically.
