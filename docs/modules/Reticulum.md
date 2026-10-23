# RETICULUM

*Eight-node rete noise net with Lissajous scope, node matrix and drunk/walk/random modulation*

16 HP — Noise · Random · Physical modeling

<p align="center"><img src="../images/Reticulum.png" alt="RETICULUM" height="560"></p>

RETICULUM is an eight-node rete noise net, ported unchanged from the RETICULUM plugin, which plays a patch: a MANUAL GATE re-rolls rete's matrix, the two RANDOM VALUES banks and MG-16; the banks drive the eight node gains; and rete's L/R runs through artifex and, optionally, Beads to the outputs. LEAK, SCALE, LVL and the node gains each carry a modulator (RAND / WALK / DRUNK), sharing one mode and one rate. The scope screen draws the output as X / Y (L/R or M/S), auto-zoomed; the matrix screen shows an 8x8 heat map of the matrix the seed makes, with the seed, the gate and each node's activity.

**Patch it**
- Patch OUT L / R and hit the gate pad to re-roll the net. Raise SCALE (matrix drive) and LEAK, then shape it with the eight node gains.
- The gate clock button picks HAND, FREE or SYNC; CLK HZ and FLUX set the gate clock, CLK sets the tempo for SYNC and synced modulators, the GATE input fires it too.
- The modulator button picks OFF / RAND / WALK / DRUNK, with RND as depth and RATE as rate. EXC feeds volts into excite (scaled by LVL); ARTFX and BEADS set the effects; LEAK and SCALE take CV (10 %/V).
- Gate destinations, clock division, bank ranges, MG-16, artifex, Beads, the output stage and the scope view are in the context menu.

**MetaModule** — Menu values are picked from a list of steps instead of a slider.

## Controls

| group | control | range | default |
|---|---|---|---|
| Nodes | Node 1 gain | 0 … 1 | 0.5 |
| Nodes | Node 2 gain | 0 … 1 | 0.5 |
| Nodes | Node 3 gain | 0 … 1 | 0.5 |
| Nodes | Node 4 gain | 0 … 1 | 0.5 |
| Nodes | Node 5 gain | 0 … 1 | 0.5 |
| Nodes | Node 6 gain | 0 … 1 | 0.5 |
| Nodes | Node 7 gain | 0 … 1 | 0.5 |
| Nodes | Node 8 gain | 0 … 1 | 0.5 |
| Rete | Leak | 0 … 1 | 0.5 |
| Rete | Scale (matrix drive) | 0 … 1 | 0.67 |
| Rete | Lvl (excite) | 0 … 1 | 0 |
| Mod | Modulator mode (OFF / RAND / WALK / DRUNK) | 0 … 1 | 0 |
| Mod | Modulation depth | 0 … 1 | 0.5 |
| Mod | Modulation rate | 0 … 1 | 0.555238 |
| Gate | Gate pad | 0 … 1 | 0 |
| Gate | Gate clock (HAND / FREE / SYNC) | 0 … 1 | 0 |
| Gate | Gate clock rate | 0 … 1 | 0.468308 |
| Gate | Gate clock flux | 0 … 1 | 0 |
| FX | Artifex amount | 0 … 1 | 0.3 |
| FX | Beads mix | 0 … 1 | 0 |
| FX | Level | -24 … 12 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Gate | Left |
| Clock (tempo for SYNC and synced modulators) | Right |
| Leak CV (10 %/V) |  |
| Scale CV (10 %/V) |  |
| Excite (volts, scaled by LVL) |  |

[← all modules](../../README.md#modules)
