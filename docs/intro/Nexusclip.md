NEXUSCLIP is a clipper, ported unchanged from the NEXUSCLIP CTZ plugin and the reference port for the series. The chain is the plugin's: DRIVE, saturation from the bottom (SAT), one of three engines at THRESH, then OUT. CLIP is a zero-latency clipper with seven transfer curves (HARD, SMOOTH, CRISP, CLASSIC, PRO, TUBE, TAPE); SPECTRAL is a short-window STFT clipper that keeps high-frequency detail at its own level; LIMIT is a wideband lookahead limiter. Every engine ends in an exact clamp, so nothing leaves above the ceiling. The screen shows the active engine's transfer curve with a live peak marker and a gain-reduction readout.

**Patch it**
- Patch a stereo signal into IN L / R (R normalled to L) and OUT L / R onward. Set THRESH as the ceiling, raise DRIVE while watching the peak marker on the curve, then pick a SHAPE.
- The engine button cycles CLIP / SPEC / LIMIT; SHAPE applies to CLIP. SOFT sets the softness of the curve, SAT the saturation from the bottom, OUT the output level.
- DRV and THR take CV: 2.4 dB/V on drive, 4.8 dB/V on threshold.
