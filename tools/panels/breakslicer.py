# BREAKSLICER — 18 HP. Units: mm. See tools/panelgen.py for the item types.
# Top: the tracker pattern (16 rows a page, two columns: ROW NOTE 09xx Rxy FX VOL) and the break
# (waveform, K S G H class tags, playheads). The perform controls: CHOP / KEEP / DENSITY / VARY /
# LEVEL on the M row, ROLLS / DRILL / FILLS / SWING / RATE on the S row. Then the sampler
# selector, NEW / LOCK and RUN; inputs; the output plate with the class triggers (the MIDI out).
_C7 = [8.6 + 12.37 * i for i in range(7)]          # seven columns (jacks)
_C5 = [10.2, 27.9, 45.72, 63.5, 81.2]              # five columns (knobs)

SPEC = {
    "slug": "Breakslicer",
    "hp": 18,
    "title": "BREAKSLICER",
    "subtitle": "generative tracker",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "PATTERN", "x": 3.22, "y": 15.2, "w": 85.0, "h": 30.6},
        {"type": "screen", "id": "WAVE", "x": 3.22, "y": 47.6, "w": 85.0, "h": 13.8},

        {"type": "knob", "id": "CHOP", "size": "M", "x": _C5[0], "y": 72.0, "label": "CHOP", "hot": True},
        {"type": "knob", "id": "KEEP", "size": "M", "x": _C5[1], "y": 72.0, "label": "KEEP BEATS"},
        {"type": "knob", "id": "DENSITY", "size": "M", "x": _C5[2], "y": 72.0, "label": "DENSITY"},
        {"type": "knob", "id": "VARY", "size": "M", "x": _C5[3], "y": 72.0, "label": "VARIATION"},
        {"type": "knob", "id": "LEVEL", "size": "M", "x": _C5[4], "y": 72.0, "label": "LEVEL"},

        {"type": "knob", "id": "ROLLS", "size": "S", "x": _C5[0], "y": 84.2, "label": "ROLLS"},
        {"type": "knob", "id": "DRILL", "size": "S", "x": _C5[1], "y": 84.2, "label": "DRILL", "hot": True},
        {"type": "knob", "id": "FILLS", "size": "S", "x": _C5[2], "y": 84.2, "label": "FILLS"},
        {"type": "knob", "id": "SWING", "size": "S", "x": _C5[3], "y": 84.2, "label": "SWING"},
        {"type": "knob", "id": "RATE", "size": "S", "x": _C5[4], "y": 84.2, "label": "RATE"},

        {"type": "button", "id": "MACHINE", "x": 7.0, "y": 95.0, "label": "SAMPLER"},
        {"type": "led", "id": "M_CLEAN", "x": 12.6, "y": 92.8, "size": "S", "label": "CLEAN 16"},
        {"type": "led", "id": "M_S950", "x": 12.6, "y": 96.0, "size": "S", "label": "S950"},
        {"type": "led", "id": "M_S1000", "x": 29.4, "y": 92.8, "size": "S", "label": "S1000"},
        {"type": "led", "id": "M_PAULA", "x": 29.4, "y": 96.0, "size": "S", "label": "PAULA"},
        {"type": "button", "id": "NEW", "x": 48.0, "y": 94.4, "label": "NEW"},
        {"type": "button", "id": "LOCK", "x": 58.0, "y": 94.4, "label": "LOCK"},
        {"type": "led", "id": "LOCK_LED", "x": 62.4, "y": 94.4, "size": "S"},
        {"type": "pad", "id": "RUN", "x": 74.0, "y": 94.4, "color": "cyan", "label": "RUN", "labelPos": "right"},
        {"type": "led", "id": "RUN_LED", "x": 86.0, "y": 97.0, "size": "S"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": _C7[0], "y": 104.0, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": _C7[1], "y": 104.0, "label": "RST"},
        {"type": "jack", "id": "RUN_IN", "kind": "in", "x": _C7[2], "y": 104.0, "label": "RUN"},
        {"type": "jack", "id": "CHOP_CV", "kind": "in", "x": _C7[3], "y": 104.0, "label": "CHOP"},
        {"type": "jack", "id": "NEW_IN", "kind": "in", "x": _C7[4], "y": 104.0, "label": "NEW"},
        {"type": "jack", "id": "GATE", "kind": "in", "x": _C7[5], "y": 104.0, "label": "GATE"},
        {"type": "jack", "id": "SLICE", "kind": "in", "x": _C7[6], "y": 104.0, "label": "SLICE"},

        {"type": "plate", "x": 2.7, "y": 108.4, "w": 86.04, "h": 15.0},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": _C7[0], "y": 118.0, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": _C7[1], "y": 118.0, "label": "R"},
        {"type": "jack", "id": "ROW", "kind": "out", "x": _C7[2], "y": 118.0, "label": "ROW"},
        {"type": "jack", "id": "KICK", "kind": "out", "x": _C7[3], "y": 118.0, "label": "KICK"},
        {"type": "jack", "id": "SNARE", "kind": "out", "x": _C7[4], "y": 118.0, "label": "SNARE"},
        {"type": "jack", "id": "GHOST", "kind": "out", "x": _C7[5], "y": 118.0, "label": "GHOST"},
        {"type": "jack", "id": "HAT", "kind": "out", "x": _C7[6], "y": 118.0, "label": "HAT"},
    ],
}
