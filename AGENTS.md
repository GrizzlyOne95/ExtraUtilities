# ExtraUtilities (EXU)

Script extender and native utility library for Battlezone 98 Redux. **EXU means `GrizzlyOne95/ExtraUtilities`.** This repository owns reusable Lua/native runtime features rather than global engine-patch policy or campaign-specific content.

## Repository role

- **EXU / ExtraUtilities** — reusable native and Lua-facing runtime features.
- **OpenShim / Battlezone98Redux_Shim** — global native patch layer, engine fixes, low-level hooks, reverse engineering, and persistent player-facing engine policy.
- **Campaign Reimagined** — campaign/addon content, Lua consumers, assets, packaging, and end-user mission integration.
- **bzfile** — Lua-accessible file I/O and related deployment/update support.

Cross-repository reading is encouraged when it prevents duplicate APIs or repeated reverse engineering. Do not assume a local sibling checkout is authoritative merely because it exists; verify the repository and branch before using it as implementation evidence.

## Shared BZR references

Before changing BZR Lua behavior or adding Lua-facing native APIs, read `Docs/BZR_LUA_AGENT_REFERENCE.md`. It is mirrored across the core BZR repositories and should remain byte-identical.

Before changing native loading, paths, filesystem behavior, process launch, module/resource discovery, installers, deployment, packaging, or update behavior, read `Docs/BZR_PLATFORM_COMPATIBILITY.md`. Treat Windows/GOG, Windows/Steam, Linux/Steam via Proton, and Linux/GOG through a compatible Wine/Proton prefix as the maintained runtime matrix.

## Architecture and ownership

Read `ARCHITECTURE.md` before changing dependency direction, runtime ownership, patch placement, object lifetimes, or supported-build behavior.

Use these rules:

- Keep build-specific BZR addresses and signatures in `exu.json`, `profiles/`, and build-validation tooling rather than scattering raw addresses through feature code.
- Keep Ogre ABI/runtime assumptions under `src/Ogre/` or another clearly owned runtime layer.
- Prefer reusable C++ feature/runtime operations with thin Lua bindings.
- Native hooks and patches must fail closed when build or target assumptions are not satisfied.
- Distinguish process lifetime, runtime/Ogre lifetime, Lua-state lifetime, mission lifetime, and temporary patch lifetime.
- OpenShim should own global stock-game fixes and dangerous native patch policy; EXU should expose safe Lua/native APIs and mission-scoped controls. The dated ownership analysis is preserved at `Docs/Research/SHIM_EXU_OWNERSHIP_STRATEGY_20260707.md`; current code and `ARCHITECTURE.md` take precedence where implementation has moved on.

## Build and validation

A normal EXU build uses the committed Ogre header subset under `third_party/ogre-1.10.0-bzr/include/`; no separate Ogre header checkout is required.

Windows release build:

```powershell
msbuild ExtraUtilities.sln /p:Configuration=Release /p:Platform=x86
```

Linux/host-side checks:

```bash
bash tests/linux/run.sh
```

For a new Redux executable/build, follow the qualification workflow in `ARCHITECTURE.md` and use the tools under `tools/`. Do not invent replacement signatures for failed or ambiguous qualification targets.

The `third_party/ogre-1.10.0-bzr/_work/` tree is generated scratch used only by optional Ogre rebuild/ABI-comparison tooling; it is not an EXU source dependency.

## Git workflow

- Inspect repository/branch state before editing and preserve unrelated work.
- Normal development belongs on a task branch, usually `agent/<short-description>`, rather than directly on protected `main`.
- Commit coherent task-owned checkpoints and stage only task-owned files.
- Do not rewrite shared history or force-push unless explicitly requested.
- PR merges, releases/tags, Workshop publication, and live deployment require explicit user instruction.
- Do not commit secrets, credentials, local player/save state, transient game logs, crash dumps, build output, or generated scratch artifacts.

## Documentation placement

Use `Docs/` as the canonical documentation directory. Put dated investigation material that remains useful as historical evidence under `Docs/Research/`. Do not create a second lowercase `docs/` tree.

The combined OpenShim + EXU feature backlog is maintained in the OpenShim roadmap. Do not reintroduce a separate EXU feature tracker whose status can diverge from it.

## Release/publication hygiene

- Keep release version declarations synchronized across `src/About.h`, `include/ExtraUtils.h`, and `Definitions/ExtraUtils.lua`.
- Do not embed a version number in static Workshop tags; it becomes stale independently of the DLL release identity.
- Prefer the shared EXU Workshop/release installation over private per-mod DLL copies.
- `workshop_description.txt`, `workshop_changenote.txt`, and `Workshop/ExtraUtilities.ini` are publication source files. Review them before any Workshop upload.
- See `Docs/WORKSHOP_RELEASE.md` for the repository's Workshop publication notes.
