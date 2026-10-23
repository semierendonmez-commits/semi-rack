# MANIFOLD

*Generative modular instrument: factory patches of generators and FX on 8 channel lanes, 24 macros, patterns, WANDER, CHAOS*

28 HP — Synth voice · Drum · Sequencer · Random · Mixer

<p align="center"><img src="../images/Nms.png" alt="MANIFOLD" height="560"></p>

MANIFOLD is a modular rebuild of Tim Exile's NMS (New Modular System) Reaktor framework. One module plays a whole NMS instrument: a factory patch (Dub Techno, Glitch Percussion, Poly Bells, Fast Landscape Slow Water, Organic Breaks, Swarm Garden, Keys > Sawbird, Didimidi Drives, Blank Slate) of generators and FX on eight mixer-channel lanes plus the RET A / RET B send lanes. The screen shows the routing: each lane's module chain, the cables between lanes, channel meters, and the patch, pattern, position and energy.

**Patch it**
- Patch L / R out, press RUN (or patch a 16th clock into CLK, which drives tempo, start / stop and position) and step the factory patches with PATCH < >. Then turn the 24 macros (relative, centre = 0).
- PATTERN < > and RND recall the dealt patterns, BLEND sets the blend time; WANDER moves through them at its speed, ROLL randomises (MEDIUM), CHAOS adds control noise, EVOLVE nudges every 1-16 bars.
- V/OCT + GATE play the keyboard; NEXT and SEL (0-10 V) pick patterns; MOD A / MOD B put +-5 V on two macros chosen in the menu.
- T1-T8 carry each channel's first instrument's trigger; the CLK and BUS outs go to the family.

## Controls

| group | control | range | default |
|---|---|---|---|
| Patch | Previous factory patch | 0 … 1 | 0 |
| Patch | Next factory patch | 0 … 1 | 0 |
| Patch | Run / stop (TRANSPORT) | 0 … 1 | 0 |
| Patch | Tempo (without CLK) | 40 … 240 | 118 |
| Patch | Output trim | -24 … 12 | 0 |
| Patterns | Previous pattern | 0 … 1 | 0 |
| Patterns | Next pattern | 0 … 1 | 0 |
| Patterns | Random pattern | 0 … 1 | 0 |
| Patterns | Pattern blend time | 0 … 8 | 0.5 |
| Patterns | WANDER play / stop | 0 … 1 | 0 |
| Patterns | WANDER speed (1 change / 32 bars .. / bar) | 0 … 1 | 0.35 |
| Patterns | Roll the dice (randomise MEDIUM) | 0 … 1 | 0 |
| Patterns | CHAOS amount (control noise) | 0 … 1 | 0 |
| Patterns | Evolve (randomiser every N bars) | 0 … 5 | 0 |
| Generators | Macro 1 PITCH (GENERATORS) | -1 … 1 | 0 |
| Generators | Macro 2 RANGE (GENERATORS) | -1 … 1 | 0 |
| Generators | Macro 3 MICRO INTENSE (GENERATORS) | -1 … 1 | 0 |
| Generators | Macro 4 MICRO COMPLEX (GENERATORS) | -1 … 1 | 0 |
| Generators | Macro 5 MESO INTENSE (GENERATORS) | -1 … 1 | 0 |
| Generators | Macro 6 MESO COMPLEX (GENERATORS) | -1 … 1 | 0 |
| Generators | Macro 7 MACRO INTENSE (GENERATORS) | -1 … 1 | 0 |
| Generators | Macro 8 MACRO COMPLEX (GENERATORS) | -1 … 1 | 0 |
| Shapes | Macro 9 MODULATION (SHAPES) | -1 … 1 | 0 |
| Shapes | Macro 10 COMPLEX (SHAPES) | -1 … 1 | 0 |
| Shapes | Macro 11 ATTACK (SHAPES) | -1 … 1 | 0 |
| Shapes | Macro 12 DECAY (SHAPES) | -1 … 1 | 0 |
| Shapes | Macro 13 ACCENT (SHAPES) | -1 … 1 | 0 |
| Shapes | Macro 14 SPEED (SHAPES) | -1 … 1 | 0 |
| Shapes | Macro 15 INTENSITY (SHAPES) | -1 … 1 | 0 |
| Shapes | Macro 16 CHARACTER (SHAPES) | -1 … 1 | 0 |
| Sound | Macro 17 TONE (SOUND) | -1 … 1 | 0 |
| Sound | Macro 18 RESONANCE (SOUND) | -1 … 1 | 0 |
| Sound | Macro 19 CHARACTER (SOUND) | -1 … 1 | 0 |
| Sound | Macro 20 NOISE (SOUND) | -1 … 1 | 0 |
| Sound | Macro 21 HARMONICS (SOUND) | -1 … 1 | 0 |
| Sound | Macro 22 INSTABILITY (SOUND) | -1 … 1 | 0 |
| Sound | Macro 23 THICKNESS (SOUND) | -1 … 1 | 0 |
| Sound | Macro 24 SPACE (SOUND) | -1 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (16ths; drives tempo, run and position) | Channel 1 trigger |
| Reset | Channel 2 trigger |
| Run / stop trigger | Channel 3 trigger |
| Keyboard V/OCT | Channel 4 trigger |
| Keyboard gate | Channel 5 trigger |
| Next pattern trigger | Channel 6 trigger |
| Pattern select (0-10 V) | Channel 7 trigger |
| Macro CV A (+-5 V, macro in the menu) | Channel 8 trigger |
| Macro CV B (+-5 V, macro in the menu) | 16th clock |
|  | FamilyBus (POS, ENERGY, SCENE = pattern, EVENT) |
|  | Left |
|  | Right |

[← all modules](../../README.md#modules)
