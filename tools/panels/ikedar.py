# IKEDAR — 16 HP. Units: mm. See tools/panelgen.py for the item types.
#
# Port of the Max 4.6 patch ryoz_ikedar_ln: nine voices, each a 64-cell automaton row.
# Top: the bit matrix (9 x 64, playhead); under it the data strip (master barcode, readouts).
# Knobs: the clock row (TEMPO = master bars) and the macro offsets over the voice rows,
# then the group levels and the automaton actions; inputs; the output plate.
W = 16 * 5.08
X6 = [8.0, 21.0, 34.0, 47.1, 60.2, 73.3]

SPEC = {
    "slug": "Ikedar",
    "hp": 16,
    "title": "IKEDAR",
    "subtitle": "ryoz_ikedar_ln",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "MATRIX", "x": 3.4, "y": 15.6, "w": 74.5, "h": 35.0},
        {"type": "screen", "id": "DATA", "x": 3.4, "y": 53.2, "w": 74.5, "h": 12.6},

        {"type": "knob", "id": "TEMPO", "size": "M", "x": X6[0], "y": 75.0, "label": "TEMPO"},
        {"type": "knob", "id": "SWING", "size": "M", "x": X6[1], "y": 75.0, "label": "SWING"},
        {"type": "knob", "id": "PITCH", "size": "M", "x": X6[2], "y": 75.0, "label": "PITCH"},
        {"type": "knob", "id": "GATE", "size": "M", "x": X6[3], "y": 75.0, "label": "GATE"},
        {"type": "knob", "id": "AUTO", "size": "M", "x": X6[4], "y": 75.0, "label": "AUTO"},
        {"type": "knob", "id": "CHANCE", "size": "M", "x": X6[5], "y": 75.0, "label": "CHANCE", "hot": True},

        {"type": "knob", "id": "WAVE", "size": "S", "x": X6[0], "y": 88.4, "label": "WAVE"},
        {"type": "knob", "id": "SPEC", "size": "S", "x": X6[1], "y": 88.4, "label": "SPEC"},
        {"type": "knob", "id": "SINE", "size": "S", "x": X6[2], "y": 88.4, "label": "SINE"},
        {"type": "knob", "id": "MASTER", "size": "S", "x": X6[3], "y": 88.4, "label": "MASTER"},
        {"type": "pad", "id": "MUTATE", "x": X6[4], "y": 88.4, "color": "magenta", "label": "MUTATE", "labelColor": "magenta"},
        {"type": "button", "id": "RAND", "x": 70.0, "y": 89.6, "label": "RAND"},
        {"type": "led", "id": "HOLD_LED", "x": 76.4, "y": 84.2, "size": "S"},
        {"type": "button", "id": "HOLD", "x": 76.4, "y": 89.6, "label": "HOLD"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": X6[0], "y": 100.0, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": X6[1], "y": 100.0, "label": "RST"},
        {"type": "jack", "id": "RUN", "kind": "in", "x": X6[2], "y": 100.0, "label": "RUN"},
        {"type": "jack", "id": "MUT", "kind": "in", "x": X6[3], "y": 100.0, "label": "MUT"},
        {"type": "jack", "id": "PITCH_CV", "kind": "in", "x": X6[4], "y": 100.0, "label": "PITCH"},
        {"type": "jack", "id": "BUS", "kind": "in", "bus": True, "x": X6[5], "y": 100.0, "label": "BUS·4"},

        {"type": "plate", "x": 2.7, "y": 107.6, "w": W - 5.4, "h": 15.6},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": 21.0, "y": 118.0, "label": "OUT L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": 40.6, "y": 118.0, "label": "OUT R"},
        {"type": "jack", "id": "STEP", "kind": "out", "x": 60.2, "y": 118.0, "label": "STEP"},
    ],
}
