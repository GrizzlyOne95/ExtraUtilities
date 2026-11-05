# ISDFC BZ2-style scrap meter

The campaign now uses an EXU overlay scrap meter on the left, following BZ2's
ScrapGauge and original `bzgame_scrap.cfg` / `bzgame_init.cfg` controls. This is
a display change. Current scrap and maximum capacity come from the player's
actual Redux team via the stock Lua getters.

## Style and evidence

BZ2 reference corpus `bz2-bzone-b131p-best-effort`:

- `ScrapGauge::Init` 0x0053ADDB, `Init-0053addb.c`, SHA256
  `58c92e97c6595d82632a29fd8fb736d8cc34fdfef3522d420b60168cdaebf87d`:
  loads bzgame_scrap and binds its Gauge and storage segments.
- `ScrapGauge::Control` 0x0053AAE1, `Control-0053aae1.c`, SHA256
  `178c36d201e240efd62fc83fe52d71446725b0b9cd51c3a1b0d79f8046618968`:
  updates capacity geometry and supply, uses segment height minus one, includes
  ten pixels for the bottom count, and supports horizontal/vertical layouts.
- Original demo extraction under BZ2DemoPort/source/unpakbzd:
  `bzgame_scrap.cfg` anchors left/bottom at (8,-105), storage width 16, gauge
  width 10 at x=3/y=-10, black padding 3, bottom count in font TINY. Fill and
  count are RGB 153/153/153; storage background is 127/127/127 alpha 51.
  `bzgame_init.cfg` supplies BACKGROUND black alpha 127. These levels are used
  as exact byte/255 values, rather than approximated decimal opacity.
- `IControl::SetBorder` 0x00655C70 sets padding at +0x1A4. The constructor
  0x00655EB9 defaults bevel width to zero, and ICWindow 0x00663031 retains it;
  its RenderSelf 0x0066320E calls RenderCtrlBackground 0x0065627F. BorderSize(3)
  expands the black background rectangle; it does not add a gray outline.
- `Font::Read` 0x0065C5DE reads FONT header, six-byte glyph records, then each
  bitmap's width/height and alpha. The original `small.bmf` TINY font SHA256 is
  `e6e16a68351e273463d628415593f9c921d028282a6a812eba1bed4fcc5f45be`.
  Authoring retains its original 5x11 digit cells, offsets and binary alpha.

The overlay scales reference pixels with screen height. Redux exposes aggregate
storage rather than BZ2's ScrapManager producer list, so the gauge uses one
aggregate capacity segment. Exceptionally large capacities cap the visible
height to 64% of the screen while keeping the correct fill fraction/count.
Zero capacity hides the gauge. Camera/no-HUD and pause visibility follow the
existing shared campaign HUD lifecycle. No camera setter is used.

## Native suppression

Existing `SetStockStatusHudVisible` gains the `scrap` part. On qualified GOG
Redux 2.2.301 (SHA256 8d71f56c...3377413), one checked 10-byte inline patch at
0x005C7057 replaces the scrap block's width comparison with a JMP to 0x005C71D0.
This skips the scrap sprite, title and count together. The shared function's
saved depth and stack remain intact, and pilot layout/drawing continues from
the resume point. It avoids replacing OpenShim's individual sprite/text hooks.
All addresses and expected bytes are catalogued in exu.json. Mission closure
restores the original instruction via BasicPatch's inactive default.

The replacement is built before stock suppression activates. Missing assets,
unsupported/native conflicts or overlay errors retain/restore the native group.
The pilot resource block and economic getters are not patched.

## Validation boundary

Release x86 build, Lua 5.1 lifecycle/geometry tests, static branch decoding,
palette/bitmap checks and hardening validate the implementation offline. Native
appearance has not been tested; the user's latest test preference was to finish
offline. The prior radar and surface-laser features are retained in the DLL.
