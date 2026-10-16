# BREAKBEAT — 16 HP. Units: mm. See tools/panelgen.py for the item types.
# Top: the cut display (time across, where in the loop each cut reads down; rolls magenta) and the
# loop's waveform with its slice grid. The cut-mode selector (MOVE = the schwung-breakbeat slice
# decisions, then four BBCut procedures) with RUN. M row: the slicer; S row: retrigger odds,
# SCRAMBLE (BBCut modes) and GATE. Inputs; outputs on the plate.
_C5 = [9.6, 25.1, 40.64, 56.2, 71.7]
_C6 = [9.6, 22.0, 34.4, 46.8, 59.2, 71.6]
_MODES = ["MOVE", "CP11", "WARP", "SQP1", "CHOOSE"]

SPEC = {
    "slug": "Breakbeat",
    "hp": 16,
    "title": "BREAKBEAT",
    "subtitle": "slicer · bbcut",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "CUTS", "x": 3.14, "y": 15.2, "w": 75.0, "h": 24.6},
        {"type": "screen", "id": "WAVE", "x": 3.14, "y": 41.6, "w": 75.0, "h": 12.4},

        {"type": "button", "id": "MODE", "x": 7.0, "y": 60.2, "label": "CUT"},
    ] + [
        {"type": "led", "id": f"MODE_LED{i}", "x": 13.0 + 10.2 * i, "y": 60.2, "size": "S", "label": _MODES[i],
         "labelColor": "text" if i == 0 else "magenta"}
        for i in range(5)
    ] + [
        {"type": "pad", "id": "RUN", "x": 74.0, "y": 60.4, "color": "cyan"},
        {"type": "label", "text": "RUN", "x": 66.2, "y": 58.6, "size": 1.9, "color": "cyan"},
        {"type": "led", "id": "RUN_LED", "x": 66.2, "y": 61.8, "size": "S"},

        {"type": "knob", "id": "COMPLEX", "size": "M", "x": _C5[0], "y": 74.0, "label": "COMPLEX", "hot": True},
        {"type": "knob", "id": "ANCHOR", "size": "M", "x": _C5[1], "y": 74.0, "label": "ANCHOR"},
        {"type": "knob", "id": "ROLL", "size": "M", "x": _C5[2], "y": 74.0, "label": "ROLL"},
        {"type": "knob", "id": "FILL", "size": "M", "x": _C5[3], "y": 74.0, "label": "FILL"},
        {"type": "knob", "id": "BCH", "size": "M", "x": _C5[4], "y": 74.0, "label": "B CHANCE"},

        {"type": "knob", "id": "R2", "size": "S", "x": _C6[0], "y": 88.0, "label": "2X"},
        {"type": "knob", "id": "R3", "size": "S", "x": _C6[1], "y": 88.0, "label": "3X"},
        {"type": "knob", "id": "R4", "size": "S", "x": _C6[2], "y": 88.0, "label": "4X"},
        {"type": "knob", "id": "R8", "size": "S", "x": _C6[3], "y": 88.0, "label": "8X"},
        {"type": "knob", "id": "SCRAMBLE", "size": "S", "x": _C6[4], "y": 88.0, "label": "SCRAMBLE", "hot": True},
        {"type": "knob", "id": "GATE", "size": "S", "x": _C6[5], "y": 88.0, "label": "GATE"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": _C6[0], "y": 103.6, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": _C6[1], "y": 103.6, "label": "RST"},
        {"type": "jack", "id": "RUN_IN", "kind": "in", "x": _C6[2], "y": 103.6, "label": "RUN"},
        {"type": "jack", "id": "CPLX_CV", "kind": "in", "x": _C6[3], "y": 103.6, "label": "CPLX"},
        {"type": "jack", "id": "GATE_IN", "kind": "in", "x": _C6[4], "y": 103.6, "label": "GATE"},
        {"type": "jack", "id": "SLICE", "kind": "in", "x": _C6[5], "y": 103.6, "label": "SLICE"},

        {"type": "plate", "x": 2.7, "y": 108.0, "w": 75.88, "h": 15.4},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": _C6[0], "y": 118.0, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": _C6[1], "y": 118.0, "label": "R"},
        {"type": "jack", "id": "TRIG", "kind": "out", "x": _C6[2], "y": 118.0, "label": "TRIG"},
        {"type": "jack", "id": "ROLL_OUT", "kind": "out", "x": _C6[3], "y": 118.0, "label": "ROLL"},
        {"type": "jack", "id": "SLC", "kind": "out", "x": _C6[4], "y": 118.0, "label": "SLC"},
        {"type": "jack", "id": "LOOPB", "kind": "out", "x": _C6[5], "y": 118.0, "label": "B"},
    ],
}
