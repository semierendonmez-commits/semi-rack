GENOTYPE is "the walk a break wants, learned from its DNA", ported from the GENOTYPE plugin on top of BREAKDNA. Break A's genome teaches the Pinball 2 matrix how the break moves from one 16th to the next, and a ball walks those steps: at MUTATION 0 the break plays as it is, at 100 it is a new walk that still lands every hit on the right place in the beat. A call & response form switches between the learned walk (CALL), a transformed one (ANSWER) and a roll (FILL). Screens: break A over the steps strip with the ball; the matrix in use, with the form.

**Patch it**
- Load break A from the menu, patch a 1/16 clock into CLK and take L / R, then raise MUTATION slowly. TEMP is the walk's temperature (x1/4..x4).
- FORM and ANSWER set the call & response; TIME is the ball's tick (x1/4..x4 of a 16th). RESET sends the ball home; HALT (trigger) stops it until the next reset.
- Break B is optional: MORPH blends in its sound (BREAKDNA's spectral layers), BLEND A>B lets its rhythm teach the walk too.
- K / S / H triggers, ACC and STEP (0-10 V over the loop) let other modules follow the walk. Without CLK it runs free at the break's tempo.
