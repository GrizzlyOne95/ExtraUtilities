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

## Next

1. Find out why `TargetLocalFirstPerson` / `exu.fps.IsAvailable()` fails on foot
   (ISDFC `isuser` -> `ispilo` with `ispilo_cockpit.skeleton`; test both the
   OpenShim path and the EXU native fallback), and log the reason.
2. Re-run the capture to get clip lists and lengths, including ISDFC's own
   clips, and compare 1.937 s with the clip lengths.
3. Then go on to step 3A (crouch clip substitution).
