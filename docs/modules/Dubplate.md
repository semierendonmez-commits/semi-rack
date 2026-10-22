# DUBPLATE

*Genre arrangement builder (jungle, DnB, UK garage, speed garage): template sections and role lanes as gates, fills, variations, FX events, FamilyBus*

22 HP — Sequencer · Drum · Clock generator · Random

<p align="center"><img src="../images/Dubplate.png" alt="DUBPLATE" height="560"></p>

DUBPLATE is the arrangement core of the DUBPLATE genre arrangement builder, run as a sequencer. A genre template (jungle, DnB, UK garage, speed garage) gives sections (intro, build, drop, breakdown, outro, each with an energy and phrase-locked FX events) and role lanes of 16-step patterns, on or off per section. It walks that on the clock and fires one gate per role group, KICK, SNR, HAT, OHAT, BRK, PERC, FX, SUB, BASS, BS2 and MUSIC, with fills, variations, drop-outs, snare rolls, half-time and swing acting on the gates. The screen shows the section timeline (width = bars, height = energy) over the role x section matrix, with one playhead.

**Patch it**
- The module makes no sound: patch the role gates into drums and samplers, V/OCT into a bass voice and the BRK slice CV into a slicer. Press the template button to pick a genre; FORM makes a new song form, VIP an alternative arrangement.
- CLK takes 1/16 (menu for others); the first clock after RST is bar 1. Without CLK it runs on TEMPO, which is set to the genre's tempo when a template loads. RUN starts and stops.
- NEXT jumps to the next section on the next 4-bar line (menu), FILL forces a fill on the next bar, MUT mutates. EVT is high while an FX event runs; BUS sends POS, ENERGY, SCENE and EVENT to the family.

## Controls

| group | control | range | default |
|---|---|---|---|
| Arrangement | Genre template (next) | 0 … 1 | 0 |
| Arrangement | New song form | 0 … 1 | 0 |
| Arrangement | VIP form (alternative arrangement) | 0 … 1 | 0 |
| Arrangement | Mutate | 0 … 1 | 0 |
| Arrangement | Energy macro (below 50% mutes the upper layers) | 0 … 1 | 0.5 |
| Arrangement | Chaos (fill probability) | 0 … 1 | 0.3 |
| Arrangement | Fill density | 0 … 1 | 0.35 |
| Arrangement | Swing | 50 … 75 | 51 |
| Clock | Tempo (internal clock) | 60 … 200 | 165 |
| Clock | Run / stop | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (16ths by default) | Kick (kick, kick sub, punch) |
| Reset (next clock = bar 1) | Snare (snare, clap) |
| Run / stop trigger | Hat (closed, shuffle, ride) |
| Next section (quantised) | Open hat |
| Mutate trigger | Break (break, break chop) |
| Fill on the next bar | Perc |
| Energy CV (10%/V) | FX (riser, impact) |
| Chaos CV (10%/V) | Sub |
|  | Bass (bass, reese, organ bass) |
|  | Bass 2 (variation) |
|  | Music (stab, vocal chop, pad) |
|  | Break slice (0..15 -> 0..9.4 V) |
|  | Bass V/OCT (last sub / bass note) |
|  | FX event gate |
|  | 16th clock |
|  | FamilyBus (POS, ENERGY, SCENE, EVENT) |

[← all modules](../../README.md#modules)
