# SETBOOK

*Family scene memory: 8 scenes x 8 CV channels with timed morph, curves, X/Y pad, BUS scene / EPOCH cues; in Rack also morphs every SEMI module's knobs*

24 HP — Utility · Controller · Sequencer · Visual

<p align="center"><img src="../images/Setbook.png" alt="SETBOOK" height="560"></p>

SETBOOK is the family's scene memory, ported from the SETBOOK plugin. In the DAW it asks every sibling for its state; here it is a CV scene memory for the cable family: 8 scenes x 8 CV channels. A channel's source is its knob plus its input, and the outputs follow the sources (LIVE) until a scene is recalled, then hold it. Recall is a timed morph with a curve, and the PAD morphs X / Y between four corner scenes. Three screens: eight scene cards (channels as bars, distance from the outputs), the cue lane over the BUS 32-bar cycle, and the morph with its curve or pad.

**Patch it**
- Patch CV sources into the channels (or just use the knobs), take the outputs, and press STORE to capture a scene; hold STORE and press a scene button to store straight into it. Press a scene button to recall.
- TIME is the morph length in beats of the BUS tempo (0 = cut), CURVE picks LINEAR / SMOOTH / EARLY / LATE, THRESH handles stepped (semitone) and gate channels. Moving a knob gives that channel back to LIVE.
- Scenes also change from SCN CV (1 V per scene; with RCL patched only on a RCL trigger), NXT, the BUS SCENE channel (EPOCH's sections), EPOCH turns and BUS POS cues (menu). END and MORPH report the end and position of the morph.

**MetaModule** — FAMILY, which captures and morphs the knobs of every SEMI module in the patch, is Rack only.

## Controls

| group | control | range | default |
|---|---|---|---|
| Scenes | Scene 1 (hold STORE: store into it) | 0 … 1 | 0 |
| Scenes | Scene 2 (hold STORE: store into it) | 0 … 1 | 0 |
| Scenes | Scene 3 (hold STORE: store into it) | 0 … 1 | 0 |
| Scenes | Scene 4 (hold STORE: store into it) | 0 … 1 | 0 |
| Scenes | Scene 5 (hold STORE: store into it) | 0 … 1 | 0 |
| Scenes | Scene 6 (hold STORE: store into it) | 0 … 1 | 0 |
| Scenes | Scene 7 (hold STORE: store into it) | 0 … 1 | 0 |
| Scenes | Scene 8 (hold STORE: store into it) | 0 … 1 | 0 |
| Scenes | Store (capture the outputs into a scene) | 0 … 1 | 0 |
| Scenes | Live: every output follows its knob + input | 0 … 1 | 0 |
| Scenes | Pad on / off (X / Y morph of the four corner scenes) | 0 … 1 | 0 |
| Morph | Morph time | 0 … 1 | 0.25 |
| Morph | Morph curve | 0 … 3 | 1 |
| Morph | Threshold | 0 … 1 | 0.5 |
| Channels | Channel 1 | 0 … 1 | 0 |
| Channels | Channel 2 | 0 … 1 | 0 |
| Channels | Channel 3 | 0 … 1 | 0 |
| Channels | Channel 4 | 0 … 1 | 0 |
| Channels | Channel 5 | 0 … 1 | 0 |
| Channels | Channel 6 | 0 … 1 | 0 |
| Channels | Channel 7 | 0 … 1 | 0 |
| Channels | Channel 8 | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Channel 1 | Channel 1 |
| Channel 2 | Channel 2 |
| Channel 3 | Channel 3 |
| Channel 4 | Channel 4 |
| Channel 5 | Channel 5 |
| Channel 6 | Channel 6 |
| Channel 7 | Channel 7 |
| Channel 8 | Channel 8 |
| Bus | Bus |
| Scene CV | End of morph |
| Recall trigger | Morph position |
| Next scene trigger |  |
| Pad X |  |
| Pad Y |  |

[← all modules](../../README.md#modules)
