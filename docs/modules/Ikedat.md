# IKEDAT

*CELLULA x DATUM x a Markov pinball: a datamatic instrument fed by any file, live datamatics visuals as the UI, lane gate/pitch/velocity outs, BUS*

18 HP — Synth voice · Sequencer · Noise · Random · Visual

<p align="center"><img src="../images/Ikedat.png" alt="IKEDAT" height="560"></p>

IKEDAT is a datamatic instrument fed by any file: CELLULA x DATUM x a Markov pinball, ported from the IKEDAT plugin with its engine unchanged. CELLULA's wave, spectral and sine voice groups and a DATUM bus read the data, and the Pinball 2 ball fires lanes that come out as gate, pitch and velocity. The VIEW screen is the interface: five live views (transit, barcode, spatial, grid, stream; AUTO cuts every two bars) under a thin HUD of view, file and head, with the eight lanes on the right edge, plus an INFO strip below.

**Patch it**
- Load a file (or a typed-text example) from the menu, patch L / R out and press RUN, or patch a 1/16 clock into CLK so the master clock follows it.
- LNCH (button or input) relaunches the ball on its starting step; HALT freezes it (latching). MULT, SWING and PATTERN shape the walk, SCAN and SPEED how the data head moves; JUMP and HEAD move the head from outside.
- MUT mutates: mono = all voices, poly channel k = voice k. GATE / PITCH / VEL, STEP (1/12 V per step) and BUS carry the lanes and the ball to the family.

**MetaModule** — Data files are read up to 256 KB (8 MB in Rack).

## Controls

| group | control | range | default |
|---|---|---|---|
| Ball | Run | 0 … 1 | 0 |
| Ball | Relaunch the ball (starting step) | 0 … 1 | 0 |
| Ball | Hault (freeze the ball, latching) | 0 … 1 | 0 |
| Ball | Time multiplier | -2 … 2 | 0 |
| Ball | Swing | 0 … 100 | 0 |
| Ball | Pattern length (relaunch) | 0 … 8 | 2 |
| Data | Scan | 0 … 3 | 0 |
| Data | Scan bytes / 16th | 0 … 12 | 3 |
| Data | Data seed (CA generations read from the data) | 0 … 100 | 35 |
| Levels | Wave level (voices 1-3) | -60 … 6 | 0 |
| Levels | Spec level (voices 4-6) | -60 … 6 | 0 |
| Levels | Sine level (voices 7-9) | -60 … 6 | -6 |
| Levels | DATUM bus | -60 … 6 | -3 |
| Levels | DATUM clear chance | 0 … 100 | 40 |
| Levels | Master | -60 … 6 | -3 |
| Visual | Next view (AUTO, TRANSIT, BARCODE, SPATIAL, GRID, STREAM) | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (1/16 by default; unpatched = own tempo) | Left |
| Relaunch the ball (and the transport) | Right |
| Run gate (the clock runs while high) | Lane gate |
| Mutate (mono: all voices; poly: channel k = voice k) | Lane note (1 V/oct, C4 = 0 V) |
| Data head to a random place | Lane velocity (0-10 V) |
| Data head (0-10 V over the file) | Ball step (1/12 V per step, step 1 = 0 V) |
|  | FamilyBus (POS, ENERGY, SCENE = view, EVENT on relaunches) |

[← all modules](../../README.md#modules)
