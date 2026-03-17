# Unit VO Hook Notes

Date: 2026-03-16

## Scope

Current EXU-side implementation for stock unit voice throttling, queue
compaction, and alternate-line selection.

## Native Path Used

- Live GOG executable was analyzed with `pyghidra` against the shipped EXE, not
  the mismatched PDB.
- The current hook no longer waits for bark playback creation. It now
  intercepts the stock `Say -> QueueCB` enqueue path by signature-scanning the
  loaded executable for the `QueueCB` call sites in the global unit dialog
  `Say(Craft*, UnitMsg, int)` path and the direct `RecycleTask::Say(char*)`
  path.
- The queue hook resolves the live `QueueCB` target, the backing `q_list`
  storage, and the stock `KillCBQueue(0)` helper from the matched Redux code so
  EXU can inspect and compact the native radio queue before enqueueing a new
  bark.
- The exact legacy BZ1 decompile was used as the behavior reference for
  `Say`, `GetUnitMsg`, `QueueCB`, and `StartCB`, then cross-checked against the
  Redux EXE by disassembly.

## Lua API

- `exu.GetUnitVoThrottle()`
- `exu.SetUnitVoThrottle(milliseconds)`
- `exu.GetUnitVoQueueDepthLimit()`
- `exu.SetUnitVoQueueDepthLimit(depth)`
- `exu.GetUnitVoQueueStaleMs()`
- `exu.SetUnitVoQueueStaleMs(milliseconds)`
- `exu.GetUnitVoMuted()`
- `exu.SetUnitVoMuted(muted)`
- `exu.GetUnitVoAlternates(filename)`
- `exu.SetUnitVoAlternates(filename, { ... })`

Example:

```lua
exu.SetUnitVoThrottle(750)
exu.SetUnitVoQueueDepthLimit(2)
exu.SetUnitVoQueueStaleMs(2000)
exu.SetUnitVoMuted(false)
exu.SetUnitVoAlternates("svrecyv0.wav", {
    "svrecyv0.wav",
    "svrecy_alt0.wav",
})
```

## Current Behavior

- Only filenames that look like stock unit bark clips are treated as unit VO.
- Mission audio such as `misn*.wav` is intentionally left alone by the current
  heuristic.
- When mute is enabled, likely unit-bark enqueue attempts are dropped before
  they enter the stock queue.
- When the throttle is active, unit-bark attempts inside the throttle window are
  dropped before they are enqueued.
- Duplicate unit barks already in the native queue are dropped.
- Alternate tables replace the requested bark with a random filename from the
  configured list, preferring alternates that are not already queued.
- If the queue is older than the configured stale threshold, or already at the
  configured unit-bark depth limit, EXU compacts the stock queue before
  enqueueing the new bark.
- If non-unit chatter is already in the queue, EXU refuses to flush it and will
  drop the new unit bark instead of killing unrelated radio audio.

## Current Limitations

- This is still not a full rewrite of the native radio system. It is a queue
  interposer on the main stock unit bark enqueue paths.
- The current pass only manages the `Say -> QueueCB` paths that were verified in
  the Redux EXE. It does not attempt to rewrite every possible direct
  `QueueCB` caller or every lower-level GAS start path.
- Queue inspection depends on the current stock queue layout: 16-byte bark name,
  owner pointer, sound pointer, priority, enqueue time, and next pointer.
- Alternate bark filenames now need to fit the stock queue name buffer. EXU
  currently enforces 1-15 character filenames for queue-managed bark names.

## Verification

- `UnitVo.cpp` and `LuaExport.cpp` were recompiled successfully on 2026-03-16
  with the project's Release x86 compiler flags after the queue-time hook and
  Lua API were wired in.
- A full solution build is still blocked by an unrelated existing environment
  issue in `OgreNativeFontBridge.cpp` (`OgreException.h` include path).
