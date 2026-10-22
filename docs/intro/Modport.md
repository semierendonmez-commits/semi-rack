MODPORT is a ProTracker .mod player, ported from the MODPORT plugin: its .mod loader and a ProTracker 2.3D replayer after pt2-clone. It plays files with 1-8 channels (M.K., xCHN, OCTA, CD81, FA0x, FLT8, 15-sample SoundTracker and more) through the Amiga mix with LRRL panning and the A500 / A1200 and LED filters. Every channel also comes out as a gate and a V/OCT, so a tracker song can play other modules. The tracker screen shows the title, order / pattern and speed / tempo, nine rows around the playing one, and a strip of channel meters.

**Patch it**
- Press LOAD to pick a .mod, patch L / R out and press PLAY. Free running, the song keeps its own CIA / VBlank timing and TEMPO scales it (x0.25 .. x4).
- With CLK patched one clock edge plays one row. RST goes back to order 0, NEXT jumps to the next order, PLAY (pad or trigger) starts and stops.
- Per channel: GATE is high while the sample sounds (1 ms low on every new trigger), V/OCT follows slides and vibrato (0 V = C-2). Each channel has a mute; STEREO sets the separation, the filters are in the menu.

**MetaModule** — .mod files over 4 MB are refused, a limit set for MetaModule RAM.
