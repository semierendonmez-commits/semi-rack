# SPLICEBUS — 16 HP. Units: mm. See tools/panelgen.py for the item types.
#
# Top: TAPE (the splices with the genes reading them) and FORM (32 bars as eight 4-bar lines, then this
# line and the next slot by slot: breaks, turns, rolls, anchors, walls). Split mode LEDs + button, HOLD.
# Knobs: the reel (CHAOS, VARI, MORPH, ORGANIZE, SLIDE) and the form / split / out row.
# Inputs: IN L/R, SC, CLK, RST, BUS, CHAOS CV. Plate: OUT L/R, BRK (a gate while a gene breaks).
W = 16 * 5.08
K5 = [9.2, 24.6, 40.6, 56.6, 72.0]
K6 = [7.8, 21.0, 34.2, 47.4, 60.6, 73.8]
J7 = [6.6, 17.3, 28.0, 38.7, 49.4, 60.1, 70.8]

SPEC = {
    "slug": "Splicebus",
    "hp": 16,
    "title": "SPLICEBUS",
    "subtitle": "the reel on any bus",
    "subtitleColor": "dim",
    "items": [
        {"type": "screen", "id": "TAPE", "x": 3.1, "y": 15.4, "w": 75.0, "h": 19.6},
        {"type": "screen", "id": "FORM", "x": 3.1, "y": 38.0, "w": 75.0, "h": 23.4},

        {"type": "led", "id": "SPLIT_OFF", "x": 5.0, "y": 66.0, "size": "S", "label": "OFF"},
        {"type": "led", "id": "SPLIT_FIXED", "x": 15.0, "y": 66.0, "size": "S", "label": "FIXED"},
        {"type": "led", "id": "SPLIT_TRACK", "x": 27.0, "y": 66.0, "size": "S", "label": "TRACK"},
        {"type": "button", "id": "SPLIT", "x": 40.0, "y": 66.0},
        {"type": "label", "text": "SPLIT", "x": 46.0, "y": 66.0, "size": 1.7, "color": "dim", "anchor": "start"},
        {"type": "button", "id": "HOLD", "x": 66.0, "y": 66.0},
        {"type": "led", "id": "HOLD_LED", "x": 71.0, "y": 66.0, "size": "S", "label": "HOLD"},

        {"type": "knob", "id": "CHAOS", "size": "L", "x": K5[0], "y": 78.4, "label": "CHAOS", "hot": True},
        {"type": "knob", "id": "VARI", "size": "M", "x": K5[1], "y": 78.4, "label": "VARI"},
        {"type": "knob", "id": "MORPH", "size": "M", "x": K5[2], "y": 78.4, "label": "MORPH"},
        {"type": "knob", "id": "ORGANIZE", "size": "M", "x": K5[3], "y": 78.4, "label": "ORGANIZE"},
        {"type": "knob", "id": "SLIDE", "size": "M", "x": K5[4], "y": 78.4, "label": "SLIDE"},

        {"type": "knob", "id": "TURNS", "size": "S", "x": K6[0], "y": 91.6, "label": "TURNS"},
        {"type": "knob", "id": "ROLL", "size": "S", "x": K6[1], "y": 91.6, "label": "ROLL"},
        {"type": "knob", "id": "FREQ", "size": "S", "x": K6[2], "y": 91.6, "label": "SPLIT HZ"},
        {"type": "knob", "id": "PROTECT", "size": "S", "x": K6[3], "y": 91.6, "label": "PROTECT"},
        {"type": "knob", "id": "MIX", "size": "S", "x": K6[4], "y": 91.6, "label": "MIX"},
        {"type": "knob", "id": "OUT", "size": "S", "x": K6[5], "y": 91.6, "label": "OUT"},

        {"type": "jack", "id": "IN_L", "kind": "in", "x": J7[0], "y": 102.0, "label": "IN L"},
        {"type": "jack", "id": "IN_R", "kind": "in", "x": J7[1], "y": 102.0, "label": "IN R"},
        {"type": "jack", "id": "SC", "kind": "in", "x": J7[2], "y": 102.0, "label": "SC"},
        {"type": "jack", "id": "CLK", "kind": "in", "x": J7[3], "y": 102.0, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": J7[4], "y": 102.0, "label": "RST"},
        {"type": "jack", "id": "BUS", "kind": "in", "bus": True, "x": J7[5], "y": 102.0, "label": "BUS"},
        {"type": "jack", "id": "CHAOS_CV", "kind": "in", "x": J7[6], "y": 102.0, "label": "CHAOS"},

        {"type": "plate", "x": 2.7, "y": 108.0, "w": W - 5.4, "h": 15.6},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": 14.0, "y": 118.2, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": 32.0, "y": 118.2, "label": "R"},
        {"type": "jack", "id": "BRK", "kind": "out", "x": 54.0, "y": 118.2, "label": "BRK"},
        {"type": "jack", "id": "TURN", "kind": "out", "x": 70.0, "y": 118.2, "label": "TURN"},
    ],
}
