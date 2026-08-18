# Extra Utilities Architecture

This document defines the dependency and ownership rules for Extra Utilities (EXU). It is intentionally small: the goal is to make future maintenance and BZR build updates predictable without forcing a large rewrite of working code.

## Dependency direction

```text
Battlezone 98 Redux / Ogre
          |
          v
Runtime integration
          |
          v
Feature logic
          |
          v
Lua / native bindings
```

Higher layers may depend on lower layers. Lower layers should not depend on Lua or other presentation APIs.

## Ownership

### BZR runtime knowledge

Version-specific executable knowledge belongs in the address catalog and build-profile tooling:

- `exu.json` is the source catalog for known BZR addresses, types, descriptions, and IDA-style signatures.
- `profiles/` describes which catalog entries qualify a supported executable build.
- `src/Util/BuildValidation.h` is the in-process fail-closed gate used before native patches activate.
- `tools/qualify_bzr_build.py` is the offline/new-build qualification utility.

Do not add new raw BZR addresses or signatures inside feature code when they can be represented in the catalog/profile layer.

### Ogre runtime knowledge

Ogre-specific ABI and runtime assumptions belong under `src/Ogre/` or a future dedicated runtime layer. Feature code should consume those helpers rather than duplicating Ogre offsets or signatures.

### Features

Feature code implements EXU behavior. New feature logic should be written so it can be called independently of Lua when practical.

### Lua bindings

Lua bindings parse Lua arguments, call EXU feature/runtime operations, and translate results back to Lua. New code should avoid mixing substantial game-memory logic directly into Lua stack-manipulation functions when a reusable C++ operation is practical.

### Patches and hooks

Executable modification belongs in the patching layer (`BasicPatch`, `InlinePatch`, `Hook`, and `src/Patches/`). Patches must fail closed when their expected target/build assumptions are not satisfied.

## Lifetimes

EXU code should distinguish these lifetimes explicitly:

1. **Process lifetime** — the loaded EXU DLL and stable process-wide facilities.
2. **Runtime lifetime** — BZR/Ogre objects that may appear and disappear while the process remains alive.
3. **Lua-state lifetime** — callbacks/references owned by one `lua_State` generation.
4. **Mission lifetime** — state that is only valid for one mission/session.
5. **Temporary patch lifetime** — reversible changes used only around one operation.

Do not assume that module initialization, Lua initialization, mission loading, and process startup are equivalent events.

## Supported-build workflow

When a new BZR executable appears:

1. Run `python tools/qualify_bzr_build.py <path-to-bzr.exe> --write-report`.
2. Review matched, relocated, missing, and ambiguous signatures.
3. Reverse-engineer only targets that failed qualification; never invent replacement signatures.
4. Add or update a build profile only after the executable and critical targets have been validated.
5. Regenerate the runtime profile header, run CI, and perform an in-game smoke test.

A new executable should remain unsupported until this process completes. Failing closed is preferable to applying a native patch to an unqualified build.
