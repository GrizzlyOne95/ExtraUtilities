# NativeChunks (vendored from OpenShim)

Pure, engine-free skinned-mesh gib splitter and its chunk cache, vendored from
`GrizzlyOne95/BZR-OpenShim` at commit `72134279c` (origin/main).

**Keep these files verbatim** so later OpenShim fixes can be diffed and
re-applied. Local edits are listed below; anything not listed is unchanged.
The namespace (`BZROpenShim::NativeChunks`), all names, cache directory macros
and the `openshim_gib_flesh*` material names are kept as upstream (packs may
override those by file name).

## File mapping

| EXU (`src/Gibs/NativeChunks/`)     | OpenShim                                      |
|------------------------------------|-----------------------------------------------|
| `native_chunk_mesh.h`              | `include/native_chunk_mesh.h`                 |
| `native_chunk_mesh.cpp`            | `src/patches/native_chunk_mesh.cpp`           |
| `native_chunk_prune.h`             | `include/native_chunk_prune.h`                |
| `native_chunk_prune.cpp`           | `src/patches/native_chunk_prune.cpp`          |
| `native_chunk_cache.h`             | `include/native_chunk_cache.h`                |
| `native_chunk_cache.cpp`           | `src/patches/native_chunk_cache.cpp`          |
| `gib_flesh_texture.h`              | `include/gib_flesh_texture.h`                 |
| `gib_flesh_texture.cpp`            | `src/patches/gib_flesh_texture.cpp`           |
| `chunk_proxy_generic_meshes.inl`   | `src/patches/chunk_proxy_generic_meshes.inl`  |
| `tests/host/native_chunk_mesh_tests.cpp`  | `tests/native_chunk_mesh_tests.cpp`    |
| `tests/host/native_chunk_prune_tests.cpp` | `tests/native_chunk_prune_tests.cpp`   |
| `tests/host/test_check.h`          | `tests/test_check.h`                          |

## Local edits

- Every file: the EXU LGPL-3 header block and a one-line provenance comment
  were added above the upstream content (OpenShim files carried no header).
- Tests: the `#include "native_chunk_*.h"` / `gib_flesh_texture.h` lines now use
  the `Gibs/NativeChunks/` prefix, because the host harness only has
  `-I src`. Test bodies are unchanged.
- Harness (not vendored code): `tests/host/run_msvc.cmd` and
  `tests/linux/run.sh` compile `src/Gibs/NativeChunks/*.cpp` into any test
  named `native_chunk_*`. The optional `--gibs` / mesh-dump modes of the mesh
  test only run when arguments are passed, so the harness skips them.

No engine or Ogre code lives here. `src/Gibs/GibPayloads.cpp` is its only caller
(exu.gibs, Docs/Research/SKINNED_GIBS_PORT_20261013.md).
