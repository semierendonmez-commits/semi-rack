# PALIMPSEST — 14 HP. Units: mm. See tools/panelgen.py for the item types.
#
# Port of tst_ingp.mxt: text -> 64 x 32 bit field, 32 sines read it row by row, a cellular
# automaton cursor rewrites it while it plays. Top: the field (cursor, per-row read heads);
# under it the rule strip and readouts; GEN / WALK toggles; sound knobs; motion knobs and the
# POKE pad (the patch's click on the LCD); inputs; outputs.
W = 14 * 5.08
X5 = [8.0, 21.8, 35.6, 49.4, 63.2]
X6 = [7.0, 18.4, 29.8, 41.2, 52.6, 64.0]

SPEC = {
    "slug": "TstIngp",
    "hp": 14,
    "title": "PALIMPSEST",
    "subtitle": "text field · 32 sines",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "FIELD", "x": 3.4, "y": 15.6, "w": 64.3, "h": 33.2},
        {"type": "screen", "id": "RULE", "x": 3.4, "y": 51.2, "w": 64.3, "h": 9.6},

        {"type": "led", "id": "GEN_LED", "x": 5.2, "y": 65.4, "size": "S", "label": "GEN"},
        {"type": "button", "id": "GEN", "x": 19.0, "y": 65.4},
        {"type": "led", "id": "WALK_LED", "x": 28.4, "y": 65.4, "size": "S", "label": "FULL"},
        {"type": "button", "id": "WALK", "x": 42.0, "y": 65.4},
        {"type": "led", "id": "AIR_LED", "x": 51.4, "y": 65.4, "size": "S", "label": "AIR"},
        {"type": "button", "id": "AIR", "x": 64.0, "y": 65.4},

        {"type": "knob", "id": "BPM", "size": "M", "x": X5[0], "y": 77.6, "label": "BPM"},
        {"type": "knob", "id": "SMOOTH", "size": "M", "x": X5[1], "y": 77.6, "label": "SMOOTH"},
        {"type": "knob", "id": "TUNE", "size": "M", "x": X5[2], "y": 77.6, "label": "TUNE"},
        {"type": "knob", "id": "TILT", "size": "M", "x": X5[3], "y": 77.6, "label": "TILT"},
        {"type": "knob", "id": "WIDTH", "size": "M", "x": X5[4], "y": 77.6, "label": "WIDTH"},

        {"type": "knob", "id": "SWING", "size": "S", "x": X6[0], "y": 89.6, "label": "SWING"},
        {"type": "knob", "id": "GENS", "size": "S", "x": X6[1], "y": 89.6, "label": "GEN/ST"},
        {"type": "knob", "id": "EVOLVE", "size": "S", "x": X6[2], "y": 89.6, "label": "EVOLVE", "hot": True},
        {"type": "knob", "id": "MASTER", "size": "S", "x": X6[3], "y": 89.6, "label": "MASTER"},
        {"type": "pad", "id": "POKE", "x": 58.3, "y": 89.6, "color": "magenta", "label": "POKE", "labelColor": "magenta"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": X6[0], "y": 100.4, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": X6[1], "y": 100.4, "label": "RST"},
        {"type": "jack", "id": "RUN", "kind": "in", "x": X6[2], "y": 100.4, "label": "RUN"},
        {"type": "jack", "id": "VOCT", "kind": "in", "x": X6[3], "y": 100.4, "label": "V/OCT"},
        {"type": "jack", "id": "TILT_CV", "kind": "in", "x": X6[4], "y": 100.4, "label": "TILT"},
        {"type": "jack", "id": "BUS", "kind": "in", "bus": True, "x": X6[5], "y": 100.4, "label": "BUS·4"},

        {"type": "plate", "x": 2.7, "y": 107.6, "w": W - 5.4, "h": 15.6},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": 10.0, "y": 118.0, "label": "OUT L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": 27.0, "y": 118.0, "label": "OUT R"},
        {"type": "jack", "id": "TRIG", "kind": "out", "x": 44.0, "y": 118.0, "label": "TRIG"},
        {"type": "jack", "id": "CA", "kind": "out", "x": 61.0, "y": 118.0, "label": "CA"},
    ],
}
