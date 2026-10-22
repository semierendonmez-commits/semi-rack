# DROP II

*Drop-2 switch-up generator for DnB: from drop-1 drums + bass and IDENTITY it writes drop 2 (half-time, kick shift, bass lock, flip, call/response)*

16 HP — Sequencer · Drum · Random

<p align="center"><img src="../images/Drop2.png" alt="DROP II" height="560"></p>

DROP II is a drop-2 switch-up generator for DnB, ported from the DROP II plugin with its engine unchanged. From drop 1 (drums + bass) and the IDENTITY knob it writes drop 2: half-time, kick shift, bass lock, register flip and a call / response split of the bass. IDENTITY at 1 gives drop 1 again, 0 everything the moves allow, and each seed yields four variants (A-D). A preview kit (sine kick, noise snare and hat, saw and pulse basses) plays it without patching. The screen shows three drum lanes over a bass roll (call cyan, response magenta), with what DROP II changed in amber.

**Patch it**
- Patch a 1/16 clock into CLK and KIT to a mixer to hear the built-in example drop; turn IDENTITY down, step through VARIANT, press NEW for a new seed and DROP 1 to compare.
- To use your own drop 1, patch your KICK / SNR / HAT gates and the bass B V/O + B GT into the learn inputs and press LEARN: from the next bar line they are recorded for N bars (menu). A lane that stays silent keeps its previous notes.
- RST: the next clock is bar 1; without CLK an internal tempo from the menu runs it. The drop loops over its own length in whole bars.
- Outs: KICK / SNR / HAT gates and the call and response basses as V/OCT + GATE.

## Controls

| group | control | range | default |
|---|---|---|---|
| Drop | Identity | 0 … 1 | 0.6 |
| Drop | Variant | 0 … 3 | 0 |
| Drop | New seed | 0 … 1 | 0 |
| Drop | Play drop 1 / drop II | 0 … 1 | 0 |
| Moves | Half-time | 0 … 1 | 0 |
| Moves | Kick shift | 0 … 1 | 0 |
| Moves | Bass lock | 0 … 1 | 0 |
| Moves | Register flip | 0 … 1 | 0 |
| Moves | Call / response | 0 … 1 | 0 |
| Learn | Learn drop 1 from the inputs | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (1/16 notes by default) | Kick gate |
| Reset (next clock = bar 1) | Snare gate |
| Identity CV (0-10 V adds 0-100 %) | Hat / perc gate |
| Learn: kick gate | Call bass V/OCT |
| Learn: snare gate | Call bass gate |
| Learn: hat gate | Response bass V/OCT |
| Learn: bass V/OCT | Response bass gate |
| Learn: bass gate | Preview kit audio |

[← all modules](../../README.md#modules)
