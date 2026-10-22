# ILLUSORIUM

*26 psychoacoustic illusions as one stereo effect (Shepard, Risset, Deutsch, Huggins, Zwicker, Warren, Bregman, Haas, Franssen and more), one at a time*

20 HP — Effect · Stereo · Pitch Shifter · Delay · Filter · Visual

<p align="center"><img src="../images/Illusorium.png" alt="ILLUSORIUM" height="560"></p>

ILLUSORIUM is 26 psychoacoustic illusions as one stereo effect, ported from the ILLUSORIUM plugin: Shepard, Risset, Deutsch, Huggins, Zwicker, Warren, Bregman, Haas, Franssen and more. The plugin chains all 26; here ILLUSION picks one at a time. P1-P6 are the selected illusion's first four continuous parameters and first two choices, and every illusion keeps its own settings. The illusion screen shows its number, name, who described it and a drawing of the percept (a barber pole for SHEPARD, phantom harmonics, a pitch-class clock for TRITONE, or the signal / percept pair as text over meters); the params screen names P1-P6.

**Patch it**
- Patch a stereo source into IN (R normalled to L) and OUT L / R onward, turn ILLUSION and read the params screen; MIX is the wet / dry. A menu test source (WHITE, PINK, SINE, SAW, CHORD, PULSES) can be added to IN or replace it.
- Rhythmic illusions follow CLK (1/16 notes by default) or, unpatched, the menu tempo. GRAB captures, restarts or reseeds RISSET, SPEECH>SONG, PHANTOM WORDS, CONST SPECTRUM and SCRAMBLE.
- MOD puts CV on one of P1-P6 (menu), 10 V = full range. The rest of each illusion's parameters are under "Illusion parameters" in the menu. Switching illusions fades out and back in.

**MetaModule** — MASK is not available (the input passes dry and the screen says so), and RISSET, CONST SPECTRUM and SHEPARD run lighter.

## Controls

| group | control | range | default |
|---|---|---|---|
| Illusion | Illusion | 0 … 25 | 4 |
| Illusion | Mix | 0 … 1 | 1 |
| Illusion | Grab / restart / reseed | 0 … 1 | 0 |
| Parameters | P1 | 0 … 1 | 0.5 |
| Parameters | P2 | 0 … 1 | 0.5 |
| Parameters | P3 | 0 … 1 | 0.5 |
| Parameters | P4 | 0 … 1 | 0.5 |
| Parameters | P5 | 0 … 1 | 0.5 |
| Parameters | P6 | 0 … 1 | 0.5 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Left | Left |
| Right (normalled to left) | Right |
| Clock (1/16 notes by default) |  |
| Reset |  |
| Grab trigger |  |
| Mod CV onto P1..P6 (menu), 10 V = full range |  |
| Mix CV (10 V = 100 %) |  |

[← all modules](../../README.md#modules)
