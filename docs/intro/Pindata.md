PINDATA is CELLULA, PALIMPSEST and DATUM played by the Pinball 2 Markov sequencer and fed by any file, ported from the PINDATA plugin. The file is the material; the ball walks the steps and fires eight lanes, each with its own destination and note (menu), which come out as gate, pitch and velocity. The VISUAL screen shows one of eight datamatic scenes (matrix, network, field, automata, buffers, barcode, numbers, spectra) over a HUD with the scene, head, step lights and lanes; the INFO strip shows the file, read head and transport.

**Patch it**
- Load a file (or one of the nine example texts) from the menu, patch L / R out and press RUN, or patch a 1/16 clock into CLK. Mix the layers with IKD, INGP and DATUM.
- RST relaunches the ball and sends the transport to bar 1; HALT stops the ball until the next reset. MULT, SWING and PATTERN shape the walk; SCAN, STEP and RATE move the read head, HEAD sets it from outside.
- FIRE (poly) fires lanes: channel k fires lane k, mono fires lane 1. GATE / PITCH / VEL, STEP and BUS go out to the family; SCENE CV picks the scene.

**MetaModule** — Data files are read up to 256 KB and decoded audio up to about 2 s (8 MB and 60 s in Rack).
