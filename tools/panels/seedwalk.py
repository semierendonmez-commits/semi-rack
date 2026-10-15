# SEEDWALK — 16 HP. Units: mm. See tools/panelgen.py for the item types.
# Top: the 4x4 net grid (current / next / walked path / ball), the CALM..CHAOS heat meter with the
# NET a > b / GLIDE readout and PIN. Knobs: the walk (M row) and the shape / net / fx (S row).
# Inputs above the cyan output plate.
_KS = [9.6, 22.0, 34.4, 46.8, 59.2, 71.6]   # six columns for the S row and the inputs

SPEC = {
    "slug": "Seedwalk",
    "hp": 16,
    "title": "SEEDWALK",
    "subtitle": "16 pinned nets · markov ball",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "NETS", "x": 3.1, "y": 15.4, "w": 75.0, "h": 44.0},
        {"type": "screen", "id": "METER", "x": 3.1, "y": 62.0, "w": 63.4, "h": 8.8},
        {"type": "button", "id": "PIN", "x": 73.0, "y": 65.6, "label": "PIN"},
        {"type": "led", "id": "PIN_LED", "x": 73.0, "y": 70.0, "size": "S"},

        {"type": "knob", "id": "STEP", "size": "M", "x": 9.6, "y": 80.0, "label": "STEP"},
        {"type": "knob", "id": "GLIDE", "size": "M", "x": 25.1, "y": 80.0, "label": "GLIDE"},
        {"type": "knob", "id": "CHAOS", "size": "M", "x": 40.6, "y": 80.0, "label": "CHAOS", "hot": True},
        {"type": "knob", "id": "SEED", "size": "M", "x": 56.1, "y": 80.0, "label": "SEED"},
        {"type": "knob", "id": "LVL", "size": "M", "x": 71.6, "y": 80.0, "label": "LVL"},

        {"type": "knob", "id": "SHAPE", "size": "S", "x": _KS[0], "y": 92.4, "label": "SHAPE"},
        {"type": "knob", "id": "SPREAD", "size": "S", "x": _KS[1], "y": 92.4, "label": "SPREAD"},
        {"type": "knob", "id": "DIP", "size": "S", "x": _KS[2], "y": 92.4, "label": "DIP"},
        {"type": "knob", "id": "EXCITE", "size": "S", "x": _KS[3], "y": 92.4, "label": "EXCITE"},
        {"type": "knob", "id": "FX", "size": "S", "x": _KS[4], "y": 92.4, "label": "FX"},
        {"type": "knob", "id": "FXMODE", "size": "S", "x": _KS[5], "y": 92.4, "label": "FX MODE"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": _KS[0], "y": 103.4, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": _KS[1], "y": 103.4, "label": "RST"},
        {"type": "jack", "id": "CHAOS_CV", "kind": "in", "x": _KS[2], "y": 103.4, "label": "CHAOS"},
        {"type": "jack", "id": "X", "kind": "in", "x": _KS[3], "y": 103.4, "label": "X"},
        {"type": "jack", "id": "Y", "kind": "in", "x": _KS[4], "y": 103.4, "label": "Y"},
        {"type": "jack", "id": "IN", "kind": "in", "x": _KS[5], "y": 103.4, "label": "IN"},

        {"type": "plate", "x": 2.7, "y": 108.0, "w": 75.88, "h": 15.4},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": 12.0, "y": 118.0, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": 28.4, "y": 118.0, "label": "R"},
        {"type": "jack", "id": "NET_OUT", "kind": "out", "x": 52.8, "y": 118.0, "label": "NET"},
        {"type": "jack", "id": "EOC", "kind": "out", "x": 69.2, "y": 118.0, "label": "EOC"},
    ],
}
