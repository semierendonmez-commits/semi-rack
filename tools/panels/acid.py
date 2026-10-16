# ACID — 14 HP. Units: mm. See tools/panelgen.py for the item types.
# Top: the TB-3PO step grid (pitch columns, accents, slides, playhead). Sequencer row (DENSITY +
# accent / slide / octave odds, NEW / MUTATE), the 303 (cutoff, reso, env mod, decay, accent, drive,
# gate length). Inputs above the cyan output plate.
_J = [7.6, 21.6, 35.56, 49.5, 63.5]

SPEC = {
    "slug": "Acid",
    "hp": 14,
    "title": "ACID",
    "subtitle": "TB-3PO x 303",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "GRID", "x": 3.06, "y": 15.4, "w": 65.0, "h": 30.0},

        {"type": "knob", "id": "DENSITY", "size": "M", "x": 9.6, "y": 56.6, "label": "DENSITY"},
        {"type": "knob", "id": "ACC_ODDS", "size": "S", "x": 23.2, "y": 57.4, "label": "ACC"},
        {"type": "knob", "id": "SLIDE_ODDS", "size": "S", "x": 34.2, "y": 57.4, "label": "SLIDE"},
        {"type": "knob", "id": "OCT_ODDS", "size": "S", "x": 45.2, "y": 57.4, "label": "OCT"},
        {"type": "button", "id": "NEW", "x": 56.0, "y": 57.4, "label": "NEW"},
        {"type": "button", "id": "MUTATE", "x": 65.2, "y": 57.4, "label": "MUT", "hot": True},

        {"type": "knob", "id": "CUTOFF", "size": "L", "x": 11.0, "y": 73.6, "label": "CUTOFF"},
        {"type": "knob", "id": "RESO", "size": "M", "x": 26.8, "y": 73.6, "label": "RESO"},
        {"type": "knob", "id": "ENVMOD", "size": "M", "x": 41.8, "y": 73.6, "label": "ENV MOD"},
        {"type": "knob", "id": "DECAY", "size": "M", "x": 56.8, "y": 73.6, "label": "DECAY"},
        {"type": "knob", "id": "ACCENT", "size": "M", "x": 11.0, "y": 89.0, "label": "ACCENT"},
        {"type": "knob", "id": "DRIVE", "size": "M", "x": 26.8, "y": 89.0, "label": "DRIVE", "hot": True},
        {"type": "knob", "id": "GATE", "size": "M", "x": 41.8, "y": 89.0, "label": "GATE"},
        {"type": "knob", "id": "LEVEL", "size": "M", "x": 56.8, "y": 89.0, "label": "LEVEL"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": _J[0], "y": 102.4, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": _J[1], "y": 102.4, "label": "RST"},
        {"type": "jack", "id": "VOCT_IN", "kind": "in", "x": _J[2], "y": 102.4, "label": "V/OCT"},
        {"type": "jack", "id": "FILL", "kind": "in", "x": _J[3], "y": 102.4, "label": "FILL"},
        {"type": "jack", "id": "CUT_CV", "kind": "in", "x": _J[4], "y": 102.4, "label": "CUT"},

        {"type": "plate", "x": 2.7, "y": 108.2, "w": 65.72, "h": 15.4},
        {"type": "jack", "id": "OUT", "kind": "out", "x": _J[0], "y": 118.0, "label": "OUT"},
        {"type": "jack", "id": "VOCT_OUT", "kind": "out", "x": _J[1], "y": 118.0, "label": "V/OCT"},
        {"type": "jack", "id": "GATE_OUT", "kind": "out", "x": _J[2], "y": 118.0, "label": "GATE"},
        {"type": "jack", "id": "ACC_OUT", "kind": "out", "x": _J[3], "y": 118.0, "label": "ACC"},
        {"type": "jack", "id": "SLIDE_OUT", "kind": "out", "x": _J[4], "y": 118.0, "label": "SLIDE"},
    ],
}
