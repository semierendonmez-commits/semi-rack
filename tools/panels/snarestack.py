# SNARESTACK — 16 HP. Units: mm. See tools/panelgen.py for the item types.
# Left: the four layers as aligned (raw dim, stacked bright; shift, polarity). Right: the
# crack / body / tail triangle with the balance point, and the per-band phase meter.
# Knobs: the four layer gains + CARVE / DEPTH (row A), the hit (row B). Inputs above the plate.
_X = [8.6, 21.5, 34.4, 47.3, 60.2, 73.1]

SPEC = {
    "slug": "Snarestack",
    "hp": 16,
    "title": "SNARESTACK",
    "subtitle": "4 layers · 1 hit",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "ALIGN", "x": 3.2, "y": 15.4, "w": 53.4, "h": 45.0},
        {"type": "screen", "id": "BAL", "x": 58.4, "y": 15.4, "w": 19.7, "h": 45.0},

        {"type": "button", "id": "ALIGN_BTN", "x": _X[0], "y": 67.4, "label": "ALIGN"},
        {"type": "led", "id": "AL_OFF", "x": 16.6, "y": 67.4, "size": "S", "label": "OFF"},
        {"type": "led", "id": "AL_ONSET", "x": 27.0, "y": 67.4, "size": "S", "label": "ONSET"},
        {"type": "led", "id": "AL_PHASE", "x": 40.4, "y": 67.4, "size": "S", "label": "PHASE"},
        {"type": "pad", "id": "HIT", "x": _X[5], "y": 68.4, "color": "magenta", "label": "HIT"},

        {"type": "knob", "id": "G1", "size": "M", "x": _X[0], "y": 80.4, "label": "L1"},
        {"type": "knob", "id": "G2", "size": "M", "x": _X[1], "y": 80.4, "label": "L2"},
        {"type": "knob", "id": "G3", "size": "M", "x": _X[2], "y": 80.4, "label": "L3"},
        {"type": "knob", "id": "G4", "size": "M", "x": _X[3], "y": 80.4, "label": "L4"},
        {"type": "knob", "id": "CARVE", "size": "M", "x": _X[4], "y": 80.4, "label": "CARVE"},
        {"type": "knob", "id": "DEPTH", "size": "M", "x": _X[5], "y": 80.4, "label": "DEPTH"},

        {"type": "knob", "id": "PITCH", "size": "M", "x": _X[0], "y": 93.4, "label": "PITCH"},
        {"type": "knob", "id": "DECAY", "size": "M", "x": _X[1], "y": 93.4, "label": "DECAY"},
        {"type": "knob", "id": "BX", "size": "M", "x": _X[2], "y": 93.4, "label": "BAL X"},
        {"type": "knob", "id": "BY", "size": "M", "x": _X[3], "y": 93.4, "label": "BAL Y"},
        {"type": "knob", "id": "VARY", "size": "M", "x": _X[4], "y": 93.4, "label": "VARY", "hot": True},
        {"type": "knob", "id": "LEVEL", "size": "M", "x": _X[5], "y": 93.4, "label": "LEVEL"},

        {"type": "jack", "id": "TRIG", "kind": "in", "x": _X[0], "y": 104.6, "label": "TRIG"},
        {"type": "jack", "id": "VEL", "kind": "in", "x": _X[1], "y": 104.6, "label": "VEL"},
        {"type": "jack", "id": "VOCT", "kind": "in", "x": _X[2], "y": 104.6, "label": "V/OCT"},
        {"type": "jack", "id": "BX_CV", "kind": "in", "x": _X[3], "y": 104.6, "label": "X"},
        {"type": "jack", "id": "BY_CV", "kind": "in", "x": _X[4], "y": 104.6, "label": "Y"},
        {"type": "jack", "id": "DECAY_CV", "kind": "in", "x": _X[5], "y": 104.6, "label": "DECAY"},

        {"type": "plate", "x": 2.7, "y": 109.0, "w": 75.88, "h": 14.6},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": 28.0, "y": 118.6, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": 53.0, "y": 118.6, "label": "R"},
    ],
}
