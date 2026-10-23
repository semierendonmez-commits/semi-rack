# FLOATDAT

*CELLULA x FLOATER x VORTESSA x DATUM played by a Markov pinball sequencer; any file is the material, drawn as datamatic scenes*

24 HP — Sequencer · Synth voice · Drum · Random · Visual

<p align="center"><img src="../images/Floatdat.png" alt="FLOATDAT" height="560"></p>

FLOATDAT is CELLULA, FLOATER, VORTESSA and DATUM played by the Pinball 2 Markov sequencer, ported from FLOATDAT 2.0. Any file is the material: it is read as bytes, text or audio (WAV) into bits and symbols, and a read head feeds IKD's automata, FLOATER's lanes and sound, VTX's tuning and DATUM's buffers. The Pinball sequencer fires eight lanes; each does something inside the instrument and comes out as a gate. Three screens: VISUAL (datamatic scenes of the file under the head: barcode, matrix, scan, numbers, cells), PINBALL (the 17 x 17 matrix and the ball) and DATA (file, type, entropy, head).

**Patch it**
- Load a file from the menu, patch L / R out and press RUN (the internal clock at BPM), or patch a 1/16 clock into CLK (RST = bar 1). Balance the four layers with IKD, FLT, VTX and DATUM.
- T1-T8 are the lanes' notes as 10 V gates; LANES (poly) fires lanes from outside.
- DATA PATCH turns the data under the head into FLOATER's patch, MUTATE varies it, SCENE changes the picture, RESET sends the ball home; each has a trigger input. TIME MULT, SWING, PATTERN and SCAN RATE shape the walk.
- The plugin's other pages (the 16 steps, lane destinations and mutes, matrix shapes, the layer pages, the ten presets) are in the context menu.

**MetaModule** — Files are read up to 256 KB and WAV up to 10 s (2 MB and 60 s in Rack).

## Controls

| group | control | range | default |
|---|---|---|---|
| Mix | CELLULA level | -60 … 6 | 0 |
| Mix | FLT (FLOATER) level | -60 … 6 | -2 |
| Mix | VTX (Vortessa) level | -60 … 6 | -4 |
| Mix | DATUM level | -60 … 6 | -3 |
| Mix | Master | -60 … 6 | -3 |
| Pinball | Tempo (without CLK) | 20 … 300 | 120 |
| Pinball | Pinball time multiplier | 0.25 … 4 | 1 |
| Pinball | Pinball swing | 0 … 1 | 0 |
| Pinball | Pattern length (the ball goes home) | 0.5 … 64 | 1 |
| Pinball | Pinball reset | 0 … 1 | 0 |
| Pinball | Run (internal clock) | 0 … 1 | 0 |
| Data / FLT / visual | Scan rate (SCAN = run) | 0.1 … 64 | 2 |
| Data / FLT / visual | FLT DATA PATCH (the data under the head becomes FLOATER's patch) | 0 … 1 | 0 |
| Data / FLT / visual | FLT MUTATE | 0 … 1 | 0 |
| Data / FLT / visual | Next scene | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (1/16 notes) | Left |
| Reset (next clock = bar 1; the ball goes home) | Right |
| Run gate | Lane T1 gate (the note, 10 V) |
| Lanes: poly channel n fires lane n (mono: lane 1) | Lane T2 gate (the note, 10 V) |
| Pinball reset trigger | Lane T3 gate (the note, 10 V) |
| DATA PATCH trigger | Lane T4 gate (the note, 10 V) |
| Next scene trigger | Lane T5 gate (the note, 10 V) |
| Time multiplier CV (x2 per 2.5 V) | Lane T6 gate (the note, 10 V) |
|  | Lane T7 gate (the note, 10 V) |
|  | Lane T8 gate (the note, 10 V) |

[← all modules](../../README.md#modules)
