# BREAKMED

*Generative tracker jungle break slicer: weighted chains (pitch, sub, glitch, reverse, drill), Paula sampler, hit log*

16 HP — Sampler · Drum · Random · Sequencer

<p align="center"><img src="../images/Breakmed.png" alt="BREAKMED" height="560"></p>

BREAKMED is a generative tracker jungle break slicer, ported from the BREAKMED plugin. The break is cut at its transients (TRANS / EQUAL / GRID), and every hit rolls a value R = 0..127 that sends it through one of seven chains by the chain weights: BASE, pitch down, pitch up, SUB, glitch, reverse and drill, played through a Paula sampler. The wave screen shows the break with slice markers and role letters (K S H G), the beat grid and playheads in their chain colours; the hit log lists every hit as a tracker row (slice, note, chain, tracker command, R).

**Patch it**
- Load a WAV from the menu (30 s max, mono-summed; until then a synthetic two-bar break plays), switch PLAY on, patch a 1/16 clock into CLK and take L / R. Raise one chain weight at a time to hear what it does.
- RST: the next clock is beat 1; the tempo comes from the clock period. PLAY on with no CLK free-runs at the break's own tempo.
- GATE plays a slice (SLICE picks it, 1/12 V per slice; unpatched = the next slice); in the menu's strict GATE mode the falling edge ends it. VARY CV scales the six variation weights, DICE gives a new seed, LOCK keeps the same variation at the same place.
- TRIG fires on every hit, VAR on every hit that left BASE, SLC carries the slice of the last hit.

## Controls

| group | control | range | default |
|---|---|---|---|
| Chains | Base weight | 0 … 100 | 55 |
| Chains | Pitch down weight | 0 … 100 | 22 |
| Chains | Pitch up weight | 0 … 100 | 12 |
| Chains | Sub octave weight | 0 … 100 | 6 |
| Chains | Glitch weight | 0 … 100 | 4 |
| Chains | Reverse weight | 0 … 100 | 4 |
| Chains | Drill weight | 0 … 100 | 3 |
| Sampler | Slice threshold (TRANS: 2 = every onset, 64 = strongest; EQUAL: parts) | 2 … 64 | 12 |
| Sampler | Paula sample rate | 0 … 36 | 29 |
| Sampler | Pitch | -24 … 24 | 0 |
| Sampler | Memory (a variation falls back to BASE) | 0 … 100 | 40 |
| Sampler | Volume (tracker 00-40 hex) | 0 … 64 | 56 |
| Play | Play (built-in player: the break in its own order) | 0 … 1 | 0 |
| Play | Vary lock (same variation at the same place) | 0 … 1 | 0 |
| Play | Dice (new seed) | 0 … 1 | 0 |
| Play | Anchor (OFF / DOWNBEAT / KICKS) | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (1/16 by default) | Left |
| Reset (next clock = beat 1) | Right |
| Gate (plays a slice) | Hit trigger |
| Slice (1 V/oct, 0 V = slice 00; unpatched = next slice) | Variation trigger (hit left BASE) |
| Dice trigger (new seed) | Slice of the last hit (1/12 V per slice) |
| Vary CV (scales the six variation weights, +-5 V = x0..x2) |  |

[← all modules](../../README.md#modules)
