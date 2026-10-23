# IKEDAT — 18 HP. Units: mm. See tools/panelgen.py for the item types.
# Top: the VIEW screen (the five datamatic views: transit, barcode, spatial, grid, stream) and
# the INFO strip; RUN / LNCH (relaunch the ball) / HALT / VIEW.
# Knobs: the ball and the read head (M row), IKEDAR groups, JDAT, data seed, clear, master (S row).
# Inputs above the cyan output plate: audio, the lanes as GATE / PITCH / VEL, STEP and BUS.
_M = [9.5, 27.6, 45.7, 63.8, 81.9]
_S = [8.7, 21.0, 33.4, 45.7, 58.1, 70.4, 82.7]
_I = [8.7, 23.5, 38.3, 53.1, 67.9, 82.7]
_O = [8.9, 21.2, 33.5, 45.8, 58.1, 70.4, 82.7]

SPEC = {
    "slug": "Ikedat",
    "hp": 18,
    "title": "IKEDAT",
    "subtitle": "cellula · datum · pinball",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "VIEW", "x": 3.1, "y": 15.4, "w": 85.24, "h": 44.0},
        {"type": "screen", "id": "INFO", "x": 3.1, "y": 62.2, "w": 50.0, "h": 8.6},

        {"type": "button", "id": "RUN", "x": 58.6, "y": 68.0, "label": "RUN"},
        {"type": "led", "id": "RUN_LED", "x": 62.4, "y": 68.0, "size": "S"},
        {"type": "button", "id": "LAUNCH", "x": 68.6, "y": 68.0, "label": "LNCH"},
        {"type": "button", "id": "HALT", "x": 76.4, "y": 68.0, "label": "HALT"},
        {"type": "led", "id": "HALT_LED", "x": 80.2, "y": 68.0, "size": "S"},
        {"type": "button", "id": "VIEW_BTN", "x": 86.0, "y": 68.0, "label": "VIEW"},

        {"type": "knob", "id": "MULT", "size": "M", "x": _M[0], "y": 80.6, "label": "MULT"},
        {"type": "knob", "id": "SWING", "size": "M", "x": _M[1], "y": 80.6, "label": "SWING"},
        {"type": "knob", "id": "PATTERN", "size": "M", "x": _M[2], "y": 80.6, "label": "PATTERN"},
        {"type": "knob", "id": "SCAN", "size": "M", "x": _M[3], "y": 80.6, "label": "SCAN"},
        {"type": "knob", "id": "SPEED", "size": "M", "x": _M[4], "y": 80.6, "label": "SPEED"},

        {"type": "knob", "id": "WAVE", "size": "S", "x": _S[0], "y": 93.2, "label": "WAVE"},
        {"type": "knob", "id": "SPEC", "size": "S", "x": _S[1], "y": 93.2, "label": "SPEC"},
        {"type": "knob", "id": "SINE", "size": "S", "x": _S[2], "y": 93.2, "label": "SINE"},
        {"type": "knob", "id": "JDAT", "size": "S", "x": _S[3], "y": 93.2, "label": "DATUM"},
        {"type": "knob", "id": "SEED", "size": "S", "x": _S[4], "y": 93.2, "label": "SEED", "hot": True},
        {"type": "knob", "id": "CLEAR", "size": "S", "x": _S[5], "y": 93.2, "label": "CLEAR", "hot": True},
        {"type": "knob", "id": "MASTER", "size": "S", "x": _S[6], "y": 93.2, "label": "MASTER"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": _I[0], "y": 104.4, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": _I[1], "y": 104.4, "label": "LNCH"},
        {"type": "jack", "id": "GATE_IN", "kind": "in", "x": _I[2], "y": 104.4, "label": "RUN"},
        {"type": "jack", "id": "MUT", "kind": "in", "x": _I[3], "y": 104.4, "label": "MUT"},
        {"type": "jack", "id": "JUMP", "kind": "in", "x": _I[4], "y": 104.4, "label": "JUMP"},
        {"type": "jack", "id": "HEAD", "kind": "in", "x": _I[5], "y": 104.4, "label": "HEAD"},

        {"type": "plate", "x": 2.7, "y": 109.6, "w": 86.04, "h": 14.0},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": _O[0], "y": 118.6, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": _O[1], "y": 118.6, "label": "R"},
        {"type": "jack", "id": "GATE", "kind": "out", "x": _O[2], "y": 118.6, "label": "GATE"},
        {"type": "jack", "id": "PITCH", "kind": "out", "x": _O[3], "y": 118.6, "label": "PITCH"},
        {"type": "jack", "id": "VEL", "kind": "out", "x": _O[4], "y": 118.6, "label": "VEL"},
        {"type": "jack", "id": "STEP_OUT", "kind": "out", "x": _O[5], "y": 118.6, "label": "STEP"},
        {"type": "jack", "id": "BUS", "kind": "out", "bus": True, "x": _O[6], "y": 118.6, "label": "BUS·4"},
    ],
}
