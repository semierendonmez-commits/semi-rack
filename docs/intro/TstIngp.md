TST_INGP is the Rack port of tst_ingp.mxt, a Max patch that turns a text into a 64 x 32 bit field (every character's ASCII code in binary) read row by row by 32 sines, each with its own counter; a 1 opens the voice. GENERATIONIZE walks a cursor over the field and rewrites the cell under it with a 32-bit rule over self, left, right, up and down, so the text changes as it plays. The field screen shows the 32 x 64 cells with the read heads, open voices and the cursor; the rule screen shows the 32-bit rule as a strip.

**Patch it**
- Patch OUT L / R; it runs at BPM (AIR is the patch's dac~ switch). Pick an example text in the menu, or load any file as the text.
- CLK, RST or BUS POS clock the steps; RUN is a gate that closes every voice when low. V/OCT transposes on top of TUNE, TILT CV adds to TILT.
- GEN turns GENERATIONIZE on, FULL switches the walk area (original 34 x 34 / full field), GEN/ST and EVOLVE set how much it rewrites; POKE applies the rule around the cursor. TRIG fires when a voice opens, CA when the rule flips a cell.
