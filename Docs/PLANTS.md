# Plants (`exu.SetPlant`, `[PlantClass]`)

Trees and shrubs are usually built as buildings, so they bump, chip and die like buildings. A plant keeps its normal collision (units still bump into it and it still blocks) but stops reading as a building.

## ODF keys

```ini
[PlantClass]
isPlant = 1                ; required (default 0)
hitExplosion = "xpltree"   ; explosion when ordnance hits it; default = the ordnance's own ground hit; "none" = no effect
explosionName = "xpltree2" ; death explosion; default = [GameObjectClass] explosionName
dropScrap = 0              ; 1 = drop the ODF scrap on death (single player only; default 0)
```

Keys are read through the engine's ParameterDB and are case-insensitive. Explosion names are at most 8 characters. A named explosion that cannot be found falls back to the default.

## Lua

| Function | Result |
|---|---|
| `exu.SetPlant(odf, enabled)` | `true` makes the ODF a plant (the other `[PlantClass]` keys still apply, with their defaults), `false` forces not-a-plant, `nil` removes the override. Overrides beat `isPlant`. `odf` may carry `.odf` and is case-insensitive. Mission-scoped and local to each machine. |
| `exu.IsPlant(odf)` | boolean, override first, then the ODF |

## What changes for a plant

- The collision bump sound is not played.
- Ordnance hits play the plant hit explosion instead of the building hit explosion, and throw no geometry chips.
- On death it plays the plant explosion and is removed. Skipped: the Betty "building lost" voice, ground shake, glare, the generic building/vehicle explosion, particles, fireballs and model fragmentation. With `dropScrap`, the scrap field is made as for a building.

## Hooks

Four entry detours (`Plant.*` in `exu.json`; design and addresses in `Docs/Research/PLANT_COLLISION_RE_20261013.md`): `ClsnSounds`, `Ordnance::Hit`, `ChunkEffect::CreateChunklet` and `Building::Explode`. They install together or not at all, fail closed on an unqualified build, and are removed with the Lua state. While a plant is hit, `Ordnance::Hit` swaps the ordnance class's building hit explosion for the plant's and restores it before returning, because ordnance classes are shared. EXU's `BulletHit` callback sits inside `Ordnance::Hit` after the stolen bytes and still fires.

The ODF file and key plumbing is shared with `exu.pathing` (same ParameterDB entries in `exu.json`).
