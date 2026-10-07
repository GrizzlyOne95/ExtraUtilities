# Damage attribution for EXU runtime events

Static Ghidra follow-up, 2026-10-07. Supplements the
[producer map](RUNTIME_EVENT_PRODUCERS_GOG_20261006.md) and
[runtime-event plan](RUNTIME_SCRIPTING_EVENTS_20261006.md).
No new production callbacks, native patches or DLLs are implemented here.

## Evidence and scope

The selected Ghidra program remains the installed Windows x86 GOG Redux
2.2.301 executable, image base `0x00400000`, SHA-256
`8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`.
The imported hash and 47 selected entry ranges match the disk image. This
count includes earlier producer research; it is not 47 new hooks. Evidence
comes from released-image RTTI, factory paths, disassembly and field accesses.
Addresses are research anchors for this exact build, not qualified detours,
Steam signatures or proof of multiplayer authority. Raw decompiler output,
entry bytes and instrumentation remain in ignored private OpenShim scratch.

An eight-site read-only observer attached to owned PID 8804 on October 6.
The interrupted run closed during loading, before mission simulation and
fixture actions. It produced no attribution records or completed observer
summary. It is not a live pass. The mission was restored byte-for-byte,
temporary module removed and Ogre config restored. On continuation, a different
game session was running; the user requested that it be left untouched.

The earlier completed isdftest probe still supplies narrow Person health/fatal
evidence. It does not establish the new source/owner conclusions below live.

## Keep three attribution relationships separate

1. **Immediate damager:** native damage-record `+0x00`, a low-level object
   pointer. For an ordinary projectile this is the creator passed to initialization.
   A deployed emitter can be the damager instead of the original firing craft.
2. **Physical damage source:** record `+0x04`, another low-level object pointer.
   For direct projectile damage it is the projectile; for splash it is the
   explosion. It need not have a GameObject or Lua handle.
3. **Owned-object creator chain:** GameObject `+0x224` stores a Lua-compatible
   owner handle. This is separate from both record fields. Capture it as an
   ownership relationship, not an automatic replacement for the damager.

`0x00479F30` simply returns low-level object `+0x8C` (or null for null input).
It does not validate object lifetime or generate a handle. The earlier observer
resolved only that GameObject field, so a null captured source did not prove
the native source pointer was null. It could be an unhandled projectile or
explosion. Distinguish absent, recognized non-GameObject, unresolved and
successfully captured GameObject sources.

The low-level object `+0x88` identifies the native entity in the inspected
factory paths: ordnance category `0x33` in `0x00586FF0`, explosion category
`0x34` in `0x004CB7B0`. Both factories dispatch initialization through virtual
slot `+0x04`; they also have object-reuse paths. Released RTTI identifies the
entity family. Validate type and entity-to-object back-reference before reading
family fields. Copy ODF/type facts immediately rather than queueing either
native pointer. A source ODF does not identify a weapon slot; multiple weapons
can use the same ordnance.

## Direct projectiles and splash use the same record shape

| Producer | Record placement | Immediate damager | Physical source |
| --- | --- | --- | --- |
| Base Ordnance initialization `0x00584FE0` | Ordnance `+0x60` | Creator argument, a low-level object pointer | Ordnance's low-level object at `+0x14` |
| Explosion initialization `0x004C9AB0` | Explosion `+0x1C` | Creator argument, a low-level object pointer | Explosion's low-level object at `+0x0C` |
| Lua Damage helper `0x005CB1C0` | Shared temporary record | Null | Null |

Bullet initialization `0x00480340` calls the base Ordnance initializer.
Bullet hit `0x00480750` dispatches the victim's DistributedObject damage
virtual with the record at ordnance `+0x60`. Its selected impact-effect
explosion builds receive ordnance `+0xD8` as creator. Thus direct and splash
can identify the same emitter while using different physical sources. A
visual explosion with zero configured damage is not itself a damage event.

Explosion initialization can apply immediate radial damage before returning.
Inside the inner radius it passes its record; in the falloff band it copies
the record and scales the amount. Therefore capturing explosion origin only
on initializer return is too late for those immediate hits. An origin snapshot
at entry, associated with that initialization, must precede downstream damage
observations. An actual producer must also handle initialization failure and
reused native objects; addresses alone are insufficient instance identities.

The shared 16-byte damage record has:

| Offset | Static meaning |
| --- | --- |
| `+0x00`, `+0x04` | Immediate damager and physical source pointers |
| `+0x08` | Packed flags; low nibble is the producer's damage type code |
| flags bits 4, 5 | Initialization copies an owner flag test and DistributedObject remote status; these are origin hints, not player IDs or authority guarantees |
| flags bits 6 through 9 | Captured origin team, derived from source object's team at initialization |
| `+0x0C` | Incoming amount for this invocation; radial falloff may already have scaled it |

Do not publish names for the low-nibble codes until each mapping is qualified.
Do not infer human identity from team or these origin bits. The Lua Damage
helper fills null pointers, flags value 1 and the requested amount: this path
does not supply an attacker. Zero team bits in this synthetic record must not
be described as verified neutral-player kill credit.

Stock `SetDamageFlags` `0x004DC130` distinguishes null source, equal damager
and source (its collision branch), and distinct pointers (its shot branch).
It uses the source's current low-level team for hostility bookkeeping, whereas
fatal reporting paths extract team from the record's captured bits. Those are
different observations; neither alone proves player/object kill attribution.

