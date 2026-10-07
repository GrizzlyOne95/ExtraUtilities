# Native producer evidence for EXU runtime events

Read-only Ghidra investigation, 2026-10-06. This supplements
[the EXU implementation plan](RUNTIME_SCRIPTING_EVENTS_20261006.md); it does not
implement or qualify any new hook. EXU owns listener registration, the runtime
queue and Lua delivery. OpenShim supplies observations at hooks it owns.

The later [damage-attribution investigation](RUNTIME_DAMAGE_ATTRIBUTION_GOG_20261007.md)
extends the damage section with projectile/explosion construction, ownership
and handle-domain evidence; its new live fixture remains unexecuted.

## Build and evidence

The existing Ghidra MCP backend was accessible through Claude's configured
bridge. The imported Windows x86 GOG executable is Redux 2.2.301, image base
`0x00400000`. Its recorded SHA-256 matches the installed executable:

`8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`

Thirty-three selected entry byte ranges also match the disk image, including the
target/order methods, four damage handlers, pilot transitions, build selection
and weapon simulation methods. Evidence comes from Lua registration entries,
callers, disassembly, field writes and RTTI in the released executable.
Same-address legacy PDB labels were not used to establish identity. Raw tool
output remains in ignored private scratch; only findings are recorded here.

All addresses below are provisional research anchors for this exact build.
They are not Steam signatures, validated detours or proof of multiplayer
authority. This initial investigation was static. The later
[isdftest probe](RUNTIME_EVENT_ISDFTEST_PROBE_20261006.md) records limited live
observations separately; no production callback hooks or DLLs were deployed.

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
raw amount into `OnDamage` as actual HP lost would be wrong. All four damage
methods use the DistributedObject subobject at complete-object `+0x18`, consume
two stack arguments (`RET 8`) and return in AL. Their return values are not a
common applied-damage signal: nonfatal Person damage can change health and
return false. Ghidra's inferred void/one-argument prototypes miss these details.

RTTI and disassembly distinguish the actual fatal branches:

| Handler | Released-build classes | Fatal observation anchor |
| --- | --- | --- |
| `0x0047EF10` | Buildings, mines and related objects | Calls flag helper at `0x0047F26E` with `0x01000200`, after the negative-health-ratio test. |
| `0x004AA630` | Craft and vehicle subclasses | Commits flag `0x200` at `0x004AADDF`, after the negative-health-ratio test and prior-flag guard. |
| `0x005A0C90` | Person | Commits flag `0x01000000` at `0x005A0DD0`, after applied health becomes negative. |
| `0x005AA330` | PowerUp subclasses, CameraPod, DayWrecker, Torpedo and ScrapDropoff | Commits flag `0x01000000` at `0x005AA621` after the negative-health-ratio test. |

These are candidate facts from specific damage invocations, not proof of a
universal death hook. Capture the current damage source there rather than a
later last-damager guess. Exactly zero health is not the inspected fatal
condition. Building damage can delegate to another object's handler, and the
PowerUp path lacks Person's already-fatal-flag guard; nested forwarding and
repeat hits need deduplication. Script deletion, recycling and load must remain
outside damage-kill observation.

There is a concrete static counterexample to treating `0x200` or disappearance
as death: successful boarding marks the surviving pilot's old Person object
with `0x201` for retirement. Career tracking regards that bit or an unresolved
handle as death. A recently damaged pilot that boards therefore presents a
false-kill path to that inference; this has not been reproduced live. The
career source comment that all four damage handlers set the same `0x200` latch
also overstates the actual class-specific code. Do not reuse either assumption
for exact EXU `OnKill` delivery.

The later isdftest probe confirms nonfatal/healing Person damage returning
false, zero HP without the fatal latch, and negative HP with it. It did not
complete boarding, so the retirement/false-kill path remains a static finding.

## Pilot transitions and sniper kills

The Lua eject/hop-out helpers only set request flags. Stronger candidates are:

| Operation | Native path | Capturable result |
| --- | --- | --- |
| Boarding | Person collision method `0x005A1550`, primary vtable slot `+0x48` | Successful branch assigns the Person class to the craft's pilot field, transfers player control or AI process, then retires the old Person. Capture both handles before retirement. |
| Voluntary exit | `0x004ADF20`, shared primary slot `+0x84` | Clears pilot/control state, creates a Person, and stores the previous craft handle at new Person `+0x22C`. |
| Ejection | `0x004ADCC0`, shared primary slot `+0x80` | Clears pilot/control state and may create a Person; guards/chance can suppress creation. Calling it does not guarantee an on-foot survivor. |
| Pilot killed | `0x004AD700`, shared primary slot `+0x78` | Clears the pilot class/control record and produces death effects rather than a surviving Person. Includes guards that can leave pilot state unchanged. |

Exit/ejection share pilot-creation helper `0x004ADB00`; it receives the craft
in ECX, returns the new Person in EAX and uses `RET 8`. Its decompiler-inferred
void return is wrong. The boarding method uses `RET 0xC`, so its inferred
two-argument prototype is incomplete too. Preserve full native ABI and engine
side effects; do not gate events on inferred boolean returns.

