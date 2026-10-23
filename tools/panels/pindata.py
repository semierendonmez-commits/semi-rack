# PINDATA — 20 HP. Units: mm. See tools/panelgen.py for the item types.
# Top: the VISUAL screen (the plugin's datamatic scenes: matrix, field, automata, buffers,
# barcode, hex), the INFO strip (file, read head, step) with RUN / RST / HALT / SCENE.
# Knobs: the ball and the read head (M row), the three layers' levels and sound (S row).
# Inputs above the cyan output plate: audio, the lanes as GATE / PITCH / VEL, STEP and BUS.
_M = [10.0, 26.3, 42.6, 59.0, 75.3, 91.6]
_S = [8.5, 20.6, 32.7, 44.8, 56.9, 69.0, 81.1, 93.2]
_O = [10.0, 23.5, 37.0, 50.5, 64.0, 77.5, 91.0]

SPEC = {
    "slug": "Pindata",
    "hp": 20,
    "title": "PINDATA",
    "subtitle": "cellula · palimpsest · datum · pinball",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "VISUAL", "x": 3.1, "y": 15.4, "w": 95.4, "h": 46.0},
        {"type": "screen", "id": "INFO", "x": 3.1, "y": 64.2, "w": 59.0, "h": 8.6},

        {"type": "button", "id": "RUN", "x": 67.6, "y": 70.0, "label": "RUN"},
        {"type": "led", "id": "RUN_LED", "x": 71.6, "y": 70.0, "size": "S"},
        {"type": "button", "id": "RESET", "x": 78.0, "y": 70.0, "label": "RST"},
        {"type": "button", "id": "HALT", "x": 86.0, "y": 70.0, "label": "HALT"},
        {"type": "led", "id": "HALT_LED", "x": 90.0, "y": 70.0, "size": "S"},
        {"type": "button", "id": "SCENE", "x": 95.6, "y": 70.0, "label": "SCN"},

        {"type": "knob", "id": "MULT", "size": "M", "x": _M[0], "y": 82.0, "label": "MULT"},
        {"type": "knob", "id": "SWING", "size": "M", "x": _M[1], "y": 82.0, "label": "SWING"},
        {"type": "knob", "id": "PATTERN", "size": "M", "x": _M[2], "y": 82.0, "label": "PATTERN"},
        {"type": "knob", "id": "SCAN", "size": "M", "x": _M[3], "y": 82.0, "label": "SCAN"},
        {"type": "knob", "id": "STEPSIZE", "size": "M", "x": _M[4], "y": 82.0, "label": "STEP"},
        {"type": "knob", "id": "RATE", "size": "M", "x": _M[5], "y": 82.0, "label": "RATE"},

        {"type": "knob", "id": "IKD", "size": "S", "x": _S[0], "y": 94.6, "label": "CELL"},
        {"type": "knob", "id": "INGP", "size": "S", "x": _S[1], "y": 94.6, "label": "TEXT"},
        {"type": "knob", "id": "JDAT", "size": "S", "x": _S[2], "y": 94.6, "label": "DATUM"},
        {"type": "knob", "id": "MASTER", "size": "S", "x": _S[3], "y": 94.6, "label": "MASTER"},
        {"type": "knob", "id": "SMOOTH", "size": "S", "x": _S[4], "y": 94.6, "label": "SMOOTH"},
        {"type": "knob", "id": "TUNE", "size": "S", "x": _S[5], "y": 94.6, "label": "TUNE"},
        {"type": "knob", "id": "CLEAR", "size": "S", "x": _S[6], "y": 94.6, "label": "CLEAR", "hot": True},
        {"type": "knob", "id": "CHANCE", "size": "S", "x": _S[7], "y": 94.6, "label": "MUTATE", "hot": True},

        {"type": "jack", "id": "CLK", "kind": "in", "x": _M[0], "y": 105.0, "label": "CLK"},
        {"type": "jack", "id": "RST_IN", "kind": "in", "x": _M[1], "y": 105.0, "label": "RST"},
        {"type": "jack", "id": "FIRE", "kind": "in", "x": _M[2], "y": 105.0, "label": "FIRE"},
        {"type": "jack", "id": "HEAD", "kind": "in", "x": _M[3], "y": 105.0, "label": "HEAD"},
        {"type": "jack", "id": "MULT_CV", "kind": "in", "x": _M[4], "y": 105.0, "label": "MULT"},
        {"type": "jack", "id": "SCENE_CV", "kind": "in", "x": _M[5], "y": 105.0, "label": "SCENE"},

        {"type": "plate", "x": 2.7, "y": 109.6, "w": 96.2, "h": 14.0},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": _O[0], "y": 118.6, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": _O[1], "y": 118.6, "label": "R"},
        {"type": "jack", "id": "GATE", "kind": "out", "x": _O[2], "y": 118.6, "label": "GATE"},
        {"type": "jack", "id": "PITCH", "kind": "out", "x": _O[3], "y": 118.6, "label": "PITCH"},
        {"type": "jack", "id": "VEL", "kind": "out", "x": _O[4], "y": 118.6, "label": "VEL"},
        {"type": "jack", "id": "STEP_OUT", "kind": "out", "x": _O[5], "y": 118.6, "label": "STEP"},
        {"type": "jack", "id": "BUS", "kind": "out", "bus": True, "x": _O[6], "y": 118.6, "label": "BUS·4"},
    ],
}
