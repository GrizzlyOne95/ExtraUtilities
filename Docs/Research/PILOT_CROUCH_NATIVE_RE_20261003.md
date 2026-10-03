# Pilot crouch transition: native RE (2026-10-03)

Static reverse engineering of `battlezone98redux.exe` (GOG Redux 2.2.301, x86, image base
`0x400000`). Closes the "UNKNOWN" items in section 3.3 of
`PILOT_ANIMATION_FSM_HANDOFF_20260928.md` and explains the 1.937 s capture in
`PILOT_FSM_LIVE_QUALIFICATION_20261003.md`. Nothing here was run in game; all offsets are from
the shipped image (read only).

Labels: **PROVEN** = seen in disassembly. **INFERRED** = follows from disassembly but not seen
directly. **UNKNOWN** = not resolved.

Tools: `pefile` + `capstone` (scratch scripts live outside the repo). Hex below is exact image
bytes. Section `.data` is `0xC0000040` (read/write), `.text` is `0x60000020`.

---

## 0. One-paragraph answer

A crouch transition is not driven by the clip length. Each FSM tick, `Person::Simulate` advances
the Ogre `AnimationState` itself with `addTime(dt * rate[idx])` and declares the clip finished when
`timePosition + dt*rate >= endTime[idx]`. For indices 0 (`stand2Kneel`) and 1 (`kneel2stand`)
`endTime = 0.967` and `rate = 0.5`, so the transition lasts `0.967 / 0.5 = 1.934 s` (+ up to one
tick), which is the 1.937 s captured. The "finished" bit is `bridge+0xDC` (first person) /
`bridge+0xD8` (world). The first-person bit makes `Simulate` release the animation handle in
`Person+0x2AC` and set it to `-1`; states 1 and 3 wait for exactly that. All of the
per-clip data (name, loop, rate, start time, end time) sits in contiguous 12-entry tables in
writable `.data`, read **only** by `Person::Simulate` (PROVEN by a full `.text` scan for
absolute references). The cheapest, safest override is a table write (or a table write/restore
around the stock call from the existing entry detour).

---

## 1. The per-clip tables (all indexed by animation index `Person+0x2A8`, 12 entries)

Index to name (PROVEN, string table at `0x8E8F24`, reads of `[idx*4 + 0x8E8F24]` at
`0x59E197/1AB/211/225`):

| idx | name (`char*` in table) | string VA |
| ---: | --- | --- |
| 0 | `stand2Kneel` | `0x8857A8` |
| 1 | `kneel2stand` | `0x88579C` |
| 2 | `idle` | `0x885794` |
| 3 | `fireRecoilSniper` | `0x88574C` |
| 4 | `runForward` | `0x885740` |
| 5 | `runBackward` | `0x885734` |
| 6 | `runLeft` | `0x88572C` |
| 7 | `runRight` | `0x885788` |
| 8 | `death1` | `0x885780` |
| 9 | `idleParachute` | `0x885770` |
| 10 | `landParachute` | `0x885760` |
| 11 | `jump` | `0x8857D0` |

This maps every index seen in the trace: 2 idle, 0 stand2Kneel, 3 fireRecoilSniper, 1
kneel2stand, 4 runForward, 9 idleParachute, 10 landParachute. The `walk*` clips are not in the
table and are not used by the FSM. Indices 4-7 are now named (they were unverified in the
handoff). Index selection for 4-7 is a stick-direction test at `0x59DF00-0x59E062` (PROVEN).

Parallel tables (all PROVEN by dump; `.data`, 12 entries each, stride 4 unless noted):