An occupant inside a craft is represented by pilot class/control state, not a
separate live Person handle. Boarding retires the entering Person; exit creates
a new Person. Their handles are not a persistent pilot identity. Copy the
vehicle handle, observed entering/exiting handle when present and qualified
pilot ODF; never substitute the vehicle handle for a missing pilot handle.
Notify only a committed transition, with known cause; creation failure and
pilot death need different results. Local user-object switching alone misses
the AI branch and does not distinguish these operations.

Sniper hit method `0x005D6E00` has a separate occupant-kill path: after its hit
volume checks it loads primary virtual slot `+0x78` at `0x005D6F98` and calls
it at `0x005D6F9B`. This can kill a pilot while the craft remains alive; the four
HP-damage fatal branches alone are insufficient. In network games the path
first checks a projectile ownership ID against the local ID. Observe the
actual state change, not merely the call, and qualify actor attribution and
peer duplication. Preserve EXU's existing death-camera call redirects inside
the pilot-kill method. Object death and loss of a craft occupant must have
explicitly different identities/coverage before advertising exact `OnKill`.

## Weapon-family coverage

The released executable contains 20 primary vtables with Weapon in their RTTI
ancestry, including base Weapon, with 13 distinct simulation methods at primary
slot `+0x18`. The family map is broader than Cannon's casing hook:

| Family | Simulation method | Successful operation candidate |
| --- | --- | --- |
| Cannon, Mortar, SniperGun | `0x0048EFE0` | Factory call `0x0048F54F` emits each round of an accepted salvo; it is not one call per blast. |
| MachineGun | `0x0050B960` | Existing accepted factory call `0x0050BE3A`. |
| BeamGun | `0x0047B9D0` | Factory call `0x0047BC76` starts/replaces a retained effect; ongoing ticks reuse it. |
| ChargeGun | `0x0048FEC0` | Emission call `0x004902CD`; charging/release and catch-up emission loop differ from ordinary Cannon. |
| Launcher, ImageLauncher, ThermalLauncher | `0x004F82D0` | Accepted launch at `0x004F8478`, after target/ammo/ready checks. |
| PopperGun | `0x005A6F70` | Factory call `0x005A7195`; its historic CannonShotCall catalog name does not establish Cannon coverage. |
| TargetingGun | `0x005DFCB0` | Two distinct factory sites, `0x005E0085` and `0x005E0318`; one must not stand for the other. |
| RemoteDetonator | `0x005B8FF0` | Placement at `0x005B9365`; detonation reaches separate helper `0x005B8E30`. |
| Dispenser | `0x004B5DA0` | Builds a GameObject directly through `0x004E1190`, with authority/type/location checks. |
| ObjectLobber | `0x00582190` | Builds a GameObject directly; creation can fail, so record success rather than attempted sound/trigger. |
| RadarLauncher | `0x005B2010` | Builds and targets a GameObject directly, with network-authority gating. |
| SpecialItem, ImageRefract, RadarDamper, TerrainExpose | `0x005D9990` | Shared utility activation/update/deactivation path; not projectile creation. |
| Base Weapon | `0x004178A0` | Shared empty method; not an activation producer. |

Factory `0x00586FF0` also has callers in non-Weapon paths: FlareMine,
SprayBuilding, Popper, QuakeBlast and a packet-decoding/reconstruction routine
`0x00584620`. A global factory hook therefore both misses some weapon actions
and includes autonomous effects/received projectile reconstruction. Its copied
spawn record remains useful, but it cannot define `OnWeaponFired` by itself.

Start with accepted discharges in the physical weapon families. Qualify what
counts as one discharge for pellets, bursts, charge release and beam onset;
do not count every retained-beam tick or detonation as another launch. Utility
activation coverage should be explicit rather than silently presented as fire.
Copy owner handle, weapon/ordnance ODF and transform while valid; native weapon
and ordnance pointers must not be deferred.

### Cannon salvos and the first fire producer

CannonClass's released RTTI identifies primary vtable `0x00876F98`. Its loader
`0x0048F940` reads `CannonClass` keys through lowercase FNV-1a hashes; reproducing
the released hash routine confirms `shotDelay`, `salvoCount` and `salvoDelay`
at class offsets `+0x80`, `+0x84` and `+0x88`. Defaults are count 1 and delay 0.
This field identification comes from the released loader, not legacy symbols.

After trigger/cooldown/ammo and the native fire check, Cannon stores the selected
salvo count into weapon `+0xC4` at `0x0048F0B8`. It emits through `0x0048F54F`
while that count is positive, the salvo timer at `+0xC8` is nonnegative and ammo
covers the next round. Each emission deducts that round's cost, subtracts
`salvoDelay`, decrements the count and loops back at `0x0048F5B5`. A delayed
salvo can continue in later simulation calls; insufficient ammo clears its
remaining count. Starting a salvo is therefore not proof that every round fired.

