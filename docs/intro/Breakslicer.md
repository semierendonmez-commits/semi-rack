BREAKSLICER is a generative tracker break slicer, ported from the BREAKSLICER plugin. The break is analysed into onsets classed as kick, snare, ghost and hat, and a Markov chain writes a 16 / 32 / 64-row tracker pattern from the break's own hits plus recuts (CHOP), with 09xx offsets, Rxy retriggers, drills and a routing matrix (PASS / PITCH / SUB / REV / DRILL). It plays through a vintage sampler: CLEAN 16, AKAI S950, AKAI S1000 or AMIGA PAULA. The pattern screen shows a tracker page (ROW NOTE 09xx Rxy FX VOL); the break screen shows the waveform, class tags and playheads.

**Patch it**
- Load a WAV from the menu (30 s max; a synthetic two-bar break plays until then), patch a 1/16 clock into CLK and take L / R. Turn CHOP and DENSITY (50 % = the break's own hits), then step through SAMPLER.
- RST: the next clock is row 00. With no CLK, RUN (pad or gate) runs the internal clock at the menu tempo.
- NEW rolls a new pattern with the same settings, LOCK keeps the current one. GATE + SLICE play slices directly (1/12 V per slice from 0 V), each through its own chain.
- ROW fires on every pattern row; KICK / SNARE / GHOST / HAT fire on every hit of that class, for layering other drums.

**MetaModule** — Breaks are mono-summed to save RAM (stereo in Rack).
