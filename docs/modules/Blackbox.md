# BLACKBOX

*Family flight recorder: records the BUS and 4 CV + 4 gate lanes into a ring buffer, replay / recall from a cursor, MIDI file export*

20 HP — Recording · Utility · Visual · Sequencer

<p align="center"><img src="../images/Blackbox.png" alt="BLACKBOX" height="560"></p>

BLACKBOX is the family's flight recorder, ported from the BLACKBOX plugin. In the DAW it records the shared-memory buses; here it records what travels on cables: the BUS (POS, ENERGY, SCENE, EVENT) and four CV and four gate lanes, into a fixed ring buffer. It is transparent: the outputs pass the inputs through until you replay or recall. The tape screen shows ENERGY, the SCENE strip, EVENT turns, the CV and gate lanes, bar ticks, marks (amber), the cursor (magenta) and the playhead (cyan); the readout shows REC / PLAY / HOLD state, tape length, how full the ring is, the cursor and the marks.

**Patch it**
- Put it inline: EPOCH's BUS into BUS, a few CVs and gates into the lanes, and the outputs on to the modules they fed. Press REC.
- Turn CURSOR (and ZOOM) to a spot on the tape. PLAY replays from there on every output (recording pauses meanwhile); RECALL holds the state at the cursor, press again to go back to THRU; MARK drops a mark. REC, PLAY and MARK each have a TRG input.
- EXPORT in the context menu writes the whole tape, or the window on the screen, as a multi-track MIDI file; marks become conductor markers.

**MetaModule** — The ring holds 512 KB (about 10 min of busy lanes) instead of 4 MB, and EXPORT goes through the firmware's save browser.

## Controls

| group | control | range | default |
|---|---|---|---|
| Transport | Record on / off | 0 … 1 | 0 |
| Transport | Replay from the cursor / stop | 0 … 1 | 0 |
| Transport | Recall: hold the state at the cursor / back to THRU | 0 … 1 | 0 |
| Transport | Mark | 0 … 1 | 0 |
| Transport | Cursor | 0 … 1 | 1 |
| Transport | Zoom | 0 … 1 | 1 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Record trigger | Bus |
| Play trigger | CV 1 |
| Mark trigger | CV 2 |
| Bus | CV 3 |
| CV 1 | CV 4 |
| CV 2 | Gate 1 |
| CV 3 | Gate 2 |
| CV 4 | Gate 3 |
| Gate 1 | Gate 4 |
| Gate 2 |  |
| Gate 3 |  |
| Gate 4 |  |

[← all modules](../../README.md#modules)
