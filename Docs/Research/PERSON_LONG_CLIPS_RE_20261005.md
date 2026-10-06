# Person clip end times and long loops (2026-10-05)

Static analysis of the GOG/qualified Redux 2.2.301 image (pefile + capstone). Backs
`exu.animation.SetPersonLongClips` (`src/Game/PersonLongClips*`).

## Symptom

Ported creatures (ISDF Chronicles wildlife: jak, worm, sifter, ray, mound, rhinos) play their
walk for about a second, then the legs stop mid-stride and the body slides on. Their run cycles
are 1.0-2.4 s long. The wing (0.7 s) and gulper (0.8 s) are fine.

## Tables (writable `.data`, 12 entries, index = `Person+0x2A8`)

| Table | Address | Values |
|---|---|---|
| end time | `0x8E8E94` | 0.967 x9 (idx 0-8), 1.867, 1.167, 1.167 |
| cockpit rate | `0x8E8EC4` | 0.5 x4, 1.0, 0.75 x6, 0.05 |
| start time | `0x8E8EF4` | 0 x11, 0.2 |
| names | `0x8E8F24` | stand2Kneel, kneel2stand, idle, fireRecoilSniper, runForward, runBackward, runLeft, runRight, death1, idleParachute, landParachute, jump |
| loop bytes | `0x8E8F54` | 0,0,0,0,1,1,1,1,0,1,0,0 |
| world rate | `0x8E8F60` | 0.5 x4, 1.0, 0.75 x6, 0.6 |

## Person::Simulate (0x59D340)

- End table read only at `0x59D9F7` (world block) and `0x59DBBF` (cockpit block):
  `movss xmm0,[idx*4+0x8E8E94]`. The other tables are read only at clip change (`0x59E160`..`0x59E203`).
- Tick (`0x59DA92`..`0x59DB09`): `if (getTimePosition() + dt*rate >= end[idx])` set the ended flag,
  else `addTime(dt*rate)`. The test runs for looped clips too, so a clip whose length exceeds its end
  time **stops advancing at the end time, permanently, until the clip changes**. The rate is the
  table value only (no speed or ODF scaling).
- World flag `bridge+0xD8`: no consumer. Cockpit flag `bridge+0xDC`: consumed at `0x59DD01`, which
  stops the clip's sound channel `Person+0x2AC` (set to -1). For death1 (idx 8) it also sets
  `0x200` in `*(Person+0xF4)+0x14`.
- FSM uses `0x2AC == -1` as "clip done" only for stand2Kneel -> fireRecoilSniper (`0x59E073`) and
  kneel2stand -> idle (`0x59E0DD`). idle, runs, landParachute, jump transitions come from velocity,
  height and ground flags (`0x59DE29`, `0x59DE50`, `0x59DEB0`).
- Clip change (`0x680670` world, `0x680770` cockpit): disable old state, enable new, `setLoop(loop[idx])`,
  `setTimePosition(start[idx])`, store the rate.

## Consequences for EXU

- Raising `end[4..7]` only lets looped runs keep looping and keeps their sound channel alive until
  the next clip change. A run shorter than the end time wraps before reaching it, so stock pilots
  see no change.
- idle (idx 2) is not looped by the engine: a raised end lets a long idle play to its end; EXU also
  loops it (Ogre `setLoop(true)` after the change) when its length exceeds the end time.
- Never raise idx 0, 1 (kneel transitions), 8 (death flag), 10, 11.
- EXU raises the entry around one Person's Simulate call and restores it right after, and leaves an
  entry alone when a pilot animation policy has already rewritten it.
