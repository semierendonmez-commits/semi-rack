# MODPORT

*ProTracker .mod player (1-8 channels, pt2-clone replayer): one row per clock or free tempo, per-channel gate and V/OCT, Amiga stereo mix*

20 HP — Sampler · Sequencer · Stereo

<p align="center"><img src="../images/Modport.png" alt="MODPORT" height="560"></p>

MODPORT is a ProTracker .mod player, ported from the MODPORT plugin: its .mod loader and a ProTracker 2.3D replayer after pt2-clone. It plays files with 1-8 channels (M.K., xCHN, OCTA, CD81, FA0x, FLT8, 15-sample SoundTracker and more) through the Amiga mix with LRRL panning and the A500 / A1200 and LED filters. Every channel also comes out as a gate and a V/OCT, so a tracker song can play other modules. The tracker screen shows the title, order / pattern and speed / tempo, nine rows around the playing one, and a strip of channel meters.

**Patch it**
- Press LOAD to pick a .mod, patch L / R out and press PLAY. Free running, the song keeps its own CIA / VBlank timing and TEMPO scales it (x0.25 .. x4).
- With CLK patched one clock edge plays one row. RST goes back to order 0, NEXT jumps to the next order, PLAY (pad or trigger) starts and stops.
- Per channel: GATE is high while the sample sounds (1 ms low on every new trigger), V/OCT follows slides and vibrato (0 V = C-2). Each channel has a mute; STEREO sets the separation, the filters are in the menu.

**MetaModule** — .mod files over 4 MB are refused, a limit set for MetaModule RAM.

## Controls

| group | control | range | default |
|---|---|---|---|
| Transport | Play / stop | 0 … 1 | 0 |
| Transport | Load .mod | 0 … 1 | 0 |
| Transport | Tempo (free running) | -2 … 2 | 0 |
| Mix | Stereo separation (Amiga LRRL) | 0 … 1 | 0.5 |
| Mix | Level | -24 … 6 | 0 |
| Mutes | Mute channel 1 | 0 … 1 | 0 |
| Mutes | Mute channel 2 | 0 … 1 | 0 |
| Mutes | Mute channel 3 | 0 … 1 | 0 |
| Mutes | Mute channel 4 | 0 … 1 | 0 |
| Mutes | Mute channel 5 | 0 … 1 | 0 |
| Mutes | Mute channel 6 | 0 … 1 | 0 |
| Mutes | Mute channel 7 | 0 … 1 | 0 |
| Mutes | Mute channel 8 | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (one row per edge) | Left |
| Reset (order 0) | Right |
| Play / stop trigger | Channel 1 gate |
| Next order trigger | Channel 2 gate |
|  | Channel 3 gate |
|  | Channel 4 gate |
|  | Channel 5 gate |
|  | Channel 6 gate |
|  | Channel 7 gate |
|  | Channel 8 gate |
|  | Channel 1 V/OCT (0 V = C-2) |
|  | Channel 2 V/OCT (0 V = C-2) |
|  | Channel 3 V/OCT (0 V = C-2) |
|  | Channel 4 V/OCT (0 V = C-2) |
|  | Channel 5 V/OCT (0 V = C-2) |
|  | Channel 6 V/OCT (0 V = C-2) |
|  | Channel 7 V/OCT (0 V = C-2) |
|  | Channel 8 V/OCT (0 V = C-2) |

[← all modules](../../README.md#modules)
