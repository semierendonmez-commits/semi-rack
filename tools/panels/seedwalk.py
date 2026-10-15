# SEEDWALK — 18 HP. Units: mm. See tools/panelgen.py for the item types.
# Top: the 4x4 net grid (current / next / walked path / ball / range brackets / HOLD), the
# CALM..CHAOS heat meter, then PIN / HOLD / RND. Knobs: the walk + its range (M row) and the
# shape / net / fx (S row) with the range CVs under FROM / TO. Inputs above the cyan plate.
_W = 18 * 5.08
_KM = [7.4 + 12.77 * k for k in range(7)]       # seven M columns
_KS = [7.4 + 10.9 * k for k in range(6)]        # six S columns, left of the range CVs
_J = [7.4 + 11.03 * k for k in range(8)]        # eight input columns

SPEC = {
    "slug": "Seedwalk",
    "hp": 18,
    "title": "SEEDWALK",
    "subtitle": "16 pinned nets · markov ball",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "NETS", "x": 3.1, "y": 15.4, "w": _W - 6.2, "h": 44.0},
        {"type": "screen", "id": "METER", "x": 3.1, "y": 62.0, "w": 50.0, "h": 8.8},
        {"type": "button", "id": "PIN", "x": 61.4, "y": 65.6, "label": "PIN"},
        {"type": "led", "id": "PIN_LED", "x": 61.4, "y": 70.0, "size": "S"},
        {"type": "button", "id": "HOLD", "x": 72.6, "y": 65.6, "label": "HOLD"},
        {"type": "led", "id": "HOLD_LED", "x": 72.6, "y": 70.0, "size": "S"},
        {"type": "button", "id": "RND", "x": 83.8, "y": 65.6, "label": "RND"},
        {"type": "led", "id": "RND_LED", "x": 83.8, "y": 70.0, "size": "S"},

        {"type": "knob", "id": "STEP", "size": "M", "x": _KM[0], "y": 80.0, "label": "STEP"},
        {"type": "knob", "id": "GLIDE", "size": "M", "x": _KM[1], "y": 80.0, "label": "GLIDE"},
        {"type": "knob", "id": "CHAOS", "size": "M", "x": _KM[2], "y": 80.0, "label": "CHAOS", "hot": True},
        {"type": "knob", "id": "SEED", "size": "M", "x": _KM[3], "y": 80.0, "label": "SEED"},
        {"type": "knob", "id": "LVL", "size": "M", "x": _KM[4], "y": 80.0, "label": "LVL"},
        {"type": "knob", "id": "FROM", "size": "M", "x": _KM[5], "y": 80.0, "label": "FROM"},
        {"type": "knob", "id": "TO", "size": "M", "x": _KM[6], "y": 80.0, "label": "TO"},

        {"type": "knob", "id": "SHAPE", "size": "S", "x": _KS[0], "y": 92.4, "label": "SHAPE"},
        {"type": "knob", "id": "SPREAD", "size": "S", "x": _KS[1], "y": 92.4, "label": "SPREAD"},
        {"type": "knob", "id": "DIP", "size": "S", "x": _KS[2], "y": 92.4, "label": "DIP"},
        {"type": "knob", "id": "EXCITE", "size": "S", "x": _KS[3], "y": 92.4, "label": "EXCITE"},
        {"type": "knob", "id": "FX", "size": "S", "x": _KS[4], "y": 92.4, "label": "FX"},
        {"type": "knob", "id": "FXMODE", "size": "S", "x": _KS[5], "y": 92.4, "label": "FX MODE"},
        {"type": "jack", "id": "FROM_CV", "kind": "in", "x": _KM[5], "y": 92.4, "label": "FROM CV"},
        {"type": "jack", "id": "TO_CV", "kind": "in", "x": _KM[6], "y": 92.4, "label": "TO CV"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": _J[0], "y": 103.4, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": _J[1], "y": 103.4, "label": "RST"},
        {"type": "jack", "id": "CHAOS_CV", "kind": "in", "x": _J[2], "y": 103.4, "label": "CHAOS"},
        {"type": "jack", "id": "X", "kind": "in", "x": _J[3], "y": 103.4, "label": "X"},
        {"type": "jack", "id": "Y", "kind": "in", "x": _J[4], "y": 103.4, "label": "Y"},
        {"type": "jack", "id": "IN", "kind": "in", "x": _J[5], "y": 103.4, "label": "IN"},
        {"type": "jack", "id": "HOLD_IN", "kind": "in", "x": _J[6], "y": 103.4, "label": "HOLD"},
        {"type": "jack", "id": "RND_IN", "kind": "in", "x": _J[7], "y": 103.4, "label": "RND"},

        {"type": "plate", "x": 2.7, "y": 108.0, "w": _W - 5.4, "h": 15.4},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": 12.0, "y": 118.0, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": 30.4, "y": 118.0, "label": "R"},
        {"type": "jack", "id": "NET_OUT", "kind": "out", "x": 61.0, "y": 118.0, "label": "NET"},
        {"type": "jack", "id": "EOC", "kind": "out", "x": 79.4, "y": 118.0, "label": "EOC"},
    ],
}
