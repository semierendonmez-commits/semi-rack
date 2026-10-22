EPOCH is the long clock, ported from the EPOCH plugin: a macro timeline of energy and silence that conducts the family over the BUS cable. A plan (ENERGY and SPACE curves plus sections, which give holes, tension, turns and macros at every 16th) is played from the clock and sent out as the FamilyBus CV (POS, ENERGY, SCENE, EVENT) and as four 0-10 V macros: NRG, DENS, CHS and DUCK. The timeline screen shows the holes shaded, energy in cyan, density dashed in amber, the section strip and the playhead; the readout shows BAR, ENERGY, DENSITY and SCENE.

**Patch it**
- Patch BUS into a sibling's BUS input (TURNAROUND, SETBOOK, DATASCREEN) and NRG or DENS into whatever should follow the arc. The plan button steps through the plans; LENGTH stretches the plan to a set number of bars, keeping its shape.
- CLK takes 1/16 by default (menu: 1/8, 1/4, 24 PPQN); unpatched, the plan runs at its own tempo. RUN toggles run / stop, RST goes back to bar 1.
- SCALE scales the energy curve, SMOOTH adds lag to the macro outs, SILENCE scales the share of time cut into holes. The menu holds the shape (seed, hole grain, cluster, tension, turn line), what happens after the end, and (Rack) measuring a reference WAV.

**MetaModule** — Measuring a reference WAV is Rack only, and plan renders wait for the knob to settle (about 10 ms per render on the A7).
