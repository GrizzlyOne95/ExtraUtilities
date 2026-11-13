# Plant collision RE (GOG 2.2.301), 2026-10-13

Goal: let ODF-flagged foliage (trees, shrubs) stop reading as buildings. They
keep normal collision, but lose the building bump sound, the building hit
explosion and chips when shot, and the building death sequence (generic
building explosion, ground shake, fireballs, model fragmentation, scrap), which
is replaced by a plant explosion. The first section records the collision path
(not hooked; kept for a possible pass-through mode); the "Sounds and effects" section is
what the feature uses.

Evidence: the 1.5 build 15227 clean-PDB decompile (names) mapped onto the GOG
2.2.301 exe by vtable slots, call shape and disassembly (capstone over the
shipping `battlezone98redux.exe`, sha256 `8d71f56c1314e69a...`). The leaked Redux PDB
RVAs do not match this build and were not used.

## How a collision is resolved

1.5 names, GOG addresses:

| 1.5 | GOG 2.2.301 | Notes |
|---|---|---|
| `CheckPair(tagENTITY*, tagENTITY*)` | `0x00443060` | Bbox + swept-sphere test; on a hit calls `ClassRegisterClsn` and plays `ClsnSounds` only if that returns non-zero. Sole caller of `ClassRegisterClsn`. |
| `ClassRegisterClsn(_OBJ76* a, _OBJ76* b, CLSN_INFO* ca, CLSN_INFO* cb)` | `0x0062C000` | `int __cdecl`. Calls the per-class handler `funk[ClassIDtoIndex(a->class_id)].reg_clsn_fn(a, b, ca, cb)` and then the same for `(b, a, cb, ca)`; returns 1 if either returned non-zero. Table column at `0x008EC7C4`, stride `0x34`. Entry `55 8B EC 83 EC 08 C7 45 F8 00 00 00 00`; the first 6 bytes are three whole instructions with no relative operands. `ret` at `0x0062C0AB`. |
| `GameObject_RegCollision` | `0x004DF970` | The table entry for game-object classes: `obj->gameObj` (`_OBJ76+0x8C`) then virtual `+0x48` (`RegCollision(other, ci, ci2)`) on the primary vtable. |
| `Building::RegCollision` | `0x0047EDF0` | Building primary vtable `0x00876630` slot 18. Copies the other object's EULER and calls `Damage_Resolve`. |
| `Craft::RegCollision` | `0x004A88B0` | Craft vtable `0x008781EC` slot 18. Returns false (no response) for powerups (class id 3) and dead people (class id 4 with flag `0x200`); otherwise copies `CLSN_INFO` (0x1E dwords) into `vhcl+0x4C`, which drives the bounce in `Craft::DoCollision`, then calls `Damage_Resolve`. |
| `GameObject::RegCollision` | `0x004D97F0` | Default: returns 1. |
| `Damage_Resolve` | `0x008245F0` | Ram damage for both objects. |

The whole collision response (bounce and ram damage) lives inside the two
`reg_clsn_fn` calls. Skipping both, i.e. returning 0 from `ClassRegisterClsn`
for a pair, makes the objects pass through each other with no sound, bounce or
damage. That is the plant hook.

## Object layout used

- `_OBJ76+0x14` flags (`0x200` = dying), `+0x84` class id, `+0x8C` owning `GameObject*` (matches `BZR::OBJ76::owner`).
- `GameObject+0x18` secondary (DistributedObject) vtable; slot 0 returns the `GameObjectClass*`; slot 14 (`+0x38`) is `DamageAlloc(DAMAGE*, VECTOR_3D*)` (`Craft::DamageAlloc` `0x004AA630`, `Building::DamageAlloc` `0x0047EF10`).
- `GameObjectClass+0x30` ODF base name, at most 8 characters, not always terminated: this is what Lua `GetOdf` (`0x004FFFD0`) returns.
- `GameObject+0xF4` `_OBJ76*`, `+0x114` `EULER`, so `+0x12C` is the velocity vector (Craft::RegCollision reads it there).

