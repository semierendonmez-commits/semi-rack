# SEEDWALK

*Markov ball walking sixteen pinned rete nets with glided matrix interpolation; walk range, hold and per-net randomise*

18 HP — Noise · Random · Sequencer

<p align="center"><img src="../images/Seedwalk.png" alt="SEEDWALK" height="560"></p>

SEEDWALK is Pinball 2's Markov ball walking sixteen pinned rete nets, ported from the SEEDWALK plugin. When the ball fires a step, rete glides from where it is to that step's net: the 8 x 8 matrix is interpolated and the integrators never reset. The 17 x 17 path matrix is drawn by SHAPE from the nets' heat and distance (chain, cool down, descent, nearest ...), so the walk goes from chaotic nets to calm ones. The nets screen shows the 4 x 4 grid with heat bars, the target net, the likeliest next one and the walked path; a meter reads CALM <-> CHAOS.

**Patch it**
- Patch L / R out. STEP sets the step time, or clocks per step with a 1/16 clock in CLK. Turn GLIDE, then CHAOS (every net's SCALE) and SEED for another set of sixteen nets.
- FROM / TO limit the walk to a range of nets (FROM > TO wraps round 16 > 1); X / Y pick a grid cell and send the ball there; RST relaunches the ball.
- HOLD (latch or gate) stops the ball and freezes the glide; RND re-rolls the nets in the range; PIN works while held. IN feeds audio into excite.
- NET (1/12 V per net) and EOC (the walk is back where it started) go out. Factory walks, glide curve and Beads are in the menu.

## Controls

| group | control | range | default |
|---|---|---|---|
| Walk | Step time | 0 … 8 | 4 |
| Walk | Walk shape | 0 … 11 | 0 |
| Walk | Spread | 0.05 … 1.5 | 0.4 |
| Walk | Net seed | 1 … 99 | 1 |
| Walk | Pin net | 0 … 1 | 0 |
| Range | Range from (net) | 1 … 16 | 1 |
| Range | Range to (net) | 1 … 16 | 16 |
| Hold / Random | Hold (latch) | 0 … 1 | 0 |
| Hold / Random | Randomise nets | 0 … 1 | 0 |
| Glide | Glide | 0 … 100 | 75 |
| Glide | Dip | 0 … 100 | 0 |
| Net | Chaos | -50 … 50 | 0 |
| Net | Excite | 0 … 100 | 50 |
| Out | FX mix | 0 … 100 | 40 |
| Out | FX mode | 0 … 8 | 0 |
| Out | Level | -24 … 12 | -3 |

## Inputs and outputs

| inputs | outputs |
|---|---|
| Clock (1/16; a step = STEP clocks) | Left |
| Reset (relaunch the ball) | Right |
| Chaos CV (10 %/V) | Current net (1/12 V per net, 0-1.25 V) |
| X: grid column, 0-10 V (a change sends the ball there) | End of cycle (the walk is back at its first net) |
| Y: grid row, 0-10 V (a change sends the ball there) |  |
| Audio into excite |  |
| Hold gate (high = held) |  |
| Randomise nets (trigger) |  |
| Range from CV (1.5 nets/V) |  |
| Range to CV (1.5 nets/V) |  |

[← all modules](../../README.md#modules)
