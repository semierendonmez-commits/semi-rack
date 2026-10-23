# QUANTA

*Microsound particle synth after Curtis Roads (grain, glisson, grainlet, trainlet, pulsar) with an arrangement generator after Ryoji Ikeda's structures*

24 HP — Synth voice · Granular · Sequencer · Polyphonic · Random

<p align="center"><img src="../images/Quanta.png" alt="QUANTA" height="560"></p>

QUANTA is a microsound particle synth after Curtis Roads ("Microsound", 2001) with an arrangement generator built on structural habits of Ryoji Ikeda's work, ported from the QUANTA plugin. The voice makes particles of five types, GRAIN, GLISSON, GRAINLET, TRAINLET and PULSAR, with density, duration, a per-type SHAPE, masking and jitter. The arrangement plays eight channels (KICK, CLICK, DATA A/B, NOISE, SINE LO/HI, CLOUD) in blocks from a FORM and a seed. The cloud screen shows every particle at its time and pitch, coloured by type; the barcode screen shows the piece as eight channel rows over time with the playhead.

**Patch it**
- Patch V/OCT + GATE from a poly sequencer (up to 8 voices, 0 V = C4) and L / R out, then step through TYPE. With GATE unpatched the voice runs free at V/OCT (menu: off).
- Press ARR to play the arrangement from CLK (1/16 notes by default) or, unpatched, the menu tempo; REGEN rolls a new seed, RST restarts the piece. EXTREMITY, SILENCE, FILLS and MOTION shape it.
- FX HOLD (or the FX gate) are the plugin's perform pads. Outputs include a block-change trigger, block energy (0-10 V) and an end-of-piece trigger.

**MetaModule** — The particle pool is smaller, the cloud screen draws fewer particles, and samples load up to 20 s (60 s in Rack).

## Controls

| group | control | range | default |
|---|---|---|---|
| Voice | Particle type | 0 … 1 | 0 |
| Voice | Density | 0 … 1 | 0.37583 |
| Voice | Duration | 0 … 1 | 0.432204 |
| Voice | Shape | 0 … 1 | 0.242424 |
| Voice | Masking (stochastic) | 0 … 100 | 0 |
| Voice | Jitter (time, pitch, amp) | 0 … 1 | 0 |
| Voice | Stereo spread | 0 … 100 | 50 |
| Voice | Tune | -24 … 24 | 0 |
| Voice | Waveform | 0 … 4 | 0 |
| Voice | Sync | 0 … 9 | 0 |
| Voice | Window | 0 … 5 | 0 |
| Envelope | Attack | 0 … 1 | 0.12281 |
| Envelope | Decay | 0 … 1 | 0.349859 |
| Envelope | Sustain | 0 … 100 | 80 |
| Envelope | Release | 0 … 1 | 0.316517 |
| Arrangement | Arrangement on / off | 0 … 1 | 0 |
| Arrangement | Regenerate (new seed) | 0 … 1 | 0 |
| Arrangement | Form | 0 … 5 | 5 |
| Arrangement | Extremity | 0 … 100 | 40 |
| Arrangement | Silence | 0 … 100 | 10 |
| Arrangement | Fills | 0 … 100 | 50 |
| Arrangement | Motion | 0 … 100 | 25 |
| Arrangement | Arrangement level | 0 … 1 | 1 |
| Perform | Perform FX (HOLD / FX gate) | 0 … 10 | 1 |
| Perform | Perform FX hold | 0 … 1 | 0 |
| Perform | Output | -60 … 6 | -6 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| V/OCT (0 V = C4; polyphonic) | Left |
| Gate (polyphonic, up to 8 voices) | Right |
| Clock (1/16 notes by default) | Block change trigger |
| Reset (restart the piece) | Block energy (0-10 V) |
| Density CV (x2 per volt) | End of the piece trigger |
| Duration CV (x2 per volt) |  |
| Perform FX gate |  |
| Regenerate trigger |  |

[← all modules](../../README.md#modules)
