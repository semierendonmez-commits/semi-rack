# PINDATA

*IKEDAR x tst_ingp x JDAT played by the Pinball 2 Markov sequencer, fed by any file; datamatic scenes, lane gate/pitch/velocity outs, BUS*

20 HP — Synth voice · Sequencer · Noise · Random · Visual · Stereo

<p align="center"><img src="../images/Pindata.png" alt="PINDATA" height="560"></p>

PINDATA is IKEDAR, tst_ingp and JDAT played by the Pinball 2 Markov sequencer and fed by any file, ported from the PINDATA plugin. The file is the material; the ball walks the steps and fires eight lanes, each with its own destination and note (menu), which come out as gate, pitch and velocity. The VISUAL screen shows one of eight datamatic scenes (matrix, network, field, automata, buffers, barcode, numbers, spectra) over a HUD with the scene, head, step lights and lanes; the INFO strip shows the file, read head and transport.

**Patch it**
- Load a file (or one of the nine example texts) from the menu, patch L / R out and press RUN, or patch a 1/16 clock into CLK. Mix the layers with IKD, INGP and JDAT.
- RST relaunches the ball and sends the transport to bar 1; HALT stops the ball until the next reset. MULT, SWING and PATTERN shape the walk; SCAN, STEP and RATE move the read head, HEAD sets it from outside.
- FIRE (poly) fires lanes: channel k fires lane k, mono fires lane 1. GATE / PITCH / VEL, STEP and BUS go out to the family; SCENE CV picks the scene.

**MetaModule** — Data files are read up to 256 KB and decoded audio up to about 2 s (8 MB and 60 s in Rack).

## Controls

| group | control | range | default |
|---|---|---|---|
| Ball | Run (internal transport / follow CLK) | 0 … 1 | 0 |
| Ball | Manual reset (the ball back to the starting step) | 0 … 1 | 0 |
| Ball | Halt (the ball stops until the next reset) | 0 … 1 | 0 |
| Ball | Time multiplier | -2 … 2 | 0 |
| Ball | Swing | 0 … 100 | 0 |
| Ball | Pattern length | 0 … 7 | 1 |
| Data | Scan | 0 … 3 | 0 |
| Data | Step size | 0 … 5 | 2 |
| Data | Scan rate | -3.32193 … 6 | 1 |
| Layers | IKD level | -60 … 6 | 0 |
| Layers | INGP level | -60 … 6 | 0 |
| Layers | JDAT level | -60 … 6 | -3 |
| Layers | Master | -60 … 6 | -3 |
| Layers | INGP smooth | 0 … 10.9658 | 3.90689 |
| Layers | INGP tune | -24 … 24 | 0 |
| Layers | JDAT clear chance | 0 … 100 | 40 |
| Layers | IKD auto mutate chance | 0 … 100 | 0 |
| Visual | Next scene | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (1/16 by default) | Left |
| Reset (ball relaunch, transport to bar 1) | Right |
| Fire lanes (poly: channel k = lane k; mono = lane 1) | Lane gate (note length = step duration) |
| Read head (0-10 V over the file) | Lane note (1 V/oct, C4 = 0 V) |
| Time multiplier CV (+-5 V = /4 .. x4) | Lane velocity (0-10 V) |
| Scene CV (0-10 V) | Ball step (1/12 V per step, step 1 = 0 V) |
|  | FamilyBus (POS, ENERGY, SCENE, EVENT on pattern resets) |

[← all modules](../../README.md#modules)
