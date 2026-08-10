# Radar offset and mission lighting — parked, unresolved

Status as of 2026-08-09. Two bugs reproduce on a **second and later mission
load in one process** (`misn04 -> misn03 -> misn04`, no restart). Both are
still open. The changes on this branch did **not** fix either symptom; read
this before trusting the commit messages on `63a6494`.

## Symptoms

**Radar.** The radar background sprite and the wireframe grid are offset from
each other. They scale together correctly on the first mission of a process
and diverge on every load after that. Confirmed at both small (0.6) and large
(2.00) `RadarSizeScale`, so it is an offset/anchoring failure, not a
magnitude failure. An earlier reading of this as "the scale compounds per
load" was wrong.

**Lighting.** Distant terrain/fog develops a bright glow that is absent on the
first load and worse on later loads.

## What was ruled out

- **Not OpenShim.** The radar breakage reproduces with the `[Terrain]` section
  removed entirely and zero terrain records in the log.
- **Not an unhooked `RefreshLayout` caller.** The decompiled corpus shows
  exactly two callers of `0x00492EC0` — `FUN_00493250` and `FUN_00493EC0` —
  and `InstallRefreshLayoutCallSiteHooks` patches both. The concentric
  correction *is* running on the second load; one of its inputs must differ.
- **Not a stale `sceneManager`.** Redux creates the SceneManager once
  (`FUN_00664110`) and never destroys it, and the runtime log shows the
  pointer unchanged (`188244A0`) across three missions. The "cached Ogre
  pointer goes stale" model that motivated the lighting change does not hold
  for the scene manager. Whether `terrainMasterLight` is recreated per
  mission is still unverified.

## Why the changes on this branch do not fix it

`Patch::ResetOgreInitialization()` was added to `Lua::Init()` on the
assumption that it runs once per mission. **It does not.** A three-mission
session logs `exu: luaopen_exu called` and `exu: Init starting` exactly once
each, while `PersistentConfig.lua` loads four times — the mission Lua state
reloads, the exu library does not. The reset call is effectively dead, and
the identity-keyed rebind it backs up has nothing to detect if the pointers
never move.

Net effect: `fogResetPatch.Reload()` still runs only once per process, same
as the `static bool done` it replaced. The latch moved; the behaviour did
not.

The radar change (`ResolveStockProjectionBase`) is kept because deriving the
stock base from `currentBase / currentScale` is idempotent and strictly
better than snapshotting a mutable global — but it addresses accumulation,
which is **not** the reported symptom, so it fixes nothing observable.

## Next step when this is picked up again

Instrument `RefreshLayoutConcentric` to log, per invocation: requested scale,
current and derived projection base, and `radarLeft` / `radarBottom` /
`cockpitWireframeCenterX` / `cockpitWireframeCenterY` after **both** the
reference pass and the real pass, plus the `screenHeight` derived from
`cam->Orig_y`. One `misn04 -> misn03` run then reduces this to a diff of two
number sets and identifies which input diverges on the second load.

For the lighting, first establish whether `terrainMasterLight` actually
changes across a mission load. If it does not, the bug is not staleness and
the fog reset patch needs a genuine per-mission trigger — EXU has none today
(`OpenShimBridge.h` exposes only `HasExport`), so one would have to come from
OpenShim's mission seam (`SetRunning` / `FUN_00434170` leaving `RUN_STARTED`,
which is proven to fire once per mission).

## Reproduction

Launch, play `misn04`, exit to main menu, load `misn03`, exit to main menu,
load `misn04` — all in one process. Radar offset is visible from the second
load; glow builds from the second load.
