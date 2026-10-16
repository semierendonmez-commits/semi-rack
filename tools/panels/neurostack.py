# NEUROSTACK — 20 HP. Units: mm. See tools/panelgen.py for the item types.
# Left: the Pinball (17 x 17 matrix, the ball, the 16 steps' articulations, the 32-cell rhythm).
# Right: the phase-locked scope (upper band + low end against the master phase) and the eight
# layers (envelope + fundamental phase against the sub). Knobs: the switch / the sound (row A),
# the low end / the movement / output (row B). Inputs above the cyan output plate.
_X = [9.5, 26.0, 42.5, 59.0, 75.5, 92.0]

SPEC = {
    "slug": "Neurostack",
    "hp": 20,
    "title": "NEUROSTACK",
    "subtitle": "8 layers · pinball · one sub",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "MATRIX", "x": 3.2, "y": 15.4, "w": 44.0, "h": 45.0},
        {"type": "screen", "id": "SCOPE", "x": 49.8, "y": 15.4, "w": 48.6, "h": 27.0},
        {"type": "screen", "id": "LAYERS", "x": 49.8, "y": 44.6, "w": 48.6, "h": 15.8},

        {"type": "button", "id": "RHY", "x": _X[0], "y": 67.4, "label": "RHYTHM"},
        {"type": "led", "id": "RHY_LED", "x": _X[0] + 5.6, "y": 67.4, "size": "S"},
        {"type": "button", "id": "SHAPE", "x": _X[2], "y": 67.4, "label": "SHAPE"},
        {"type": "button", "id": "PRESET", "x": _X[4], "y": 67.4, "label": "PRESET"},
        {"type": "led", "id": "RUN_LED", "x": _X[5], "y": 67.4, "size": "S", "label": "CLK"},

        {"type": "knob", "id": "TIME", "size": "M", "x": _X[0], "y": 79.4, "label": "TIME"},
        {"type": "knob", "id": "XFADE", "size": "M", "x": _X[1], "y": 79.4, "label": "XFADE"},
        {"type": "knob", "id": "TONE", "size": "M", "x": _X[2], "y": 79.4, "label": "TONE"},
        {"type": "knob", "id": "MOTION", "size": "M", "x": _X[3], "y": 79.4, "label": "MOTION"},
        {"type": "knob", "id": "GRIT", "size": "M", "x": _X[4], "y": 79.4, "label": "GRIT"},
        {"type": "knob", "id": "DRIVE", "size": "M", "x": _X[5], "y": 79.4, "label": "DRIVE"},

        {"type": "knob", "id": "SPLIT", "size": "M", "x": _X[0], "y": 93.0, "label": "SPLIT"},
        {"type": "knob", "id": "SUB", "size": "M", "x": _X[1], "y": 93.0, "label": "SUB"},
        {"type": "knob", "id": "VARIATION", "size": "M", "x": _X[2], "y": 93.0, "label": "VARY", "hot": True},
        {"type": "knob", "id": "DRIFT", "size": "M", "x": _X[3], "y": 93.0, "label": "DRIFT"},
        {"type": "knob", "id": "WIDTH", "size": "M", "x": _X[4], "y": 93.0, "label": "WIDTH"},
        {"type": "knob", "id": "LEVEL", "size": "M", "x": _X[5], "y": 93.0, "label": "LEVEL"},

        {"type": "jack", "id": "VOCT", "kind": "in", "x": _X[0], "y": 104.4, "label": "V/OCT"},
        {"type": "jack", "id": "GATE", "kind": "in", "x": _X[1], "y": 104.4, "label": "GATE"},
        {"type": "jack", "id": "CLK", "kind": "in", "x": _X[2], "y": 104.4, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": _X[3], "y": 104.4, "label": "RST"},
        {"type": "jack", "id": "TONE_CV", "kind": "in", "x": _X[4], "y": 104.4, "label": "TONE"},
        {"type": "jack", "id": "SC", "kind": "in", "x": _X[5], "y": 104.4, "label": "DUCK"},

        {"type": "plate", "x": 2.7, "y": 108.8, "w": 96.2, "h": 14.8},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": 12.0, "y": 118.4, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": 28.0, "y": 118.4, "label": "R"},
        {"type": "jack", "id": "TRIG", "kind": "out", "x": 56.0, "y": 118.4, "label": "SWITCH"},
        {"type": "jack", "id": "ART", "kind": "out", "x": 72.0, "y": 118.4, "label": "LAYER"},
        {"type": "jack", "id": "PITCH", "kind": "out", "x": 88.0, "y": 118.4, "label": "F0"},
    ],
}
