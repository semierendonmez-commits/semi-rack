VERTEX is a percussion voice, ported from the plugin rebuild of Vertex 1.3 by aorist (aorist.io), a Max for Live instrument in the spirit of a Drummer From Another Mother. The filter is the plugin's port of sm.sallenkey~ by Pete Dowling and Matt Jackson (Surreal Machines). The device carries no licence text, so ask the author before any public release. Two oscillators, noise, an external input, the Sallen & Key filter and three envelopes are played by four 8-step rows (PITCH, VEL, MOD 1, MOD 2). The sequencer screen shows the four rows as bar cells; the scope screen shows the output, the envelopes and the oscillators.

**Patch it**
- Patch OUT to a mixer and press RUN: the internal clock runs at TEMPO. Or patch a 1/16 clock into CLK (the first edge after RST is step 1); the master clock menu divides it.
- The 2 x 8 step knobs edit PITCH + VEL or MOD 1 + MOD 2; ROWS switches between them.
- TRIG plays a note on every rising edge, pitch from V/OCT and velocity from VEL; with Master clock = EXT it is also the step trigger.
- IN takes external audio for mix, FM or filter mod. Besides the mono OUT there are a trigger, the PITCH and VEL rows and the amp and filter envelopes (0-10 V).
