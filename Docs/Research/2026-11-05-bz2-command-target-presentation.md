# EXU-owned BZ2 command and selection presentation

`SetBz2HudPresentationEnabled(bool)` owns rendering and animation inside EXU.
OpenShim is optional; its only added responsibility is honoring a command-area
presentation lease (bit 32) by yielding its palette and idle fading. No second
renderer DLL or OpenShim implementation of the BZ2 renderer is required.

The API wraps the released command renderer while always executing its native
input, menu and sound processing. It replaces command/target backplates, draws
registered BZ2 command icons beside existing labels, and replaces native-visible
objective/target sprite submissions with rotating triangle indicators. Objective
rotation is 0.5 radians/second; target rotation is 2 radians/second. Both use native
game time so their pose does not advance while game time is paused. Hover feedback
pulses at the existing native command row hit rectangles. Gameplay, selection,
visibility eligibility, labels, targeting rules and objectives remain native.

Icon resources are supplied by the content pack as `iu_attack`, `iu_follow`,
`iu_gotonav`, `iu_recycle`, `iu_pickup`, `iu_dropoff`, `iu_deploy`, `iu_undeploy`,
`iu_service`, `iu_cancel`, `iu_recy`, `iu_fact`, `iu_armo`, and `iu_cons`. Missing
icons leave the native label available. The renderer itself does not depend on
campaign Lua for its per-frame drawing or animation.

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
context in icon drawing; icons now use the six-argument native helper, which
resolves its own context. The corrected normal `isdfms06` run produced verified
2560x1440 PNGs with command icons and panels visible, right-side pilot text, and
the capacity-scaled scrap bar clear of the radar. No new crash or Ogre exception
occurred in that smoke. No camera scripts or replacement mission controllers.

Normal `isdfms01` also produced native 2560x1440 captures showing the Scrap Outpost
objective's four triangle indicators at different rotation phases. The native
marker draw path was logged, with no fresh crash or Ogre exception.

Target-indicator/info states, command selection/cancel,
mission transitions, no-OpenShim runtime, other renderers and Steam/Proton/Wine
still require acceptance checks. Runtime artifacts are local to the game's
`output/isdfc-native-review-20261105/command-ui` folder, not committed here.
