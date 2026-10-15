# Co-op HUD runtime controls — 15 October 2026

Workstream: `agent/coop-hud-runtime-20261015`, based on current EXU main
`ba7433d`. No open PR overlapped Multiplayer/KillMessages when inspected.
Campaign policy belongs in CR; the following reusable operations belong in EXU.

## APIs

- `exu.SetShowKillMessages(bool) -> applied`: local, mission-scoped suppression
  of stock multiplayer death, destruction, snipe and ejection HUD notices.
  `GetShowKillMessages()` reports the effective state. Existing custom team-name
  replacement remains separate. Ordinary chat and mission text are unaffected.
- Existing `SetShowScoreboard(false)` hides the stock overlay. CR applies it
  once after network mission setup, so the stock M toggle keeps working.
- `exu.GetPlayerNetworkStats(netId) -> table|nil`: a copied read-only snapshot
  with id, team, name, kills, deaths and lives. Unset scalar slots use the stock
  row's zero default. Remote rows also have pingMs and lossPercent. Local ping
  and loss are absent, since the stock row has no remote-link measurement for
  itself. Missing players, non-network contexts and unsupported targets return
  nil. The id is the CreatePlayer/Receive network id, not a team number.

The API reads the same NetPlayer slots/readers used by the stock scoreboard;
no scoring or transport hook is required. It does not display, refresh, sort or
mutate that overlay, and retains no player pointers. Consumers can render the
snapshot with the existing EXU overlay/text APIs, preferably at a modest refresh
rate such as once per second. `lives` is the stock multiplayer counter, not CR's
separate respawn-service budget. `lossPercent` is the native ten-packet history
indicator, not a measured long-window loss rate. Ping follows the stock 9999 ms
clamp. Missing network samples can initially show the same defaults as stock.

## Released-build evidence and guards

The GOG 2.2.301 executable SHA-256 is
`8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`.
Rizin instruction inspection corroborates the existing EXU KillMessages hook's
formatter, the native scoreboard constructor's data slots (15 kills, 16 deaths,
19 lives), player lookup/name reader, average-ping reader and loss indicator.
No leaked PDB offsets or decompiler output are included in this change.

All build-specific sites/signatures are in exu.json, with optional feature
identity anchors in the generated build profile. The stats API qualifies its
six identities plus RuntimeGate, uses bounded immediate copies with SEH/C++
barriers, validates id/team and bounds the native ping-sample iteration. Native
faults return nil. The notice switch qualifies the full formatter identity before
installing a one-byte RET at its entry; the function only formats local notices,
while score recording is upstream. BasicPatch guards the preimage/restoration,
and mission reset clears suppression even if a consumer pins the DLL.

## Validation checkpoint

Release x86 DLL builds. Hardening/API parity and both generated-header checks
pass. All 162 catalog signatures match the shipped GOG image; 12 qualification
regressions pass. Private raw evidence remains under
`C:\BZRCoop\recovery-20261015`; no runtime output is committed.

A live two-client native local-server prototype shows scoreboard and notices
hidden on both peers, and stats queries returning matching ids/names/teams,
native lives and 3 ms/0% remote link samples while the overlay remains hidden.
The initial reader exposed absent untouched kill/death slots; final source now
uses the same zero fallback as the native row. A small presentation trace observed
ordinary mission text while notices were disabled, then an enemy ship-destruction
notice after re-enabling the switch. Only the presentation function was monitored;
there was no scoring hook or instrumentation of exception dispatch.

Final uninstrumented `recovery1015-coop-hud-qualified` passes seven steps and
15 checks on two DX11 GOG clients using the configured native local server.
Both peers start with notices and scoreboard hidden. Physical M reveals the
scoreboard, it stays visible across campaign updates, and M hides it again.
Rows match real peer ids, names and teams; absent players return nil; queries
leave the scoreboard hidden. A controlled guest death advances deaths from 0
to 1 and lives from 999 to 998 on both peers while kills stay 0 and both HUD
switches remain off. This also checks the released row's kill/death ordering.
All clients survive final validation, with clean Lua, Ogre and subtitle logs.
The test uses explicitly labelled player/objective healing and is not a story
completion test. Clients and the loopback server were stopped gracefully.

CR owns the default-off policy and reusable live fixture in draft CR #214.
No custom stats overlay is enabled by this API. Windows Steam, Proton and Wine
runtime acceptance is not implied by a GOG local test. Optional identity failure
disables the feature rather than changing addresses heuristically.
