# IKEDAR

*ryoz_ikedar_ln: nine 64-cell automata as rhythm, wavetable and FFT mask; sine/click datamatics voice with bit-matrix and barcode screens*

16 HP — Synth voice · Sequencer · Random · Visual · Stereo · Digital

<p align="center"><img src="../images/Ikedar.png" alt="IKEDAR" height="560"></p>

IKEDAR is the Rack port of ryoz_ikedar_ln, a Max/MSP datamatics patch with a sine / click voice. Nine voices each run on a 64-cell one-dimensional automaton row, and the same 64 bits are the voice's rhythm, its wavetable and, for voices 4-6, an FFT bin mask. A master row sets the tempo. In Rack the panel plays the bar rows instead of drawing them, and returning a knob to 0 gives the preset's row back exactly. The matrix screen shows nine rows x 64 cells with the playhead; the data screen shows the master row as a barcode with tempo, step and clock readouts.

**Patch it**
- Patch OUT L / R and it runs on its own metro at TEMPO (default 66 bars = 132 BPM, the patch preset). PITCH and GATE move every voice's pitch and decay rows; the PITCH input adds 5 bars per volt.
- CLK (1/16 per pulse by default) or BUS POS replace the patch's metro; RST makes the next step step 1. RUN is a gate: it runs while high and restarts on the rising edge.
- MUTATE, RAND and HOLD are the automaton actions, AUTO and CHANCE the auto-mutate. STEP fires on every step. On the BUS, EVENT mutates and ENERGY scales the chance.

## Controls

| group | control | range | default |
|---|---|---|---|
| Clock | Tempo (master bars: BPM = bars x 2, > 25 runs, > 10 DAC on) | 0 … 167 | 66 |
| Clock | Swing (odd steps late) | 0 … 50 | 12 |
| Rows | Pitch (bars added to every PITCH row) | -25 … 25 | 0 |
| Rows | Gate (bars taken from every DECAY row: + = longer notes) | -12 … 12 | 0 |
| Automata | Auto-mutate every | 0 … 7 | 0 |
| Automata | Auto-mutate chance | 0 … 100 | 50 |
| Automata | Mutate all (one generation per unlocked voice) | 0 … 1 | 0 |
| Automata | Randomise patterns (unlocked voices) | 0 … 1 | 0 |
| Automata | Hold automata | 0 … 1 | 0 |
| Levels | Wave level (voices 1-3) | -60 … 6 | 0 |
| Levels | Spec level (voices 4-6) | -60 … 6 | 0 |
| Levels | Sine level (voices 7-9) | -60 … 6 | 0 |
| Levels | Master | -60 … 6 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (1/16 per pulse by default; replaces the metro) | Left |
| Reset (next step is step 1) | Right |
| Run gate (runs while high, restarts on the rising edge) | Step trigger |
| Mutate trigger |  |
| Pitch CV (5 bars/V) |  |
| FamilyBus (POS clocks when CLK is unpatched, EVENT mutates, ENERGY scales chance) |  |

[← all modules](../../README.md#modules)
