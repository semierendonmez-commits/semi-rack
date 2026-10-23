# FLOATER

*A floater case: one clock, a rotating divider, eight Euclidean lanes with chance and bursts, macro-oscillator, granular and texture voices (Mutable Instruments open-source DSP), a pin matrix and the REEL*

32 HP — Drum · Sequencer · Synth voice · Random · Visual

<p align="center"><img src="../images/Floater.png" alt="FLOATER" height="560"></p>

FLOATER is a whole floater case in one module, ported from FLOATER v0.7 with Mutable Instruments Plaits, Braids, Clouds and Beads, Parasites, CloudSeed and a Drum Buss model inside. A master clock feeds a rotating divider and eight lanes (Euclidean or drawn, with chance, bursts, LOCK and VARY) that play seven module slots (own voices, 24 Plaits engines, 31 Braids shapes, REEL SPLICE) through SCATTER, ZIGZAG, SPACE, the REEL and the Drum Buss, alongside a pin matrix, SEQ, DRONE, RESONATOR, ARC and FEEDBACK. 34 factory patches are included. Screens: the eight lanes, the REEL's splice window, and the pin matrix.

**Patch it**
- Patch L / R out and press RUN (its own clock at BPM), or patch a 1/16 clock into CLK (menu: 1/8, 1/4, 24 PPQN; RST = bar 1). NEW gives a new patch, MUTATE varies it.
- Select a lane with its button: STEPS, HITS, OFFSET, DIV, CHANCE, BURST, LOCK and VARY edit its ring, TUNE to SEND its module. STRIKE plays it by hand (STRIKE input: mono = the selected lane, poly channel n = lane n).
- Each lane has a 10 V, 5 ms gate out (bursts included); the CV out carries a matrix source chosen in the menu.
- Everything else from the plugin's pages (slot models, SCATTER, the pin matrix, SEQ, effect details) is in the context menu and saved with the patch.

**MetaModule** — Continuous menu values are picked from 11 steps instead of a slider.

## Controls

| group | control | range | default |
|---|---|---|---|
| Lanes | Select lane KICK | 0 … 1 | 0 |
| Lanes | Select lane MESH | 0 … 1 | 0 |
| Lanes | Select lane FM | 0 … 1 | 0 |
| Lanes | Select lane OPTO | 0 … 1 | 0 |
| Lanes | Select lane HATS | 0 … 1 | 0 |
| Lanes | Select lane ZAP | 0 … 1 | 0 |
| Lanes | Select lane STRK | 0 … 1 | 0 |
| Lanes | Select lane SCAT | 0 … 1 | 0 |
| Lanes | Lane steps | 1 … 32 | 16 |
| Lanes | Lane hits | 0 … 32 | 4 |
| Lanes | Lane offset | 0 … 31 | 0 |
| Lanes | Lane division | 1 … 8 | 1 |
| Lanes | Lane chance | 0 … 1 | 1 |
| Lanes | Lane burst | 0 … 1 | 0 |
| Lanes | Lane lock (Turing: 100% loops as set) | 0 … 1 | 1 |
| Lanes | Lane vary | 0 … 1 | 0 |
| Module | Tune | 0 … 1 | 0.5 |
| Module | Decay | 0 … 1 | 0.4 |
| Module | Color | 0 … 1 | 0.5 |
| Module | Harm | 0 … 1 | 0.5 |
| Module | Morph | 0 … 1 | 0.5 |
| Module | Level | 0 … 1 | 0.8 |
| Module | Send | 0 … 1 | 0.2 |
| Module | Strike the selected lane | 0 … 1 | 0 |
| Case | Tempo (without CLK) | 40 … 220 | 150 |
| Case | Rotate (the divider) | 0 … 7 | 0 |
| Case | ZIGZAG mix | 0 … 1 | 0.3 |
| Case | SPACE mix | 0 … 1 | 0.25 |
| Case | REEL order <> chaos | 0 … 1 | 0 |
| Case | REEL varispeed (75% = as recorded) | 0 … 1 | 0.75 |
| Case | Swing | 0 … 0.5 | 0 |
| Case | Turns (fills before 4/8/16/32) | 0 … 1 | 0 |
| Case | NEW PATCH | 0 … 1 | 0 |
| Case | MUTATE | 0 … 1 | 0 |
| Case | Run (own clock) | 0 … 1 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (1/16 by default, see menu) | Left |
| Reset (next clock = bar 1) | Right |
| Run gate (high = running) | Gate KICK (every hit, bursts included) |
| Strike: mono = the selected lane; poly = channel n strikes lane n | Gate MESH (every hit, bursts included) |
| NEW PATCH trigger | Gate FM (every hit, bursts included) |
| MUTATE trigger | Gate OPTO (every hit, bursts included) |
| REEL chaos CV (+10% per V) | Gate HATS (every hit, bursts included) |
| Rotate CV (one step per V) | Gate ZAP (every hit, bursts included) |
|  | Gate STRK (every hit, bursts included) |
|  | Gate SCAT (every hit, bursts included) |
|  | CV (a matrix source, +-5 V; menu) |

[← all modules](../../README.md#modules)
