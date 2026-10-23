# NEUROSTACK

*Neuro bass voice: eight layers on one phase switched by the Pinball Markov walk, a 32-step rhythm, pitch-tracked split over one clean sub*

20 HP — Synth voice · Sequencer · Random

<p align="center"><img src="../images/Neurostack.png" alt="NEUROSTACK" height="560"></p>

NEUROSTACK is a neuro bass voice, ported from the NEUROSTACK plugin. Eight neuro bass layers run on one master phase and are switched by the Pinball 2 Markov walk, split at SPLIT x f0 over one clean sine sub, with a 32-step rhythm. TONE, MOTION and GRIT on the panel offset all eight slots; the plugin's 21 factory presets and 17 rhythms are included. Three screens: the Pinball matrix with each step's slot and the 32-cell rhythm; a scope locked to the master phase, upper band against low end; and the eight layers with their model, envelope and phase against the sub.

**Patch it**
- Patch a sequencer's V/OCT and GATE in, a 1/16 clock into CLK and L / R out. Step PRESET, then turn TONE, MOTION and GRIT. A glide on the V/OCT cable is followed.
- TIME sets how often the ball moves, XFADE the crossfade at each switch, SHAPE picks the next matrix shape. RHYTHM turns the rhythm on; VARY adds rhythm edits, DRIFT a slow tone / motion LFO.
- DUCK is the sidechain input. SWITCH fires on every switch; F0 into SUBSTRATE's V/OCT hands it the pitch.
- Without CLK it free-runs at the menu tempo. Each slot's model and values, the 16 steps, the matrix shape and the rhythm are in the menu.

## Controls

| group | control | range | default |
|---|---|---|---|
| Pinball | Time (how often the ball moves) | 0 … 11 | 10 |
| Pinball | Crossfade (the switch) | 0 … 1 | 0.474539 |
| Pinball | Next matrix shape | 0 … 1 | 0 |
| Pinball | Next preset | 0 … 1 | 0 |
| Sound | Tone (all slots) | -0.5 … 0.5 | 0 |
| Sound | Motion (all slots) | -0.5 … 0.5 | 0 |
| Sound | Grit (all slots) | -0.5 … 0.5 | 0 |
| Sound | Drive | 0 … 1 | 0.25 |
| Rhythm | Rhythm on / off | 0 … 1 | 0 |
| Rhythm | Variation (rhythm edits) | 0 … 1 | 0.35 |
| Rhythm | Drift (slow tone / motion LFO) | 0 … 1 | 0.25 |
| Low end | Split | 1.5 … 6 | 2.5 |
| Low end | Sub | 0 … 1 | 0.7 |
| Output | Width (above the split) | 0 … 1 | 0.35 |
| Output | Level | -24 … 6 | -9 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| V/OCT | Left |
| Gate | Right |
| Clock | Switch trigger |
| Reset | Layer |
| Tone CV (10 %/V) | F0 (V/OCT) |
| Duck sidechain |  |

[← all modules](../../README.md#modules)
