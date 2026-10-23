# TURNAROUND

*Stereo phrase-end modulator: 4/8/16/32 bar turns, edit budget, rolls, wall and landing gap*

14 HP — Sequencer · Logic · Effect

<p align="center"><img src="../images/Turnaround.png" alt="TURNAROUND" height="560"></p>

TURNAROUND is a stereo phrase-end modulator for breaks, ported unchanged from the TURNAROUND plugin. Every 4th bar line is a turn (4 / 8 / 16 / 32-bar tier), and before it the engine rewrites the last beats of the break playing through IN using the break's own slices: ghost kicks, rolls, retriggers, reverse, tape stop, cut. Edits are paid for out of a budget per 32 bars, then comes the wall, silence right before the bar line. Both sides get the same plan. The screen shows the 32-bar block as four phrase lines: a filled cell where a turn got budget, an outline where it asked but got none.

**Patch it**
- Patch a break into IN L / R (R normalled to L), OUT L / R onward, and a 1/16 clock into CLK; the first clock after RST is bar 1. If the clock stops for 2.5 periods the audio passes dry.
- Or patch EPOCH's BUS: the bar comes from POS and ENERGY scales the edit depth; CLK, if also patched, only refines the tempo.
- BUDGET, DEPTH and VARY set how much is edited; ROLL sets the roll pitch, WALL where the wall falls and GAP its length. TURN and ROLL outs mark turns and rolls; BUS out adds this module's EVENT.
- The menu holds presets, the turn ladder (action and window per tier) and the roll source.

## Controls

| group | control | range | default |
|---|---|---|---|
| Budget | Edit budget | 0 … 1 | 0.5 |
| Budget | Edit depth | 0 … 1 | 1 |
| Budget | Variation | 0 … 1 | 0.25 |
| Roll | Roll pitch | -12 … 24 | 5 |
| Wall | Wall at | 0 … 4 | 3 |
| Wall | Wall length | 0 … 3 | 1 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (1/16 notes) | Audio L |
| Reset (next clock = bar 1) | Turn gate (one 1/16) |
| FamilyBus (POS = bar, ENERGY scales depth) | Roll gate |
| Break audio L | FamilyBus (input + this module's EVENT) |
| Break audio R (normalled to L) | Audio R |

[← all modules](../../README.md#modules)
