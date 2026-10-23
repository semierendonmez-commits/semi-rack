# SNARESTACK

*Four snare layers (built-in or WAV) aligned in time and phase into one hit, re-weighted as crack / body / tail*

16 HP — Drum · Sampler

<p align="center"><img src="../images/Snarestack.png" alt="SNARESTACK" height="560"></p>

SNARESTACK makes four snare layers into one hit, ported from the SNARESTACK plugin. Every attack is lined up, each layer slid by a fraction of a millisecond and its polarity picked; the stack is then split into CRACK, BODY and TAIL and re-weighted per hit from a balance point (BAL X / BAL Y). Each slot plays one of the plugin's synthesised parts or a WAV, as a one-shot or as a break whose loudest snare is cut out. The align screen shows the four layers around the attack as loaded and as stacked; the balance screen shows the crack / body / tail triangle and a per-band phase meter.

**Patch it**
- The default stack uses the built-in parts, so patch L / R out and press HIT, or patch a trigger into TRIG and a velocity into VEL. Then move BAL X and BAL Y.
- ALIGN cycles OFF / ONSET / ONSET+PHASE; L1-L4 set the layer gains, CARVE lets the weaker layer step back per band, VARY adds per-hit drift.
- Load WAV layers from the menu. V/OCT and PITCH tune the stack; X, Y and DECAY take CV.

**MetaModule** — A stack rebuild takes seconds and waits for the knob to settle before it starts.

## Controls

| group | control | range | default |
|---|---|---|---|
| Layers | Layer 1 gain | -30 … 12 | 0 |
| Layers | Layer 2 gain | -30 … 12 | 0 |
| Layers | Layer 3 gain | -30 … 12 | 0 |
| Layers | Layer 4 gain | -30 … 12 | 0 |
| Stack | Align (OFF / ONSET / ONSET+PHASE) | 0 … 1 | 0 |
| Stack | Carve (the weaker layer steps back per band) | 0 … 100 | 0 |
| Balance | Balance X (body <-> tail) | 0 … 1 | 0.5 |
| Balance | Balance Y (up: crack) | 0 … 1 | 0.333333 |
| Balance | Balance depth | 0 … 24 | 12 |
| Hit | Hit | 0 … 1 | 0 |
| Hit | Pitch | -12 … 12 | 0 |
| Hit | Decay | 0 … 100 | 100 |
| Hit | Vary (per-hit drift) | 0 … 100 | 0 |
| Output | Level | -24 … 6 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Trigger | Left |
| Velocity (0-10 V) | Right |
| V/OCT (pitch, 0 V = as it is) |  |
| Balance X CV (0.1/V) |  |
| Balance Y CV (0.1/V) |  |
| Decay CV (10 %/V) |  |

[← all modules](../../README.md#modules)
