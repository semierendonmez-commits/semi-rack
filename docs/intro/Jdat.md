DATUM is the Rack port of kdr_jdat.mxt, a Max patch in which a 120 ms metro writes one random -1, 0 or +1 into each of three 512-sample buffers per tick and clears them with 40 % chance. Three data voices read the buffers through wave~ at 25 Hz gated by the "mlts" lottery, and two drones read buffers 1 and 3 at 100 Hz. Rack adds FEED, so any file can supply the writes. The LCD screen fills with rows of random digits beside the lottery's heat columns; the buffer screen draws the three buffers as barcodes with the read position and a status line.

**Patch it**
- Patch L / R out and it runs on its own metro (RATE). Load any file from the menu and turn FEED to set how many of the writes come from it; DENS sets writes per buffer per tick (the patch: 1).
- CLK, RST or BUS POS replace the metro; RUN is a gate (runs while high, restarts on the rising edge). CLR forces a clear on the next tick, and BUS EVENT clears too.
- G1-G3 and the CLR out are the plugin's MIDI notes as gates, ready to trigger other voices from the data.
