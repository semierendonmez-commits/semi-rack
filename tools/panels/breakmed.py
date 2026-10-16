# BREAKMED — 16 HP. Units: mm. See tools/panelgen.py for the item types.
# Top: break.view (waveform, hex slice tags, role letters, chain-coloured playheads) and the
# hit.log tracker rows. Then the chain matrix: one weight knob per chain with its flash LED.
# M row: slicing + sampler + generator. Buttons: PLAY, LOCK, DICE, ANCHOR. Inputs, output plate.
_C7 = [7.3 + 11.11 * i for i in range(7)]          # seven chain columns
_C5 = [9.6, 25.1, 40.64, 56.2, 71.7]               # five columns
_C6 = [9.6, 22.0, 34.4, 46.8, 59.2, 71.6]          # six columns
_CHAINS = ["BASE", "P.DN", "P.UP", "SUB", "GLCH", "REV", "DRIL"]

SPEC = {
    "slug": "Breakmed",
    "hp": 16,
    "title": "BREAKMED",
    "subtitle": "tracker jungle slicer",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "WAVE", "x": 3.14, "y": 15.2, "w": 75.0, "h": 17.6},
        {"type": "screen", "id": "LOG", "x": 3.14, "y": 34.6, "w": 75.0, "h": 23.4},
    ] + [
        {"type": "led", "id": f"CH_LED{i}", "x": _C7[i], "y": 62.4, "size": "S", "label": _CHAINS[i],
         "labelPos": "above", "labelColor": "magenta" if i == 6 else "text"}
        for i in range(7)
    ] + [
        {"type": "knob", "id": f"W{i}", "size": "S", "x": _C7[i], "y": 68.4, "hot": i == 6}
        for i in range(7)
    ] + [
        {"type": "knob", "id": "THRES", "size": "M", "x": _C5[0], "y": 81.0, "label": "THRES"},
        {"type": "knob", "id": "SRATE", "size": "M", "x": _C5[1], "y": 81.0, "label": "S.RATE"},
        {"type": "knob", "id": "PITCH", "size": "M", "x": _C5[2], "y": 81.0, "label": "PITCH"},
        {"type": "knob", "id": "MEMORY", "size": "M", "x": _C5[3], "y": 81.0, "label": "MEMORY"},
        {"type": "knob", "id": "LEVEL", "size": "M", "x": _C5[4], "y": 81.0, "label": "VOLUME"},

        {"type": "pad", "id": "PLAY", "x": 9.6, "y": 93.6, "color": "cyan", "label": "PLAY", "labelPos": "right"},
        {"type": "led", "id": "PLAY_LED", "x": 23.6, "y": 93.6, "size": "S"},
        {"type": "button", "id": "LOCK", "x": 31.0, "y": 93.6, "label": "LOCK"},
        {"type": "led", "id": "LOCK_LED", "x": 35.4, "y": 93.6, "size": "S"},
        {"type": "button", "id": "DICE", "x": 45.0, "y": 93.6, "label": "DICE"},
        {"type": "button", "id": "ANCHOR", "x": 56.2, "y": 93.6, "label": "ANCHOR"},
        {"type": "led", "id": "ANCHOR_DB", "x": 63.0, "y": 92.0, "size": "S", "label": "DOWN"},
        {"type": "led", "id": "ANCHOR_K", "x": 63.0, "y": 95.6, "size": "S", "label": "KICKS"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": _C6[0], "y": 103.6, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": _C6[1], "y": 103.6, "label": "RST"},
        {"type": "jack", "id": "GATE", "kind": "in", "x": _C6[2], "y": 103.6, "label": "GATE"},
        {"type": "jack", "id": "SLICE", "kind": "in", "x": _C6[3], "y": 103.6, "label": "SLICE"},
        {"type": "jack", "id": "DICE_IN", "kind": "in", "x": _C6[4], "y": 103.6, "label": "DICE"},
        {"type": "jack", "id": "VARY_CV", "kind": "in", "x": _C6[5], "y": 103.6, "label": "VARY"},

        {"type": "plate", "x": 2.7, "y": 108.0, "w": 75.88, "h": 15.4},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": _C5[0], "y": 118.0, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": _C5[1], "y": 118.0, "label": "R"},
        {"type": "jack", "id": "TRIG", "kind": "out", "x": _C5[2], "y": 118.0, "label": "TRIG"},
        {"type": "jack", "id": "VAR", "kind": "out", "x": _C5[3], "y": 118.0, "label": "VAR"},
        {"type": "jack", "id": "SLC", "kind": "out", "x": _C5[4], "y": 118.0, "label": "SLC"},
    ],
}
