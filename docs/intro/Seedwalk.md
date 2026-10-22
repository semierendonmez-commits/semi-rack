SEEDWALK is Pinball 2's Markov ball walking sixteen pinned rete nets, ported from the SEEDWALK plugin. When the ball fires a step, rete glides from where it is to that step's net: the 8 x 8 matrix is interpolated and the integrators never reset. The 17 x 17 path matrix is drawn by SHAPE from the nets' heat and distance (chain, cool down, descent, nearest ...), so the walk goes from chaotic nets to calm ones. The nets screen shows the 4 x 4 grid with heat bars, the target net, the likeliest next one and the walked path; a meter reads CALM <-> CHAOS.

**Patch it**
- Patch L / R out. STEP sets the step time, or clocks per step with a 1/16 clock in CLK. Turn GLIDE, then CHAOS (every net's SCALE) and SEED for another set of sixteen nets.
- FROM / TO limit the walk to a range of nets (FROM > TO wraps round 16 > 1); X / Y pick a grid cell and send the ball there; RST relaunches the ball.
- HOLD (latch or gate) stops the ball and freezes the glide; RND re-rolls the nets in the range; PIN works while held. IN feeds audio into excite.
- NET (1/12 V per net) and EOC (the walk is back where it started) go out. Factory walks, glide curve and Beads are in the menu.
