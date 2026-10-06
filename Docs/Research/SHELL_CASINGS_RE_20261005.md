# Shell casings: engine facts (2026-10-05)

These are the facts `src/Game/ShellCasings.cpp` relies on, for the GOG Redux 2.2.301 executable. Struct layouts come from the leaked PDB, a different build. Each offset below was confirmed against GOG disassembly, apart from the one marked "PDB only".

## Per-frame driver

- **The hook.** `Ogre::Root::addFrameListener` with an EXU object whose vtable matches `Ogre::FrameListener` 1.10. That class has no base, and its vtable order is `frameStarted`, `frameRenderingQueued`, `frameEnded`, then the destructor.
- **Why the listener fires.** Redux renders through `Root::renderOneFrame`, which fires frame listeners (OpenShim `nonrender_cpu_attribution_phase2_20260823.md`). The exe itself imports `addFrameListener`, as does `OgreTheoraVideoManager`.
- **Return true.** `frameStarted` must return true, because a false aborts the frame.
- **Removal is deferred.** `removeFrameListener` only queues the pointer. Ogre erases it at the start of the next frame without calling it, so the DLL may unload straight after `ShellCasings::Shutdown`.

## Time and pause (`exu.json` Time.*)

- **`0x02CC1B2C` fTime.** A float, in seconds; this is what Lua `GetTime` returns. `SetLoopTimes` (`0x00822AE0`) advances it by the clamped frame step.
- **`0x02CC1B1C` bPaused.** An int. `Pause` (`0x00822A70`) sets it, but only in singleplayer, and Resume clears it.
- **How casings use them.** Casings step by the Ogre frame time (smooth), but only on frames where fTime advanced and bPaused is 0, so they freeze with the game. A jump of more than 1 s (a load or restart) counts as a stall.
- **Net games.** Pause does not stop the simulation, so the casings keep moving, as everything else does.

## Terrain (`exu.json` Terrain.HeightAt)

- **Signature.** `0x007855E0 double __cdecl(double x, double z)`, taking simulation x,z: OBJ76 posit, as Lua `GetPosition` returns.
- **How it computes.** It is a bilinear grid lookup and returns a float in st0.
- **Normals.** The normal is 4 samples, 0.3 m apart. `0x00785730` (`TerrainHeightAndNormal(x, z, float* h, VECTOR_3D* n)`) exists, but it is not used because it is not catalogued.

## Object collision shape

- **Arena layout.** Each GameObject arena slot is 0x400 bytes, with 0x1000 slots at `GameObject.GetObj_base`. The serial is at +0x15C, and a serial of 0 means the slot is free. The handle is `(index << 20) | serial`.
- **Slot fields:**
  - +0xF0 tagENTITY*
  - +0xF4 OBJ76*
  - +0xF8 GameObjectClass*
  - +0x108 float pos
- **Bounding sphere.** `GameObject::GetSphere` is `ent ? &ent->bSphere : &obj->bSphere`, which puts the radius at `ent+0x14` (a float, in metres). GOG `0x004DB100` builds `ent+0x44..0x58` as pos ± `[ent+0x14]`. The OBJ76 fallback (+0xD4) is PDB only and is not used.
- **Class model box.** `GameObjectClass+0x10C` is a BBOX: min floats, then max floats up to +0x120, in object-local axes.
  - It is valid once `class+0x130` (the class sphere radius) is > 0.
  - GOG accessor `0x0049C560` lazily calls `0x004E17A0` when it is not > 0.
  - EXU never calls that function; it only reads a box that is already built.
- **Orientation.** The box is placed with the OBJ76 matrix at +0x20 (right, up and front as floats) and posit at +0x48 (doubles).
- **Fallback.** Without a valid box, EXU uses the entity sphere.

Scanning the arena costs 4096 serial reads. It runs at most 4 times a second, and only while a casing is flying or rests on an object. Up to 24 of the nearest objects plus each owner are kept, and their transforms are re-read every frame.

## Render space

Casing nodes hang under one named child of the root scene node (`__exu_casing_root_<tick>`).

- **Conversion.** Positions and orientations go through `OgreRenderSpace` (Z mirrored, origin offset).
- **The mesh.** Because of the mirror, the mesh's local +Z is the casing's simulation-local −Z. `casing.mesh` therefore puts the base and rim at +Z, so the base faces the breech when `transform` is the shot matrix.
- **Stale scenes.** If the named parent disappears (cleared scene) or the scene manager changes, every casing is forgotten without any call into Ogre.

## Limitations

- The casing is a sphere of `radius`. A casing stood on end penetrates for the ~0.1 s it takes to tip flat.
- Obstacles are a box or a sphere. There is no mesh-accurate hull, so concave shapes (gun barrels, hangar doors) are filled in.
- Obstacle velocity is estimated from the change in position between frames.
- The class box is assumed to be in the object's sim-local axes. The first six obstacles are logged as `[EXU::Casing] obstacle ...` to check this in game.
