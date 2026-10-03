# Damage screen flash: native RE (2026-10-03)

Static RE of `battlezone98redux.exe` (GOG Redux 2.2.301, x86, image base `0x400000`). Goal: let Lua
suppress the red full-screen flash the engine plays when the local player is hurt. Function names
come from the private reference corpus; every address, byte and constant below was read from the
shipped GOG image. Not yet exercised in game.

Labels: **PROVEN** = seen in disassembly. **INFERRED** = follows from it, not seen directly.

## The fade object (PROVEN)

All full-screen color flashes go through one global `ColorFade` at `0x0097838C` (found from its
RTTI `.?AVColorFade@@` → vtable `0x00877774`, stored by the constructor `0x0049B3F0` called from
the static initializer at `0x00405110`).

| Member | Offset | Written by |
|---|---|---|
| glare (float) | `+0x28` | `SetGlare` `0x0049B4C0` (`glare += x`) |
| ratio (float) | `+0x2C` | `SetFade` adds to it |
| rate (float) | `+0x30` | `SetFade` overwrites it |
| r, g, b (bytes) | `+0x34..+0x36` | `SetFade` overwrites them |

`SetFade` `0x0049B430` is `thiscall(float ratio, float rate, int r, int g, int b)` and ends in
`ret 0x14`. `ClearFade` `0x0049B480` zeroes ratio, rate and color.

## Callers of SetFade (PROVEN)

| Call site | Function | Arguments | Gate |
|---|---|---|---|
| `0x004AA6FB` | `Craft::DamageAlloc` (`0x004AA630`) | `damage * 0.002, 5.0, 255, 0, 0` | craft is the user (`0x00917AFC`) |
| `0x005A0D57` | `Person::DamageAlloc` (`0x005A0C90`) | `damage * 0.03, 5.0, 255, 0, 0` | pilot is the user |
| `0x004AB278` | `Craft::UpdateTemperature` (`0x004AAEA0`) | `heat * 0.01, 5.0, 255, 64, 0` | craft is the user |
| `0x005E1D3A`, `0x005E1E0A` | TerrainExpose activate / deactivate | `1.0, 1.0, 255, 255, 255` | n/a |
| `0x00506F72`, `0x0050BBE5` | variable-color effects | data driven | n/a |
| `0x0049B78A` | stock Lua `ColorFade()` wrapper | script supplied | n/a |

Constants read from `.rdata`: `0x008A24D8` = 0.002, `0x008A2510` = 0.03, `0x0087B038` = 0.01,
`0x008A285C` = 5.0.

## Suppression (shipped in `src/Patches/ScreenFlash.cpp`)

Each of the three player-hurt sites ends in the same 10 bytes after its five argument pushes:

```
B9 8C 83 97 00     mov  ecx, colorFade
E8 <rel32>         call ColorFade::SetFade      ; callee pops 0x14
```

Suppressing a site replaces those 10 bytes with `83 C4 14` (`add esp, 0x14`) and seven NOPs: the
pushed arguments are discarded, the stack stays balanced and the fade never accumulates. The
preimage includes the call's rel32, so each patch only installs on its own site and fails closed
anywhere else. `eax/ecx/edx` are caller-saved and `SetFade` returns nothing, so skipping the call
changes no state the caller reads afterwards (**PROVEN**: the instruction after each site, which is
also the target of the "not the user" `jne`, reloads its registers from the frame).

The patches default to inactive and `BasicPatch::ResetRequestedStatusesToDefaults` puts them back
when the Lua state closes, so suppression is mission scoped. Lua surface:
`exu.Get/SetDamageFlashEnabled` (craft + pilot) and `exu.Get/SetHeatFlashEnabled` (overheat).
Other flashes, including mission `ColorFade()`, are untouched.
