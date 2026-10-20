# OpenShim ODF recoil companion (experimental)

The `agent/odf-recoil-trial` branches in EXU and OpenShim add a small optional
native bridge for ship-authored `recoilAnimation1..5`. OpenShim owns accepted
shots, slot configuration and simulation timing. EXU owns world/cockpit entity
resolution and guarded AnimationState operations, reusing its existing
animation API/FPS-controller infrastructure. The legacy Lua controllers are
unchanged; a mod must disable its old recoil profile for a craft before handing
that craft to the ODF controller.

`EXU_OdfRecoilAnimation` uses C linkage / cdecl with fixed primitive arguments:
`uint32_t handle, const char* clip, int cockpit, int mode, float time,
float* length`. Return one means success. Mode zero queries an idle named clip,
one seeks/enables it and two restores the disabled clip's previous loop,
weight and time. `cockpit` is zero for the world entity and one for the cockpit.
Calls run on one engine thread and never enter Lua. An enabled clip already
owned by another controller is refused. The optional length output is clip
seconds. Missing targets/build support return zero.

Entity/state snapshots are re-resolved for every operation. Replacement drops
the old record without dereferencing an old entity. Snapshots are bounded to
256 and restored/reset with EXU's mission state. OpenShim resolves the export
while holding a temporary module reference; it never retains EXU function
pointers across mission unload. Loading EXU is still the mod's responsibility.

The matched OpenShim branch documents ODF examples, priority and the avtank /
gatstab node-slide fixture in `Docs/ODF_RECOIL_TRIAL.md`. Node slides do not
require this bridge. OpenShim Settings > Video > **Muzzle Recoil** (INI
`[SinglePlayer] MeshRecoil`) is the master switch for this authored path.
**Muzzle Flashes** (`MuzzleFlash`) gates ODF mesh flashes and companion native
`muzzleRender` smoke/light (OpenShim `Docs/ODF_MUZZLE_FLASH.md`). Both default
OFF, require restart and remain experimental exact-GOG/SP cannon paths. ODF
configuration and matching assets are still required; no generic stock effects
are synthesized. Old developer environment flags no longer bypass an OFF
master setting. Legacy `flashName`/broader activation remains excluded.

Release x86 EXU build, hardening validation and HardeningSmoke pass. No new
native hook/signature or Lua binding is added here. Live world/cockpit firing,
controller coexistence, reload/teardown and platform acceptance remain open.

## Mesh-flash companion

`EXU_SpawnOdfFlash(const BzrOdfFlashBridge::Spec*)` copies a versioned Win32
request containing owner handle, resource names, simulation-space pose, lifetime,
start/end scale, roll and a nonzero caller lifetime cookie. It returns an EXU pool ID, or zero when unavailable.
`EXU_CancelOdfFlash(uint32_t, uint64_t cookie)` cancels only the matching ID/caller cookie; already expired/recycled
IDs are harmless. The bridge uses the same non-Lua spawn operation as the Lua
API, with the existing render-space conversion, owner following, frame listener
and scene/Lua shutdown handling. No new engine hook is added. The primitive ABI
is documented in `include/odf_flash_bridge.h` (copied unchanged in OpenShim).

The cookie prevents stale cleanup from cancelling a reused ID after EXU reload,
and refuses cancellation of Lua-owned flashes (cookie zero). Windows builds,
OpenShim 120/120 host checks and EXU hardening smoke pass. On 2026-10-20 the
matched OpenShim/EXU builds passed a windowed GOG/DX11 avtank/gatstab smoke
test using weapon ODF mesh/material/duration/scale/offset fields and no Lua
flash producer: 210 flashes, zero failures/orphans, zero live effects at shutdown.
Screenshots show the flash while the OpenShim node slide runs. All temporary
DLL/resource/mission/weapon overrides were restored. Authored clips and broader
lifecycle/platform acceptance remain unqualified; broader activation stays closed.
