CELLULA is the Rack port of ryoz_ikedar_ln, a Max/MSP datamatics patch with a sine / click voice. Nine voices each run on a 64-cell one-dimensional automaton row, and the same 64 bits are the voice's rhythm, its wavetable and, for voices 4-6, an FFT bin mask. A master row sets the tempo. In Rack the panel plays the bar rows instead of drawing them, and returning a knob to 0 gives the preset's row back exactly. The matrix screen shows nine rows x 64 cells with the playhead; the data screen shows the master row as a barcode with tempo, step and clock readouts.

**Patch it**
- Patch OUT L / R and it runs on its own metro at TEMPO (default 66 bars = 132 BPM, the patch preset). PITCH and GATE move every voice's pitch and decay rows; the PITCH input adds 5 bars per volt.
- CLK (1/16 per pulse by default) or BUS POS replace the patch's metro; RST makes the next step step 1. RUN is a gate: it runs while high and restarts on the rising edge.
- MUTATE, RAND and HOLD are the automaton actions, AUTO and CHANCE the auto-mutate. STEP fires on every step. On the BUS, EVENT mutates and ENERGY scales the chance.
