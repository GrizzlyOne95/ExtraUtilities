# Stock status HUD suppression in EXU

Date: 2026-10-03. GOG 2.2.301, SHA-256
`8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`.

## Result

`exu.SetStockStatusHudVisible(part, visible)` hides the stock player status
display (bottom-right hull/ammo bars and weapon list) in three groups without
OpenShim. It NOPs the draw calls inside the StatusDisplay renderer
(`0x005DC300`, `__fastcall`, object in ECX) and leaves the renderer itself
running. That matters: the renderer also fires the voice warnings
(`0x0047C330` all weapons out, `0x0047C380` hull below 0.25, guarded by the
object's `+0x29`/`+0x28` bytes), so skipping the whole function would silence
them. Statically disassembled and pattern-checked only; not yet seen in game.

## Call sites

Every draw helper here is cdecl: the caller pops its arguments with the
`add esp` that follows the call, and no caller reads the return value (the next
instruction overwrites or ignores EAX). Replacing the 5-byte `call rel32` with
NOPs drops exactly one draw and keeps the stack balanced. Each catalog pattern
(`exu.json`, group `StatusHud`) is the call plus the `add esp`, unique in the
executable sections; the patch preimage is the call with its rel32, so a site
another module already redirected is left alone and the group reports visible.

| Group | Site | Target | Draw |
| --- | --- | --- | --- |
| weapons | `0x005DC843` | `0x0068CA30` sprite | weapon-row plate (sprite `[0x00918364]`), inside the row loop |
| weapons | `0x005DCAB5` | `0x0068C560` sprite | hardpoint icon |
| weapons | `0x005DCB37` | `0x00689D10` font | weapon name |
| hull | `0x005DCDF4` | `0x0068CA30` | hull label (object `+0x10`) |
| hull | `0x005DCEF5` | `0x0068CA30` | hull bar (object `+0x14`), pane-clipped |
| ammo | `0x005DD0C4` | `0x0068CA30` | ammo label (object `+0x1C`) |
| ammo | `0x005DD1C5` | `0x0068CA30` | ammo bar (object `+0x20`), pane-clipped |
| ammo | `0x005DD2F8` | `0x00689D10` | shots-remaining count |
| ammo | `0x005DD3EE`, `0x005DD41E`, `0x005DD44E` | `0x0068AF70` filled rect | three ammo-cost marker rows |

The bars' pane adjustments around `0x005DCEF5`/`0x005DD1C5` are plain stores
before and after the call and are unaffected. Hiding is mission scoped through
BasicPatch's Lua-state reset.

## Relation to the native layout work

This only hides; it does not move the stock bars. The OpenShim meter
relocation planned in [NATIVE_HUD_LAYOUT_20260930.md](NATIVE_HUD_LAYOUT_20260930.md)
would intercept these same call sites. The preimage checks make the two fail
closed against each other rather than stack: whichever patches a site first
owns it. Correction to that note: `0x005DBEA0` is an AI-process vtable entry,
not the ratio update.

The scripted replacement HUD is `Workshop/exu_ring_gauge.lua`, after the BZ2
status ring (`StatusDisplay` in the BZ2 1.3 b131p decompile: ten 9-degree
segments per gauge, inner radius 0.6, `n = ceil(ratio * 10)` lit with the last
faded, `GetHealthColor` thresholds 0.5/0.25, ammo azure 0,127,255).
