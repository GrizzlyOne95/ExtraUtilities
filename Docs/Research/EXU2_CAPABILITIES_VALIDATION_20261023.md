# BZR capability implementation and validation — 2026-10-23

## Scope and delivery

Implemented in EXU on `agent/exu2-capabilities`, based on current `origin/main`
`f24b720`: world-to-screen/frustum queries, buffered production observations,
native terrain placement checks, remote-player Steam64 lookup, and mission
console commands/variables. No BZCC loader or hooking infrastructure is imported.
Magnets are excluded because BZR has no magnet weapons. The API guide is
[EXU2_CAPABILITIES.md](../EXU2_CAPABILITIES.md).

The canonical OpenShim roadmap is updated in a separate documentation branch,
`agent/exu-capability-roadmap`, based on `origin/main` `187a3aec7`. No merge,
release, installed-game deployment, or Workshop publication is part of this work.
The normal GOG install and unrelated working copies were preserved.

## Native qualification

The released GOG Windows x86 executable reports BZR 2.2.301, SHA-256
`8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`.
Offline qualification reports `SUPPORTED_PROFILE_MATCH`. Four producer, three
identity and seven placement patterns are unique in executable sections and
matched the settled live process bytes before native hooks were enabled.
Identity also requires the existing FindNetPlayer anchor.

`exu.json`, `profiles/bzr_2.2.301.json` and their generated headers own the native
sites. Optional anchors are checked per feature, so an unavailable capability
does not disable unrelated EXU functionality. Production installs also compare
complete stolen instruction preimages and refuse foreign hooks. A partial
installation rolls back. No PDB data, decompiler output or game binary is added
to this repository.

Camera operations resolve the installed Ogre exports for current viewport,
camera matrices and point-frustum visibility. Native ABI assumptions stay under
`src/Ogre/`. Simulation-to-render conversion uses the existing shared helper.
Object inputs use the same OBJ76 simulation transform as stock GetPosition and
GetTransform; GameObject's cached integration position is not interchangeable.

Placement uses native class/model bounds and floor height, the rig's expanded
footprint and CanBuildHere validator. Power-requiring classes use native team
alliances and powerplant position/class accessors. It performs no object spawn
or build command. The console registry uses Lua-state storage and explicit
dispatch: BZR has no BZCC VarSys subsystem to wrap.

## Automated checks

Passed on the task source:

- Release x86 MSBuild of `ExtraUtilities.sln`.
- `tools/validate_hardening.py`: 470 top-level EXU functions, 550 including
  sub-APIs; API/version/shared-document/catalog/preimage/exception checks pass.
- Both generated-header `--check` operations.
- Lua 5.1 syntax checks of the API definitions and runtime smoke scripts.
- `tools/test_bzr_qualification.py`: 12 tests pass.
- All host C++ test executables through the Linux runner's `test_host_cpp` lane,
  including projection, render conversion, event overflow/order and Steam
  identity precision/provider rejection.
- The actual `ConsoleRegistry.cpp` built as a native Lua 5.1 module; command,
  variable, validation, callback failure/reentrancy and callback lifetime checks
  pass. The new lane is part of `tests/linux/run.sh`.
- `git diff --check`.

The full Linux script did not complete: this WSL environment has no native Node
runtime, and using Windows Node against its UNC source path failed package
resolution. Its C++ and native Lua lanes were then run separately and passed.
The complete Linux suite subsequently passed in
[GitHub CI](https://github.com/GrizzlyOne95/ExtraUtilities/actions/runs/40048035786)
for implementation checkpoint `52c720b`, with the required dependencies
provisioned. Pure host checks do not qualify the Windows DLL under Proton or Wine.

## Isolated Windows/GOG DX11 mission

The fixture ran outside the normal installation, with the source EXU DLL,
windowed DX11 and Enhanced rendering. BZRHarness serialized launches; shutdown
used Stop-BZRGame without force killing. The mission waited for read-only live
entry-byte verification before enabling production capture.

Observed checks:

- Console command/variable registration, invocation, types/bounds, error paths
  and cleanup passed in BZR's real Lua runtime.
- Handle, stock GetPosition vector and GetTransform matrix projected to equal
  coordinates/depth. Invalid handles and nonfinite positions returned nil.
  A point 100 units along the actual camera front projected to viewport centre,
  depth approximately 100 and visible=true. The corresponding behind-camera
  point returned visible=false.
- Placement returned a terrain-height-adjusted accepted site, blocked sites,
  outside-map rejection, invalid ODF and unsupported vehicle results. A gun
  tower returned power-required; adding an allied powerplant at the fixture site
  changed that result to buildable.
- Singleplayer/absent remote Steam identity returned nil, and an out-of-range
  network ID raised the documented argument error.
- Constructor `abspow` queue → cancellation → requeue → completion, factory
  `avtank` queue → completion, and armory `aprepa` queue → completion were
  observed. Constructor/factory completions supplied live built-object handles.
  Armory completion returned no handle, matching the native launch operation.

The final run (process 28684) passed the committed runtime observer: exactly
eight observations, contiguous
sequence numbers, per-producer order/ODF/kind, completed handles and no dropped
or pending events. It exited with game return code 0. Cancellation is distinct
from completion. The fixture uses
`avcnst`, deployed `avmuf` and deployed `avslf`; native production prerequisites
and geysers must be provided by the consuming test mission.

Runtime smoke scripts live under `tests/runtime/`. Private fixture maps,
settled-byte reports and runtime logs remain local scratch outside Git.

## Remaining qualification

Positive remote Steam identity requires a real Steam multiplayer session. Host
tests cover exact identity decoding; the singleplayer probe only verifies
absence/error behavior. Multiplayer ownership transfer, replica exclusion and
duplicate behavior still require two-peer runtime validation. The implementation
filters capture to native local authority and does not broadcast events.

DX9, Windows/Steam and Proton/Wine runtime behavior remain unqualified. DX9
could not initialize with the current headless display's available mode; no
display settings were changed to bypass that condition. Broader mission/view
cycling and repeated producer lifecycle stress also remain release checks.
Windows/GOG DX11 evidence does not establish those other lanes.
