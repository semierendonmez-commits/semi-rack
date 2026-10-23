# SUBSTRATE

*Automatic DnB low end: pitch-tracked mono split, a rebuilt phase-continuous sub, edit repair; low-end map and phase scope*

14 HP — Effect · Filter · Visual

<p align="center"><img src="../images/Substrate.png" alt="SUBSTRATE" height="560"></p>

SUBSTRATE is automatic DnB low end for a stereo bass, ported from the SUBSTRATE plugin. A YIN tracker finds f0, everything below SPLIT x f0 is folded to mono, and the sub is rebuilt as one clean phase-continuous sine that follows the bass's own fundamental. Cuts between resampled edits are found and crossfaded from a Burg LPC extrapolation before they are heard. Latency is fixed at about 60 ms plus 32 samples. The low-end map shows the tracked f0 over the last 4 s, the mono zone and a tick per repaired edit; the phase scope lines up the input's low band against the rebuilt sub.

**Patch it**
- Patch the bass into IN L / R, take L / R, then set SPLIT and SUB. MONITOR cycles MIX / LOW / UPPER / DELTA so you can hear each part.
- Patch V/OCT to take f0 from a cable instead of the tracker, for example NEUROSTACK's F0 output.
- PHASE cycles FREE / FOLLOW / AUTO, REPAIR switches edit repair (SENS sets its sensitivity). The SUB and F0 outs carry the rebuilt sub and the tracked pitch. MAX PITCH and FADE are in the menu.

## Controls

| group | control | range | default |
|---|---|---|---|
| Track | Mono split | 0.5 … 2 | 1.5 |
| Sub | Sub level | -36 … 12 | 0 |
| Sub | Sub phase | -180 … 180 | 0 |
| Sub | Phase follow | 0 … 1 | 0.683641 |
| Sub | Stabilize | 0 … 1 | 0 |
| Sub | Sub phase mode (FREE / FOLLOW / AUTO) | 0 … 1 | 0 |
| Bands | Low residual (-60 = off) | -60 … 6 | 0 |
| Bands | Upper level | -36 … 12 | 0 |
| Bands | Upper width | 0 … 2 | 1 |
| Repair | Edit repair on / off | 0 … 1 | 0 |
| Repair | Repair sensitivity | 0 … 1 | 0.5 |
| Output | Output | -24 … 12 | 0 |
| Output | Monitor (MIX / LOW / UPPER / DELTA) | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Left | Left |
| Right (normalled to left) | Right |
| V/OCT pitch | Rebuilt sub (mono, with SUB level) |
| Split CV (0.15 x/V) | Tracked f0 (V/OCT, 0 V = C4) |
| Sub level CV (4.8 dB/V) |  |

[← all modules](../../README.md#modules)
