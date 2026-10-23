# SPLICEBUS

*FLOATER's REEL as a bus effect: splices and genes on the clock, ORDER <> CHAOS over a split that keeps the bass whole, TURNAROUND's phrase form*

16 HP — Effect · Granular · Sampler · Visual

<p align="center"><img src="../images/Splicebus.png" alt="SPLICEBUS" height="560"></p>

SPLICEBUS is FLOATER's REEL as a stereo bus effect, ported from SPLICEBUS 0.1: splices and genes on the clock, ORDER <> CHAOS, over SUBSTRATE's split, with TURNAROUND's phrase form. The input is split (OFF / FIXED / TRACK) so the bass stays whole, and the REEL works above the split. The plugin's 14 presets and MUTATE are included. BRK is high while a gene breaks and TURN inside a turn window. The tape screen shows the reel with the split readout; the form screen shows 32 bars of turn windows, walls and anchors, the current 4-bar line slot by slot, the next one and the budget.

**Patch it**
- Patch a channel into IN L / R (R normalled to L), OUT L / R onward, and a 1/16 clock into CLK (RST = bar 1 on the next clock). Turn CHAOS, then VARI and MORPH.
- Or patch BUS: POS gives the bar and ENERGY scales CHAOS. With neither, FREE RUN at BPM (menu).
- SPLIT picks OFF / FIXED / TRACK (SPLIT HZ for FIXED); SC is the sidechain for PROTECT BY SC HITS / SC LEVEL. HOLD stops recording.
- LENGTH, GENE, BUDGET, WALL, ANCHOR, SEED, presets and MUTATE are in the menu. Latency is 3 ms (OFF / FIXED) or about 60 ms (TRACK), plus 64 samples.

## Controls

| group | control | range | default |
|---|---|---|---|
| Reel | Order <> chaos | 0 … 1 | 0.2 |
| Reel | Varispeed (75% = as recorded, 50% stopped) | 0 … 1 | 0.75 |
| Reel | Morph (gaps / seamless / layers) | 0 … 1 | 0.45 |
| Reel | Organize (which splice) | 0 … 1 | 0 |
| Reel | Slide (where in the splice) | 0 … 1 | 0 |
| Reel | Hold (stop recording) | 0 … 1 | 0 |
| Form | Turns (4/8/16/32) | 0 … 1 | 0.6 |
| Form | Roll | 0 … 1 | 0.5 |
| Split / out | Split (OFF / FIXED / TRACK) | 0 … 1 | 0 |
| Split / out | Split frequency (FIXED) | 30 … 1000 | 120 |
| Split / out | Protect | 0 … 1 | 0.8 |
| Split / out | Mix | 0 … 1 | 1 |
| Split / out | Output | -24 … 12 | 0 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Left | Left |
| Right (normalled to left) | Right |
| Sidechain (PROTECT BY SC HITS / SC LEVEL) | Break gate (a gene breaks) |
| Clock (1/16 notes) | Turn gate (inside a turn window) |
| Reset (next clock = bar 1) |  |
| FamilyBus (POS = the bar, ENERGY scales CHAOS) |  |
| Chaos CV (+10% per V) |  |

[← all modules](../../README.md#modules)