| table | VA | meaning | values idx 0..11 |
| --- | --- | --- | --- |
| end time (s) | `0x8E8E94` | clip-time at which the clip counts as finished | `0.967` x9 (0-8), `1.867` (9), `1.167` (10), `1.167` (11) |
| FP rate | `0x8E8EC4` | dt multiplier, first-person entity | `0.5 0.5 0.5 0.5 1.0 0.75 0.75 0.75 0.75 0.75 0.75 0.05` |
| start time (s) | `0x8E8EF4` | `setTimePosition` on apply (both models) | all `0`, except idx 11 = `0.2` |
| name ptr | `0x8E8F24` | see above | |
| loop byte | `0x8E8F54` (1 byte each, `movzx`) | `setLoop` on apply | `00 00 00 00 01 01 01 01 00 01 00 00` (idle is NOT looped; run*/idleParachute are) |
| world rate | `0x8E8F60` | dt multiplier, third-person entity | `0.5 0.5 0.5 0.5 1.0 0.75 0.75 0.75 0.75 0.75 0.75 0.6` |

Note idx 11 differs between the FP table (0.05) and the world table (0.6).

Only readers (PROVEN: scan of every 4-byte value in `.text` that falls in `0x8E8E90-0x8E8F90`;
`0x5B0897/08BC/0904/0912` hit the *next* table at `0x8E8F90`/`0x8E8F9C`, unrelated):

| reader VA | table |
| --- | --- |
| `0x59D9F7` | end time, world tick |
| `0x59DBBF` | end time, FP tick |
| `0x59E165` / `0x59E1DF` | start time (world / FP apply args) |
| `0x59E17A` | world rate (apply arg) |
| `0x59E1F4` | FP rate (apply arg) |
| `0x59E18C` / `0x59E206` | loop byte |
| `0x59E19A/1AE/214/228` | names (new, old, new, old) |

All inside `Person::Simulate` (`0x59D340`). The `0x8E8E94` readers are exactly the two
asked about in the brief (the third hit in the brief's first scan, `0x59E1F4`, is the FP rate
table, not the end time).

---

## 2. Per-tick advance and the 0.5 rate

`Person+0x18` is an embedded sub-object; `(*(*(Person+0x18)+0x2C))(this+0x18)` returns the
**bridge** `R` (the object the handoff calls the render bridge, equal to `*(Person+0xF0)`;
the tick block writes the done flag through `[Person+0xF0]` and clears it through the vcall
result, and the apply helpers store into the same offsets, so they are one object; INFERRED
for the identity, PROVEN for the field uses).

Bridge fields used (PROVEN from the helpers and tick code):

| offset | meaning |
| --- | --- |
| `+0x94` | WORLD Ogre `Entity*` |
| `+0xB4` | WORLD current clip name (`const char*`, stored by apply; NOT a std::string, must outlive use) |
| `+0xB8` | WORLD "has animation" flag (nonzero) |
| `+0xBC` | WORLD rate (float) |
| `+0xC0` | FP Ogre `Entity*` |
| `+0xC4` | FP "has animation" flag (nonzero) |
| `+0xD0` | FP current clip name (`const char*`) |
| `+0xD4` | FP rate (float) |
| `+0xD8` | WORLD "finished" dword |
| `+0xDC` | FP "finished" dword |

