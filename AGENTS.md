# ExtraUtilities (EXU)

EXU is the reusable Lua/native runtime library for Battlezone 98 Redux (`GrizzlyOne95/ExtraUtilities`). It does not own global engine-patch policy or campaign-specific content. Keep this root file concise and load task-specific references only when needed.

## Ownership and required context

- Reusable C++ runtime features and thin Lua bindings belong here. Route global stock-game fixes, dangerous patch policy, and low-level hooks to **OpenShim**; campaign content and integration to **CR**; file/update primitives to **bzfile**.
- Cross-repository reading is encouraged when it prevents duplicate APIs or reverse engineering. Verify a sibling's `origin` and branch, and read its `AGENTS.md` before editing it.
- Before changing Lua behavior or adding Lua-facing APIs, read `Docs/BZR_LUA_AGENT_REFERENCE.md`.
- Before changing loading, paths, filesystem/process behavior, discovery, installers, deployment, packaging, or updates, read `Docs/BZR_PLATFORM_COMPATIBILITY.md` and cover Windows/GOG, Windows/Steam, Proton, and Wine as applicable.
- The two shared BZR documents must remain byte-identical across EXU, OpenShim, CR, and bzfile; update all four in one workstream if either changes.
- Before changing dependency direction, runtime ownership, patch placement, object lifetimes, or supported-build behavior, read `ARCHITECTURE.md`.

## Architecture guardrails

- Keep build-specific BZR addresses and signatures in `exu.json`, `profiles/`, and qualification tooling rather than feature code.
- Keep Ogre ABI/runtime assumptions under `src/Ogre/` or another clearly owned runtime layer.
- Prefer reusable C++ operations with thin Lua bindings. Native hooks and patches must fail closed when build or target assumptions fail.
- Distinguish process, runtime/Ogre, Lua-state, mission, and temporary-patch lifetimes.
- Current code and `ARCHITECTURE.md` take precedence over dated research under `Docs/Research/`.

## Work and validation

- Inspect `git status -sb` and the relevant diff before editing. Preserve unrelated work.
- Use one `agent/<short-description>` branch per workstream, normally from current `origin/main`; do not reuse finished branches or mix unrelated follow-ups.
- Start exploratory runtime or rendering work with the smallest testable API/prototype and targeted checks. Expand to full qualification, publication docs, or broad cleanup only after the direction is accepted or when explicitly requested.
- A normal Windows release build is `msbuild ExtraUtilities.sln /p:Configuration=Release /p:Platform=x86`. Host-side checks use `bash tests/linux/run.sh`. The committed Ogre headers under `third_party/ogre-1.10.0-bzr/include/` are authoritative; `_work/` is generated scratch.
- For a new Redux executable/build, follow `ARCHITECTURE.md` and use `tools/`; do not invent replacement signatures for failed or ambiguous targets.
- Stage only task-owned files. Do not blanket-stage, clean, restore, force-push, or rewrite shared history. Agents may commit and push coherent checkpoints; merges, releases/tags, Workshop publication, and live deployment require explicit user instruction.
- Do not commit secrets, credentials, player/save state, transient logs, crash dumps, build output, or generated scratch.

## Documentation and release

- Use `Docs/`; put useful dated investigations under `Docs/Research/`. Do not create a lowercase `docs/` tree.
- The combined OpenShim + EXU backlog lives in OpenShim; do not create a divergent EXU tracker.
- For Workshop/release work, read `Docs/WORKSHOP_RELEASE.md`. Keep release declarations synchronized across `src/About.h`, `include/ExtraUtils.h`, and `Definitions/ExtraUtils.lua`, and review the publication source files before upload.