## Two handle domains: do not mix them

Ordnance `+0xD8` stores the creator low-level pointer. Its `+0xDC` is populated
by `0x00439CC0`, which reads the first DWORD of that low-level object. It is
**not** the creator's Lua GameObject handle.

The distinction is corroborated by `0x00439CD0`: it compares a supplied ID with
the first DWORD of a supplied low-level pointer. BounceBomb simulation
`0x0047DF00` uses the `+0xDC`/`+0xD8` pair through this helper and expires its
lifetime when validation fails. This observation does not establish the same
owner-loss behavior for every projectile family.

Lua GameObject handles instead come from `0x00462380`, combining arena index
and serial. Resolution `0x004DA060` checks that serial. Lua SetOwner's native
setter `0x0046FC40` stores such a handle at complete GameObject `+0x224`;
GetOwner `0x004B0400` resolves it. Queued attribution should capture a validated
GameObject handle while the actor exists, separately from any private native
source-instance token. Never feed ordnance `+0xDC` to Lua handle resolution.

The existing
[owned-object research](https://github.com/GrizzlyOne95/Battlezone98Redux_Shim/blob/agent/native-event-callback-plan/reverse_engineering/owned_object_reveal_owner_handle_20260919.md)
already demonstrates a stock Splinter deployment boundary that severs the
owner chain. OpenShim has an optional enhancement that preserves it. EXU must
report the observed chain and its completeness rather than assuming that all
stock/custom deployables retain their original firing craft. A bounded chain
walk must stop on missing handles, serial mismatch, cycles or its depth limit.
An ownership chain is not proof that its final actor should receive kill credit.

## Applied damage and fatal attribution

Preserve incoming amount separately from HP lost. Handlers can reject or scale
it. The previous live Person probe confirmed healing, nonfatal HP loss despite
AL false, exactly zero without the fatal flag, and negative HP with it.

Health writer `0x004A76A0` takes the complete GameObject in ECX, writes HP at
`+0x204` and updates ratio `+0x200` when max HP `+0x208` is positive. It is
also used outside damage, so it is not a standalone OnDamage producer.
DistributedObject damage handlers take complete object `+0x18`; do not apply
that subtraction to the health writer.

Building damage can forward the same record to another object's handler.
Fatal effects can create explosions and additional nested damage. A simple
before/after measurement around every handler can therefore include nested
changes and double-count damage when forwarded invocations share health state.
Use per-thread invocation/parent IDs during qualification. Compare qualified
HP writes with the invocation and victim that produced them before choosing
the public applied-damage calculation. Do not claim an outer call's whole HP
delta is always its own direct contribution.

For OnKill, associate the class-specific committed fatal branch with its
current damage invocation, captured identities and source facts. Do not replace
this with the career sink's recent-damager/disappearance inference. Repeated
fatal calls, nested forwarding and already-fatal objects require deduplication.
Sniper occupant death remains a separate path and identity from vehicle HP
death. A generalized exact kill callback is still unqualified.

Network receive `0x00584620` can construct ordnance through the same factory,
then overwrite its network source/ordnance IDs and run catch-up simulation.
Initialization is therefore not proof of a new local weapon discharge, and
origin flags are not sufficient to establish authoritative multiplayer kills.
Two-peer qualification remains required.

## Minimal EXU payload direction

Keep the proposed dispatcher small. A future copied damage record should
contain victim handle, incoming amount, qualified applied HP change and separate
immediate-damager/source facts. Add origin actor/owner-chain facts only where
captured and qualified, with explicit absent/unresolved status. Source facts
can include copied kind/ODF and a mission-scoped instance token without a Lua
object handle. Preserve the relationship to a fatal invocation when available.

These are notification facts, not synchronous damage cancellation or replication.
OpenShim supplies observations through the existing hook owner; EXU owns the
queue, listener registry and Lua names. No API availability is changed here.

## Prepared fixture and remaining qualification

[damage_attribution_check.lua](../../tests/runtime/damage_attribution_check.lua)
is opt-in, syntax checked and **not yet executed in game**. It uses installed
ISDFC `ivscout`, `gatstb1` and `gianbaz` assets. Each shooter gets one round's
ammo; target weapons are removed. Cases cover direct plus splash, an explicitly
Lua-assigned owner, low-HP fatal damage and removal of a slower projectile's
shooter before expected impact. Request/result logs are not native proof; each
case needs an observed creation and impact correlated to its exact actors.
The assigned-owner case does not represent naturally deployed ordnance.

Copy beside isdftest and call New({delay = 15}):Update() through an existing
mission Update wrapper, preserving mission work. Call Stop() to remove only
exact handles created by the fixture. Native-created pilots/effects may remain
until mission teardown. Keep the fixture out of regular campaign startup and
saves. Do not instrument an unrelated active game session.

The pending guarded observer records source initialization, BulletHit, four
damage handlers and the HP writer. Required next checks: direct/splash identity,
owner loss/reuse, nested damage attribution, fatal deduplication, naturally
deployed ownership, immunity/remote rejection, sniper occupant credit, VM/mission
reload, and two-peer replication. Native-source instance lifetimes and full
producer coverage must be qualified before shipping queued Lua events.
