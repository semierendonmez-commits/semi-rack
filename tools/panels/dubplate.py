# DUBPLATE — 22 HP. Units: mm. See tools/panelgen.py for the item types.
# From the top: arrangement screen (section timeline + role x section matrix); genre
# selector (LEDs + button) and the generators (FORM, VIP, MUTATE); ENERGY / CHAOS / FILL /
# SWING / TEMPO + RUN; inputs; output plate: one gate per role group, SLICE, BASS V/OCT,
# EVT, CLK and the BUS.
W = 22 * 5.08
X0, DX = 8.2, 13.6
COL = [X0 + DX * i for i in range(8)]

SPEC = {
    "slug": "Dubplate",
    "hp": 22,
    "title": "DUBPLATE",
    "subtitle": "genre arrangement builder",
    "items": [
        {"type": "screen", "id": "ARRANGE", "x": 3.4, "y": 15.4, "w": W - 6.8, "h": 46.0},

        {"type": "led", "id": "G_JUNGLE", "x": 4.6, "y": 67.4, "size": "S", "label": "JUNGLE"},
        {"type": "led", "id": "G_DNB", "x": 18.4, "y": 67.4, "size": "S", "label": "DNB"},
        {"type": "led", "id": "G_UKG", "x": 28.2, "y": 67.4, "size": "S", "label": "UKG"},
        {"type": "led", "id": "G_SPEED", "x": 38.0, "y": 67.4, "size": "S", "label": "SPEED"},
        {"type": "button", "id": "GENRE", "x": 51.6, "y": 67.4},
        {"type": "button", "id": "FORM", "x": 66.0, "y": 67.4, "label": "FORM", "labelPos": "right"},
        {"type": "button", "id": "VIP", "x": 81.0, "y": 67.4, "label": "VIP", "labelPos": "right"},
        {"type": "button", "id": "MUTATE", "x": 93.6, "y": 67.4, "label": "MUT", "labelPos": "right"},

        {"type": "knob", "id": "ENERGY", "size": "L", "x": 10.0, "y": 80.0, "label": "ENERGY"},
        {"type": "knob", "id": "CHAOS", "size": "M", "x": 25.6, "y": 80.0, "label": "CHAOS", "hot": True},
        {"type": "knob", "id": "FILL", "size": "M", "x": 40.0, "y": 80.0, "label": "FILL"},
        {"type": "knob", "id": "SWING", "size": "M", "x": 54.4, "y": 80.0, "label": "SWING"},
        {"type": "knob", "id": "TEMPO", "size": "M", "x": 68.8, "y": 80.0, "label": "TEMPO"},
        {"type": "pad", "id": "RUN", "x": 86.0, "y": 80.0, "color": "cyan", "label": "RUN"},
        {"type": "led", "id": "RUN_LED", "x": 96.4, "y": 80.0, "size": "S", "label": "PLAY", "labelPos": "above"},
        {"type": "led", "id": "CLK_LED", "x": 104.0, "y": 80.0, "size": "S", "label": "EXT", "labelPos": "above"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": COL[0], "y": 93.0, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": COL[1], "y": 93.0, "label": "RST"},
        {"type": "jack", "id": "RUN_IN", "kind": "in", "x": COL[2], "y": 93.0, "label": "RUN"},
        {"type": "jack", "id": "NEXT", "kind": "in", "x": COL[3], "y": 93.0, "label": "NEXT"},
        {"type": "jack", "id": "MUT_IN", "kind": "in", "x": COL[4], "y": 93.0, "label": "MUT"},
        {"type": "jack", "id": "FILL_IN", "kind": "in", "x": COL[5], "y": 93.0, "label": "FILL"},
        {"type": "jack", "id": "NRG_CV", "kind": "in", "x": COL[6], "y": 93.0, "label": "NRG"},
        {"type": "jack", "id": "CHS_CV", "kind": "in", "x": COL[7], "y": 93.0, "label": "CHS"},

        {"type": "plate", "x": 2.7, "y": 98.0, "w": W - 5.4, "h": 25.4},
    ] + [
        {"type": "jack", "id": n, "kind": "out", "x": COL[i], "y": 105.2, "label": l}
        for i, (n, l) in enumerate([("KICK", "KICK"), ("SNARE", "SNR"), ("HAT", "HAT"), ("OHAT", "OHAT"),
                                    ("BREAK", "BRK"), ("SLICE", "SLICE"), ("PERC", "PERC"), ("FX", "FX")])
    ] + [
        {"type": "jack", "id": n, "kind": "out", "x": COL[i], "y": 118.6, "label": l}
        for i, (n, l) in enumerate([("SUB", "SUB"), ("BASS", "BASS"), ("BASS2", "BS2"), ("VOCT", "V/OCT"),
                                    ("MUSIC", "MUSIC"), ("EVT", "EVT"), ("CLK_OUT", "CLK")])
    ] + [
        {"type": "jack", "id": "BUS", "kind": "out", "bus": True, "x": COL[7], "y": 118.6, "label": "BUS·4"},
    ],
}
