DATASCREEN is the family's visual hub, ported from the DATASCREEN plugin: one big screen that renders the 26 family scenes (PINDATA 01-12, FLOATDAT films 13-20, and case, reel, orbit, attractor, vortex and ghost 21-26) from the audio it sits on, its siblings' events and a file. A HUD over the picture shows the scene, bar / BPM, steps, lanes and the file. The audio passes through. The scenes are redrawn in NanoVG; the plugin's video output, shared-memory bus and GPU renderer are not part of this port, and every section is made from the data and the audio.

**Patch it**
- Put it on a stereo signal (IN L / R in, L / R out), patch a 1/16 clock into CLK and step through SCENE; NEXT picks the next scene in the pool, FRZ freezes the picture, FLSH flashes it.
- CHANGE sets when it cuts: manual, every 1/2/4/8 bars, on step, random, on onset or on a BUS turn. CUT fires on every cut.
- TRIG takes poly lane hits, STEP a sibling's STEP CV, BUS the FamilyBus (POS clocks the picture, ENERGY drives it, EVENT flashes or cuts). Unpatched, BUS out is made from its own clock.
- Load any file from the menu as data; HEAD (0-10 V) moves the read head over it, SCENE CV (0-10 V) picks a scene.

**MetaModule** — Data files are capped at 256 KB and decoded audio at about 2 s, and some scenes draw half the rows.