The installed ISDFC `gdbshot.odf` selects Cannon with `salvoCount = 12` and no
`salvoDelay` override. Static code predicts up to twelve factory calls for one
SG-2 blast with sufficient ammo. The old `gshotgun.odf` sets count 18. Neither
factory-call count nor ammo-cost change alone defines one presentation event.
Qualify one `OnWeaponFired` on the first successful emission of each accepted
salvo, preserving separate per-projectile snapshots. Trace the acceptance and
emission facts together before adopting that grouping; include partial ammo,
delayed salvos, multiple selected hardpoints and weapon replacement.

The later live probe observed 12 successful creations per full SG-2 blast,
one with five ammo and none with zero ammo. These are narrow Cannon observations;
they do not qualify the broader weapon families or a production event hook.

The existing Cannon/MachineGun casing bridge already runs the native factory
first and preserves its EAX result. Its observation is gated by casing enable,
filter and queue capacity; startup can remove both call patches when casings are
disabled. Event capture must sit before cosmetic filtering and have independent
availability/queue limits, with the same hook owner. Do not require casings or
the currently gated weapon-presentation adapter for gameplay notifications.

Global weapon pass `0x00611270` calls the primary `+0x18` simulation slot with
its native float delta. It can supply an observation-step boundary, but contains
no common accepted-fire signal and cannot replace the family-specific sites.
There is no reason to call Lua from that pass; the initial EXU dispatcher still
pumps explicitly from mission Update.

ISDFC's six FP modules were rechecked read-only. Shotgun, marksman, bazooka and
grenade use ammo-drop tests to call their presentation `OnShot`; pulse also
detects ammo loss, while minigun presentation follows trigger/spin/ammo state.
They do not implement native weapon emission themselves. Native discharge
records can eventually replace those guesses, but queued timing and pellet/
burst counts must be measured before changing reload/recoil logic or legacy
BulletInit/BulletHit consumers. Ammo can also be changed by the controllers'
parking/reload code, so ammo loss alone is not a universal fire event.

## Build and player observations

- Producer `SetActiveMode` at `0x005AEAB0` and construction-rig mode selection
  at `0x0049D0E0` turn values above `0x1A` into pending `CMD_BUILD = 0x15`
  through `0x004DBF90`. The rig also stores the selected class at `+0x370`.
  Armory uses its own method `0x00472C10`, which changes category at `+0x378`
  for named modes and issues a build order for a real item. Armory/rig can issue
  that order and return false; return values are not a common success signal.
  The mode above `0x1A` is a class pointer, not a stable ID. OpenShim's custom
  categories use fake class-like stubs that it intercepts before stock dispatch.
  Observe real-item selection through the existing hook owner, exclude Back/
  category navigation, and copy a qualified item ODF rather than queueing the
  mode pointer. Selection still does not prove resource validation/completion.
- Stock LuaMission methods `0x0050AEF0`, `0x0050AFD0`, `0x0050B0B0` invoke
  `CreatePlayer`, `AddPlayer`, `DeletePlayer` respectively through protected
  Lua calls with player ID, name and team. Start with explicit EXU forwarding
  from those existing callbacks, preserving mission handlers. Initial roster
  population versus joining, duplicate callbacks and reconnects still need MP
  qualification; no new native player hook is needed for the first adapter.
  Released RTTI confirms primary slots `+0x24`, `+0x28`, `+0x2C` in LuaMission,
  LuaMissionMP and four instant/multiplayer mission subclasses. The wrappers
  check for functions and make protected calls; this confirms dispatch shape,
  not join/roster/authority semantics. The SP probe did not exercise membership.

## Focused qualification fixture

Keep the first live trace observational and bounded. Record native operation,
sequence, owning peer, captured handles/classes, before/after health and flags,
and actual creation/activation counts; compare with mission-side actions.

- Damage above zero, exactly zero and below zero; immunity; healing; repeated
  same-frame hits; delegated building damage; several attackers and removed owner.
- Recently damage a surviving player/AI pilot, then board inside the career
  timeout: boarding must not deliver a kill. Test delete/recycle/load separately.
- Board, hop out and eject with both player and AI. Include denied/no-survivor
  ejection and sniper occupant death while vehicle health remains positive.
- One shotgun discharge, sustained minigun, empty trigger, salvo/charge catch-up,
  partial/delayed salvos, retained beam/restart, launcher target failure, mine
  placement/detonation and deployable creation failure. Count activations
  separately from projectiles. Repeat with casing/presentation features OFF.
- Producer, rig and Armory real-item selection versus categories/Back; accepted
  order versus build completion; existing custom menu stubs must never escape.
- Two-peer replay/ownership checks, roster initialization/join/leave/reconnect,
  then VM close/reload with pending records. Static findings do not cover these.

## Implementation consequence

Keep the first milestone small: the EXU dispatcher, explicit mission-Update
pump and lifecycle tests. Target changes and command activation now have useful
native candidates for a subsequent producer fixture. Add each site to the
owning address catalog with ABI/build qualification before installation. Keep
unsupported semantics unavailable until measured rather than filling them
from input requests, raw pointers or career-stat inference.
