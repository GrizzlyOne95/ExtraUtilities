# BZR camera, producer, placement, identity and command utilities

These APIs implement the useful capabilities discussed in the EXU2 comparison
using BZR's own runtime. They do not import BZCC's loader, offsets, hooks, or Lua
runtime. Magnet weapon callbacks are excluded: BZR has no magnet weapons.

## Camera projection

```lua
local x, y, depth = exu.WorldToScreen(target)
local inFrustum = exu.IsPointInView(target)
```

`target` accepts a live object handle, a vector/`{x,y,z}` table, or a matrix
translation. Handles project the object origin, not its bounding-box centre.
Coordinates are render-target pixels, including viewport left/top offsets:
X increases rightward, Y downward. Depth is signed camera-forward distance in
world units. Points behind the camera have negative depth; off-screen positions
are not clamped. The eye plane has no stable projection and returns three nils.

Projection reads Ogre's actual view and canonical projection matrices, after
the shared simulation-to-render conversion. It handles perspective and
orthographic cameras without deriving FOV or guessing desktop dimensions.
Query after the camera has rendered a frame; camera state can lag simulation.

`IsPointInView` calls Ogre's point-frustum test, including near and far clipping.
It does **not** test occlusion or line of sight. An invalid handle, unavailable
camera/build, or nonfinite position returns nil. The runtime keeps no Ogre object
pointers between calls and requires a live mission view.

Native consumers can use `EXU_WorldToScreen` and `EXU_IsPointInView` from
`include/ExtraUtils.h`. Both return 1 for a successful query and 0 if unavailable.
They run on the engine thread.

## Buffered production events

```lua
if exu.SetBuildEventsEnabled(true) then
    local events, dropped, pending = exu.DrainBuildEvents(64)
    for _, event in ipairs(events) do
        if event.type == "completed" then
            print(event.odf, event.producer, event.built)
        end
    end
end
```

Events contain `type` (`queued`, `cancelled`, `completed`), `producer`, `built`,
`team`, `odf`, `producerType`, and `sequence`. ODF names are extensionless
basenames. The accepted native StartBuild operation defines `queued`: resources
have been committed, rather than a player merely highlighting a menu item.
The shared BZR Producer path covers factories/recyclers and construction rigs;
Armory has a separate completion override. Its completed event has no `built`
handle because native Armory completion returns null after launching the item.

Capture is local-authority only in multiplayer. Remote replicas are excluded;
events are not broadcast and peers must explicitly exchange any desired mission
decisions. Events are observations, not veto callbacks. Handles may become stale
before the Lua consumer drains them; validate before querying live objects.

The hook copies fixed POD records and executes no Lua, allocations, file I/O,
Ogre work, or networking. The 512-entry queue overwrites its oldest record on
overflow and reports cumulative `dropped` and current `pending` counts. Drains
are destructive and return up to 64 entries. Disabling clears the queue; Lua
state teardown disables capture, restores hooks, and frees trampolines even when
another consumer pins EXU. Reentrant enable/disable changes invalidate an old
capture generation. Native entry preimages and optional feature anchors must
qualify before installation; foreign hooks cause refusal.

## Terrain placement

```lua
local valid, adjusted, reason = exu.IsTerrainBuildable(
    team, "abspow.odf", proposed, SetVector(0, 0, 1))
```

This uses the released BZR rig placement path: native class lookup and model
bounds, the rig's 10-unit X/Z footprint clearance, native floor height, and the
engine's `CanBuildHere` terrain/collision validator. Communication towers, gun
towers and shields also require allied power range, using BZR's alliance test and
the transformed model sphere centre, as the rig preview does.

It does not create an object, alter rig selection, issue commands, reserve a
site, or check affordability, prerequisites, producer distance, or build menus.
It can load ODF/model resources lazily. Only structures and turrets are supported.
Facing uses the horizontal X/Z components and must be nonzero. Team must be
0..15; ODF names are at most eight ASCII basename characters with an optional
`.odf` suffix. Nonfinite input is refused. Off-map footprints and excessive
native iteration ranges are rejected before calling terrain readers.

