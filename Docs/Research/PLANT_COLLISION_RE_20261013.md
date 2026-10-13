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

### Ordnance hit on a building

`Ordnance::Hit` (`0x00480750`, `void __thiscall(Ordnance*, GameObject* hit /*may be null*/, float* normal)`; EXU's `BulletHitHook` is at `0x00480771` inside it) applies the ordnance damage through the hit object's secondary vtable `+0x38` and then builds one hit explosion chosen by the hit `_OBJ76` class id:

- 1, 3, 4, 5, 6, 7 -> `OrdnanceClass+0x40` (`xplVehicle`)
- 2, 8, 10 (buildings) -> `OrdnanceClass+0x44` (`xplBuilding`), built at `0x00480916`
- other / no object -> `OrdnanceClass+0x3C` (`xplGround`)

Each is skipped when the pointer is null. The hit sound is the chosen explosion's own sound; there is no separate building hit sound. `OrdnanceClass` is `Ordnance+0x0C`. Entry `55 8B EC 81 EC D0 00 00 00` (9 bytes).

`Building::DamageAlloc` (`0x0047EF10`) throws `min((damage - 16) / 32, 10)` geometry chips when the damage source `_OBJ76` has class id `0x33` (ordnance), via `ChunkEffect::CreateChunklet` (`0x004927D0`, `__thiscall(ChunkEffect* /*0x00950190*/, a, b, bool)`, `ret 0x0C`) at `0x0047F228`. Entry `55 8B EC 81 EC B8 00 00 00` (9 bytes). The same function is the only per-hit chip source in this path; at health < 0 it sets `_OBJ76` flags `0x01000200`, and the building explodes later.

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

## Open points

- Whether the AI path planner avoids buildings independently of collision
  (AI units may still route round plants). Not needed for the hook.
- Multiplayer: the hook is deterministic given the same ODFs and EXU on every
  client; `DamageAlloc` already ignores remote objects.
