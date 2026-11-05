# EXU-owned BZ2 command and selection presentation

`SetBz2HudPresentationEnabled(bool)` owns rendering and animation inside EXU.
OpenShim is optional; its only added responsibility is honoring a command-area
presentation lease (bit 32) by yielding its palette and idle fading. No second
renderer DLL or OpenShim implementation of the BZ2 renderer is required.

The API wraps the released command renderer while always executing its native
input, menu and sound processing. It removes the command backplate, draws BZ2-style
row bars behind Redux's own key sprites, labels and gauges, keeps a target panel, and
replaces native-visible objective/target sprite submissions with rotating triangle
indicators. Objective rotation is 0.5 radians/second; target rotation is 2
radians/second. Both use native game time so their pose does not advance while game
time is paused. Gameplay, selection, visibility eligibility, labels, targeting rules
and objectives remain native. The renderer does not depend on campaign Lua for its
per-frame drawing or animation.

## Command rows

A row's bars are drawn when its native label draws, so only populated rows get one.
No label text is inspected, so this works in any language and needs no icon assets.

Redux side (`FUN_0049f9e0-0049f9e0.c`, `FUN_004a08e0-004a08e0.c`,
`FUN_0068af70`):

- Rects and layout: `FUN_0049f9e0` lines 41-52 (key width, gap) and 64-68 (row rects
  at 0x9173E8, label anchors at 0x9174E8, label X = left + key width + gap, plus `_DAT_009174b8`, line 65, in the alternate layout).
- A row is found by exact label X and Y inside its rect, once per frame. Row state is
  the text color stored at self+0x1030+i*0x18: `FUN_004a08e0` lines 983-992 store
  current (0x917560), inert (0x91755C) or disabled (0x917588).
- Highlight is the mouse over the row (`FUN_004a08e0` line 427) or the cursor row at
  self+0x1198 (lines 374-398).
- The white selection fill (lines 412-422, call at 0x4A0D91) is redirected to a no-op
  while active. Bars use its depth, base+.03, which is behind the gauges at +.01
  (lines 954-973). `FUN_0068af70` lines 8-9 add one to x2,y2, so Fill is inclusive.

BZ2 side (`bz2r_res\config\game`):

- `bzgame_init.cfg` COMMANDTEXT gives the eight background and gradient colors, alpha 127.
- `bzgame_command.cfg` gives the key column, a gap, then the item, all `colorize.tga`.
- `baked\HUD\colorize.dds` is a 127x16 bar: opaque edge, alpha 100 interior, ends in a
  half circle. The key column uses its rounded left end, the item its rounded right end.

UNVERIFIED: how the gradient color is applied. These controls set no gradient style in
the configs, and neither `bzone.exe` b131p nor the BZCC decompile holds a gradient
string to trace. Top-to-bottom is a choice made here, not decompile evidence.

## Evidence

Build scope: GOG Redux 2.2.301, executable SHA256
`8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`.
Addresses live in `exu.json`, with seven optional runtime anchors and checked
function-entry detours. Entry spans contain whole relocatable prologue instructions.
Lua close, including exceptional cleanup, restores the hooks and releases ownership.

Redux corpus `bzr-gog-indexed`:

- `FUN_0068ca30-0068ca30.c`, SHA256
  `d08bb24b5fbc2a308344ea0fadb5f517d9fcb311e02c3e5b4e1300ef19fa6061`:
  sized sprite submission requires its explicit material context; alignment bits
  0x10000/0x40000 mean centered, 0x20000/0x80000 mean right/bottom.
- `FUN_0068c560-0068c560.c`, SHA256
  `4205fa9daa2e729e2e48ff42584a09acc60bce17c54489d7373c254952b6b048`:
  six-argument sprite submission resolves the material from its sprite record.
  Its UI-scale getter returns float bits in EAX/EDX; released disassembly verified
  the getter and its two scale globals.
- `FUN_004a08e0-004a08e0.c`, SHA256
  `b813f424cf0541116a96e30c142191c7484e209b69227efd138e67a4b1b57067`:
  native command renderer, retained for input processing. Existing independently
  qualified OpenShim call/entry evidence supplies the shared ABI identities.

BZ2 reference corpus `bz2-bzone-a130-best-effort` (presentation reference only):

- `RenderObjective-0053c0b6.c`, SHA256
  `751db4cfc0367af13da11f7aa2fc53d2e4c9be6682e162c8ab8b4bc6d3afd571`.
- `RenderTarget-0053c2a3.c`, SHA256
  `5e1f069128892f1db1bfe5e1507ad21ec90654e8cf57193c107c4d04d2f19b92`.
- Retail BZ2 `bzgame_command.cfg`/`bzgame_init.cfg` and original command/interface
  assets informed the panel layout and palette. Redux keeps its existing menu
  hierarchy and bindings; this is an adaptation, not BZ2's command simulation.

## Validation and limits

Release x86 build, 48 MSVC host programs, hardening/API parity and generated
catalog/profile checks passed. A first native test caught an invalid material
context in icon drawing, since removed with the icons. The corrected normal
`isdfms06` run produced verified 2560x1440 PNGs with the earlier panel style,
right-side pilot text, and the capacity-scaled scrap bar clear of the radar. No new
crash or Ogre exception occurred in that smoke. No camera scripts or replacement
mission controllers.

Normal `isdfms01` also produced native 2560x1440 captures showing the Scrap Outpost
objective's four triangle indicators at different rotation phases. The native
marker draw path was logged, with no fresh crash or Ogre exception.

Target-indicator/info states, command selection/cancel,
mission transitions, no-OpenShim runtime, other renderers and Steam/Proton/Wine
still require acceptance checks. Runtime artifacts are local to the game's
`output/isdfc-native-review-20261105/command-ui` folder, not committed here.
