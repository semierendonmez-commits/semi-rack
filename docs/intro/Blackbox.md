BLACKBOX is the family's flight recorder, ported from the BLACKBOX plugin. In the DAW it records the shared-memory buses; here it records what travels on cables: the BUS (POS, ENERGY, SCENE, EVENT) and four CV and four gate lanes, into a fixed ring buffer. It is transparent: the outputs pass the inputs through until you replay or recall. The tape screen shows ENERGY, the SCENE strip, EVENT turns, the CV and gate lanes, bar ticks, marks (amber), the cursor (magenta) and the playhead (cyan); the readout shows REC / PLAY / HOLD state, tape length, how full the ring is, the cursor and the marks.

**Patch it**
- Put it inline: EPOCH's BUS into BUS, a few CVs and gates into the lanes, and the outputs on to the modules they fed. Press REC.
- Turn CURSOR (and ZOOM) to a spot on the tape. PLAY replays from there on every output (recording pauses meanwhile); RECALL holds the state at the cursor, press again to go back to THRU; MARK drops a mark. REC, PLAY and MARK each have a TRG input.
- EXPORT in the context menu writes the whole tape, or the window on the screen, as a multi-track MIDI file; marks become conductor markers.

**MetaModule** — The ring holds 512 KB (about 10 min of busy lanes) instead of 4 MB, and EXPORT goes through the firmware's save browser.
