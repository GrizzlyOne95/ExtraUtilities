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
require this bridge. The first trial excludes native flashes and is GOG SP
cannon only, explicitly requested at launch with
`OPENSHIM_TRIAL_ODF_RECOIL=1`; normal activation remains closed.

Release x86 EXU build, hardening validation and HardeningSmoke pass. No new
native hook/signature or Lua binding is added here. Live world/cockpit firing,
controller coexistence, reload/teardown and platform acceptance remain open.
