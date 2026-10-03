# Pilot FSM live qualification (2026-10-03)

Step 0 / 5a capture from `PILOT_ANIMATION_FSM_HANDOFF_20260928.md`, run once in game.

## Setup

- GOG Redux 2.2.301, OpenShim present (`firstPersonStatus`: "OpenShim resolver
  active; EXU native fallback available").
- EXU Release build of `agent/fps-live-qualification` at `146de86` (main +
  iterator export-name fix), deployed as `addon\ISDF Chronicles\exu.dll`.
- Mission: ISDFC "Test Range" (`isdftest.lua`, map `hilo`), pilot `isuser`
  (`g2snipe` in slot 2). `tests/runtime/pilot_fsm_capture.lua` copied in as
  `pilotcap.lua` (the asset searcher truncates module names to 11 characters).
- Five sniper select/deselect crouch cycles on foot. Lua prints land in
  `logs\BZLogger.txt` on this install (not the game root).
- Side note from the tester: the `hilo` terrain looks offset from the BZN, so
  the player spawns on flat ground outside the real terrain. It does not affect
  this capture.

## Results

### Proven

- `Person::Simulate` entry detour: installed, active, observe-only, no visible
  gameplay change. `calls`/`localCalls` stay 0 while the player is in a vehicle.
- The FSM sequence is exactly `0 -> 1 -> 2 -> 3 -> 0` on every cycle (5/5).
  The state change shows up in the same call that `after state` is sampled in.
- On foot, there is **one local `Simulate` call per Lua `Update`** (8085 / 8085
  over 33.99 s). Trace `dt` sum (34.002) matches `GetTime()` elapsed (33.988).
  This shows the same tick cadence; it does not prove the calls are on the same
  thread.
- **Crouch transitions take a fixed time:** enteringCrouch min/mean/max
  1.9360 / 1.9372 / 1.9380 s, exitingCrouch 1.9370 / 1.9372 / 1.9380 s, with a
  largest sampled dt of 0.018 s. The spread is within one tick, so the duration
  does not depend on frame time. Whether it equals the clip length is still
  unknown (see below).
- Native animation index per state: standing 2, enteringCrouch 0,
  crouched 3, exitingCrouch 1. On spawn, indices 9, 10 and 4 appear briefly
  (handle -1/0) before standing settles on 2. Indices 4, 9 and 10 stay unnamed.
- `animationHandle` is 0 throughout the crouch transitions in this capture; it
  is -1 only around the on-foot entry and the standing idle. So the
  "wait for handle == -1" reading in the handoff is **not** what ends states
  1 and 3 here. Re-check this before step 6.

### Failed / open

- `exu.fps.IsAvailable()` was **false**, and `ListAnimations()` returned nil,
  both in the vehicle and on foot. The local first-person target did not
  resolve, even with the OpenShim resolver reported active. This happened
  with the iterator export-name fix in place, so the cause is target
  resolution, not the iterator. As a result, `stand2Kneel` / `kneel2stand`
  clip lengths could not be read, and the 1.937 s duration could not be
  compared against them.
- There are no EXU log lines about first-person resolution, so the failure is
  silent. Add diagnostics.

## Fix found before the run

`OgreAnimationInventoryBridge.cpp` resolved
`AnimationStateSet::getAnimationStateIterator` using the `std::allocator`
mangled name, but the shipped DLL exports the
`STLAllocator<..., CategorisedAllocPolicy<0>>` name. `dumpbin /exports` on the
GOG `OgreMain.dll` confirms the right name is exported. All 218 mangled Ogre
names in `src/` now resolve against the shipped `Ogre*.dll` exports.

## Second run: resolver fixed, enumeration fault (13:26)

Build `e7eced1` (native fallback + resolver diagnostics).

- **Root cause of the silent failure:** OpenShim's resolver only accepts its
  strict stock FP mesh list (`aspilo_fp`, `bspilo_fp`, `sspilo_fp`,
  `cspilo_fp`, `bsheav_fp`). ISDFC's FP mesh is `ispilo_cockpit.mesh`, which
  does not even match OpenShim's broad `_fp` filter. When the OpenShim export
  was present, EXU returned nullptr on an OpenShim miss and never tried its own
  render-bridge resolver, even though `GetCapabilities` advertised that
  fallback.
- After the fix, `exu.log` shows `[EXU::FPS] ... OpenShim declined; native user
  object is not a Person` in the vehicle, and `resolved via EXU native render
  bridge` on foot. `IsAvailable()` becomes true on foot. `ispilo_cockpit.skeleton`
  carries the stock vocabulary (`idle`, `stand2Kneel`, `kneel2stand`, run/walk
  set, `jump`, `fireRecoilSniper`, `idleEject`, `idleParachute`).
- EXU works with OpenShim or without it: the OpenShim lookup is a runtime
  export check (no import), and the native path only needs EXU's own
  BZR 2.2.301 runtime gate.
- `ListAnimations()` then faulted (`[EXU::Animation] enumeration fault`, an AV
  in a VCRUNTIME140 copy called from exu.dll, reading a garbage address).
  **Cause:** the shipped `OgreMain.dll` is built with MSVC 2013 (`MSVCP120`).
  Its `std::map` node stores the value before `_Color`/`_Isnil`, but EXU's
  MSVC 2015+ headers put it after. So the header-only `MapIterator` read keys
  from the wrong offset. Fixed in `fc58a6f`: enumeration now goes by index
  through exported calls only (`SkeletonInstance::getNumAnimations` /
  `getAnimation(ushort)`, `Animation::getName`, then
  `AnimationStateSet::has/getAnimationState`). Only `std::string` crosses the
  boundary, and `HasAnimation` already relies on that. Entities without a
  skeleton now report the inventory as unavailable.
- Separate, unrelated to EXU: stock HUD text broke on the Test Range only
  (other missions were fine). The tester recognises it as a known stock bug,
  possibly caused by invalid TRN view-range values. Logged for the OpenShim
  backlog.

## Third and fourth runs: offset fault fixed, inventory proven (14:12, 14:16)

- Run 3 (`ef78dc0` breadcrumbs) logged `stage=getNumAnimations` with a
  non-null skeleton. Disassembly of the shipped forwarders:
  `mov ecx,[ecx+0x68]; add ecx,0xD0; mov eax,[ecx]; jmp [eax+4]`.
  **Cause:** `SkeletonInstance::getNumAnimations` and `getAnimation(ushort)`
  override virtuals of `AnimationContainer`, a non-primary base of `Skeleton`
  (offset `0xD0`). MSVC compiles them to expect `this` at that subobject, not
  at the `SkeletonInstance`. Fixed in `211c5af`: EXU decodes the offset from
  both forwarders' `add ecx,imm32`, and if the two disagree or the bytes don't
  match, it reports the inventory as unavailable.
- Run 4: no fault. `ListAnimations()` on foot returns 18 states for ISDFC's
  `ispilo_cockpit`:

| name | length (s) |
| --- | ---: |
| death1, death2 | 1.7 |
| fireRecoilSniper | 0.067 |
| idle | 2.5 (the only enabled state at capture) |
| idleEject, idleParachute | 1.9 |
| jump | 1.2 |
| landParachute | 1.2 |
| stand2Kneel, kneel2stand | 1.0 |
| run{Forward,Backward,Left,Right} | 0.8 |
| walk{Forward,Backward,Left,Right} | 0.8 |

- **Crouch timing conclusion:** `stand2Kneel`/`kneel2stand` are 1.0 s, but every
  FSM crouch state lasts about 1.937 s (every captured transition, all runs; spread
  about 0.002 s). The crouch duration comes from a fixed engine timer, not the
  clip length. A substituted clip in step 3A must either fill about 1.94 s
  (play slower or longer), or the timer has to be overridden alongside it.

## Next

1. ~~Find out why the FP target fails to resolve~~ (done, see above).
2. ~~Re-run the capture to get clip lists and lengths~~ (done, see above).
3. Then go on to step 3A (crouch clip substitution).