dt source: `call 0x822D60` = `fld dword [0x946790]` (a global frame-delta float in `.bss`; the
value is runtime only; INFERRED to be the game's per-tick dt).

### 2.1 WORLD block `0x59D969-0x59DB0F` (annotated)

```
0059D969  cmp  dword [eax+0xB8],0          ; R+0xB8 world flag (eax = vcall result)
0059D970  je   0x59DB0F
0059D98D  mov  ecx,[eax+0x94]  -> [ebp-0x4D0]   ; world Entity*
0059D9E1  mov  dword [eax+0xD8],0          ; clear world finished
0059D9F1  mov  ecx,[Person+0x2A8]
0059D9F7  movss xmm0,[ecx*4+0x8E8E94]       ; endTime[idx]   -> [ebp-0x4BC]
0059DA1A..  push [Person+0xF0]+0xB4 ; std::string ctor (0x416EF0) from char*
0059DA3A  call [0x869E04]  Entity::getAnimationState(const std::string&)
0059DA58  call 0x822D60  (fld [0x946790]) ; dt
0059DA92  mulss xmm0,[eax+0xBC]             ; dt * R+0xBC (world rate)
0059DAA8  call [0x869E08]  AnimationState::getTimePosition
0059DABC  addss  xmm0,[ebp-0x3D0]           ; pos + dt*rate
0059DAD4  comiss xmm0,[ebp-0x4BC]           ; vs endTime
0059DADB  jb   0x59DAF5
0059DAE9  mov  dword [edx+0xD8],1           ; finished  (no addTime on this tick)
0059DAF5  call [0x869E0C]  AnimationState::addTime(dt*rate)   ; else advance
```

### 2.2 FP block `0x59DB26-0x59DCD7`

Same shape with the FP offsets: gate `[R+0xC4]`, entity `[R+0xC0]`, name `[R+0xD0]` (gate
`cmp [R+0xD0],0` at `0x59DB85`), clear `[R+0xDC]` (`0x59DBA9`), endTime load at
`0x59DBB9/0x59DBBF`, rate `mulss [eax+0xD4]` at `0x59DC5A`, compare `0x59DC9C`, set `[R+0xDC]=1`
at `0x59DCB1`, else `addTime` at `0x59DCD1`.

So **duration of a transition = endTime[idx] / rate[idx]** in real seconds (+ quantisation
to one tick, since the tick that crosses the end only sets the flag). With idx 0/1:
`0.967/0.5 = 1.934`, plus the one extra tick = observed `1.936-1.938`. PROVEN (arithmetic
matches the capture to within a tick). The rate is **not** a per-Person constant; it is stored
into the bridge at apply time (`R+0xBC`, `R+0xD4`) from the table, and the end time is
re-read from the table each tick.

Consequence for ISDFC clips (length 1.0 s per the qualification note): the clip is played in
full at half speed until the clip position reaches 0.967 clip-seconds, then frozen (non-looped Ogre state,
no further addTime), at the last pose before the end. This also explains the earlier finding
"kneel clips play in full at about half speed".

The finished flags are only ever **written** inside `Simulate`; the tick block clears them
before recomputing every tick, so an outside write to `+0xD8/+0xDC` between calls is
overwritten before use (see 4). A whole-`.text` scan for other readers of `+0xD8/+0xDC` was
too noisy (those offsets are common) and was not completed: **UNKNOWN** whether anything other
than `0x59DD01` reads `+0xDC`/`+0xD8` of the bridge.

---

## 3. State 0->1 / 2->3 and the apply helpers

### 3.1 Choosing the index

Index selection writes `[ebp-0x348]` (new index); `[ebp-0x354]` is a force-reapply byte (only
set to 1 on jump entry, `0x59DEBA`); `[ebp-0x364]` is an optional AnimObj speed (default
`-1e30`, `0x8A2FC8`). The jump table for the FSM is at `0x5A0A98` (4 entries:
`0x59DDE6`, `0x59E073`, `0x59E0AA`, `0x59E0DD`), dispatched at `0x59DDDF`.

| transition | VA of `mov [ebp-0x348],imm` + state store (exact bytes) |
| --- | --- |
| 0->1 (sniper selected, grounded, not jumping) | `0x59DEE1`: `C7 85 B8 FC FF FF 00 00 00 00 8B 95 C0 FC FF FF C7 82 28 02 00 00 01 00 00 00` (idx 0, state 1) |
| 1->2 (handle == -1) | `0x59E082`: `C7 85 B8 FC FF FF 03 00 00 00 8B 8D C0 FC FF FF C7 81 28 02 00 00 02 00 00 00` (idx 3, state 2) |
| 2->3 (sniper deselected) | `0x59E0B5`: `C7 85 B8 FC FF FF 01 00 00 00 8B 85 C0 FC FF FF C7 80 28 02 00 00 03 00 00 00` (idx 1, state 3) |
| 3->0 (handle == -1) | `0x59E0EC`: `C7 85 B8 FC FF FF 02 00 00 00 8B 95 C0 FC FF FF C7 82 28 02 00 00 00 00 00 00` (idx 2, state 0) |

Waits (PROVEN): state 1 `0x59E079: 83 B8 AC 02 00 00 FF 75 1C ...` (`cmp [eax+0x2AC],-1`),
state 3 `0x59E0E3: 83 B9 AC 02 00 00 FF 75 1C ...`. While waiting the new index stays 0 (resp. 1)
(`0x59E09E`, `0x59E108`), i.e. no re-apply (idx equals current, force byte 0).

`[ebp-0x351]` (sniper selected) is set to 1 at `0x59D7E0` after a `cmp [weapon_class+0xC],
0x534E4950` and zeroed at `0x59D5F3`; `[ebp-0x352]` (jump held) is `Person+0x230`'s
`+0xD8` dword at `0x59D735`. This confirms the handoff's inference about the `0x59DEA5` OpenShim
patch (`AH` = sniper local). `[ebp-0x359]` is `(Person+0xFC == 0)`; when true the index is forced
to 8 (`death1`, `0x59DDAB`).

### 3.2 Apply block (`0x59E112-0x59E28B`)

```
0059E118  mov ecx,[Person+0x2A8]
0059E11E  cmp ecx,[ebp-0x348]
0059E124  jne apply
0059E126  movzx edx,byte [ebp-0x354] ; force
0059E12F  je   0x59E28E               ; same idx and no force -> skip
apply:
0059E13B  cmp dword [Person+0x2AC],0
0059E142  jl   skip_release
0059E151  call 0x62A5D0                ; AnimObj release(handle)   (cdecl, 1 arg)
          ; WORLD apply
          push startTime=[idx*4+0x8E8EF4]  push rate=[idx*4+0x8E8F60]
          push loop=byte[idx+0x8E8F54]
          push newName=[idx*4+0x8E8F24]    push oldName=[Person.idx*4+0x8E8F24]
          push R (vcall result)
0059E1CB  call 0x680670 ; add esp,0x18
          ; FP apply: identical, rate=[idx*4+0x8E8EC4]
0059E245  call 0x680770 ; add esp,0x18
0059E253  mov [Person+0x2A8],newIdx
0059E286  call 0x62A270 (Person+0xF4, newIdx, &Person+0x2AC)  ; AnimObj start -> handle
0059E294  if handle>=0 && speed>=0.0 -> call 0x62A5A0(handle,speed)
0059E2D7  if handle>=0: call 0x62A540(handle,&slot)  ; reads slot, drives a mid-clip event
```

**Apply helper signature (both PROVEN, cdecl, caller cleans 0x18):**

```
void Apply(Bridge* R,            // [ebp+0x08]
           const char* oldName,  // [ebp+0x0C]   disabled (setEnabled(false))
           const char* newName,  // [ebp+0x10]   enabled, stored at R+0xB4 / R+0xD0
           int loop,             // [ebp+0x14]   from byte table
           float rate,           // [ebp+0x18]   stored at R+0xBC / R+0xD4
           float startTime);     // [ebp+0x1C]   setTimePosition
```

- `0x680670` WORLD (`R+0x94`, `R+0xB4`, `R+0xB8`, `R+0xBC`), `0x680770` FP (`R+0xC0`, `R+0xD0`,
  `R+0xC4`, `R+0xD4`). Both start with `55 8B EC 83 EC 44 A1 00 70 8E 00 33 C5 89 45 FC`
  (16 bytes, stack cookie), and are identical apart from offsets.
- Body (PROVEN): stores `newName`/`rate` into the bridge first, then, if the "has animation"
  flag is nonzero: `std::string` from `oldName` (`0x416EF0`), `Entity::getAnimationState`
  (`[0x869E04]`), `setEnabled(false)` (`[0x86998C]`); same for `newName` with
  `setEnabled(true)`, then `setLoop(loop!=0)` (`[0x869818]`),
  `Entity::refreshAvailableAnimationState` (`[0x869788]`), `setTimePosition(startTime)`
  (`[0x869734]`). No `AnimationState*` is cached: the tick code re-resolves it **by name every
  tick**.
- A name that does not exist on the entity makes `getAnimationState` throw an Ogre
  `ItemIdentityException`. There is no handler in the helpers or in `Simulate`. A substituted
  name must exist in both the world and FP skeletons (INFERRED consequence; the throw itself is
  Ogre behaviour).
- Note the helpers disable `oldName` from the **table by current index**, not from the bridge's
  stored name. Any substitution that changes a name must therefore account for the old name too
  (see 5a).

### 3.3 How a clip name reaches Ogre

`char*` from table (`0x8E8F24`) -> stored in `R+0xB4`/`R+0xD0` -> each tick `std::string` temp
(`0x416EF0` ctor / `0x416F30` dtor) -> `OgreMain.dll` `Entity::getAnimationState(const
string&)` (IAT `0x869E04`) -> `AnimationState*` -> `getTimePosition` (`0x869E08`) / `addTime`
(`0x869E0C`) / `setEnabled` (`0x86998C`) / `setLoop` (`0x869818`) / `setTimePosition`
(`0x869734`). The same calls are made on the WORLD entity (`R+0x94`) and the FP entity
(`R+0xC0`), so both models are advanced by the same rules.

---

## 4. `Person+0x2AC` and what the handle owns

`Person+0x2AC` is **not** an Ogre object. It is a slot index into an engine "animation
object" pool (`0x2A17498`, 512 slots x 0x20 bytes; live count at `0x920C58`, cap `0x200`):

- `0x62A270` `AnimObjStart(obj=Person+0xF4, idx, int* outHandle)` (cdecl, `55 8B EC 83 EC 30
  8B 45 08 89 45 F4 8B 4D 10 C7`): sets `*out = -1`, refuses if the pool is full, searches the
  list at `0x2A1B498` (count `0x920C5C`) for the owner whose `+0x38 == obj`, finds the clip
  entry whose first dword == `idx` (stride `0x94`, array at `owner+0x24`, count `owner+0x10`), takes
  the first free pool slot and fills: `[+0]=1 active, [+4]=owner, [+8]=speed(entry+0x90),
  [+0xC]=frame cursor(float), [+0x10]=start frame(entry+0x84), [+0x14]=end frame, [+0x18]=
  +/-length(entry+0x88), [+0x1C]=...`, then `*out = slot` and `++count`. On failure it logs a
  string and returns -1 (handle stays -1).
- `0x62A540` `get(handle, &slot*)`, `0x62A5A0` `setSpeed(handle,float)` (writes slot `+8`),
  `0x62A5D0` `release(handle)` (`55 8B EC 51 83 7D 08 00 7C 09 81 7D 08 00 02 00`; decrements
  the counter, zeroes the 32-byte slot).
- The animation index is the join key between the engine's frame-animation entries and the
  Ogre clip table; indices >= 12 do not exist in the table and have no AnimObj entry
  (INFERRED).
- This pool advances independently (a separate per-frame update; not examined). `Simulate`
  reads the cursor at `0x59E2F8-0x59E3A0` to toggle the byte at `Person+0x2B0` when the cursor
  crosses `(start+length)*0.5` (`0x8A2584 = 0.5`) and fires an event (`0x43A990/0x43A9E0/0x43AA30`;
  effect/sound; purpose INFERRED).

**What sets it to -1 (PROVEN, only two places):**

1. `0x59DD10-0x59DD25` (done path): `if (handle >= 0 && bridge+0xDC != 0) { release(handle);
   handle = -1; ... }` (`83 B8 DC 00 00 00 00 74 6D` at `0x59DD01`). It tests the **first
   person** finished flag only. The WORLD flag `+0xD8` is not read there (INFERRED: a person
   without an FP entity, `R+0xC4 == 0`, never gets `+0xDC` set by this code and so would
   never leave states 1/3; probably irrelevant because only the local player selects the
   sniper rifle. UNKNOWN whether the constructor presets `+0xDC`).
   After the release, if idx == 8 (death) and `0x4B9830` is false, `Person+0xF4`'s `+0x14 |= 0x200`
   (`0x59DD35-0x59DD74`).
2. `0x59E13B-0x59E156` (index change): `release(handle)` but the handle is then re-assigned by
   `0x62A270` at `0x59E286`; if that fails the handle is `-1`.

Order inside one `Simulate` call (PROVEN): input/sniper scan -> WORLD tick -> FP tick
-> **handle release** -> FSM decision (waits on `-1`) -> apply -> AnimObj start.
So a transition finishes in the same call that the FP flag is set; a state-1 call can only see
`-1` on the call after the one that applied idx 0 (minimum one tick).

**Correction to the live note**: it says `animationHandle` was `0` during crouch and "-1 only
around standing idle", and concludes the handle wait does not end states 1/3. `0` is a valid
slot index (first free slot), not "done"; `-1` is "no handle". The wait **is** the real
completion test; the FP finished flag is what clears it. The capture is consistent with
that (the handle is `-1` on idle because idle is non-looped, rate 0.5, end 0.967 and finishes
after about 1.93 s, releasing its handle).

**Safe early completion.** Releasing the handle yourself must go through `0x62A5D0(handle)`
and then store `-1`, exactly as `0x59DD10` does; storing `-1` alone leaks a pool slot (512 max,
`0x62A270` starts failing at the cap). Setting `bridge+0xDC=1` externally does not work: it is
cleared at `0x59DBA9` before it is recomputed in the same call. The reliable early finisher is
a smaller `endTime[idx]` (read every tick) or a faster rate (see 5b).

---

## 5. Recommended patch points

Order of preference. Every preimage is verified bytes of the shipped image.

### 5a. Substituting the crouch clip name (idx 0 and idx 1)

**Preferred: write the name pointers in the table (no code patch).**

- VA `0x8E8F24`, 8 bytes, preimage `A8 57 88 00 9C 57 88 00` (ptr `stand2Kneel`, ptr `kneel2stand`).
- Replace the `char*` with a pointer to an EXU-owned, NUL-terminated, never-freed string
  (the engine stores the raw pointer in `R+0xB4/0xD0` and rebuilds a `std::string` from it every
  tick).
- Write only while the Person is in a stable state (FSM state 0 or 2) so `oldName`
  (re-read from the table by the current index at the *next* apply) refers to the clip that is
  actually enabled; change the table back/forward consistently with the clip that was last
  applied, or the old clip is never disabled and two clips blend.
- Gate to the local pilot and restore after the stock call from the existing
  `PilotFsmIntercept` entry detour (table readers are only in `Simulate`, so a write/restore
  pair around the trampoline call is invisible to other Persons if `Simulate` is not
  re-entered and runs on one thread; threading is still UNKNOWN per the handoff).
- Risks: substituted name must exist on both world and FP skeletons (uncaught Ogre throw
  otherwise); the global write affects every Person (AI, remote) unless restored around the
  local call; the clip is still 0.967/0.5 timed (see 5b).

**Per-transition / per-Person alternative: detour the apply helpers.**

- `0x680670` (WORLD) and `0x680770` (FP), 16-byte preimage
  `55 8B EC 83 EC 44 A1 00 70 8E 00 33 C5 89 45 FC` (prologue, stack-cookie; both identical).
  `EntryDetour32` steals the first 10 bytes in the same way as for `Person::Simulate`.
  cdecl, 6 stack args (3.2). In the detour: if `newName` is the stock `stand2Kneel` /
  `kneel2stand` string, replace `newName` with the substitute; **also replace `oldName`** with
  the bridge's stored current name (`R+0xB4` WORLD, `R+0xD0` FP) when it is non-null, so the
  previously applied (substituted) clip is the one disabled. Optionally replace `rate`/`startTime`.
- Risks: two hooks must stay in step (WORLD+FP); OpenShim, if it ever hooks the helpers, would
  collide (preimage check fails and EXU stands down); the same helpers also serve all other
  indices, so the match must be by string/index.

**Index re-pointing is not recommended**: `0x59DEE1`/`0x59E0B5` (table in 3.1) could change
idx 0/1 to another table index, but idx also keys the AnimObj entry (`0x62A270`), the
`idx == 8`/`idx == 11` special cases and the three `[+0x2A8]` consumers, and the table has no
spare slots (12 entries, contiguous).

### 5b. Overriding the transition duration

Duration = `endTime[idx] / rate[idx]`. For a wanted `D` and clip length `L` (read it with
`exu.animation`), keep the clip whole with `endTime = L` (or slightly under, like stock 0.967
under 1.0) and `rate = L / D`. Never set `endTime < L` unless truncation is wanted (the clip
freezes at the cut).

| what | VA | bytes (preimage) | note |
| --- | --- | --- | --- |
| end time idx 0,1 | `0x8E8E94` | `50 8D 77 3F 50 8D 77 3F` (0.967 x2) | read every tick; can change mid-transition |
| FP rate idx 0,1 | `0x8E8EC4` | `00 00 00 3F 00 00 00 3F` (0.5 x2) | latched at apply (`R+0xD4`) |
| WORLD rate idx 0,1 | `0x8E8F60` | `00 00 00 3F 00 00 00 3F` (0.5 x2) | latched at apply (`R+0xBC`) |
| start time idx 0,1 | `0x8E8EF4` | `00 00 00 00 00 00 00 00` | `setTimePosition` on apply |

- Set the rate tables **before** the call in which the FSM applies the clip (state 0 with
  sniper selected for 0->1, state 2 with sniper deselected for 2->3); the end time can be set
  any time. Completion is decided only by the FP finished flag, so FP `rate`/`end` are the ones that
  matter for FSM timing; keep the WORLD rate in step to keep third-person presentation matched
  (the WORLD flag does not gate the FSM).
- Minimum duration is one tick (apply tick, then the next tick's advance sets the flag and
  releases the handle, then the FSM moves on in the same call). `endTime <= 0` is therefore
  "complete next tick".
- Code-level alternative (only if a per-call table write is unacceptable): hook `0x59DBBF`
  (FP end time load, whole instruction group `8B 91 A8 02 00 00 F3 0F 10 04 95 94 8E 8E 00` at
  `0x59DBB9`, WORLD twin `8B 88 A8 02 00 00 F3 0F 10 04 8D 94 8E 8E 00` at `0x59D9F1`), or the
  multiplies `F3 0F 59 80 D4 00 00 00` at `0x59DC5A` (FP) and `F3 0F 59 80 BC 00 00 00` at
  `0x59DA92` (WORLD), or the compares `0F 2F 85 14 FB FF FF 72 18` at `0x59DC9C` and
  `0F 2F 85 44 FB FF FF 72 18` at `0x59DAD4`. These are mid-function in a function that already
  carries an EXU entry detour and an OpenShim patch at `0x59DEA5`; none overlap, but each needs a
  trampoline that relies on `[ebp-0x...]` locals. Prefer the data-table route.

Risks common to 5b: the tables are global (all Persons, all missions) unless restored around the
local call; changing `rate` also changes the visual speed (that is the intent); a rate of `0`
makes `cur+dt*0 >= end` false forever (the clip never ends, the FSM hangs); a negative or `NaN`
value is unvalidated.

### 5c. Things to avoid

- Do not write `Person+0x2AC` directly, and do not rely on writing `+0xD8/+0xDC` (see 4).
- Do not assume `+0x2A8` index values >= 12 are valid.
- Do not read `AnimationState*` from the bridge: it is never stored (resolved by name each tick).

---

## 6. Open items

- UNKNOWN: runtime value of `[0x946790]` (dt) and whether it is clamped.
- UNKNOWN: whether `bridge+0xD8` has any reader outside `Simulate`; only the in-function
  usage was verified.
- UNKNOWN: which function is the per-frame AnimObj pool update (`0x2A17498`), and what the
  `0x43A990/9E0/AA30` event on the `Person+0x2B0` toggle does.
- UNKNOWN: the three model-setup functions that reference the `idle` string (call sites
  `0x67D7BC`, `0x67E944`, `0x67FF91` and neighbours) were noted, not analysed; they enable
  `idle` when the entity is created.
- UNKNOWN: whether `Simulate` and Lua share a thread (relevant for table write/restore).
- INFERRED only: the bridge/`Person+0xF0` identity and that non-local Persons never satisfy
  the FP finished flag.
- Next live check to promote INFERRED to PROVEN: from the intercept, write `0x8E8E94[0]` to
  `0.5` before a 0->1 transition and confirm the state-1 duration halves (expect about 1.0 s).