`true` means the placement check accepted the site; `false` means a checked
rejection. `nil` means the query could not be performed. Reasons are
`buildable`, `blocked`, `outside-map`, `power-required`, `invalid-odf`,
`unsupported-class`, or `unavailable`. An adjusted vector accompanies checked
results; an early off-map rejection returns the proposed position unchanged.

`EXU_IsTerrainBuildable` exposes the same operation to native consumers; see its
documented status values in `include/ExtraUtils.h`. All queries are engine-thread
only. This remains an observation: later world changes can invalidate a result.

## Remote player Steam64

```lua
local steam64 = exu.GetPlayerSteam64(networkId)
```

The input is a network ID from `AddPlayer`, `Receive`, or `GetMyNetID`, not a team
number or object handle. Each call resolves the current NetPlayer, checks its ID,
and inspects the native provider tag. Only Steam individual-account identities
are returned, as decimal strings without Lua double-precision loss. GOG IDs,
absent/disconnected players, singleplayer, and failed qualification return nil.
No identity is cached after disconnect. Existing local `GetSteam64()` behavior
is unchanged. Native consumers use `EXU_GetPlayerSteam64`.

## EXU command and variable registry

BZR has no BZCC VarSys subsystem. These APIs provide reusable, typed,
mission-scoped registration and explicit dispatch for an authored console/UI;
they do not patch stock menus, evaluate code, or intercept arbitrary keystrokes.

```lua
exu.RegisterConsoleCommand("mission.marker", function(arguments)
    print("Requested marker:", arguments)
end)
exu.RegisterConsoleVariable("hud.scale", 1, {
    min = 0.5, max = 2,
    onChanged = function(value, previous) print(value, previous) end,
})
exu.ExecuteConsoleCommand("mission.marker", "north ridge")
exu.SetConsoleVariable("hud.scale", 1.25)
local scale = exu.GetConsoleVariable("hud.scale")
local items = exu.ListConsoleItems()
exu.UnregisterConsoleItem("mission.marker")
```

Names are case-sensitive, 1..96 ASCII letters/digits/dots/hyphens/underscores.
Commands and variables share one namespace and duplicate registration is
refused. Commands receive one uninterpreted string (empty by default). Variables
retain their default's exact number/string/boolean type; numeric values must be
finite and satisfy optional bounds. `onChanged(new, previous)` runs only when
the value actually changes, after the new value is stored. Callback failure
returns `false,error` and preserves the committed value. Unknown command/set
operations return `false,error`; an unknown variable getter returns nil.
Invalid API arguments raise Lua argument errors.

Callbacks can unregister themselves safely. All storage is in the originating
Lua state's registry, with no retained native Lua pointer or process-global
callback reference. Closing the state releases it, even if EXU remains loaded.
`ListConsoleItems` returns an independent name-to-`command`/`variable` snapshot.

## Validation and release boundary

The targeted host tests cover projection conventions, offsets, orthographic and
behind-camera behavior, render recentering/Z conversion, queue ordering and
overflow, Steam identity discrimination/precision, and the actual native console
bindings under Lua 5.1. `tests/runtime/capabilities_smoke.lua` and
`tests/runtime/console_registry_smoke.lua` provide live Lua boundary checks.
`tests/runtime/producer_events_smoke.lua` verifies the rig cancellation/requeue,
factory and armory lifecycle in an isolated mission fixture.

Release x86 build, EXU hardening/API/version/catalog checks, all host C++ tests,
and the native console module under Lua 5.1 pass. An isolated Windows/GOG DX11
mission passed projection, native placement/power, console and production
lifecycle checks, with settled live entry bytes verified before hook activation.
See [the validation record](Research/EXU2_CAPABILITIES_VALIDATION_20261023.md)
for evidence and limits. The full Linux script was blocked by a missing native
Node runtime; its C++/Lua lanes were run separately, and the complete Linux
suite subsequently passed in GitHub CI with provisioned dependencies. Positive Steam multiplayer
identity, multiplayer authority/deduplication, DX9 and Steam/Proton/Wine runtime
behavior require their own qualification. These changes are source development;
no installed release or Workshop publication is implied.
