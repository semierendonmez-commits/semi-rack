BREAKBEAT is a loop slicer: the Rack port of Breakbeat, the desktop port of mestela/schwung-breakbeat, with upstream's slice selection compiled unchanged plus BBCut cut procedures ported from bencodec/BBenCut. The loop is cut into 8, 16 or 32 equal slices and re-sequenced on every eighth by biased-random decisions, with a fill bar that can switch to loop B and per-bar 2X / 3X / 4X / 8X retriggers. CUT picks who decides: MOVE (the original), CUTPROC11, WARPCUT, SQPUSHER1 or CHOOSECUT. The cut display plots two bars of cuts against where each reads in the loop (rolls magenta, loop B amber); the loop screen shows the waveform, slice grid and playhead.

**Patch it**
- Load a WAV from the menu as loop A (until then a synthetic two-bar break plays), patch a 1/16 clock into CLK and take L / R. Start with COMPLEX and ROLL, then step through CUT.
- RST: the next clock is bar 1. With no CLK, RUN (pad or gate) plays at the menu tempo.
- GATE + SLICE jump to a slice (1/12 V per slice from 0 V). TRIG, ROLL, SLC and B follow every hit, roll, slice and loop-B pass.
- In the BBCut modes SCRAMBLE sets how often a block reads from a random eighth; ANCHOR and 2X-8X only act in MOVE.

**MetaModule** — Loops load mono (stereo in Rack); 30 s max on both.
