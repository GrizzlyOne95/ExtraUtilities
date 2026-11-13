# Plants (`exu.SetPlant`, `[PlantClass]`)

Trees and shrubs are usually built as buildings, so they bump, chip and die like buildings. A plant keeps its normal collision (units still bump into it and it still blocks) but stops reading as a building.

## ODF keys

```ini
[PlantClass]
isPlant = 1                ; required (default 0)
hitExplosion = "xpltree"   ; explosion when ordnance hits it; default = the ordnance's own ground hit; "none" = no effect
explosionName = "xpltree2" ; death explosion; default = [GameObjectClass] explosionName
collisionExplosion = "xplbump" ; damage-free explosion when something bumps it at over 5 m/s, at most once per second per plant; default/"none" = nothing
dropScrap = 0              ; 1 = drop the ODF scrap on death (single player only; default 0)
```

Keys are read through the engine's ParameterDB and are case-insensitive. Explosion names are at most 8 characters. A named explosion that cannot be found falls back to the default.

## Lua

| Function | Result |
|---|---|
| `exu.SetPlant(odf, enabled)` | `true` makes the ODF a plant (the other `[PlantClass]` keys still apply, with their defaults), `false` forces not-a-plant, `nil` removes the override. Overrides beat `isPlant`. `odf` may carry `.odf` and is case-insensitive. Mission-scoped and local to each machine. |
| `exu.IsPlant(odf)` | boolean, override first, then the ODF |

## What changes for a plant

- The collision bump sound is not played. With `collisionExplosion`, that explosion (with its own sound and particles) is built at the plant's position instead, for any unit, AI included, that hits it at over 5 m/s relative speed; the same plant fires at most once per second. The explosion must have no damage (`damageRadius` 0 or absent); one with damage is ignored and logged, because it would run inside the collision loop. This is the BZ2 `hitByCarName` equivalent.
- Ordnance hits play the plant hit explosion instead of the building hit explosion, and throw no geometry chips.
- On death it plays the plant explosion and is removed. Skipped: the Betty "building lost" voice, ground shake, glare, the generic building/vehicle explosion, particles, fireballs and model fragmentation. With `dropScrap`, the scrap field is made as for a building.
- It is not shown on the cockpit radar or the map, and being shot never draws the "recently shot" marker.
- It cannot be targeted: the target key never picks it, an existing plant target (for example set by script) is cleared, aiming at it shows no name/health readout and gives no reticle command (units cannot be ordered onto it), and clicking it in the overview selects nothing.
- Not changed: ordnance still hits it, it still dies and collides, and AI behaviour toward it is stock. Image-seeking weapons that run their own lock search may still lock onto a plant (not covered; see the research note).
- It stays the reticle's aim object, so lobbed weapons (mortars) and OpenShim weapon convergence aim at it like any other object. That is the only player-facing use left.

## Hooks

Twelve hooks (`Plant.*` in `exu.json`; design and addresses in `Docs/Research/PLANT_COLLISION_RE_20261013.md`): entry detours on `ClsnSounds`, `Ordnance::Hit`, `Building::Explode` and `GameObject::SetDamageFlags`; the pool-full compare inside `ChunkEffect::CreateChunklet` (OpenShim's chunk trace detours that function's entry); the object store in the `CockpitRadar::Render`, `MapRadar::Render` and `OverView::ClickedOn` loops, which skip a plant to the loop's own continue label; and the candidate-test calls in `Targeting::FindTarget`/`ValidateTarget` and the info panel's `InfoDisplay::FindSightObject` (a plant reads as dying); and `Reticle::Simulate`'s `ControlPanel::SetCommand(cmd, obj)` call, which fills the command button as when aiming at the sky when the object is a plant. The HUD sites look plants up through a per-class cache. They install together or not at all, fail closed on an unqualified build, and are removed with the Lua state. While a plant is hit, `Ordnance::Hit` swaps the ordnance class's building hit explosion for the plant's and restores it before returning, because ordnance classes are shared. EXU's `BulletHit` callback sits inside `Ordnance::Hit` after the stolen bytes and still fires.

The ODF file and key plumbing is shared with `exu.pathing` (same ParameterDB entries in `exu.json`).
