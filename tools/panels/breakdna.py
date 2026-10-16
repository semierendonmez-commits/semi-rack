# BREAKDNA — 16 HP. Units: mm. See tools/panelgen.py for the item types.
# Top: dna.a / skin.b (A's waveform, slices by class, the lit slice; B under it, the slices that
# answer A bright) and the genome (K S G H per 16th, micro-timing, the morph lane, the playhead).
# Knobs: the DNA (M row) and the cross / output (S row) with the SOURCE selector.
_C7 = [7.6 + 11.0 * i for i in range(7)]   # seven input columns

SPEC = {
    "slug": "Breakdna",
    "hp": 16,
    "title": "BREAKDNA",
    "subtitle": "groove dna · spectral cross",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "DNA", "x": 3.1, "y": 15.4, "w": 75.0, "h": 27.0},
        {"type": "screen", "id": "GENOME", "x": 3.1, "y": 44.6, "w": 75.0, "h": 19.6},

        {"type": "knob", "id": "FEEL", "size": "M", "x": 9.6, "y": 75.6, "label": "FEEL"},
        {"type": "knob", "id": "DYN", "size": "M", "x": 25.1, "y": 75.6, "label": "DYN"},
        {"type": "knob", "id": "GHOSTS", "size": "M", "x": 40.6, "y": 75.6, "label": "GHOSTS"},
        {"type": "knob", "id": "MORPH", "size": "M", "x": 56.1, "y": 75.6, "label": "MORPH", "hot": True},
        {"type": "knob", "id": "LANE", "size": "M", "x": 71.6, "y": 75.6, "label": "LANE"},

        {"type": "knob", "id": "ROTATE", "size": "S", "x": 9.6, "y": 89.0, "label": "ROTATE"},
        {"type": "knob", "id": "DECAY", "size": "S", "x": 22.0, "y": 89.0, "label": "DECAY"},
        {"type": "knob", "id": "TUNE", "size": "S", "x": 34.4, "y": 89.0, "label": "TUNE"},
        {"type": "knob", "id": "LEVEL", "size": "S", "x": 46.8, "y": 89.0, "label": "LEVEL"},
        {"type": "button", "id": "SOURCE", "x": 58.6, "y": 89.0, "label": "SOURCE"},
        {"type": "led", "id": "SRC_BREAK", "x": 64.4, "y": 87.0, "size": "S", "label": "BREAK A"},
        {"type": "led", "id": "SRC_GATES", "x": 64.4, "y": 91.0, "size": "S", "label": "GATES"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": _C7[0], "y": 101.6, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": _C7[1], "y": 101.6, "label": "RST"},
        {"type": "jack", "id": "K_IN", "kind": "in", "x": _C7[2], "y": 101.6, "label": "K"},
        {"type": "jack", "id": "S_IN", "kind": "in", "x": _C7[3], "y": 101.6, "label": "S"},
        {"type": "jack", "id": "H_IN", "kind": "in", "x": _C7[4], "y": 101.6, "label": "H"},
        {"type": "jack", "id": "MORPH_CV", "kind": "in", "x": _C7[5], "y": 101.6, "label": "MORPH"},
        {"type": "jack", "id": "FEEL_CV", "kind": "in", "x": _C7[6], "y": 101.6, "label": "FEEL"},

        {"type": "plate", "x": 2.7, "y": 107.6, "w": 75.88, "h": 15.6},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": 9.0, "y": 118.0, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": 21.6, "y": 118.0, "label": "R"},
        {"type": "jack", "id": "K_OUT", "kind": "out", "x": 34.2, "y": 118.0, "label": "K"},
        {"type": "jack", "id": "S_OUT", "kind": "out", "x": 46.8, "y": 118.0, "label": "S"},
        {"type": "jack", "id": "H_OUT", "kind": "out", "x": 59.4, "y": 118.0, "label": "H"},
        {"type": "jack", "id": "ACC_OUT", "kind": "out", "x": 72.0, "y": 118.0, "label": "ACC"},
    ],
}