## Engine helpers

| Function | GOG address | Signature (verified from disassembly) |
|---|---|---|
| Lua `Damage` native | `0x00503600` | wraps the next row |
| `DamageHandle` | `0x005CB1C0` | `void __cdecl(handle h, float amount)`: fills a static DAMAGE (type 1, value = amount, no source) and calls `DamageAlloc` through the secondary vtable. Scripting path; death, explosion and net handling are the engine's. |
| `HandleToGameObject` | `0x00462630` | `GameObject* __cdecl(handle)` |
| `ParameterDB::ParameterDB` | `0x00589430` | `ParameterDB* __thiscall(ParameterDB* this /*4 bytes*/, const char* fileName /*"name.odf"*/)`; used by Lua `OpenODF` (`0x00506310`) with placement new. |
| `ParameterDB::~ParameterDB` | `0x00589530` | `void __thiscall(ParameterDB*)`; the Lua `__gc` (`0x00506470`, via `0x005064A0`). |
| `ParameterDB::GetBool` | `0x00589920` | `bool __thiscall(ParameterDB*, uint32 sectionHash, uint32 keyHash, bool* out, bool defaultValue)`, `ret 0x10`; returns whether the key exists. |
| `ParameterDB::GetFloat` | `0x00589800` | `bool __thiscall(ParameterDB*, uint32 sectionHash, uint32 keyHash, float* out, float defaultValue)`, `ret 0x10`. |
| `HashString` | `0x00446460` | `uint32 __cdecl(const char* s, uint32 seed)`, lower-cases each byte (FNV-1a style); ODF lookups pass seed `0x811C9DC5` for both section and key. |

## Sounds and effects (second pass, same day)

Plants keep normal collision; what changes is the building feedback.

### Bump sound

`CheckPair` (`0x00443060`) calls `ClsnSounds` (`0x004436C0`, `void __cdecl(CLSN_INFO* a, CLSN_INFO* b)`) at `0x00443349` only when `ClassRegisterClsn` returned non-zero. `CLSN_INFO+4` is the `_OBJ76*`. It plays only when one side is the user's craft (`0x0062DFE0`) and picks the wav from the other side's class id (`+0x84`). Entry `55 8B EC 83 EC 2C` (6 bytes, three instructions).

### collisionExplosion (2026-11-13)

The `ClsnSounds` detour builds the plant's `collisionExplosion` in place of the bump sound.
- Speed gate: the stock function's own measure, the squared difference of the two `CLSN_INFO` velocities at `+0x60/+0x64/+0x68`, with nothing played at `<= 25` (1.5 decompile `functions/0047/004736fc_ClsnSounds.c:36-39` and `:50`; GOG disassembly reads the same `+0x60`).
- Position: the plant's own `_OBJ76` transform (`+0x20`), not `CLSN_INFO` `point` (`+0x54`, 1.5 PDB), whose space is UNVERIFIED.
- Build in the shipping exe (`0x004CB7B0`) takes a pooled or new `_OBJ76` and calls Explosion init (`FUN_004c9ab0`). When the class's float at `+0x50` (damage radius) is `> 0`, init walks the grid and calls each object's `DamageAlloc` (secondary vtable `+0x38`) at once (GOG decomp `FUN_004c9ab0-004c9ab0.c:118-155`). The pair loop (`FUN_00442e70`) caches its next-node pointers before each `CheckPair`, so a death that unlinks an object at once could leave a dangling pointer. Whether craft and person deaths are deferred is UNVERIFIED. EXU therefore builds only explosions whose damage radius is `<= 0`; others are ignored and logged. A damage-free Build leaves the pair lists alone: the `_OBJ76` pool allocator (`FUN_0062ccc0-0062ccc0.c:13-77`) and Explosion init call none of the functions that touch `0x0260cac4`/`0x0260d0f0` (only `FUN_00442980`, `00442ca0`, `00442d30`, `00442e10`, `00442e70`, `004435f0`, `00443650` reference them).
- The cooldown (1 s per plant `_OBJ76*`, `GetTickCount`) is EXU's; the stock code has none.

