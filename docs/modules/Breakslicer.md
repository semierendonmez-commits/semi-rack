# BREAKSLICER

*Generative tracker break slicer: onset analysis, Markov pattern with 09xx / Rxy / drills, routing matrix, S950 / S1000 / Paula samplers*

18 HP — Sampler · Drum · Sequencer · Random · Stereo

<p align="center"><img src="../images/Breakslicer.png" alt="BREAKSLICER" height="560"></p>

BREAKSLICER is a generative tracker break slicer, ported from the BREAKSLICER plugin. The break is analysed into onsets classed as kick, snare, ghost and hat, and a Markov chain writes a 16 / 32 / 64-row tracker pattern from the break's own hits plus recuts (CHOP), with 09xx offsets, Rxy retriggers, drills and a routing matrix (PASS / PITCH / SUB / REV / DRILL). It plays through a vintage sampler: CLEAN 16, AKAI S950, AKAI S1000 or AMIGA PAULA. The pattern screen shows a tracker page (ROW NOTE 09xx Rxy FX VOL); the break screen shows the waveform, class tags and playheads.

**Patch it**
- Load a WAV from the menu (30 s max; a synthetic two-bar break plays until then), patch a 1/16 clock into CLK and take L / R. Turn CHOP and DENSITY (50 % = the break's own hits), then step through SAMPLER.
- RST: the next clock is row 00. With no CLK, RUN (pad or gate) runs the internal clock at the menu tempo.
- NEW rolls a new pattern with the same settings, LOCK keeps the current one. GATE + SLICE play slices directly (1/12 V per slice from 0 V), each through its own chain.
- ROW fires on every pattern row; KICK / SNARE / GHOST / HAT fire on every hit of that class, for layering other drums.

**MetaModule** — Breaks are mono-summed to save RAM (stereo in Rack).

## Controls

| group | control | range | default |
|---|---|---|---|
| Perform | Chop (how much of the break is recut) | 0 … 1 | 0.25 |
| Perform | Keep beats | 0 … 1 | 0.6 |
| Perform | Density (50 % = the break's own hits) | 0 … 1 | 0.5 |
| Perform | Variation | 0 … 1 | 0 |
| Perform | Level | -36 … 6 | 0 |
| Rolls | Rolls (Rxy retrigger chance) | 0 … 1 | 0.12 |
| Rolls | Drill chance | 0 … 1 | 0.05 |
| Rolls | Fills | 0 … 1 | 0.35 |
| Rolls | Swing | 0 … 1 | 0 |
| Rolls | Sampler clock (S950 7.5-48, Paula 3.5-28.8) | 3.5 … 48 | 31.25 |
| Pattern | Sampler (CLEAN 16 / S950 / S1000 / PAULA) | 0 … 1 | 0 |
| Pattern | New pattern (same settings, new dice) | 0 … 1 | 0 |
| Pattern | Pattern lock | 0 … 1 | 0 |
| Pattern | Run (internal clock when CLK is not patched) | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (1/16 by default) | Left |
| Reset (next clock = row 00) | Right |
| Run gate (internal clock) | Row trigger |
| Chop CV (+-5 V = +-50 %) | Kick hit trigger |
| New pattern trigger | Snare hit trigger |
| Gate (plays a slice) | Ghost hit trigger |
| Slice (1/12 V per slice from 0 V; unpatched = next slice) | Hat hit trigger |

[← all modules](../../README.md#modules)
