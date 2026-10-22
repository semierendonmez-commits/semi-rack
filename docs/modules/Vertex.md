# VERTEX

*Percussion voice after Vertex 1.3 by aorist (aorist.io, Max for Live): two oscillators, noise, Sallen & Key filter (port of sm.sallenkey~ by Surreal Machines), three envelopes and four 8-step rows*

24 HP — Drum · Synth voice · Sequencer · Oscillator · Filter

<p align="center"><img src="../images/Vertex.png" alt="VERTEX" height="560"></p>

VERTEX is a percussion voice, ported from the plugin rebuild of Vertex 1.3 by aorist (aorist.io), a Max for Live instrument in the spirit of a Drummer From Another Mother. The filter is the plugin's port of sm.sallenkey~ by Pete Dowling and Matt Jackson (Surreal Machines). The device carries no licence text, so ask the author before any public release. Two oscillators, noise, an external input, the Sallen & Key filter and three envelopes are played by four 8-step rows (PITCH, VEL, MOD 1, MOD 2). The sequencer screen shows the four rows as bar cells; the scope screen shows the output, the envelopes and the oscillators.

**Patch it**
- Patch OUT to a mixer and press RUN: the internal clock runs at TEMPO. Or patch a 1/16 clock into CLK (the first edge after RST is step 1); the master clock menu divides it.
- The 2 x 8 step knobs edit PITCH + VEL or MOD 1 + MOD 2; ROWS switches between them.
- TRIG plays a note on every rising edge, pitch from V/OCT and velocity from VEL; with Master clock = EXT it is also the step trigger.
- IN takes external audio for mix, FM or filter mod. Besides the mono OUT there are a trigger, the PITCH and VEL rows and the amp and filter envelopes (0-10 V).

## Controls

| group | control | range | default |
|---|---|---|---|
| Oscillators | Osc 1 frequency | 0 … 1 | 0.420649 |
| Oscillators | Osc 1 pitch envelope | -600 … 600 | 0 |
| Oscillators | Osc 1 level | 0 … 1 | 0 |
| Oscillators | Noise level | 0 … 1 | 0 |
| Oscillators | Osc 2 frequency | 0 … 1 | 0.420649 |
| Oscillators | Osc 2 pitch envelope | -600 … 600 | 0 |
| Oscillators | Osc 2 level | 0 … 1 | 1 |
| Oscillators | Osc 1 -> Osc 2 FM | 0 … 600 | 0 |
| Oscillators | Osc 1 shape (TRI / SQU / SIN / SAW) | 0 … 1 | 0 |
| Oscillators | Osc 2 shape (TRI / SQU / SIN / SAW) | 0 … 1 | 0 |
| Envelopes / filter | Attack (all envelopes) | 0 … 1 | 0 |
| Envelopes / filter | Pitch envelope decay | 0 … 1 | 0.212666 |
| Envelopes / filter | Amp envelope decay | 0 … 1 | 0.212666 |
| Envelopes / filter | Volume | 0 … 2 | 1 |
| Envelopes / filter | Cutoff | 0 … 140 | 140 |
| Envelopes / filter | Resonance | 0 … 1 | 0.3 |
| Envelopes / filter | Filter envelope | -150 … 150 | 0 |
| Envelopes / filter | Filter envelope decay | 0 … 1 | 0.212666 |
| Sequencer | Row A (PITCH / MOD 1) step 1 | 0 … 1 | 0.5 |
| Sequencer | Row A (PITCH / MOD 1) step 2 | 0 … 1 | 0.5 |
| Sequencer | Row A (PITCH / MOD 1) step 3 | 0 … 1 | 0.5 |
| Sequencer | Row A (PITCH / MOD 1) step 4 | 0 … 1 | 0.5 |
| Sequencer | Row A (PITCH / MOD 1) step 5 | 0 … 1 | 0.5 |
| Sequencer | Row A (PITCH / MOD 1) step 6 | 0 … 1 | 0.5 |
| Sequencer | Row A (PITCH / MOD 1) step 7 | 0 … 1 | 0.5 |
| Sequencer | Row A (PITCH / MOD 1) step 8 | 0 … 1 | 0.5 |
| Sequencer | Row B (VEL / MOD 2) step 1 | 0 … 1 | 1 |
| Sequencer | Row B (VEL / MOD 2) step 2 | 0 … 1 | 1 |
| Sequencer | Row B (VEL / MOD 2) step 3 | 0 … 1 | 1 |
| Sequencer | Row B (VEL / MOD 2) step 4 | 0 … 1 | 1 |
| Sequencer | Row B (VEL / MOD 2) step 5 | 0 … 1 | 1 |
| Sequencer | Row B (VEL / MOD 2) step 6 | 0 … 1 | 1 |
| Sequencer | Row B (VEL / MOD 2) step 7 | 0 … 1 | 1 |
| Sequencer | Row B (VEL / MOD 2) step 8 | 0 … 1 | 1 |
| Sequencer | Step knobs edit PITCH + VEL / MOD 1 + MOD 2 | 0 … 1 | 0 |
| Clock | Tempo (CLK unpatched) | 40 … 240 | 120 |
| Clock | Run / stop | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (1/16 notes by default) | Audio |
| Reset (next clock = step 1) | Trigger |
| Trigger (note-on; the trigger with Master clock = EXT) | PITCH row (0-10 V) |
| V/OCT (0 V = C4, sampled at TRIG or when it changes) | VEL row (0-10 V) |
| Velocity (0-10 V, the M mod source) | Amp envelope (0-10 V) |
| External audio (mix / FM / filter mod) | Filter envelope (0-10 V) |
| Cutoff CV (12 st/V) |  |
| FM CV (60 Hz/V) |  |

[← all modules](../../README.md#modules)
