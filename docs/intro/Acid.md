ACID is a generative acid line and the 303 that plays it, ported from the Schwung Acid plugin ("TB-3PO x 303"). The sequencer is the Phazerville TB-3PO algorithm (Logarhythm1 / djphazer), rebuilt as Tb3poCore; the voice is schwung-303, Robin Schmidt's Open303 with Devilfish mods and Soft / RAT drive, its DSP compiled unchanged. Besides the 303 audio, the line comes out as V/OCT, GATE, ACC and SLIDE, so it can play other voices too. The screen draws the line one column per step: blocks at pitch, accents in magenta, slides as amber lines, the current step underlined, and a footer with density, key, seed and clock.

**Patch it**
- Patch a 1/16 clock into CLK and OUT to a mixer, then turn DENSITY (negative = root only, 0 = sparsest, positive = dense) and press NEW for a fresh seed; MUT re-rolls about a quarter of the steps.
- RST makes the next clock step 1. Other clock divisions are in the menu; with CLK unpatched an internal tempo can run it (menu).
- FILL is the play-mode input: in GATE mode the line plays while FILL is high, in TRIGGER mode each rising edge opens a window and restarts from step 1 (menu).
- V/OCT in transposes the line and the 303 in semitones; CUT is cutoff CV (+-5 V = +-50 %). Audio and CV outs share the same 32 samples of latency, so they stay aligned.
