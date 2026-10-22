# CONDUIT

*Family patch bay: BUS + trigger / CV rows into a pin matrix (amount, curve, delay, probability, length) driving 8 CV / trigger / gate outputs*

20 HP — Utility · Logic · Random · Visual

<p align="center"><img src="../images/Conduit.png" alt="CONDUIT" height="560"></p>

CONDUIT is the family's patch bay, ported from the CONDUIT plugin. In the DAW it reads the shared FamilyBus and writes into the siblings; here the family talks over cables, so the matrix rows are what arrives on cables (CLK, BAR, TURN, TURN+, SCENE, ENERGY, PHASE, four triggers, four CVs) and the columns are eight output jacks, each a MOD (CV), TRIG or GATE with its own DEPTH attenuverter. Every pin has CONDUIT's own AMOUNT, CURVE, DELAY, PROB and LENGTH. The matrix screen draws a square per pin sized by AMOUNT, the cursor in magenta; the pin screen shows the selected pin's response and its five values.

**Patch it**
- Patch EPOCH's BUS into BUS and a clock into CLK, choose a source row and an output column with the cursor knobs, press PIN to patch it, then set AMOUNT.
- When the cursor moves, the five pin knobs jump to that pin. In Rack, clicking the matrix moves the cursor and double-clicking patches or pulls a pin.
- Outputs can be merged onto BUS OUT (ENERGY += value, EVENT <- triggers); otherwise BUS OUT passes BUS IN through. The router runs in blocks, so there is one block of latency.

## Controls

| group | control | range | default |
|---|---|---|---|
| Pin editor | Row (source) | 0 … 14 | 0 |
| Pin editor | Column (output) | 0 … 7 | 0 |
| Pin editor | Pin amount | -1 … 1 | 0 |
| Pin editor | Pin curve | 0 … 5 | 1 |
| Pin editor | Pin delay | 0 … 16 | 0 |
| Pin editor | Pin probability | 0 … 1 | 1 |
| Pin editor | Pin length | 0 … 1 | 0.375 |
| Pin editor | Patch / pull the pin | 0 … 1 | 0 |
| Pin editor | Master | 0 … 1 | 1 |
| Outputs | OUT1 depth | -1 … 1 | 1 |
| Outputs | OUT2 depth | -1 … 1 | 1 |
| Outputs | OUT3 depth | -1 … 1 | 1 |
| Outputs | OUT4 depth | -1 … 1 | 1 |
| Outputs | OUT5 depth | -1 … 1 | 1 |
| Outputs | OUT6 depth | -1 … 1 | 1 |
| Outputs | OUT7 depth | -1 … 1 | 1 |
| Outputs | OUT8 depth | -1 … 1 | 1 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Bus | OUT1 |
| Clock | OUT2 |
| Trigger 1 | OUT3 |
| Trigger 2 | OUT4 |
| Trigger 3 | OUT5 |
| Trigger 4 | OUT6 |
| CV 1 | OUT7 |
| CV 2 | OUT8 |
| CV 3 | Bus |
| CV 4 |  |

[← all modules](../../README.md#modules)
