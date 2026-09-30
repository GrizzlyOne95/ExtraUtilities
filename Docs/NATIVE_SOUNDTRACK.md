# Native Redux soundtrack controls

EXU owns the reusable soundtrack runtime and Lua bindings. A separate mod can
ship `exu.dll` and use these controls without installing OpenShim. The native
backend first requires EXU's full Redux 2.2.301 build gate, then unique matches
for the soundtrack selector, start and stop at their recorded addresses.
Unsupported layouts return `false`/`nil` rather than guessing addresses.

OpenShim independently supports the five existing basic exports. If EXU cannot
qualify its own native backend, those basic calls can use a live OpenShim
provider. State tables and fades require EXU's native backend. An operation
that fails after native qualification does not retry through OpenShim.

## Lua API

| Operation | Result and behavior |
| --- | --- |
| `SetMusicTrack(index)` | Boolean; plays and loops `NN.ogg`, index 0..255. Alias: `PlayMusic(index)`. Checks the engine resource layer before replacing the current track. Cancels an EXU fade only after validation. |
| `StopMusic()` | Boolean; releases the stream, cancels fades, retains the selected track. Repeated calls are harmless. |
| `PauseMusic()` | Boolean; pauses without releasing the stream. Repeated calls are harmless. |
| `ResumeMusic()` | Boolean; resumes a paused stream at its retained position. Does not restart stopped music. |
| `GetMusicTrack()` | Selected engine track, including stock TRN/playlist changes; -1 means no selection, nil means unavailable. A stopped track remains selected. |
| `GetMusicState()` | Table with `track`, `playing`, `paused`, `gain`, `fading`, `userVolume`, or nil when unavailable. Playing/paused are the engine's flags, not proof of audibility. |
| `FadeMusic(gain, seconds=1)` | Boolean; ramp temporary gain 0..1 over finite, non-negative seconds. Zero gain silences the stream without stopping or pausing it. |
| `ChangeMusicTrack(index, fadeOut=1, fadeIn=1)` | Boolean; fade out, switch to a looping track, then fade in to gain 1. Replaces a pending transition after validating the request. No active stream skips fade-out. |
| `UpdateMusic(dt)` | Boolean; call once per mission Update using finite, non-negative simulation seconds. Advances fades and reapplies temporary gain against current player volume. |
| `ResetMusic()` | Boolean; cancel a transition and restore gain 1 without changing selected track or pause/play state. Also runs when the Lua state closes or EXU initializes again. |

```lua
local exu = require("exu")

function Start()
    exu.SetMusicTrack(7)
end

function Update(dt)
    exu.UpdateMusic(dt)
    -- Other mission logic follows.
end

-- Example event:
-- exu.ChangeMusicTrack(12, 2, 3)
-- exu.PauseMusic() -- freezes an in-progress fade too
-- exu.ResumeMusic()
```

`PlayMusic` and `SetMusicTrack` are the same operation. All controls are local
audio state, including in multiplayer. Missions choose whether and when to
send their own music events to peers. Fades are sequential transitions, not
overlapping tracks: this layer controls Redux's single soundtrack stream.
Pausing freezes timed transitions; selecting a track explicitly can start a
new stream. External track or stream changes cancel an EXU gain override.

No saved options are written. The backend obtains the current OGG
DirectSound buffer for each volume operation, uses Redux's existing
`800 * log2(volume / 10)` curve, and clamps exact silence to DirectSound's
minimum volume before evaluating the logarithm. It does not use Redux's
high-level volume setter, whose zero/positive branches pause/resume playback.
It retains no buffer pointer across calls, creates no worker thread, loads no
extra DLL, and installs no new engine detour.

## Identity evidence and limitations

The source catalog is `exu.json` and the optional feature anchors are in
`profiles/bzr_2.2.301.json`. Generated headers contain the catalog addresses
and anchor bytes; feature code carries no raw engine addresses. The three
anchor patterns come from OpenShim's already verified soundtrack resolves.
The remaining targets use the fixed layout recorded for the supported GOG
2.2.301 image, not newly invented signatures:

| Target | Evidence |
| --- | --- |
| Select/start/stop/resource size | OpenShim PR #280 and `live_scan_findings_20260706.md` #17; world loader, `%02d.ogg` start path, and mission teardown. |
| Pause/resume | The same findings and matching GOG corpus; retained OGG slot, guarded paused-flag transitions. OpenShim resolves them at recorded offsets from its unique stop anchor. |
| Track/slot/started/paused globals | Selector stores and start/stop/pause/resume/playlist consumers in the same corpus. |
| GetDSBuffer | OggManager's two-slot validation and streaming-buffer lookup; the native volume path obtains this buffer and invokes its DirectSound SetVolume method. |
| User volume | Native music-volume accessor reads the profile pointer plus the existing `SoundOptions.musicOffset`. |

This is an implementation for review, pending fresh native runtime validation.
The fixed-layout pause/resume and buffer targets must be confirmed before a
release. Host tests cannot prove these calls are safe or audible in the game.
The private binary/corpus remains outside the public repositories.

## Validation and release gate

Host checks cover fade endpoints, pause retention, update overshoot, missing
tracks before and during a transition, failed playback, cancellation, invalid
numeric inputs, and the DirectSound volume curve. Generated catalog/profile,
Lua definition parity, address census and shared-document checks also apply.

Run Release x86 builds for EXU and OpenShim. Use OpenShim's serialized
`BZRHarness.ps1` workflow for game launches, windowed mode, and orderly stop.
For each affected Windows/GOG, Windows/Steam, Proton/Steam and Wine/GOG lane:

1. Run `tests/runtime/musicchk.lua` in a Lua mission with EXU alone; repeat
   with the current OpenShim load chain installed.
2. Confirm track 7 -> pause -> resume retains position, repeat pause/resume
   is harmless, stop twice is harmless, and stopped read-back remains 7.
3. Confirm a missing item leaves the current track and pending fade untouched.
4. Listen to gain 1 -> 0 -> 1 and a 7 -> 12 transition. Check no unintended
   pause/resume, a correct muted-player result, and unchanged saved options.
5. Pause midway through a fade, resume it, change the options volume while
   attenuated, then exit/restart a mission. Confirm gain does not leak.
6. Confirm the unsupported/ambiguous-anchor path refuses calls. Verify the
   fixed-layout targets and all `Music::*` resolve diagnostics against settled
   runtime bytes before declaring the native lane validated.

Unavailable runtime lanes remain unverified and block release, not review.

Current host validation: EXU Linux host checks pass (Lua 5.1 through a host
interpreter wrapper); OpenShim CTest passes 56/56. The runtime fixture and Lua
definitions parse as Lua 5.1. Windows DLL builds and live audio remain
unverified in this environment.
