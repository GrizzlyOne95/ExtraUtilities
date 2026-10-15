# Optional transport statistics bridge

`exu.GetNetworkTrafficStats()` returns a copied local-client aggregate snapshot
from OpenShimGetNetworkTrafficSnapshot, or nil outside network play/when the
optional provider is unavailable. Based on EXU main `fadf5f1`; no overlapping
open EXU PR existed when the workstream branch was created.

The bridge resolves through the existing live-provider winmm discovery path,
introduces no load/link dependency, validates the v1 record size and version,
and copies values to Lua immediately. It retains no native payload/endpoint or
game pointers. The 88-byte POD is mirrored intentionally rather than coupling
EXU's build to a sibling checkout. Existing Lua bindings provide the exception
boundary. A missing provider/export returns nil and can become available later.

Fields and units are documented in Definitions/ExtraUtils.lua. Rates are BZR UDP
payload B/s and datagrams/s over the native two-second rolling window, across
this client's peers. Tx is accepted submission, including pending sends, not a
delivery guarantee. Rx is successful observed receive completion. Native UDP
transport ages use wall-clock milliseconds. Route is recent passive evidence,
not a requested route setting or a peer-specific engine connection state.
No addresses, identities or payloads are returned. coverageLimited warns of
bounded observer overflow; unobserved completion methods can undercount.

Release x86 and hardening/API parity pass. Windows/GOG local-server native
evidence is recorded in the cross-repository handoff. Windows Steam and
Proton/Wine runtime remain unverified; the optional Win32 ABI degrades to nil
without affecting co-op admission or existing stats. No release is performed.
