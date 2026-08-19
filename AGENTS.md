# ExtraUtilities

This repo is part of the local Battlezone workspace opened via
`%USERPROFILE%\Documents\Battlezone98Redux_Shim.code-workspace`.

## Workspace Layout
- Sibling repos normally live under `%USERPROFILE%\Documents\GIT\...`.
- The primary local game install is typically `%USERPROFILE%\Documents\Battlezone 98 Redux`.
- Prefer the workspace file and these conventions over hardcoded profile-specific paths.

## Local Role
- Script extender and native utility library for Battlezone 98 Redux.

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
