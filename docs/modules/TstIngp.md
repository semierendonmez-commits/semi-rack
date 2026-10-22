# TST_INGP

*tst_ingp: text as a 64 x 32 bit field read by 32 sines, a cellular-automaton cursor rewriting it as it plays; field and rule screens*

14 HP — Synth voice · Sequencer · Random · Visual · Stereo · Oscillator

<p align="center"><img src="../images/TstIngp.png" alt="TST_INGP" height="560"></p>

TST_INGP is the Rack port of tst_ingp.mxt, a Max patch that turns a text into a 64 x 32 bit field (every character's ASCII code in binary) read row by row by 32 sines, each with its own counter; a 1 opens the voice. GENERATIONIZE walks a cursor over the field and rewrites the cell under it with a 32-bit rule over self, left, right, up and down, so the text changes as it plays. The field screen shows the 32 x 64 cells with the read heads, open voices and the cursor; the rule screen shows the 32-bit rule as a strip.

**Patch it**
- Patch OUT L / R; it runs at BPM (AIR is the patch's dac~ switch). Pick an example text in the menu, or load any file as the text.
- CLK, RST or BUS POS clock the steps; RUN is a gate that closes every voice when low. V/OCT transposes on top of TUNE, TILT CV adds to TILT.
- GEN turns GENERATIONIZE on, FULL switches the walk area (original 34 x 34 / full field), GEN/ST and EVOLVE set how much it rewrites; POKE applies the rule around the cursor. TRIG fires when a voice opens, CA when the rule flips a cell.

## Controls

| group | control | range | default |
|---|---|---|---|
| Clock | BPM (internal metro) | 5 … 300 | 140 |
| Clock | Swing | 0 … 75 | 0 |
| Sound | Smooth (line~ ramp) | 0 … 2000 | 250 |
| Sound | Tune | -24 … 24 | 0 |
| Sound | Tilt (spectral) | -1 … 1 | 0 |
| Sound | Width (pan) | 0 … 2 | 1 |
| Sound | Master | -60 … 12 | 0 |
| Sound | On air (dac~) | 0 … 1 | 0 |
| Automaton | Generationize (cursor rewrites the field) | 0 … 1 | 0 |
| Automaton | Walk area (original 34 x 34 / full field) | 0 … 1 | 0 |
| Automaton | Generations per step | 0 … 16 | 1 |
| Automaton | Evolve | 0 … 100 | 0 |
| Automaton | Poke (apply the rule around the cursor) | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (1/16 per pulse by default; replaces the metro) | Left |
| Reset (all counters to step 1) | Right |
| Run gate (closes every voice when low, restarts on the rising edge) | Trigger (a voice opened) |
| Tune V/oct (adds to TUNE) | Automaton trigger (the rule flipped a cell) |
| Tilt CV (0.2 / V) |  |
| FamilyBus (POS clocks when CLK is unpatched, EVENT evolves) |  |

[← all modules](../../README.md#modules)
