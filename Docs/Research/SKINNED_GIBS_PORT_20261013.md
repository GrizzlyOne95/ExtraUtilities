# Skinned gibs: port from OpenShim (2026-10-13)

`exu.gibs` brings OpenShim's SkinnedGibs (`src/patches/skinned_gibs.cpp`, OpenShim `72134279c`) to installs that run EXU without OpenShim. When a person (RTTI `.?AVPerson@@`: pilots, soldiers, zombies, and every ISDF Chronicles wildlife ODF, which are all `classLabel = "person"`) is fully fragmented, EXU cuts rigid per-bone pieces from the dead body's own skinned mesh, poses each one where its limb was on the frame of death, and simulates them with the legacy chunk launch math and a flesh ground response.

## What is reused and what is not

| Part | OpenShim | EXU |
|---|---|---|
| Mesh split, caps, flesh textures, disk cache | `native_chunk_mesh`, `native_chunk_cache`, `gib_flesh_texture` | Vendored verbatim under `src/Gibs/NativeChunks/` (namespace `BZROpenShim::NativeChunks`; see its README) |
| Death hook | `ChunkEffect::FullFragmentObject` detour from the shared chunk hook set | Own `EntryDetour32` on the same function, catalogued in `exu.json` |
| Per-frame step | `TickSkinnedGibs` after `ChunkEffect::Simulate` | `Ogre::FrameListener`, as `exu.casing` (steps by the real frame time on frames where `fTime` advanced, zero while `bPaused`) |
| Drawing | Manual submit from the `LegacyWorld::updateRenderQueue` vtable hook | Scene nodes under a named child of the root node, as `exu.casing` and `exu.flash`, which draw without a submit hook |
| Legacy body chunks | Proxy renderer told not to draw them | Nothing to do: without OpenShim the engine never draws legacy chunks |
| Authored `gibs.txt` payloads | Supported | Not ported (no pack ships one); runtime split only |
| Config | `[General] SkinnedGibs` via env | Lua: `exu.gibs.Enable`, `exu.gibs.Configure` |

## Hook

`ChunkEffect::FullFragmentObject`, GOG 2.2.301 `0x00492640`, entry bytes `55 8B EC 83 EC 28 A1 00 70 8E 00 33 C5 89 45 FC` (OpenShim `scripts/patches.json` `ChunkEffectFullFragment`). `__thiscall(ChunkEffect* this, OBJ76* root, const float* velocity, uint8_t preserve)`, callee-clean; the detour is `__fastcall(this, edx, root, velocity, preserve)`. Instruction boundaries 1/3/6/11: the detour copies 6 bytes (`push ebp; mov ebp,esp; sub esp,28h`).

Only the outermost call (depth 0) captures; the original is re-entrant (fragmenting a tree can fragment children).

Order, as OpenShim:
1. Before the original: `Begin` — gate, Person RTTI, `root` must be the person's own `OBJ76` (`GameObject+0xF4 == root`, back-link `OBJ76+0x8C`), entity from the render bridge (`GameObject+0xF0`, then `+0x94` entity, `+0x98` node fallback), capture node derived pose and every bone's derived pose plus binding-pose inverse under one SEH frame, read `velocity`, prepare the model's payload (cached per mesh basename for the mission; first death of a model pays the split).
2. Call the original.
3. After: `End` — spawn the pieces from the capture, hide the body entity (`setVisible(false)`), drop the entity pointer.

The capture is POD; the entity is never touched after `End` returns.

## Coexistence with OpenShim

OpenShim installs its own detour on this entry lazily, from `ChunkEffect::Simulate`, guarded by the same entry bytes. An EXU detour there first would make OpenShim's guard fail and cost it every chunk feature, so **`exu.gibs` stands down whenever the OpenShim provider is live** (`OpenShimBridge::GetModule() != nullptr`), whatever OpenShim's own SkinnedGibs setting. `GetCapabilities().openShim` reports it; OpenShim's gibs are the same feature. `Enable(true)` then returns false and logs once.

## Payloads

