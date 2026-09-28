# Pilot animation FSM ownership: development handoff

Date: 2026-09-28
State: `main` at `b0a294f` (all of PRs #73-#78 merged; Windows and Linux CI green)
Audience: whoever finishes the remaining work, human or agent.

This is a dated handoff for one workstream. It is not a backlog: the combined
OpenShim + EXU backlog lives in OpenShim (see `AGENTS.md`), so record status
there, not here. Current code and `ARCHITECTURE.md` win over anything below if
they disagree.

Labels used throughout:

- **PROVEN** - established by owner RE or captured at runtime and recorded.
- **INFERRED** - follows from proven bytes/behavior but has not been confirmed
  in disassembly or at runtime. Verify before building on it.
- **UNKNOWN** - not located or not tested. Do not invent a value.

---

## 1. Goal and non-negotiables

Make EXU independently capable of first-person/pilot animation control for
Battlezone 98 Redux mods, including control over the native pilot animation
FSM's transitions and timing.

- **OpenShim stays.** Mods cannot hard-require OpenShim, so EXU must provide the
  same mod-facing capability alone. Do not remove, weaken, or replace OpenShim
  functionality. OpenShim remains the preferred backend where present.
- **No second animation runtime.** Redux owns Ogre and the animation state set.
  EXU manipulates the same native `AnimationState`s Redux already uses. A managed
  clock is a deliberate, separate later feature (step 9), never a side effect.
- **Presentation and logical state stay separate.**
  `exu.animation.*` / `exu.fps.Play|Seek` = presentation.
  `GetPilotState` / `SetCrouched` / `SetPilotAnimationProfile` = logical policy.
- **`speed` and `duration` are different things.** `speed` is Ogre skeleton
  playback rate. `duration` is the gameplay FSM's transition-completion time.
  Never conflate them.
- **Do not commit invented stock timing values.** The `0.35` in the design sketch
  is illustrative only.
- **Fail closed.** Exact build gate, byte-verified preimages, no cached
  `Person*` / `Entity*` / `AnimationState*` across operations.
- **`SetCrouched(true)` is not `Play("stand2Kneel")`.** Playing or seeking a clip
  changes presentation only; `Person::Simulate` reclaims the animation and state
  on its next update. Real crouch control must go through the policy/interception
  layer.

## 2. What is on `main` now

| PR | Merge commit | What it delivered |
| --- | --- | --- |
| #73 | `7e33c13` | Standalone EXU local-FP resolver (`Person` -> bridge `+0xC0`); OpenShim preferred when present |
| #74 | `f78dd20` | `exu.animation.List(target)` via `AnimationStateSet`; Ogre calls resolved at run time |
| #75 | `71201c4` | `exu.fps` facade over `exu.animation.TargetLocalFirstPerson()` |
| #76 | `93d60c0` | Read-only `GetPilotState` / `IsCrouched` / `IsGrounded` / `IsSniperSelected` |
| #77 | `a17f97b` | Observe-only `Person::Simulate` entry detour (`EntryDetour32`) |
| #78 | `b0a294f` | Mission-scoped stock-only pilot animation policy + read-back |

Public Lua surface (see `Definitions/Fps.lua`, `Docs/FPS_API.md`):

- `exu.animation.*`: `Target`, `TargetLocalFirstPerson`, `GetCapabilities`, `Has`,
  `GetInfo`, `List`, `Play`, `Stop`, `Restart`, `SetEnabled`, `SetLoop`,
  `SetWeight`, `Seek`.
- `exu.fps.*`: `IsAvailable`, `GetCapabilities`, `GetPilotState`,
  `GetPilotAnimationProfile`, `GetPilotInterceptStatus`, `IsCrouched`,
  `IsGrounded`, `IsSniperSelected`, `ListAnimations`, `HasAnimation`, `GetInfo`,
  `Play`, `Stop`, `Restart`, `Seek`.
- Capabilities: `pilotStateInspection`, `pilotFsmIntercept`,
  `pilotAnimationOverrides` (**false**), `managedClock` (**false**),
  `nativeAdvancement` (`"unvalidated"`).

Code map:

| Concern | Where |
| --- | --- |
| Policy model (pure, host-tested) | `src/Game/PilotAnimationPolicy.{h,cpp}`, `tests/host/pilot_animation_policy_tests.cpp` |
| Detour hook + stats | `src/Game/PilotFsmIntercept.{h,cpp}` |
| Logical state reads (SEH-guarded) | `src/Game/PilotState.{h,cpp}`, `PilotStateSemantics.h` |
| Entry detour primitive | `src/EntryDetour32.h` (`PrepareTrampoline`, `GetTrampolineAs`, `SetStatus`, `IsActive`); synthetic smoke test in `tests/hardening_smoke.cpp` |
| Lua bindings | `src/Game/AnimationApi.cpp` (`fpsFunctions[]` table) |
| Local-FP resolver | `src/Game/FirstPersonTarget.{h,cpp}` |
| Animation inventory bridge | `src/Ogre/OgreAnimationInventoryBridge.{h,cpp}` |
| Address catalog | `exu.json` (`PersonRuntime.PersonSimulate`) -> `src/Util/EngineAddresses.generated.h` |
| Lifecycle | `Init` in `src/luaexport.cpp`; `ResetMissionScopedState` / `HandleLuaStateClosing` in `src/PublicAPI.cpp` |

Seam call order today, per `Person::Simulate` call for the **local** Person:

1. `PilotState::CaptureIfCurrent` (before snapshot)
2. `PilotAnimationPolicy::EvaluateActive(before.nativeState)` (result recorded, not acted on)
3. stock `Person::Simulate` via trampoline
4. after snapshot + counters

## 3. Ground truth

### 3.1 PROVEN (owner RE, GOG Redux 2.2.301)

- `Person::Simulate` is at `0x0059D340`, prologue `55 8B EC 6A FF 68 D6 C1 84 00`
  (`push ebp; mov ebp,esp; push -1; push 0x0084C1D6`). Older PDB-derived addresses
  around `0x004F43E0` are wrong for the live GOG image.
- `Person` fields: `+0x1A0` Carrier*, `+0x228` FSM state, `+0x230` ground-flags
  object, `+0x2A8` current animation index, `+0x2AC` animation handle, `+0x0F0`
  render bridge.
- Grounded = `(*(Person+0x230) + 0x114) & 0x80`.
- Render bridge: `bridge+0x094` WORLD Ogre entity, `bridge+0x0C0` FP Ogre entity
  (runtime-proven; observed `aspilo.mesh` / `aspilo_fp.mesh`).
- FSM (`Person+0x228`): `0` standing, `1` entering crouch, `2` crouched/sniper,
  `3` exiting crouch.
  - State 0: jump held -> animation 11 (jump), else sniper selected ->
    animation 0 (`stand2Kneel`) and state 1, else normal locomotion/idle. The
    jump-before-sniper ordering is the Redux jump/sniper regression.
  - State 1: wait until `animationHandle == -1`, then animation 3
    (`fireRecoilSniper`), state 2.
  - State 2: sniper deselected -> animation 1 (`kneel2stand`), state 3; else stay
    on animation 3.
  - State 3: wait until `animationHandle == -1`, then animation 2 (`idle`), state 0.
- Animation indices: `0 stand2Kneel`, `1 kneel2stand`, `2 idle`,
  `3 fireRecoilSniper`, `10 landParachute`, `11 jump`. **Indices 4-7 are not
  verified**; do not name them.
- Sniper detection: scans 5 Carrier weapon slots (array at `Carrier+0x18`,
  selected mask `Carrier+0x30`); `weapon+0x08` -> `WeaponClass`, `+0x0C` signature
  `0x534E4950` (`"SNIP"`).
- WORLD and FP apply helpers are called in sequence from `Person::Simulate`:
  `0x0059E1CB -> 0x00680670` (WORLD), `0x0059E245 -> 0x00680770` (FP). Cloned
  time-position setter sites near `0x0028075D` (WORLD) and `0x0028085D` (FP).
- OpenShim's SP jump-snipe fix patches `0x0059DEA5`:
  `0F B6 85 AE FC FF FF 85 C0 74 26` -> `66 8B 85 AE FC FF FF 38 E0 76 26`.
  Byte-verified, SP/net-id gated, stands down in MP.

### 3.2 INFERRED (verify before use)

- The patched bytes read a 16-bit word at `[ebp-0x352]`: `AL` = jump-held byte,
  `AH` = the byte at `[ebp-0x351]`, then `cmp al,ah; jbe`. That means
  `[ebp-0x351]` is very likely the per-call **sniper-selected local** that states 0
  and 2 consume. If so, it is the natural override point for a "virtual sniper"
  (see step 8). Confirm the writer(s) and every reader in the disassembly first.
- The Ogre `AnimationStateSet` map key equals the animation name accepted by
  `getAnimationState(name)`. `List` relies on this.
- The by-value `MapIterator` return through a `__thiscall` function pointer (see
  `OgreAnimationInventoryBridge.cpp`) is ABI-correct. It compiles and links but has
  never run in-game.

### 3.3 UNKNOWN

- **The hardcoded numeric transition durations.** The owner remembers hardcoded
  numeric values governing "is the user in crouch-sniper position". Only the
  animation-handle wait in states 1 and 3 is proven. Whether those waits end
  because a clip finished, because a timer expired, or because of a numeric table
  is **not known**. Candidate places to trace: `AnimObj_Start`, animation-handle
  lifetime (who writes `Person+0x2AC`, and what turns it to `-1`), helpers
  `0x00680670` / `0x00680770`, Ogre `AnimationState` advancement, completion
  bookkeeping. Existing Ogre clip lengths are **not** automatically the FSM
  transition durations.
- Whether `Person+0x228` and `+0x2AC` are replicated in multiplayer.
- Whether `Person::Simulate` runs on the same thread as Lua.
- The signatures/arguments of the two apply helpers.
- How the animation index maps to a clip name inside the engine.

## 4. Design constraints

- **Layers** (`ARCHITECTURE.md`): runtime integration -> feature logic -> Lua
  bindings. Reusable C++ ops with thin bindings; the Lua binding must not hold game
  memory logic.
- **Addresses** live in `exu.json` and generate
  `src/Util/EngineAddresses.generated.h` (`python tools/generate_engine_addresses.py`,
  `--check`). Raw engine-range literals in `src/` fail validation.
- **Ogre** runtime knowledge stays under `src/Ogre/`. Resolve Ogre exports at run
  time with `src/Ogre/OgreProc.h`; **do not** add load-time imports to the
  hand-made `lib/OgreMain.lib` (a missing export would stop `exu.dll` loading).
- **Lifetimes.** `exu.dll` is reloaded per mission Lua state, so statics live for
  one Lua state. Reset per-mission state explicitly from `Init` and
  `ResetMissionScopedState`. Policy is mission-scoped.
- **Ownership vs OpenShim.** EXU's entry detour steals only the first 10 bytes of
  `Person::Simulate`; OpenShim's patch at `0x0059DEA5` is inside the function and
  does not overlap. If another module already detours the entry, EXU's verified
  preimage fails and it stands down; **do not weaken that**. Any new EXU patch in
  the same function must verify its preimage, stand down when the preimage
  mismatches (including "OpenShim already patched it"), and prefer the OpenShim
  export when present.
- **Threading.** `g_activePolicy` is a plain object, safe only because it can hold
  nothing but stock. The first writer must decide how the hook and Lua share it
  (e.g. publish an immutable snapshot through an atomic pointer/generation). Do
  not assume same-thread.

## 5. Roadmap

Numbering follows the original plan. Steps 1-2 are done. **Do the read-only
instrumentation in 5a before step 3's writes:** step 3 needs to know the call
order and timing that 5a measures.

### Step 0 - Runtime qualification of what is already merged

Nothing merged has run in-game. Before building on it, capture (Lua-only capture is
the established method; see the 2026-08-28 section in `Docs/ANIMATION_API.md`):

1. `dumpbin /exports OgreMain.dll | findstr /i AnimationStateIterator`. If absent,
   `exu.animation.List` returns `nil` and needs a different enumeration route.
2. `exu.animation.List(exu.animation.TargetLocalFirstPerson())` after hopping out.
   Expect the stock clips (`idle`, `stand2Kneel`, `kneel2stand`,
   `fireRecoilSniper`, `jump`, `runForward`, `landParachute`).
3. OpenShim-absent local-FP resolution (PR #73 asked for this before "proven").
4. `exu.fps.GetPilotInterceptStatus()`: before hopping out `active=true`,
   `localCalls` 0; after, `localCalls` rises, `policyDecision == "passThrough"`.
   Select sniper: expect `0 -> 1 -> 2`; deselect: `2 -> 3 -> 0`. No visible
   gameplay difference from stock.
5. Record results in a new dated `Docs/Research/` note. Do not claim
   "proven-runtime" without the capture.

### Step 5a (do first) - Read-only timing trace

Objective: measure, don't guess. Add an opt-in, bounded ring buffer to the seam
that records per local call: a call counter, `dt`, before/after native state,
animation index, animation handle. Expose it read-only (for example
`exu.fps.GetPilotInterceptTrace(n)` plus start/stop), off by default.

Use it to answer:

- How long (sum of `dt`) do states 1 and 3 last, across many runs?
- Does that equal `exu.fps.GetInfo("stand2Kneel").length` / `kneel2stand`? If yes,
  completion is animation-driven and the "hardcoded durations" live elsewhere. If
  it is constant regardless of clip length, a numeric constant exists.
- When exactly does `animationHandle` become `-1` relative to the state change?
- Is the state change visible after the same call, or one call later?

Files: `PilotFsmIntercept.{h,cpp}` (ring), `AnimationApi.cpp` (binding),
`Definitions/Fps.lua`, `Docs/FPS_API.md`. Keep it lock-free and allocation-free in
the hook. Acceptance: no gameplay change, no measurable cost when off, host test for
the ring logic (keep it in a pure header like the policy).

### Step 3 - First writable feature: crouch animation substitution only

Two candidate approaches. Recommend **A** for the first prototype.

- **A. Presentation-level substitution after stock.** After stock `Person::Simulate`
  returns (both apply helpers have already run), if the policy says
  `enterCrouch`/`exitCrouch` = override, re-target the local FP entity's Ogre states:
  disable the stock clip, enable the substitute at the matching time. No `Person`
  writes; the FSM is untouched; local presentation only. Reuse the existing guarded
  animation operations. Unknowns to settle with the trace: whether stock re-applies
  every tick (then the override must be re-applied every call), and what drives the
  time position.
- **B. Native substitution** by detouring the apply helpers or writing `+0x2A8`.
  Needs their signatures (UNKNOWN) and changes gameplay-visible state. Only if A
  proves insufficient.

Also required:

- The first override must **replace** the `stock`-only guarantees, not bypass them:
  add a `Mode` value, extend `Evaluate`, keep "unrecognised mode / unmapped state
  -> pass-through", extend the host tests, and flip `pilotAnimationOverrides` only
  when a real override can be applied.
- `SetPilotAnimationProfile` arrives **with** this feature, not before. Validate
  strictly: unknown keys and unsupported override fields **error** (fail closed);
  never silently ignore. Return the effective profile through
  `GetPilotAnimationProfile`. `mode` and `nativeState` are the stable read-back
  fields.
- Gate to single player by default (see section 6).

### Step 4 - Prove interception timing/order

Use the 5a trace plus a runtime capture to document, for enter- and exit-crouch,
exactly where in the call the state and handle change relative to the hook's
before/after points. Write it up as a dated research note. This is the contract the
writers rely on.

### Step 5 - Trace the hardcoded duration mechanism

Static RE, driven by 5a's measurements:

1. Find every writer of `Person+0x2AC` and what sets it to `-1`.
2. Follow the animation-start call the FSM uses in states 0 and 2, and the
   animation object's lifetime/completion.
3. Look for float/int constants and per-animation tables near those paths.
4. Any new address goes in `exu.json` with a verified pattern
   (`tools/qualify_bzr_build.py`; never invent replacement signatures), then
   regenerate the address header.
5. Cross-check every found constant against the 5a trace before believing it.

Deliverable: a research note, catalog entries, and (only when proven) documented
stock values. **Do not expose stock durations before this is done.**

### Step 6 - Transition completion policy

Modes: `stock`, `animation`, `duration`, `manual`, plus
`exu.fps.CompleteTransition()` for `manual`.

- The stock wait in states 1 and 3 is `animationHandle == -1`. Ending early,
  extending, or holding manually needs control of that wait. **Do not write `-1`
  into the handle blindly**: the handle likely refers to a live animation object
  and doing so may leak or dangle it (INFERRED risk). Prefer a verified in-function
  patch of the wait predicate driven by policy (the way OpenShim patches the jump
  predicate), after step 5 defines what the handle owns.
- Host-test the completion decision as a pure function of
  (policy, elapsed, native handle state).

### Step 7 - Duration overrides

Only after 5 and 6. `duration` is gameplay FSM completion time in seconds, distinct
from `speed`. Reject non-finite/negative values. Document the proven stock behavior
next to the override.

### Step 8 - `SetCrouched(true|false)`

Must go through the FSM policy layer so `Person::Simulate` cannot reclaim the state
next tick.

- Leading design (INFERRED, section 3.2): a **virtual sniper** predicate. Locate the
  writer of the sniper-selected local (likely `[ebp-0x351]`) inside `Person::Simulate`
  and patch it (or its consumers in states 0 and 2) to also honour a policy flag.
  The stock FSM then performs its own transition side effects, which is why this is
  preferable to writing `Person+0x228` directly.
- Requires a mid-function patch with a byte-verified preimage, build gate, fail-closed
  stand-down, and no overlap with the `0x0059DEA5` site or the entry detour. Decide
  per patch how the flag is stored and how it resets per mission.
- Do **not** implement it as `Play("stand2Kneel")` or a seek.
- Acceptance: with the flag set and no sniper selected, the pilot enters and holds
  crouch across many ticks; clearing it exits; both use stock transitions and stock
  animations; state observed through `GetPilotState`.

### Step 8b - Standalone jump/sniper parity

Today `exu.SetJumpSnipeCrouch` only works when the OpenShim export is present. For
EXU to be independent, express the jump-vs-sniper ordering as a `jump` slot policy
and, when OpenShim is absent, apply an EXU-owned patch at the same site with the
same preimage check. It must stand down when OpenShim has already patched it, keep
the MP stand-down, and never double-patch. Do not replace OpenShim's implementation.

### Step 9 - Playback speed / managed clock (separate)

Redux/Ogre currently advances time (`managedClock=false`,
`nativeAdvancement="unvalidated"`). First capture the stock `Play/Stop/Seek`
runtime matrix to validate native advancement. A managed clock risks
double-advancement and frame-order bugs, so it is opt-in per animation and must not
touch FSM `duration`. Expose it as `speed`.

### Step 10 - Generalise the FSM

Only after crouch ownership is reliable: draw/holster, reload, melee, inspect,
sprint, prone, custom stances. Extend `Slot`/`Mode` deliberately; keep the
pass-through guarantee for anything unrecognised.

## 6. Multiplayer and safety policy

Nothing that writes logical `Person` state may silently run in MP. Classify every
feature explicitly before enabling it:

| Class | Example | MP rule |
| --- | --- | --- |
| Local presentation only | FP Ogre clip substitution (step 3A) | Likely safe; still opt-in |
| Local gameplay state | `Person+0x228`, virtual-sniper flag | SP-only until replication is understood |
| Replicated gameplay state | (UNKNOWN which fields) | Do not touch |
| Host-authoritative | (UNKNOWN) | Do not touch |

Precedent: OpenShim's jump fix stands down in MP. EXU can read `isNetGame` via
`BZR::Multiplayer::isNetGame` (see `src/Game/Multiplayer.cpp`), behind
`RuntimeGate::IsSupported()`. Default every writer to SP-only and require an
explicit, documented decision to relax it.

## 7. Engineering workflow and CI facts

- Follow `AGENTS.md`: inspect `git status -sb` first; stage only task-owned files;
  no force-push or history rewrite; no merge/tag/release/Workshop publish without
  explicit instruction; no secrets, logs, or build output.
- Host checks: `bash tests/linux/run.sh`. Needs `lua5.1`, `python3-pil`,
  `python3-numpy`. Also run `python tools/generate_engine_addresses.py --check`.
- **Only the Windows build proves MSVC compiles and links.** It runs for pull
  requests whose **base is `main`** (drafts included) and on pushes to `main`.
  Pushes to `agent/**` run Linux checks only.
- **Base every PR on `main`.** Stacked PRs on non-`main` bases skip the Windows build
  and drift from policy changes that land on `main`; that is why this stack needed
  rework at merge time. Open dependent PRs against `main` as drafts (the diff shows
  parent commits until the parent merges) so the build runs early, and merge often.
- If a retarget does not trigger a run, retarget first and then push.
- Reading CI: check status through the GitHub connector; job logs through
  `get_job_logs` with `tail_lines` (direct log downloads are blocked by the proxy);
  unauthenticated `api.github.com/.../commits/<sha>/check-runs` works for status
  (rate limit 60/hour).
- Merge convention: merge commits ("Merge pull request #N ..."), draft -> ready ->
  merge pinned to `expectedHeadSha`.

### MSVC/CI pitfalls hit in this workstream

- **C4459 is an error.** A local named `state` in any TU that sees
  `ExtraUtilities::Lua::state` (from `LuaState.h`) fails the build. Linux cannot
  catch this.
- **Every `__except` must use `Seh::Filter(GetExceptionCode())`** (`Util/SehGuard.h`);
  functions with `__try` are POD-only.
- **No raw engine-range literals** in `src/`; use the generated `EngineAddresses`.
- **`lib/OgreMain.lib` is a hand-made import subset.** Missing symbols surface only as
  `LNK2001` on Windows. Resolve at run time instead (see section 4).
- The C++14-pinned TUs (Ogre bridges) must stay C++14.
- New `.cpp`/`.h` files must be added to both `ExtraUtilities.vcxproj` and
  `.filters`; the validators and MSVC build both depend on it.

### Host-test habits that worked

- Keep decision logic in a pure header (no Windows/Ogre/Lua) so
  `tests/host/*.cpp` can compile it under both g++ and MSVC `/W4 /WX`.
  `storage_codec_tests.cpp` shows including a `.cpp` directly.
- A Lua binding can be checked on Linux by extracting the function text into a scratch
  harness against system Lua 5.1; this caught nothing wrong here but proved the
  table shape. It is not in the repo.
- Add hardening markers to `tools/validate_hardening.py` for new invariants and
  mutation-test them.

## 8. Suggested PR sequence

Each PR targets `main`; merge before starting a dependent one when practical.

1. Step 0 qualification note (docs only) + fixes for anything it finds.
2. Step 5a read-only trace.
3. Steps 4 and 5 research notes + `exu.json` entries (no behavior change).
4. Step 3A substitution + `SetPilotAnimationProfile` for that slot only.
5. Step 6 completion policy, then step 7 durations.
6. Step 8 `SetCrouched` (+ 8b jump parity).
7. Step 9 speed / managed clock.
8. Step 10 generalisation.

## 9. Decisions needed from the owner

- Slot key names. This work used `stand`, `enterCrouch`, `crouched`, `exitCrouch`,
  `jump`, `land` (matching the draft profile), not `standing`/`landing`.
- Whether unknown profile keys should hard-error (recommended) or be ignored.
- The MP stance per class in section 6.
- Whether presentation-only substitution (3A) is acceptable as "the first writable
  feature", or native substitution (3B) is required.
- Whether `#74`'s run-time Ogre resolution stays, or the original import-library
  design is restored (and `OgreMain.lib` regenerated on Windows).
- Version bump and release declaration sync (`src/About.h`, `include/ExtraUtils.h`,
  `Definitions/ExtraUtils.lua`) when this ships; `Docs/WORKSHOP_RELEASE.md` applies.
- Remote branches from PRs #73-#78 still exist; delete when ready.

## 10. Do not

- Do not publish guessed animation names for indices 4-7.
- Do not expose or commit stock duration values until traced and cross-checked.
- Do not write `-1` (or any value) into `Person+0x2AC` without knowing what the
  handle owns.
- Do not cache `Person*`, `Entity*`, or `AnimationState*` across operations.
- Do not make EXU and OpenShim patch the same bytes; verified preimages must make
  the loser stand down.
- Do not weaken the entry-detour preimage check.
- Do not enable logical `Person` writes in MP by default.
- Do not add load-time Ogre imports.
- Do not create a divergent EXU backlog; use OpenShim's.
- Do not claim runtime qualification that was not captured.
