BREAKMED is a generative tracker jungle break slicer, ported from the BREAKMED plugin. The break is cut at its transients (TRANS / EQUAL / GRID), and every hit rolls a value R = 0..127 that sends it through one of seven chains by the chain weights: BASE, pitch down, pitch up, SUB, glitch, reverse and drill, played through a Paula sampler. The wave screen shows the break with slice markers and role letters (K S H G), the beat grid and playheads in their chain colours; the hit log lists every hit as a tracker row (slice, note, chain, tracker command, R).

**Patch it**
- Load a WAV from the menu (30 s max, mono-summed; until then a synthetic two-bar break plays), switch PLAY on, patch a 1/16 clock into CLK and take L / R. Raise one chain weight at a time to hear what it does.
- RST: the next clock is beat 1; the tempo comes from the clock period. PLAY on with no CLK free-runs at the break's own tempo.
- GATE plays a slice (SLICE picks it, 1/12 V per slice; unpatched = the next slice); in the menu's strict GATE mode the falling edge ends it. VARY CV scales the six variation weights, DICE gives a new seed, LOCK keeps the same variation at the same place.
- TRIG fires on every hit, VAR on every hit that left BASE, SLC carries the slice of the last hit.
