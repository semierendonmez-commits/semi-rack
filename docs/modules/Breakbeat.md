# BREAKBEAT

*schwung-breakbeat slicer with BBCut cut procedures (CutProc11, WarpCut, SQPusher1, ChooseCut), loop A/B phrase, retriggers, cut display*

16 HP — Sampler · Drum · Random · Sequencer · Stereo

<p align="center"><img src="../images/Breakbeat.png" alt="BREAKBEAT" height="560"></p>

BREAKBEAT is a loop slicer: the Rack port of Breakbeat, the desktop port of mestela/schwung-breakbeat, with upstream's slice selection compiled unchanged plus BBCut cut procedures ported from bencodec/BBenCut. The loop is cut into 8, 16 or 32 equal slices and re-sequenced on every eighth by biased-random decisions, with a fill bar that can switch to loop B and per-bar 2X / 3X / 4X / 8X retriggers. CUT picks who decides: MOVE (the original), CUTPROC11, WARPCUT, SQPUSHER1 or CHOOSECUT. The cut display plots two bars of cuts against where each reads in the loop (rolls magenta, loop B amber); the loop screen shows the waveform, slice grid and playhead.

**Patch it**
- Load a WAV from the menu as loop A (until then a synthetic two-bar break plays), patch a 1/16 clock into CLK and take L / R. Start with COMPLEX and ROLL, then step through CUT.
- RST: the next clock is bar 1. With no CLK, RUN (pad or gate) plays at the menu tempo.
- GATE + SLICE jump to a slice (1/12 V per slice from 0 V). TRIG, ROLL, SLC and B follow every hit, roll, slice and loop-B pass.
- In the BBCut modes SCRAMBLE sets how often a block reads from a random eighth; ANCHOR and 2X-8X only act in MOVE.

**MetaModule** — Loops load mono (stereo in Rack); 30 s max on both.

## Controls

| group | control | range | default |
|---|---|---|---|
| Slicer | Complexity (BBCut: roll / repeat chance, SQPUSHER1 activity) | 0 … 1 | 0.5 |
| Slicer | Anchor (MOVE: keep 1 and 3 in place) | 0 … 1 | 0 |
| Slicer | Roll (MOVE: stay on a slice; BBCut: roll shape) | 0 … 1 | 0 |
| Slicer | Fill (the phrase's last bar) | 0 … 1 | 0 |
| Slicer | B chance (the fill bar plays loop B) | 0 … 1 | 0 |
| Retrig | Retrigger 2x odds per bar (MOVE) | 0 … 1 | 0 |
| Retrig | Retrigger 3x odds per bar (MOVE) | 0 … 1 | 0 |
| Retrig | Retrigger 4x odds per bar (MOVE) | 0 … 1 | 0 |
| Retrig | Retrigger 8x odds per bar (MOVE) | 0 … 1 | 0 |
| Retrig | Scramble (BBCut: blocks from a random eighth) | 0 … 1 | 0.5 |
| Retrig | Gate (part of each hit at full level) | 0.02 … 1 | 1 |
| Mode | Cut mode (MOVE / CUTPROC11 / WARPCUT / SQPUSHER1 / CHOOSECUT) | 0 … 1 | 0 |
| Mode | Run (internal clock when CLK is not patched) | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (1/16 by default) | Left |
| Reset (next clock = bar 1) | Right |
| Run gate (internal clock) | Hit trigger |
| Complexity CV (+-5 V = +-50 %) | Roll / retrigger gate |
| Gate (jump to a slice) | Slice of the last hit (1/12 V per slice) |
| Slice (1/12 V per slice from 0 V; unpatched = next slice) | Loop B gate |

[← all modules](../../README.md#modules)