### Ordnance hit on a building

`Ordnance::Hit` (`0x00480750`, `void __thiscall(Ordnance*, GameObject* hit /*may be null*/, float* normal)`; EXU's `BulletHitHook` is at `0x00480771` inside it) applies the ordnance damage through the hit object's secondary vtable `+0x38` and then builds one hit explosion chosen by the hit `_OBJ76` class id:

- 1, 3, 4, 5, 6, 7 -> `OrdnanceClass+0x40` (`xplVehicle`)
- 2, 8, 10 (buildings) -> `OrdnanceClass+0x44` (`xplBuilding`), built at `0x00480916`
- other / no object -> `OrdnanceClass+0x3C` (`xplGround`)

Each is skipped when the pointer is null. The hit sound is the chosen explosion's own sound; there is no separate building hit sound. `OrdnanceClass` is `Ordnance+0x0C`. Entry `55 8B EC 81 EC D0 00 00 00` (9 bytes).

`Building::DamageAlloc` (`0x0047EF10`) throws `min((damage - 16) / 32, 10)` geometry chips when the damage source `_OBJ76` has class id `0x33` (ordnance), via `ChunkEffect::CreateChunklet` (`0x004927D0`, `__thiscall(ChunkEffect* /*0x00950190*/, a, b, bool)`, `ret 0x0C`) at `0x0047F228`. Entry `55 8B EC 81 EC B8 00 00 00` (9 bytes). EXU hooks the pool-full compare at `0x004927EB` (`81 B8 28 80 00 00 00 04 00 00`, 10 bytes, followed by `jge 0x00492A84`, the function's own exit) rather than this entry, because OpenShim's chunk create trace detours the entry. The same function is the only per-hit chip source in this path; at health < 0 it sets `_OBJ76` flags `0x01000200`, and the building explodes later.

### Building death

`Building::Explode` (`0x0047F330`, `void __thiscall(this = GameObject+0x18)`, secondary vtable slot 5) does, in order:

1. `obj_set_flag(obj, 0x200)` (`0x0062DE40`, cdecl `(OBJ76*, uint32)`); `obj` is `this+0xDC`.
2. Betty "building lost" (`0x0047C2B0`, user team only).
3. Matrix: the `_OBJ76` transform (`obj+0x20`, 0x40 bytes, position as doubles at `+0x48/+0x50/+0x58`).
4. For radius > 5 m: ground shake `xplGround` (`0x00917A50`) when the class type (`objClass+0x1C`) is 2, then glare (`ColorFade 0x0097838C`, `0x0049B4C0`).
5. Explosion: the ODF `explosionName` (`objClass+0x144`, `objClass` = `this+0xE0`) if set, else by radius `xplBuilding` `0x00917A44` (> 5 m), `xplVehicle` `0x00917A48` (> 2 m), `xplSecondary` `0x00917A4C`.
6. `radius * 10` particles (`ParticleEffect 0x009C9278`, `0x0058B650`) and `radius` fireballs (`FireballEffect 0x009B5A60`, `0x004D10B0`).
7. `Remove_Light_Source(obj)` (`0x0068A2D0`, cdecl) when obj flags has `0x800`.
8. `ChunkEffect::FullFragmentObject` (`0x00492640`): the model shatters into geometry chunks.
9. Virtual `this->vtbl[+0x10]()` (`0x004DAE70` for GameObject): releases Ogre entities and removes the object. Must always run.
10. Scrap field (`0x005C65A0`) of `objClass+0x4C` scrap.

Entry `55 8B EC 81 EC 24 01 00 00` (9 bytes).

### Explosion helpers

- `ExplosionClass::Build` `0x004CB7B0`: `void __thiscall(ExplosionClass*, MAT_3D*, OBJ76* owner)`, `ret 8`.
- `ExplosionClass::Find` `0x004CB870`: `ExplosionClass* __cdecl(uint32 nameLo, uint32 nameHi)`; the 8-character name packed little-endian, zero padded (masked with `0x7F7F7F7F`). Returns the loaded class, or opens `<name>.odf` and builds it on demand; null (with a log line) when missing.
- `ParameterDB::Lookup` `0x00589620`: `const char* __thiscall(ParameterDB*, uint32 sectionHash, uint32 keyHash)`, the raw value used by `GetBool`/`GetFloat`/Lua `GetODFString`; null when the key is absent.

## Radar, hit marker and targeting (third pass, 2026-11-13)

Goal: a plant never shows on the cockpit radar or the map, never draws the
"recently shot" sprite, and can never be selected as a target, by the reticle,
or by a click in the overview. Ordnance hits, plant death, collision and AI
behaviour toward plants stay stock.

Citations: `G:` is the GOG ghidrecomp corpus
`Battlezone_Source/BZ1/Redux/openshim_re_corpus/bzr_gog_best_effort/ghidrecomp/results/bins/battlezone98redux.exe-6777ca/decomps/`;
`1.5:` is `Battlezone_Source/BZ1/1.5/functions/`. Every byte string below was
read from the shipping exe with capstone and matches `exu.json` (`qualify_bzr_build.py --catalog`).

### Hit marker

- `GameObject::SetDamageFlags` `0x004DC130`, `void __thiscall(GameObject*, Damage*)`, `ret 4` at `0x004DC2B9`. It stores the current time in `playerShot` (`+0x1D8`) when the damage carries flag bit 4 (`G:FUN_004dc130-004dc130.c:31-32`; `1.5:0049/00496da9_GameObject_SetDamageFlags.c:37`), then calls the Betty attack warning (`G:...:41`; `1.5:...:42`). `this` is the GameObject base: `Building::DamageAlloc` does `sub ecx,0x18` before the call (`0x0047EF9C`). Entry `55 8B EC 83 EC 0C` (6 bytes, three instructions).
- `SelectionDisplay` `0x00497AA0` draws the sprite while `now < playerShot + 2.0` (`G:FUN_00497aa0-00497aa0.c:145-147`; `1.5:004c/004c1db7_SelectionDisplay_Render.c:129`). The getter is `FUN_00497890`.
- EXU detours the entry, calls the original, and for a plant writes back the old `playerShot` when it changed. The Betty call and the other flag work still run.
- Other `playerShot` readers (through `FUN_00497890`): `G:FUN_005c4aa0-005c4aa0.c:59` and `G:FUN_00605560-00605560.c:22,41`. They are not identified (UNVERIFIED: they look like AI code). For a plant they now see "never shot by the player".
- OpenShim: its `GameObjectSetDamageFlags` row only validates the "Damage Reveal Probe", which rewrites the callers' rel32 (`0x0047EF9F`, `0x004AA677`, `0x005A0CE1`, `0x005AA371`), not this entry. Either load order works.

### Cockpit radar

`CockpitRadar::Render` `0x00493EC0` walks the object list (`1.5:004c/004c05b7_CockpitRadar_Render.c:181-186`; `G:FUN_00493ec0-00493ec0.c:290-292`). `0x00494569 mov edx,[eax]` loads the GameObject*, then:

- `0x0049456B` `89 95 AC FE FF FF` (`mov [ebp-0x154],edx`, 6 bytes) is replaced by `call [ptr]` to `RadarGatherStub`. The illumination test follows at `0x0049457F` and the own-team fallback after it, so filtering here covers both.
- Continue label `0x00494521` (`6A 00 8D 95 30 FD FF FF 52 ...`), the target of the stock skips at `0x004945AA` and `0x00494802`. The frame is ebp-relative and nothing is pushed at either point, so the stub returns there for a plant.
- EXU's existing radar sites are `0x0049405B`-`0x0049446F`; no overlap.

### Map radar

`MapRadar::Render` `0x0050D6F0` builds its own object list (`1.5:004d/004d2395_MapRadar_Render.c:226-227`; `G:FUN_0050d6f0-0050d6f0.c:331-332`).

- `0x0050E2C8` `89 95 B0 FE FF FF` (`mov [ebp-0x150],edx`) -> `MapRadarGatherStub`.
- Continue label `0x0050E27E` (`6A 00 8D 95 90 FD FF FF 52 ...`), the target of `0x0050E3AA` and `0x0050E435`.
- In 1.5 a map click only moves the overview position and picks no object (`1.5:004d/004d2395_MapRadar_Render.c:113-123`). The GOG map click was only skimmed: UNVERIFIED.

### Overview click

`OverView::ClickedOn` `0x005879E0` (1.5 `004d3676`), called from `0x0058821A` and `0x005884F4`, returns the clicked object (`1.5:004d/004d3676_OverView_ClickedOn.c:46-47`; `G:FUN_005879e0-005879e0.c:38-39`).

- `0x00587A60` `89 55 EC 8B 4D EC` (`mov [ebp-0x14],edx; mov ecx,[ebp-0x14]`) -> `ClickedOnStub`, which also redoes the `mov ecx`.
- Continue label `0x00587A25` (`6A 00 8D 55 B4 52 8D 4D E4 E8`), the target of `0x00587A82` and `0x00587A99`.
- `ControlPanel::SelectRect` (drag select) was not checked: UNVERIFIED whether a drag box can include a plant.

### Target (T key) and target validation

`Targeting::Simulate` `0x005DEAE0` (1.5 `004dc119`): with the target key it calls `FindTarget` when there is no target, with the cycle key it walks unit slots `0x23`-`0x2C` (own units, not plants), otherwise it calls `ValidateTarget` on the current target; the result goes to `GameObject::SetTarget` `0x0049F450` (`1.5:004d/004dc119_Targeting_Simulate.c:31,63,69`; `G:FUN_005deae0-005deae0.c:16-19,56,61`).

- `Targeting::FindTarget` `0x005DEC80` (1.5 `004dbe3d`) rejects a candidate whose `obj->flags & 0x201` is set (`1.5:004d/004dbe3d_Targeting_FindTarget.c:69`; `G:FUN_005dec80-005dec80.c:67`). The flags come from `GameObject::GetObjFlags` `0x0045C4B0` (`this->obj(+0xF4)->flags(+0x14)`), called at `0x005DEDD4` `E8 D7 D6 E7 FF`, then `25 01 02 00 00 74 02`.
- `Targeting::ValidateTarget` `0x005DF0E0` (1.5 `004dc098`) returns null for the same test (`1.5:004d/004dc098_Targeting_ValidateTarget.c:21`; `G:FUN_005df0e0-005df0e0.c:17-18`). Call at `0x005DF0F9` `E8 B2 D3 E7 FF`, then `25 01 02 00 00 74 07`.
- EXU retargets both calls to `TargetFlagsFilter`, which returns the real flags plus `0x200` for a plant. Only these two tests see it. A plant is never found, and a plant target set any other way (for example by script) is cleared on the next `ValidateTarget` frame.

### Reticle

`Reticle::FindReticleObject` `0x005BC640` (1.5 `004d95fa`), called from `0x005BA974` and `0x005BA9B1`, skips a candidate when `isCargo` (`+0x9C`, `GameObject::IsCargo` `0x0045BBC0`) is set (`1.5:004d/004d95fa_Reticle_FindReticleObject.c:61,94`; `G:FUN_005bc640-005bc640.c:70,110`). Its result is `Reticle::selectObj`, which drives the name/health readout and `ControlPanel::SetCommand` (`1.5:004d/004d997d_Reticle_Simulate.c:101-121`).

- Not hooked (revised 2026-11-13): filtering `FindReticleObject` also removed the plant from `selectObj`, which lobbers and OpenShim convergence aim at. Instead the command-button fill is filtered: `Reticle::Simulate` calls `ControlPanel::SetCommand(startCommand, selectObj)` (`0x004A7230`, thiscall `ret 8`, `this` = controlPanel `0x00978E20`) at `0x005BAA7D` `E8 AE C7 EE FF`. That call is retargeted to `ReticleSetCommandFilter`, which for a plant calls the position overload `0x004A71E0` with `CMD_NONE` (0) and a null position, the same state as aiming at the sky (on bare ground stock would instead fill a ground command; a plant gives neither) (`1.5:004d/004d997d_Reticle_Simulate.c:114-119`). The button (+0x111C) is what `ControlPanel::Render` draws the name/health from and what the menu key issues (`1.5:004c/004c4998_ControlPanel_Render.c:194-215,397`; `1.5:004c/004c475e_ControlPanel_FillCmdButton.c`). `Reticle::startCommand` (+0xB0) keeps the plant's command, but its only reader, `Reticle::Submit`, uses it only when `groundPos` is set, and that is null whenever `selectObj` is (`1.5:004d/004d89d3_Reticle_Submit.c:368`).
- OpenShim patches range literals in this function at `0x005BC6CF`, `0x005BC6F6`, `0x005BC728` and `0x005BC8BD`, and `0x005BCCB3` in `FindGroundPos`. None overlaps. A scan of all `patches.json` rows within 16 bytes of every new site found only the `SetDamageFlags` validation row above.

### Weapon lock (NOT DONE)

- `Launcher`/`RadarLauncher` lock onto the carrier's `targetHandle` (`+0x21C`), so the target filter covers them.
- `InfoDisplay::FindSightObject` `0x004F4010` (called from `InfoDisplay::Render` `0x004F31D0` at `0x004F321D`-`0x004F3234` when `objectShow` +0x11C or profile option `[0x94672C]+0x30 & 0x80` is set) is a separate name/health picker. Its candidate test `call 0x0045C4B0; and eax,0x600` at `0x004F416E` is retargeted to the same dying filter (`G:FUN_004f4010`; 1.5 `InfoDisplay_FindSightObject.c`; bytes confirmed in the GOG exe 2026-11-13).
- `Reticle::selectObj` (global `0x025CE6D0`+0xAC) is also read by `ObjectLobber::Control` `0x00581EF0` (1.5 `0x005326C0`) for the user's aim point and by OpenShim `weapon_convergence_hooks.cpp` `TryGetReticleConvergenceRange`. Plants stay in `selectObj`, so lobbers and convergence aim at them (user decision 2026-11-13).
- Image-seeking launchers/missiles (`FUN_004f2210`, `FUN_005e1ea0`, `FUN_005e2810`) run their own range search with a `flags & 0x400` test and are shared with AI. Filtering only the player's launcher needs owner-aware RE that was not done; a plant may still be locked by these weapons. UNVERIFIED which ODF classes use them in Redux.
- `UserPref_automaticAim` has no callers in 1.5; its Redux use is UNVERIFIED.

### Classification cost

The HUD sites classify every listed object every frame. `ClassifyGameObject` caches `GameObjectClass* -> Entry*` (`g_classEntries`), so after the first object of a class the cost is one virtual call and one hash lookup. The verdict is re-read from the entry, so `exu.SetPlant` applies at once. Both maps are cleared together with the mission state.

## Open points

- Whether the AI path planner avoids buildings independently of collision
  (AI units may still route round plants). Not needed for the hook.
- Multiplayer: the hook is deterministic given the same ODFs and EXU on every
  client; `DamageAlloc` already ignores remote objects.
