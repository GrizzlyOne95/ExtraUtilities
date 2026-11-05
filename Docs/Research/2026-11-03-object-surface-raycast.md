# Read-only object surface ray prototype

`exu.RaycastObjectSurface(handle, from, to)` returns the first collision-polygon
point, unit normal facing the segment origin, and distance in metres, or nil.
This supplies the object-surface point absent from `GetReticleHit`'s object case.
It does not fire a shot, change damage, or install engine hooks.

The reader uses the existing qualified Redux 2.2.301 obj76/GEO layout and live
serial-checked arena handles. Part frames are current on every query, including
parent/child composition. `BZR::MAT_3D` contains nine floats, four bytes of
padding, and three doubles; its position at object +0x48 matches the OpenShim
reader. LOD group 11, native mesh-bearing kinds and hidden flags filter parts.
Foreign geometry reads have SEH boundaries; malformed/oversized geometry and
unsupported executables return nil. Geometry cache lifetime is the Lua state,
reset at initialization and close, bounded to 4096 entries and 2 million triangles.

The BVH math and its tests are adapted from BZR OpenShim's MIT-licensed
`include/mesh_impact_geometry.h` and `tests/mesh_impact_geometry_tests.cpp`.
The original MIT notice is included in both adapted files. The native walker
matches `src/patches/mesh_impact.cpp`; it is a read-only API in EXU rather than a
stock-game global collision patch.

Validation completed: Windows Release x86 build; native x86 geometry tests for
front/back hits, misses, gaps, BVH versus brute force, closest points, and rotated
and scaled part frames; `tools/validate_hardening.py` API parity, profile/address,
SEH and documentation checks. Native gameplay qualification has not been run.

Limitations: native collision polygons can differ from Ogre render surfaces,
skeletal render-only deformation, and alpha-cutout holes. Cache changes are
detected with vertex count, face pointer and a sampled position checksum, not a
full collision topology hash. Callers should bound query frequency and candidate
objects. This prototype is not evidence of Steam, Proton or Wine live acceptance.

The ISDFC integration lives outside this reusable library: a camera segment,
optional muzzle-clearance segment, terrain heightfield bracketing, closest-object
selection, and one local additive red billboard at a valid hit. It hides on sky,
death, vehicle entry, third-person view and weapon handling; it has no ammo cost.
