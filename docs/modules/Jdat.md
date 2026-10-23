# DATUM

*A metro writes random (or any file's) data into three 512-sample buffers read as sound; LCD digit field, buffer barcodes, gate outs*

14 HP — Synth voice · Noise · Random · Sequencer · Visual

<p align="center"><img src="../images/Jdat.png" alt="DATUM" height="560"></p>

DATUM is the Rack port of kdr_jdat.mxt, a Max patch in which a 120 ms metro writes one random -1, 0 or +1 into each of three 512-sample buffers per tick and clears them with 40 % chance. Three data voices read the buffers through wave~ at 25 Hz gated by the "mlts" lottery, and two drones read buffers 1 and 3 at 100 Hz. Rack adds FEED, so any file can supply the writes. The LCD screen fills with rows of random digits beside the lottery's heat columns; the buffer screen draws the three buffers as barcodes with the read position and a status line.

**Patch it**
- Patch L / R out and it runs on its own metro (RATE). Load any file from the menu and turn FEED to set how many of the writes come from it; DENS sets writes per buffer per tick (the patch: 1).
- CLK, RST or BUS POS replace the metro; RUN is a gate (runs while high, restarts on the rising edge). CLR forces a clear on the next tick, and BUS EVENT clears too.
- G1-G3 and the CLR out are the plugin's MIDI notes as gates, ready to trigger other voices from the data.

## Controls

| group | control | range | default |
|---|---|---|---|
| Metro | Metro | 0 … 1 | 0.539591 |
| Metro | Clear chance | 0 … 100 | 40 |
| Metro | Noise jump chance | 0 … 100 | 30 |
| Metro | Pattern lock | 0 … 5 | 0 |
| Data | Writes per buffer per tick | 1 … 64 | 1 |
| Data | Feed (share of the writes taken from the loaded file) | 0 … 100 | 100 |
| Data | Data sources (DATA / SINE / NOISE / none) | 0 … 1 | 0 |
| Data | Hold data | 0 … 1 | 0 |
| Levels | Data 1 level | -60 … 6 | 0 |
| Levels | Data 2 level | -60 … 6 | 0 |
| Levels | Data 3 level | -60 … 6 | 0 |
| Levels | Drone level | -60 … 6 | 0 |
| Levels | Master | -60 … 6 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (1/16 per pulse by default; replaces the metro) | Left (data 1 + 2 + drone 1) |
| Reset (restarts the metro, rewinds the file) | Right (data 2 + 3 + drone 2) |
| Run gate (runs while high, restarts on the rising edge) | Data 1 gate |
| Force clear (on the next tick) | Data 2 gate |
| Rate CV (1 V/oct faster) | Data 3 gate |
| FamilyBus (POS clocks when CLK is unpatched, EVENT clears) | Clear trigger |

[← all modules](../../README.md#modules)
