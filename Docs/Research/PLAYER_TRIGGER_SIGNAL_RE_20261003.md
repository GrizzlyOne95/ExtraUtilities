# Player "fire trigger held" signal: native RE (2026-10-03)

Static reverse engineering of `battlezone98redux.exe` (GOG Redux 2.2.301, x86, image base
`0x400000`). Goal: a reliable per-tick "the LOCAL PLAYER is holding fire right now" signal readable
from EXU's `Person::Simulate` hook (`0x0059D340`) while on foot, to drive a minigun barrel spin.
Nothing here was run in game; everything is from the shipped image (read only). Style and the
`Person` field map follow `PILOT_CROUCH_NATIVE_RE_20261003.md`.

Labels: **PROVEN** = seen in disassembly. **INFERRED** = follows from disassembly, not seen
directly. **UNKNOWN** = not resolved.

---

## 0. Answer in one paragraph

`carrier+0x3C` (EXU's `weaponTriggerTillTime`) **does not exist in this build**: the Carrier is
allocated as `0x3C` bytes, so `+0x3C` is one dword past the object. Nothing reads or writes it.
There is also no "trigger" field anywhere on the Carrier, the Person or the controls block: the
player's fire button is **not** stored on the controlled object at all. It lives in a global
per-key state byte, `weapon_fire` = **byte at `0x009198C0`** (and `weapon_fire_auto` = byte at
`0x009198C1`), written by the input poll (`0x006210A0`) and consumed once per tick by
`UserProcess` (`0x0060A480`), which calls `Carrier::FireSelected` (`0x00511FC0`) when the byte is
nonzero. Recommended signal: **`*(volatile int8_t*)0x009198C0 != 0`** (optionally
`| *(int8_t*)0x009198C1`), read inside the `Person::Simulate` hook for the local Person.

---

## 1. carrier+0x3C (`weaponTriggerTillTime`): does not exist (PROVEN)

### 1.1 The Carrier is 0x3C bytes

`GameObject+0x1A0` is the Carrier pointer (accessor `0x00417CA0`: `mov eax,[ecx+0x1A0]`). The only
two allocation sites:

```
004DA834  push 0x3C                 ; operator new(0x3C)   (GameObject init, 0x004DA0B0 region)
004DA836  call dword ptr [0x869428]
004DA85F  call 0x4D9800             ; Carrier ctor(owner)
004DA892  mov  [ecx+0x1A0], edx

006127FC  push 0x3C                 ; second creation path (0x00612730)
00612804  ...
0061281E  call 0x4D9800
```

Carrier ctor `0x004D9800` (complete, it is short):

```
004D980F  mov  [eax], ecx           ; +0x00 owner
          for i in 0..4:
004D982F    mov [this+i*4+4],  0    ; +0x04..+0x14 hardpoint[5]
004D983D    mov [this+i*4+0x18], 0  ; +0x18..+0x2C weapon[5]
004D984A  mov  [eax+0x2C], 0        ; existingMask
004D9851  mov  [eax+0x34], 0        ; enabledMask
004D985E  mov  [eax+0x30], 0        ; selectedMask
004D9868  mov  [eax+0x38], -1       ; special
004D9875  ret  4
```

So the real layout is `owner, hardpoint[5], weapon[5], existing(+2C), selected(+30), enabled(+34),
special(+38)` and the object ends at `+0x3B`. EXU's `CarrierWeaponSelectionLayout` in
`src/Game/GameObjectInternal.h` matches up to `special`; its last member `weaponTriggerTillTime` at
`+0x3C` is a PDB-era field that is **not in the GOG 2.2.301 binary**. It is currently only
declared (no use found in `src/`), but any read of it would read the heap neighbour. Recommend
deleting the member (or marking it "PDB only, not present in GOG").

### 1.2 No code touches it (PROVEN by full scan)

A full `.text` scan (capstone, skipdata mode) for every memory operand with displacement `0x3C`
and a non-stack base found 857 hits; none sits in a Carrier method. The Carrier method set
(all inspected): `0x417F40` GetHardpoint(i), `0x417F60` GetWeapon(i) (`existing` bit test, then
`[this+i*4+0x18]`), `0x417F90` GetSelected (`+0x30`), `0x417FB0` GetEnabled (`+0x34`), `0x4A77A0`
SetWeapon(i,w), `0x4D9880` SetSelected(mask), `0x4D9950` SetEnabled(mask `& existing`), `0x511FC0`
FireSelected. The `+0x3C` stores that exist belong to unrelated classes (audio buffer ctor
`0x4187B7`, `0x418A51`; tree/list nodes `0x42082A`, `0x4230B1`; etc.).

Conclusion for question 1: there is no writer and no reader of `carrier+0x3C`; the value is neither
current time + hold, nor AI-written, nor player-written. **Do not use it.**

---

## 2. The player control path (PROVEN)

### 2.1 Chain

```
keyboard / mouse / joystick
  -> InputPoll 0x006210A0 (called from 0x00618301)       ; fills the per-command state bytes
       command table 0x008EB1E8: 0x6F entries x 0x20 bytes {name*, state_byte*, type, 0...}
       type 1 = "held" (level), type 2 = "edge/pressed"
       -> global state block 0x0091989C..0x009198D3 (0x38 bytes)
  -> UserProcess "Execute" 0x0060A480  (vtable slot 0x00889FE8; class-name string "UserProcess"
       sits at 0x00889FEC)         ; per sim tick, for the player-controlled object [this+0xC]
       -> writes the controls block   [obj+0x230] + 0xC4 .. +0xEF  (0x2C bytes)
       -> calls Carrier::FireSelected 0x00511FC0 when the fire byte is set
```

### 2.2 Command table entries (PROVEN, dumped from `.data`)

`0x008EB888` onward (name -> state byte VA, type). The fire-relevant ones:

| entry VA | command | state byte | type |
| --- | --- | --- | --- |
| `0x8EB888` | `weapon_fire` | `0x009198C0` | 1 (held) |
| `0x8EB8A8` | `weapon_fire_auto` | `0x009198C1` | 1 (held) |
| `0x8EB8C8` | `weapon_cycle` | `0x009198C2` | 2 (edge) |
| `0x8EB8E8` | `weapon_link` | `0x009198C3` | 2 |
| `0x8EB908` | `weapon_special` | `0x009198C4` | 1 |
| `0x8EB928..0x8EB9A8` | `weapon_select_0..4` | `0x009198C5..C9` | 2 |
| `0x8EB9C8` | `turbo` | `0x009198CA` | 1 |
| `0x8EB9E8` | `jump` | `0x009198CB` | 1 |
| `0x8EBA08` | `eject` | `0x009198CD` | 2 |
| `0x8EBA28` | `deploy` | `0x009198CC` | 2 |
| `0x8EBA48` | `abandon` | `0x009198CE` | 2 |
| `0x8EBA68` | `cloak` | `0x009198D2` | 2 |

(The handler pointers in the sibling table at `0x8E7040` / `0x4182D0`, `0x4183B0`, `0x418490`
are command *availability* predicates for the HUD, not the trigger: `weapon_fire` available when a
selected+enabled weapon has enough ammo, ammo = `[obj+0x210] ^ 0x33333333` via `0x417C80`;
`weapon_fire_auto` available when all selected weapons have class byte `+0x79` set.)

### 2.3 InputPoll lifecycle of the held byte (PROVEN)

```
00621210  for i in 0..0x6E:                          ; every poll
0062122E    if [i*0x20 + 0x8EB1F0] (type) != 0:
00621243       byte [ [i*0x20 + 0x8EB1EC] ] = 0       ; clear every state byte first
00621248  cmp dword [0x9198F8],0 ; jne 0x62153E       ; blocked: bytes stay 0
...
006212D0  for each bound input j (stride 0x74, base 0x91A45C..):
0062135A    byte [state_ptr] |= (byte)current_key_state   ; OR in held keys/buttons
```

So the byte is "bound key/button is down at the last poll" (nonzero), rebuilt from scratch each poll.
It is **not** cleared by the engine on fire; it simply stays set while the key is held.
INFERRED: the poll runs once per frame/tick (called at `0x618301`); which of the two is UNKNOWN,
irrelevant for an animation.

### 2.4 UserProcess::Execute (0x0060A480) fire logic (PROVEN, annotated)

```
0060A49B  mov [ebp-0x110], ecx            ; this (UserProcess)
0060A4A1  ecx = [this+0xC]                ; the controlled GameObject  -> [ebp-0x158]
0060A4B3  call 0x417CA0                   ; carrier = obj+0x1A0        -> [ebp-0x120]
0060A4BE  call 0x6217C0                   ; "input allowed": (cmp [0x8EAD6C],0 ; je -> 0)
0060A4C5  jne 0x60A52C                    ;   && 0x5D50C0() == 0  -> 1
          ; if NOT allowed: memset(0x91989C, 0, 0x38) keeping its first word,
          ;                 memset([obj+0x230]+0xC4, 0, 0x2C), [this+0x78]=0   (0x60A4C7-0x60A523)
...
0060A788  cmp [ebp-0x120],0 ; je 0x60ABFE ; no carrier -> skip all weapon handling
...
0060A932  mov ecx,[ebp-0x158]
0060A938  call 0x4AEF30                   ; cmp dword [obj+0x298],0 -> nonzero = skip fire
0060A942  jne 0x60ABFE
0060A948  movsx eax, byte [0x9198C0]      ; weapon_fire (held)
0060A94F  test eax,eax ; je 0x60A95F
0060A953  mov [ebp-0x160],1 ; ...         ; -> [ebp-0x115] = 1 (fire this tick)
0060A97E  jne 0x60ABD2                    ; fire key held -> straight to the fire call
0060A984  movsx eax, byte [0x9198C1]      ; weapon_fire_auto
0060A98B  test eax,eax ; je 0x60ABD2
          ... only if every selected weapon class has +0x79 set (loop 0x60A9A6-0x60A9FC),
          ... and [this+0x78] (locked target) != 0, aim test 0x60A320 + 0x462B60 -> [ebp-0x115]
0060ABD2  movzx eax, byte [ebp-0x115]
0060ABDB  je 0x60ABE8
0060ABDD  mov ecx,[ebp-0x120]             ; carrier
0060ABE3  call 0x511FC0                   ; Carrier::FireSelected
0060ABE8  movsx ecx, byte [0x9198C4]      ; weapon_special held -> 0x608AA0(obj)
```

Carrier::FireSelected (`0x00511FC0`, complete):

```
for i in 0..4:
  if ((carrier[+0x30] & carrier[+0x34]) & (1<<i)):     ; selected AND enabled
      w = carrier[+0x18 + i*4]
      (*w->vtable[2])(w)                                ; Weapon::Fire  (vtable +8)
```

It is called **every tick the key is down**, with no hold-time bookkeeping and no trigger timestamp
anywhere; the per-weapon fire rate / reload / ammo is enforced inside the weapon's `Fire`
(not analysed, UNKNOWN). Weapon vtable slots `+0x1C` / `+0x20` are select / deselect
(called by `0x4D9880`).

### 2.5 The controls block on the Person (PROVEN)

`UserProcess` writes a 0x2C-byte block at `ctrl = *(obj+0x230) + 0xC4` (so Person+0x230 is a
pointer to a control/physics object, ctor-time value from `0x45C4F0`, `+0x120` points back at the
Person). Stores near the end of Execute:

```
0060B5B6  [ctrl+0x10] = (byte)[0x9198CA] | (mouse-magnitude test)   ; turbo/sprint  = Person+0xD4
0060B5BF/DE [ctrl+0x14] = (byte)[0x9198CB]                           ; jump          = Person+0xD8
0060B5EE  [ctrl+0x18] = (byte)[0x9198CD]                             ; eject         = Person+0xDC
0060B5FE  [ctrl+0x1C] = (byte)[0x9198CC]                             ; deploy        = Person+0xE0
0060B614  [ctrl+0x20] = (byte)[0x9198CE]                             ; abandon       = Person+0xE4
0060B639  [ctrl+0x28] = (byte)[0x9198D2]  (if 0x417DD0)              ; cloak         = Person+0xEC
ctrl+0x00..+0x0C: four float axes (Person+0xC4/C8/CC/D0), smoothed (0060AECC...)
```

This matches what `Person::Simulate` reads: `[Person+0x230]+0xD8` dword = jump held (`0x59D735`),
`+0xC4/+0xC8/+0xCC/+0xD0` axes (`0x59D688..0x59D729`), `+0x114` ground flags (separate; written by
Simulate itself). **There is no fire field in this block.** The network encode
(`0x5A1E80`, vtable slot +0xE8) packs only the four axes, jump (bit0 of byte `+0x12`), turbo
(bit1), eject (bit2), grounded, `+0x114&4`, `GameObject+0x189`, and weapon-mode bits
(`byte +0x13`; bit3 = a selected weapon is `SNIP`, sig `0x534E4950`). Fire is not in the Person
packet, so **remote Persons have no equivalent trigger signal in the controls**.

---

## 3. Person weapons use the same path (PROVEN structurally, firing details UNKNOWN)

* `UserProcess` is object-agnostic: it works on `[this+0xC]` and its carrier. The jump/turbo
  stores above are exactly the fields `Person::Simulate` consumes, so the on-foot player is
  driven by the same `UserProcess` as any craft (PROVEN by the matching `+0x230`/`+0xD8` use).
* `Person::Simulate` itself never fires. Its only Carrier interaction (`0x59D3EA..0x59D41E`
  deselect-all on death, `0x59D766..0x59D7E0` the `SNIP` scan) is selection only. Its final call is
  virtual slot `+0x98` = `0x005A12A0` which only re-poses the weapon hardpoint transforms
  (pitch `[Person+0x280]`, GC1 hardpoint via `0x6819A0/0x681A00`), no firing.
* The hand-held weapon therefore fires through `Carrier::FireSelected -> Weapon::Fire`, driven
  by byte `0x9198C0`. For the SNIP path the on-foot FSM (`0x59D7E0`) only reads `selected` +
  weapon class sig; it does not read any trigger.
* UNKNOWN: `Weapon::Fire` (vtable slot 2 of the per-weapon object) internals, i.e. where the
  per-weapon last-fire time / ammo gate lives; not needed for the held-key signal.

---

## 4. Recommendation

### 4.1 Best signal (plain field read, no patch)

```cpp
// GOG 2.2.301 only. .data (bss part), valid for the process lifetime.
constexpr uintptr_t kWeaponFireHeld     = 0x009198C0; // command "weapon_fire"       (level)
constexpr uintptr_t kWeaponFireAutoHeld = 0x009198C1; // command "weapon_fire_auto"  (level)

bool triggerHeld = (*reinterpret_cast<volatile int8_t*>(kWeaponFireHeld) != 0);
```

Access path from the `Person*`: none needed, it is a global; the Person only supplies identity.
Apply it to the local Person only.

Validity checks (all optional, in order of value):

1. **Local player only**: `person == *(void**)0x00917AFC` (player object global, accessor
   `0x00417C70`, same test `Person::Simulate` uses at `0x59D463`). Remote Persons must not spin
   from your own key.
2. **Input allowed**: the byte is zeroed by the engine whenever `0x6217C0()` is false
   (menus/cutscene/input disabled), and held at 0 while `[0x9198F8] != 0` (poll blocked); no
   extra check required, the byte already reads 0 then.
3. **Weapon is the minigun**: carrier at `*(void**)(person+0x1A0)`, `selected = carrier[+0x30]`,
   `enabled = carrier[+0x34]`, test `(selected & enabled & (1<<i))` and `weapon = carrier[+0x18+i*4]`,
   `weapon->class = *(void**)(weapon+8)`, then your minigun identification (class sig at
   `class+0xC`, ODF name). This is exactly the set `FireSelected` fires, so it is the right
   "would fire if trigger held" mask.
4. Optional strictness: `*(int*)(person+0x298) == 0` (`0x4AEF30`; nonzero suppresses the fire
   branch in `UserProcess`; meaning of the field UNKNOWN, probably an "inactive/ejected" state).
5. Out of ammo / reloading: the key byte stays set regardless, so the barrel spins "even without a
   shot leaving" for free (the nice-to-have). If you ever need "a shot actually leaves", that is
   inside `Weapon::Fire` (UNKNOWN).

`weapon_fire_auto` (`0x9198C1`) is a second, mouse-button-style binding that only fires when all
selected weapons have class byte `+0x79` set and a target is locked; for a "trigger pulled" spin
use `0x9198C0` alone, or `0x9198C0 | 0x9198C1` if auto-fire weapons should also spin on that bind.

### 4.2 Why not the alternatives

* `carrier+0x3C`: does not exist (section 1).
* `Person+0x230` controls block: contains jump/turbo/eject/deploy/abandon/cloak and axes, no fire.
* Carrier selected/enabled masks: selection state only, say nothing about the trigger.
* Hooking `Carrier::FireSelected` (`0x511FC0`) / `Weapon::Fire`: works (fires per tick while held,
  skipped if `obj+0x298 != 0`) but needs a patch and cannot see "held while no ammo" semantics
  differently from the key byte; the key byte is strictly simpler.

### 4.3 Cheap live proof

In the `Person::Simulate` hook, for the local Person only, log on change (not per tick):

```
fire=%d auto=%d sel=%02X en=%02X  (bytes 0x9198C0, 0x9198C1; carrier+0x30, carrier+0x34)
```

Expected while the tester: idle -> `fire=0 auto=0`; hold LMB/fire key -> `fire=1` (or any
nonzero) every tick for the whole hold, including when the weapon is empty or reloading; release ->
`fire=0` next tick. `auto` should only flicker on the auto-fire bind. Also sanity check
`jump` the same way (`*(int8_t*)0x9198CB`, equals `*(int*)(*(char**)(person+0x230)+0xD8)`) to
prove the sampling point is aligned with the controls block.

### 4.4 Risks and caveats

* **Version-locked absolute address** (`.data`): add to `exu.json` as a data address with a
  code-pattern anchor. Unique anchor (verified unique in `.text`, 1 hit at `0x60A93D`):
  `0F B6 D0 85 D2 0F 85 B6 02 00 00 0F BE 05 C0 98 91 00 85 C0 74 0C` with the `C0 98 91 00` operand
  being the address (and `0F BE 05 C1 98 91 00` at `0x60A984` for the auto byte). Resolve the VA
  from the operand instead of hard-coding.
* Sampling lag: INFERRED that the poll/process order relative to `Person::Simulate` can put the
  byte up to one tick behind; irrelevant for a spin-up animation.
* The byte is whatever the bound input returned OR-ed together (compared with `movsx` + `test` by
  the engine); treat as `!= 0`, not `== 1`.
* Multiplayer: `UserProcess` runs for the local player only; remote Persons expose no trigger in
  their packet (section 2.5), so a remote-minigun spin must come from another cue (muzzle effect,
  weapon state) and is out of scope here.

---

## 5. Address index

| VA | what |
| --- | --- |
| `0x0059D340` | `Person::Simulate` (no fire logic; calls vslot `+0x98` `0x5A12A0` hardpoint re-pose) |
| `0x005A1E80` | Person net-state encode (vtable `+0xE8`), no fire bit |
| `0x00417CA0` | `GameObject::GetCarrier` (`+0x1A0`) |
| `0x00417C70` | player object accessor (`[0x917AFC]`) |
| `0x00417F60 / 0x417F90 / 0x417FB0` | Carrier GetWeapon / GetSelected / GetEnabled |
| `0x004D9800` | Carrier ctor, object size `0x3C` (new sites `0x4DA834`, `0x6127FC`) |
| `0x00511FC0` | `Carrier::FireSelected` (selected & enabled slots -> `weapon->vtable[2]`) |
| `0x0060A480` | `UserProcess` Execute (player input -> controls block + fire) |
| `0x006210A0` | InputPoll (clears then ORs command state bytes) |
| `0x008EB1E8` | command table (0x6F x 0x20 bytes: name, state byte ptr, type) |
| `0x0091989C..0x009198D3` | input state block (0x38 bytes) |
| `0x009198C0 / 0x009198C1` | `weapon_fire` / `weapon_fire_auto` held bytes |
| `0x009198CB` | `jump` held byte (feeds `Person+0x230+0xD8`) |
| `0x006217C0` | "input allowed" predicate |
| `0x009198F8` | input-blocked flag (poll skipped when nonzero) |

Scratch scripts (not in repo): `...\scratchpad\re2\{lib,idx,q,calls,xref,ctx}.py` (pefile +
capstone, skipdata mode required for whole-`.text` scans).
