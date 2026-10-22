FLOATDAT is IKEDAR, FLOATER, VORTESSA and JDAT played by the Pinball 2 Markov sequencer, ported from FLOATDAT 2.0. Any file is the material: it is read as bytes, text or audio (WAV) into bits and symbols, and a read head feeds IKD's automata, FLOATER's lanes and sound, VTX's tuning and JDAT's buffers. The Pinball sequencer fires eight lanes; each does something inside the instrument and comes out as a gate. Three screens: VISUAL (datamatic scenes of the file under the head: barcode, matrix, scan, numbers, cells), PINBALL (the 17 x 17 matrix and the ball) and DATA (file, type, entropy, head).

**Patch it**
- Load a file from the menu, patch L / R out and press RUN (the internal clock at BPM), or patch a 1/16 clock into CLK (RST = bar 1). Balance the four layers with IKD, FLT, VTX and JDAT.
- T1-T8 are the lanes' notes as 10 V gates; LANES (poly) fires lanes from outside.
- DATA PATCH turns the data under the head into FLOATER's patch, MUTATE varies it, SCENE changes the picture, RESET sends the ball home; each has a trigger input. TIME MULT, SWING, PATTERN and SCAN RATE shape the walk.
- The plugin's other pages (the 16 steps, lane destinations and mutes, matrix shapes, the layer pages, the ten presets) are in the context menu.

**MetaModule** — Files are read up to 256 KB and WAV up to 10 s (2 MB and 60 s in Rack).
