# ExtraUtilities

This repo is part of the local Battlezone workspace opened via
`%USERPROFILE%\Documents\Battlezone98Redux_Shim.code-workspace`.

## Workspace Layout
- Sibling repos normally live under `%USERPROFILE%\Documents\GIT\...`.
- The primary local game install is typically `%USERPROFILE%\Documents\Battlezone 98 Redux`.
- Prefer the workspace file and these conventions over hardcoded profile-specific paths.

## Local Role
- Script extender and native utility library for Battlezone 98 Redux.

## BZR Bundle Repository Map
- Treat the following four repositories as the core **BZR bundle**. They are separate repositories with separate ownership boundaries, but they are expected to be available together for cross-reference during Battlezone 98 Redux work.
- On the primary development PC, look for local sibling checkouts under `%USERPROFILE%\Documents\GIT` before searching GitHub. Prefer local source for fast code/reference lookup when the checkout is present and current; fall back to GitHub when a sibling repo is unavailable locally or when remote state must be verified.
- Do not assume the folder name from memory. Verify the checkout and its remote before editing; some repositories may use historical local directory names.
- **EXU / ExtraUtilities** = `GrizzlyOne95/ExtraUtilities` (this repo): script extender and native utility library, especially reusable Lua-facing APIs and higher-level runtime features. When a task or document says **EXU**, it means this repository.
- **OpenShim** = `GrizzlyOne95/Battlezone98Redux_Shim`: native `winmm.dll` shim, engine hooks/patches, reverse engineering, SDK/native integration, and low-level Redux behavior.
- **Campaign Reimagined / CR** = `GrizzlyOne95/Battlezone98Redux_CampaignReimagined`: campaign/addon content, Lua consumers, materials/shaders, packaging, integration examples, and end-user validation. Its local Git checkout may be useful for reference, but its own `AGENTS.md` defines the authoritative source/edit and promotion paths.
- **bzfile** = `GrizzlyOne95/bzfile`: Lua-accessible file I/O support used by Battlezone scripts and addon-side systems.
- Cross-repo reading is encouraged when it avoids duplicating an API, misunderstanding ownership, or re-reverse-engineering something already solved elsewhere. Cross-repo editing is not automatic: modify another bundle repo only when the task actually requires a coordinated change and after reading that repo's own `AGENTS.md`.

### BZR Reference and Tooling Repositories
These repositories are especially useful for research and implementation reference, but are **not default edit targets** for new OpenShim/EXU/CR features. When working locally, first look for them under `%USERPROFILE%\Documents\GIT\<repository-name>` and verify the checkout/remote before relying on it.

- `GrizzlyOne95/BZ98RBlenderToolKit` — Redux asset, mesh/skeleton, animation, and Blender pipeline reference.
- `GrizzlyOne95/Battlezone98Redux_DedicatedServer` — dedicated-server behavior and multiplayer/server reference.
- `GrizzlyOne95/BZ1-GameWatcher` — Battlezone 1 game/server watching and related integration reference.
- `GrizzlyOne95/BZ1_Source` — Battlezone 1 source reference for legacy engine/game behavior.
- `GrizzlyOne95/BZ2_Source` — Battlezone II source reference for related engine/game concepts.
- `GrizzlyOne95/Battlezone_LobbyMonitor` — lobby/network monitoring reference.
- `GrizzlyOne95/BZNTools` — BZN/map tooling and format reference.
- `GrizzlyOne95/Battlezone98Redux_AudioTool` — Redux audio tooling/format reference.
- `GrizzlyOne95/Battlezone98Redux_WorldBuilder` — world/map-building tooling reference.
- `GrizzlyOne95/Battlezone98Redux_ZFSSpecialist` — ZFS/archive/content-format reference.
- Use these repos to answer questions, compare implementations, recover formats/behavior, and avoid duplicated investigation. Do not include them in a feature's change set merely because they were consulted.

## Git Checkpoint and Publishing Workflow
- At the start of any task that may modify the repo, inspect `git status -sb` and the relevant diff before editing. Treat pre-existing local changes as user-owned unless they are clearly part of the active task.
- Do not work directly on `main`, `master`, or another protected/default branch for normal feature, fix, RE, or documentation work. Create or use a task branch, normally named `agent/<short-description>`.
- Agents are pre-authorized to create coherent checkpoint commits and push task-owned changes to the current task branch without asking for permission after every checkpoint.
- Create checkpoints at meaningful engineering boundaries: after a coherent implementation slice, an important RE discovery, a known-good build/test state, or before beginning a riskier follow-on change. Do not create a commit for every trivial edit.
- Prefer checkpoints that build or pass the most relevant available validation. If valuable investigative work must be preserved before validation, a clearly labeled `WIP:` commit is acceptable on a task branch; keep unvalidated WIP out of the default branch.
- Push the task branch after meaningful checkpoints and before ending a substantial work session so the remote branch serves as a durable recovery point.
- Stage only task-owned files. On a mixed or pre-dirty worktree, do not use `git add -A`, `git add .`, blanket restore/clean commands, or other operations that can silently absorb or destroy unrelated workstation changes.
- If task changes overlap pre-existing user changes, preserve both where safely possible. Ask for direction only when the overlap cannot be isolated without risking user work.
- Use concise, descriptive commit subjects. For reverse-engineering assumptions, native hooks, offsets, ABI/lifetime findings, or in-game fixes, prefer a commit body that records the important evidence, assumptions, and validation performed.
- Documentation, roadmap, changelog, or release-note updates that are part of the same logical task should normally travel with the implementation checkpoints rather than being left only in the workstation tree.
- Do not amend, rebase, reset, rewrite, delete, or force-push shared history unless explicitly requested. Never use a force push as routine checkpoint behavior.
- Do not merge pull requests, push task work directly to the protected/default branch, create release tags/releases, publish Workshop content, or perform other external release/deployment actions unless the user explicitly requests that action.
- Do not commit secrets, machine-specific credentials, transient build output, runtime deployment copies, crash dumps, scratch RE artifacts, or generated files that the repository does not intentionally track.

## Cross-Repo Pointers
- Addon-side consumers and Lua usage live primarily in the deployed campaign addon under the workspace game install, usually `%USERPROFILE%\Documents\Battlezone 98 Redux\addon\campaignReimagined`.
- Other native support repos in this workspace include `%USERPROFILE%\Documents\GIT\Battlezone98Redux_Shim`, `%USERPROFILE%\Documents\GIT\BZR-Subtitles`, and `%USERPROFILE%\Documents\GIT\bzfile`.

Open `%USERPROFILE%\Documents\Battlezone98Redux_Shim.code-workspace` when a task may span repos.
