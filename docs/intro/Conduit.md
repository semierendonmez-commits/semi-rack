CONDUIT is the family's patch bay, ported from the CONDUIT plugin. In the DAW it reads the shared FamilyBus and writes into the siblings; here the family talks over cables, so the matrix rows are what arrives on cables (CLK, BAR, TURN, TURN+, SCENE, ENERGY, PHASE, four triggers, four CVs) and the columns are eight output jacks, each a MOD (CV), TRIG or GATE with its own DEPTH attenuverter. Every pin has CONDUIT's own AMOUNT, CURVE, DELAY, PROB and LENGTH. The matrix screen draws a square per pin sized by AMOUNT, the cursor in magenta; the pin screen shows the selected pin's response and its five values.

**Patch it**
- Patch EPOCH's BUS into BUS and a clock into CLK, choose a source row and an output column with the cursor knobs, press PIN to patch it, then set AMOUNT.
- When the cursor moves, the five pin knobs jump to that pin. In Rack, clicking the matrix moves the cursor and double-clicking patches or pulls a pin.
- Outputs can be merged onto BUS OUT (ENERGY += value, EVENT <- triggers); otherwise BUS OUT passes BUS IN through. The router runs in blocks, so there is one block of latency.
