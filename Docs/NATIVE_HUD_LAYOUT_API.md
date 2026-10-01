# Native HUD layout API

Implementation checkpoint, 2026-09-30: EXU controls, Lua bindings, a mod helper,
and OpenShim's portable geometry/provider boundary are implemented. **The native
render interception is not installed or qualified.** These APIs currently
report unavailable for hull/ammo on every game build. They do not yet move a
live bar. Custom numeric readouts in `Workshop/exu_hud.lua` work independently
through stock Lua values and existing EXU TextArea elements.

The native trace and Windows capture procedure are in
[`Research/NATIVE_HUD_LAYOUT_20260930.md`](Research/NATIVE_HUD_LAYOUT_20260930.md).
OpenShim owns the native adapter; EXU owns mod intent and bindings. No executable
addresses, signatures, engine objects or atlas mutations are added to EXU.

## Mod contract

The semantic IDs are exactly `"hull"` and `"ammo"`. Rectangles use physical
viewport pixels with a top-left origin: `x, y, width, height`. Width and height
are positive pixel counts; they are not inclusive right/bottom edges. A getter
reports the full bar instrument, including its currently empty area.

| EXU function | Result |
| --- | --- |
| `IsNativeHudLayoutAvailable(meter)` | Whether OpenShim has a qualified adapter for that meter. An older/missing provider or a bootstrap-only `winmm.dll` returns false. |
| `GetNativeHudMeterRect(meter)` | Effective full `x,y,w,h`, or nil without an observed stock frame. |
| `GetNativeHudMeterDefaultRect(meter)` | Current stock full `x,y,w,h` before overrides, or nil. |
| `SetNativeHudMeterRect(meter,x,y,w,h)` | Request geometry for the next native draw. Returns true when accepted, otherwise false and a reason. |
| `SetNativeHudMeterVisible(meter,visible)` | Request group visibility; `visible` must be a boolean. |
| `RestoreNativeHudMeter(meter)` | Clear that meter's geometry and visibility intent. |
| `RestoreAllNativeHudMeters()` | Clear only meter slots successfully changed through EXU this mission. With no owned slots it succeeds as a no-op. |

Mutators return `false, reason` for `invalid_meter`, `invalid_rect`,
`unavailable`, or `native_state_unavailable`. Wrong Lua argument types raise
normal argument errors. Unknown meter IDs return false/nil. Coordinates and
far edges must lie in `-65535..65535`, dimensions in `1..16384`, all integral
and finite. Offscreen positions are allowed within these bounds so an asset
can use the native viewport clipping.

An accepted rectangle is intent, not proof that a frame has drawn. A future
qualified provider can accept intent before the first stock draw; getters still
return nil until it observes a source rectangle. Availability describes adapter
qualification rather than player-object readiness.

Stock geometry is observed per frame and follows native UI scale/resolution.
An override stays in physical pixels; mods should recompute their authored
slots after viewport changes. Restoration uses the current stock geometry,
never a saved rectangle from a previous resolution. EXU explicitly restores
its owned slots on mission/Lua teardown even when a native consumer pins the
DLL. OpenShim also clears its own intent at mission exit/reset. Each meter is
a single-writer layout slot; independent consumers should agree on ownership.

## Authored slots and live values

`Workshop/exu_hud.lua` takes an explicitly required EXU table, normalized slot
bounds and optional names of existing authored TextArea elements. It retries
when the provider/frame appears late, reflows after resolution changes, and
restores only slots it successfully changed. It leaves native meters alone
while relocation is unavailable.

```lua
local exu = require("exu")
local Hud = require("exu_hud")
local cockpit = Hud.New(exu, {
    hull = {0.12, 0.70, 0.018, 0.20},
    ammo = {0.86, 0.70, 0.018, 0.20},
}, {hull = "cockpit_hull_value", ammo = "cockpit_ammo_value"})

-- From the mission's existing Update callback:
cockpit.Update()
-- From a mission-owned teardown/reload path, never DeleteObject(handle):
cockpit.Shutdown()
```

The helper reacquires `GetPlayerHandle()` every update and reads
`GetCurHealth/GetMaxHealth` and `GetCurAmmo/GetMaxAmmo`. It clears captions when
the object disappears or stock menus open. The asset should pass false to
`Update(showValues)` in other camera/cockpit modes where its values should be
hidden. These are live simulation values, not the native bars' smoothed ratios
or the native ammo shots-remaining value.

The helper only fills TextArea elements the asset already owns. Use the existing
overlay API to create, place and style them. Existing scrap/pilot position and
color, HUD sprite visibility, and radar size APIs remain independent composition
controls. `examples/NativeHudLayout.lua` shows the mission integration.

## Native adapter acceptance

A meter capability may be enabled only after OpenShim qualifies its complete
draw group: bar sprite and fill clip, faction label, and the ammo marker and
shots origin. Font metrics remain native; a transformed text origin does not
promise arbitrary native font scaling. Hiding suppresses these visual submits;
the original status renderer and its warning/audio logic must still execute.

The renderer must resolve native pane-relative positions/alignment into screen
coordinates before applying the full-bar transform, then submit with equivalent
UVs, flags and clipping. Restore the original pane around each draw, including
failure paths. Capture source geometry before the group's first draw: discovering
it only at the later bar submission cannot move a label already rendered.
Do not mutate the shared sprite atlas or intercept unrelated radar/weapons draws.

Required qualification remains: live GOG bytes/ABI and screenshots over full,
partial and empty bars; labels, markers/readout and warning audio; viewport/UI
scale changes; cockpit/player transitions; mission/load/teardown; existing
scrap/pilot, sprite visibility and radar composition. Windows Steam and
Proton/Wine require their own runtime evidence. Host tests prove geometry,
intent and binding behavior, not game compatibility.
