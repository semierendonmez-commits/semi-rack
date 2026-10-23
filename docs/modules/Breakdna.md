# BREAKDNA

*Groove-DNA transfer and A/B spectral cross-synthesis of two breaks, clocked*

16 HP — Sampler · Drum · Sequencer

<p align="center"><img src="../images/Breakdna.png" alt="BREAKDNA" height="560"></p>

BREAKDNA takes the feel of one break and the sound of another, ported from the BREAKDNA plugin. Break A is the DNA: where every hit lands inside its 16th, how loud it is, which hits are ghosts. Break B is the skin: its slices answer A's hits, and MORPH blends their spectra into A's through five pre-rendered layers (A, 25 / 50 / 75 % blends, B), so the result keeps A's feel. The DNA screen shows A's waveform with slices by class (K, S, G, H) over B; the genome screen shows one column per 16th with K S G H levels, each hit's micro-timing and the morph lane.

**Patch it**
- Load A and B from the menu (WAV, analysed off the audio thread), patch a 1/16 clock into CLK and take L / R. Turn MORPH, then FEEL, DYN and GHOSTS.
- SOURCE switches between BREAK A (A's own hits on the clock) and GATES: your K / S / H gates get A's micro-timing, accents and ghosts (the S gate's height is the velocity, below about 3 V it is a ghost). In GATES mode notes play 25 ms late, the engine's lookahead.
- Without CLK the break runs free at its own tempo (menu). The K / S / H trigger outs and ACC (velocity, 0-10 V) let other drums follow.

## Controls

| group | control | range | default |
|---|---|---|---|
| DNA | Feel | 0 … 150 | 100 |
| DNA | Dynamics | 0 … 150 | 100 |
| DNA | Ghosts | 0 … 200 | 100 |
| DNA | Rotate | 0 … 31 | 0 |
| Cross | Morph A > B | 0 … 100 | 35 |
| Cross | Lane | 0 … 100 | 100 |
| Output | Decay | 0 … 100 | 100 |
| Output | Tune | -12 … 12 | 0 |
| Output | Level | -24 … 6 | 0 |
| Output | Source (BREAK A / GATES) | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock | Left |
| Reset | Right |
| Kick gate (GATES mode) | Kick trigger |
| Snare gate (GATES mode; height = velocity, below ~3 V a ghost) | Snare / ghost trigger |
| Hat gate (GATES mode) | Hat trigger |
| Morph CV (10 %/V) | Accent (the last hit's velocity, 0-10 V) |
| Feel CV (15 %/V) |  |

[← all modules](../../README.md#modules)
