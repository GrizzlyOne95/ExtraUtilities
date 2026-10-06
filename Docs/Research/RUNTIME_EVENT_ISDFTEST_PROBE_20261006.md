# Runtime event producer observations in isdftest

2026-10-06, Windows/GOG Redux 2.2.301. Follow-up to
[the static producer map](RUNTIME_EVENT_PRODUCERS_GOG_20261006.md).
EXU's dispatcher and Lua callback API are still unimplemented. These are
observations of stock producer behavior under temporary instrumentation,
not qualification of a production detour, multiplayer authority or Lua delivery.

## Method and evidence

The user authorized ISDFC's `isdftest.bzn` for prototyping. The opt-in
[stock-engine fixture](../../tests/runtime/event_producer_check.lua) ran from
mission Update. It created its own actors, labeled each action and kept the
player's possession/input unchanged. Only a temporary Update wrapper and the
fixture were added to the installed mission for each run.

The executable SHA-256 remained
`8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`.
`BZRHarness.ps1` serialized launches, forced windowed mode and stopped each
owned PID gracefully. Frida 17.7.3 observed nine entries: four damage handlers,
Cannon simulation, the ordnance factory, Person collision/boarding, pilot
creation and pilot kill. Disk PE identity, live module identity and 32 entry
bytes at every observed site were checked before instrumentation, again after
settling, and again after observer removal. No engine/Lua functions were called
from the observer, and no arguments, returns or gameplay data were changed by it.

| Run | PID | Result |
| --- | --- | --- |
| Initial smoke | 39488 | Nine guards passed; 32 ambient records, no observer read errors/drops. Fixture stopped before creation because it used an unavailable terrain API. Not a fixture pass. |
| Corrected fixture | 35612 | Nine guards passed; 71 records, no observer errors/drops. Damage, two full blasts, hop-out and pilot kill observed. The shotgun actor disappeared before the partial/empty cases, so those cases were inconclusive. |
| Isolated ammo cases | 52212 | Nine guards passed; 169 records, no observer errors/drops. Fresh shotgun actors isolated full, partial and empty ammo. All labeled fixture actions reached completion. Boarding still did not complete. |

After every run, native entry bytes were restored, `isdftest.lua` was restored
byte-for-byte, the temporary module was removed and the original Ogre config
was restored. Installed EXU/OpenShim DLLs and feature configuration were not
replaced. Private traces, entry bytes and logs remain in ignored OpenShim
scratch under `reverse_engineering/workshop/event_callbacks_20261006/`.

## Damage: confirmed handler behavior

The isolated run's `ispilo` instances started with 100 HP:

| Script action | Observed HP | Native result/fatal flags |
| --- | --- | --- |
| `Damage(h, 5)` | 100 to 95 | AL 0; no fatal flag change. |
| `Damage(h, -3)` | 95 to 98 | AL 0; no fatal flag change. |
| `Damage(h, 100)` | 100 to 0 | AL 0; no fatal flag change in that invocation. |
| `Damage(h, 101)` | 100 to -1 | AL 1; Person's `0x01000000` flag added. |

This confirms the static warning: false does not mean no health change, and
exactly zero does not take this Person fatal branch. Negative incoming amount
can heal. Ambient traffic also included zero-amount damage calls with no HP
change. Preserve requested amount and applied health delta as separate facts;
define filtering for damage attempts, zero changes and healing explicitly.
Do not substitute a handler's boolean result for applied damage.

The scripted damage records had no owner/source objects. They cannot establish
attacker attribution. The existing career producer collapses damager/source
into one handle, preferring the damager pointer and using source only when that
pointer is null. It does not fall back after a nonnull damager fails resolution.
Exact EXU attribution should preserve qualified owner/source facts separately,
including unknown values, rather than invent a killer from that fallback.

## Shotgun: confirmed emission counts

The final run used fresh `isdoomx` actors with only `gdbshot` selected. Its
ordnance was `fosg`. Native records captured the live weapon class's salvo
count 12 and interval 0, the remaining count before each emission, the owner
handle and the returned factory result.

| Case | Native emissions | Remaining-count sequence |
| --- | --- | --- |
| Full ammo, first blast | 12 successful creations | 12 down through 1. |
| Full ammo, second blast | 12 successful creations | 12 down through 1. |
| Five ammo | 1 successful creation | 12 at the first emission; the remaining salvo was not emitted. |
| Zero ammo | 0 | No successful creation for that actor. |

Full-salvo emissions occurred within the same simulation invocation. The
partial salvo establishes why configured pellet count cannot stand for actual
creation count. The empty trigger establishes why a request alone cannot stand
for a fire event. One accepted blast should produce one `OnWeaponFired`, with
separate projectile records; the first successful emission is a useful Cannon
candidate. Delayed salvos, catch-up, several hardpoints, charge weapons, beams,
deployables and network replay still need their own qualification.

## Pilot transitions: narrow positive observations

`HopOut` reached the shared creation helper and returned a new `ispilo` handle
while the `ivscout` retained 2000 HP. The helper is reached after pilot state
has already been cleared: capture the outgoing occupant/cause at the parent
exit path, not solely at this creation helper.

`KillPilot` changed a different craft's pilot-class pointer from present to
null while vehicle HP stayed at 2000; no surviving Person was created by that
operation. This confirms occupant loss is distinct from HP death of the craft.
It does not qualify sniper hit detection, killer attribution or ejection.

In the corrected run, a subsequently created pilot reused the former shotgun
actor's arena address with a different handle serial. Queued identity must use
captured handles and mission generation; resolving a deferred native address
could identify a different object even within the same mission.

The injured AI pilot remained alive at 95 HP and did not finish boarding, even
after the final fixture placed it in contact with the empty craft. Therefore
the static `0x201` retirement/false-kill counterexample remains unverified live.
No kill inference or boarding callback was qualified by this request. Diagnose
the failed transition before advertising `OnPilotEnter` availability.

## Fixture use and next boundary

Copy `event_producer_check.lua` beside the test mission and opt in explicitly:

```lua
local eventCheck = require("event_producer_check").New({ delay = 15 })
-- Inside the existing mission Update, after normal mission work:
eventCheck:Update()
-- eventCheck:Stop() ends the fixture and removes its directly created objects.
```

The fixture requires ISDFC's `ispilo`, `ivscout`, `isdoomx` and `gdbshot` assets.
It exercises real stock actions and logs requests/results; it does not emulate
the proposed events or infer missing native notifications. Native-created exit
pilots may remain until mission teardown. Keep this opt-in test out of saves
and regular campaign startup.

Next, implement and test the small EXU copied queue/listener/pump independently
of native sites. Add the first qualified producer through its existing hook
owner, then compare one-fire-versus-pellet counts and mission-Update latency.
Remaining lanes include boarding, ejection/sniping, delayed/charge/beam fire,
cosmetics disabled, VM reload and two-peer multiplayer. Steam/Proton/Wine are
unverified by this Windows/GOG probe.