- Source bytes: the dead entity's mesh (`Entity::getMesh`, `Resource::getName/getGroup`) and its skeleton (`NativeChunks::SkeletonName`), read with `ResourceGroupManager::openResource` into EXU-owned storage (OpenShim `native_chunk_runtime.cpp` `readResource`: Redux `DataStream` vtable slot 3 is `read`; release through the owning DLL's destructor and `StdAllocPolicy::deallocateBytes`, never the CRT).
- Cache: `<game dir>\exu\cache\` (beside OpenShim's `openshim\cache`) + `gibs/v6/<fnv1a64(mesh+skeleton) hex>/`, via `NativeChunks::ReadGibCache/WriteGibCache`. The hash keys the folder, so two mods reusing a basename never share pieces.
- Ogre: one resource group `ExuGibPayloads` with the cache root added recursively (`createResourceGroup`, `addResourceLocation(root, "FileSystem", group, recursive=true, readOnly=true)`, `initialiseResourceGroup`), registered once per process before the first entity is made. Piece resource names are `gibs/v6/<hash>/<piece>.mesh`.
- Flesh caps: the pieces reference `openshim_gib_flesh*` materials. Their scripts and TGAs (`NativeChunks::GibFleshMaterialScript`, `GibFleshZoneMaterialScript`, `GibFleshTextures`) are written to the cache root with OpenShim's rules: rewritten when the version header line is stale, left alone when the first line lacks the marker (the user's), stepped aside when a pack ships the same file name. Written before the group is initialised so the scripts parse. A missing material draws as BaseWhite.

## Spawn and simulation

Verbatim from OpenShim (`SpawnFromCapture`, `StepGib`, `GibTuning`, `GibRandom`): engine-free, so it lives in `src/Gibs/SkinnedGibCore.h` and is host-tested.

- Runtime pieces are bone-frame: world = node pose * (bone derived pose * piece pivot).
- Launch: per axis `Legacy() * 10 * force` + 5 up, plus the kick `(p - origin) * (2, 1, 2)` and the body velocity; spin `Legacy() * 5` per axis, halved above radius 0.3.
- Ground: restitution 0.15, friction 0.55, spin kept 0.5, settle below 0.6 m/s, contact lift `radius * 0.35`; rest `linger` s (default 8), sink 0.4 m over 2 s, hard lifetime 20 s.
- Gibs live in render space (their start pose is read from Ogre). Terrain is sampled in sim space through `OgreRenderSpace` (Z mirrored, map origin), the bug OpenShim fixed in `5449946a9`.

Threading (decided in phase 2): the death hook and the frame listener share the gib pool with no lock, and OpenShim's spawn-inside-the-hook is kept, because both run on the one game thread. Evidence: EXU starts no thread (`Docs/CODE_AUDIT_20260927.md`); `exu.casing` already creates Ogre entities from a Lua call and mutates the same pool from its frame listener; and OpenShim's sampled profile (`reverse_engineering/nonrender_cpu_attribution_phase2_20260823.md`) puts `Root::renderOneFrame` and the world simulation tick on the same main thread. That is not proven for `Person` death specifically, so it is checked at run time rather than assumed: `Enable(true)` records the calling (Lua/sim) thread, the first `frameStarted` records the listener's, and the hook records its own on every death. Any mismatch (listener vs Lua thread at the first frame, or hook vs Lua thread at a death) makes capture stand down and `GetCapabilities().reason` say so; the hook never spawns from a thread other than the one that enabled gibs. If a mismatch ever shows up in the log, the fix is to queue the capture from the hook and spawn it in the listener.

Files: `src/Gibs/SkinnedGibCore.h` (engine-free: math, launch, pose, `StepGib`, slot choice, frame step), `src/Gibs/GibPayloads.cpp` (payload split, cache, flesh materials, resource group), `src/Gibs/SkinnedGibs.cpp` (detour, capture, pool, listener, Lua), `src/Ogre/OgreGibRuntime.cpp` (the Ogre ABI).

Pool: up to `max` slots (default 160, 12..1024). A free slot with the same mesh is reused without a new entity; otherwise the entity is replaced on the slot's node; when full, the oldest settled gib (else the oldest) is recycled. A mesh Ogre refuses is not retried that mission. Nodes hang under `__exu_gib_root_<tick>`; if that node disappears or the scene manager changes, every slot is forgotten without calling Ogre (as `exu.casing`).

## Lua

```lua
exu.gibs.Enable(true)                        --> boolean active
exu.gibs.Configure{ max = 160, linger = 8, force = 1 }
exu.gibs.IsEnabled()                         --> boolean
exu.gibs.Clear()
exu.gibs.GetStats()       --> { live, settled, sinking, max, deaths, spawned, recycled, failed, models, forgotten }
exu.gibs.GetCapabilities() --> { gibs, openShim, hook, reason }
```

Off by default; a mod opts in per mission. `Shutdown` (mission end) removes the detour and the listener and forgets every slot.

## Not ported

- `gibs.txt` authored payloads, and OpenShim's render probe (trace-only).
- Blood on first impact: OpenShim leaves a TODO there too (no verified explosion entry point).
- Steam: the catalogue is GOG-only (`RuntimeGate`).
- The bone binding-pose inverse (`Bone::_getBindingPoseInverse*`): only authored payloads used it, so the capture reads each bone's derived pose only.

## Phase 2 notes

- The catalog entry `ChunkEffect.FullFragmentObject` pattern runs past the 16-byte entry (it matches 17 functions) to the first call, rel32 wildcarded, so qualification finds it uniquely. At run time all 16 entry bytes must still match before the detour is built (a foreign detour fails closed), and only the first 6 are stolen.
- "Stepped aside when a pack ships the same file name" means: another Ogre resource group already holds a resource of that file name (`ResourceGroupManager::resourceExistsInAnyGroup`), checked once, when the group is first created in the process. An existing `ExuGibPayloads` group (EXU reloads per mission; Ogre keeps the group) is reused as is.
- `Enable(false)` removes the hook only; gibs already shown finish their life and `Shutdown` destroys the rest. `Clear()` hides every live gib (the pooled objects are kept).
- Terrain is sampled in simulation space with `x + origin.x`, `-(z + origin.z)` and the height is returned as `h - origin.y`: the inverse of `OgreRenderSpace::SimPositionToRender`.

## To check in game

1. Scene-graph drawing: gibs appear without a submit hook (casings and flashes do).
2. The body is hidden on the frame the pieces appear, with no T-pose flash.
3. First-death split time for a wildlife mesh (the log prints `prepareMs`); the disk cache makes later missions free.
4. With OpenShim installed, `exu.gibs` reports `openShim = true` and OpenShim's gibs and chunks still work.
