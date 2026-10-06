# Native producer evidence for EXU runtime events

Read-only Ghidra investigation, 2026-10-06. This supplements
[the EXU implementation plan](RUNTIME_SCRIPTING_EVENTS_20261006.md); it does not
implement or qualify any new hook. EXU owns listener registration, the runtime
queue and Lua delivery. OpenShim supplies observations at hooks it owns.

## Build and evidence

The existing Ghidra MCP backend was accessible through Claude's configured
bridge. The imported Windows x86 GOG executable is Redux 2.2.301, image base
`0x00400000`. Its recorded SHA-256 matches the installed executable:

`8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`

Eight selected entry byte ranges also match the disk image: the target setter,
command activation, damage flags, both user-possession methods, both build-menu
methods and Cannon simulation. Evidence comes from Lua registration entries,
callers, disassembly, field writes and existing RTTI-qualified OpenShim sites.
Same-address legacy PDB labels were not used to establish identity. Raw tool
output remains in ignored private scratch; only findings are recorded here.

All addresses below are provisional research anchors for this exact build.
They are not Steam signatures, validated detours or proof of multiplayer
authority. No game, DLL deployment or live hook test was performed.

## Target changes: useful committed setter

Lua `SetTarget` at `0x005007A0` calls helper `0x005C8950`, which resolves the
handles and reaches `0x0049F450`. That setter takes the subject in ECX and a
target object on the stack, converts the target to a handle (zero for null),
writes `GameObject + 0x21C` and returns with `RET 4`. Getter `0x00462610` reads
the same field. Ghidra reports 19 direct call sites, including engine paths
beyond the Lua helper.

Candidate observation: capture the subject handle and old target, run stock,
then copy the committed new target; suppress unchanged values. A direct
displacement-write scan also found initialization in the GameObject constructor
`0x004DA0B0`; the other inspected hits concern mission-local handle/audio state.
That scan cannot prove complete coverage of indirect writes, bulk copies,
save/load or object reuse. Initialization must not become a gameplay event.

## Commands: observe activation, not just request storage

Lua `SetCommand` at `0x00504230` reaches `0x005CB6A0` or `0x005CB790`, then
`0x004DBAF0`. The latter copies a 32-byte pending order to `GameObject + 0xC8`.
Native overloads such as `0x004DBCE0`, `0x004DBD60` and `0x004DBF90` write the
same pending fields directly. Hooking only the Lua helper or record-copy method
would miss native orders.

The stronger candidate is `0x004DBB40`: a no-stack-argument ECX method that
releases the old active path, transfers the pending block to the active block
at `+0xA8`, clears pending ownership and returns the active command at `+0xAC`.
Ghidra reports 18 direct call sites across multiple process routines. A special
pending-command-1 branch clears that request and returns 1 without transferring
the active block; it must not be reported as normal command activation.

Copy the resulting command/priority/target and qualified location facts after
stock execution. The path pointer at `+0xB4` is engine-owned and cannot enter
the EXU queue. Resets, repeated orders, AI bypasses and later rejection still
need testing. Activation here does not guarantee the unit completes the order.

## Damage and kills: current probe is before health changes

The four existing damage probe calls reach `0x004DC130` before health changes.
The native damage getter `0x0047E990` reads the incoming amount at damage-record
`+0x0C`; handlers can scale it or reject application. Health modification at
`0x004DC030` also checks local-player immunity before reaching health writer
`0x004A76A0`. The writer is used for initialization and other health changes too.

Incoming amount, applied health delta and death are separate facts. Copying the
raw amount into `OnDamage` as actual HP lost would be wrong. The inspected
handlers have differing death-state branches; this pass did not establish a
universal committed death transition or exact killer attribution. The existing
career-derived kill inference remains insufficient for an exact `OnKill`.

## Pilot, build, weapon and player observations

- Lua `EjectPilot`/`HopOut` reach `0x005CCC90`/`0x005CCD00`. They only set flags
  at `+0xDC`/`+0xE4` on the associated object at `GameObject + 0x230`. These are
  requests, not completed exits. `0x004DB930` actually replaces the local user
  object and notifies the previous one; `0x004DBA60` removes user-specific
  control. They corroborate existing possession research but do not establish
  all AI boarding, pilot identity, exit reason or a paired enter/exit event.
- Producer `SetActiveMode` at `0x005AEAB0` and construction-rig mode selection
  at `0x0049D0E0` turn values above `0x1A` into pending `CMD_BUILD = 0x15`
  through `0x004DBF90`. The rig also stores the selected class at `+0x370`.
  This identifies build-item selection, not resource validation or completion.
  Their return values are not a common acceptance boolean. Reuse OpenShim's
  existing menu hook owner and distinguish selection from accepted build order.
- Cannon simulation `0x0048EFE0` reaches factory call `0x0048F54F` after firing
  checks and prepares the owner/shot transform. This corroborates OpenShim's
  casing observation point. Popper simulation `0x005A6F70` has a separate
  factory call at `0x005A7195`; the catalog's historic CannonShotCall name must
  not imply Cannon coverage. One factory observation is not proven to equal
  one trigger activation; pellet/burst grouping and other weapon families remain
  open. Preserve distinct projectile and weapon events.
- Stock LuaMission methods `0x0050AEF0`, `0x0050AFD0`, `0x0050B0B0` invoke
  `CreatePlayer`, `AddPlayer`, `DeletePlayer` respectively through protected
  Lua calls with player ID, name and team. Start with explicit EXU forwarding
  from those existing callbacks, preserving mission handlers. Initial roster
  population versus joining, duplicate callbacks and reconnects still need MP
  qualification; no new native player hook is needed for the first adapter.

## Implementation consequence

Keep the first milestone small: the EXU dispatcher, explicit mission-Update
pump and lifecycle tests. Target changes and command activation now have useful
native candidates for a subsequent producer fixture. Add each site to the
owning address catalog with ABI/build qualification before installation. Keep
unsupported semantics unavailable until measured rather than filling them
from input requests, raw pointers or career-stat inference.
