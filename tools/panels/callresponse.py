# CALL & RESPONSE — 16 HP. Units: mm. See tools/panelgen.py for the item types.
# Top: the phrase screen (call + response rolls, the pass plan, playhead), a name strip (style /
# answer / mode). Selector knobs (STYLE, MOTIF, ANSWER, MODE), macros (DENSITY, CONTRAST, OCT,
# DRIFT), NEW / MUTATE / DICE. Inputs above the cyan plate: call and response voices out.
_K = [9.6, 25.1, 40.6, 56.1]
_I = [9.6, 25.1, 40.6, 56.1, 71.6]
_O = [7.8, 20.4, 33.0, 48.2, 60.8, 73.4]

SPEC = {
    "slug": "CallResponse",
    "hp": 16,
    "title": "CALL & RESP",
    "subtitle": "Nordl3 / Side Brain",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "PHRASE", "x": 3.1, "y": 15.4, "w": 75.08, "h": 32.0},
        {"type": "screen", "id": "NAMES", "x": 3.1, "y": 49.8, "w": 75.08, "h": 9.0},

        {"type": "knob", "id": "STYLE", "size": "M", "x": _K[0], "y": 69.0, "label": "STYLE"},
        {"type": "knob", "id": "MOTIF", "size": "M", "x": _K[1], "y": 69.0, "label": "MOTIF"},
        {"type": "knob", "id": "ANSWER", "size": "M", "x": _K[2], "y": 69.0, "label": "ANSWER"},
        {"type": "knob", "id": "MODE", "size": "M", "x": _K[3], "y": 69.0, "label": "MODE"},
        {"type": "knob", "id": "DENSITY", "size": "M", "x": _K[0], "y": 85.0, "label": "DENSITY"},
        {"type": "knob", "id": "CONTRAST", "size": "M", "x": _K[1], "y": 85.0, "label": "CONTRAST"},
        {"type": "knob", "id": "OCTJUMP", "size": "M", "x": _K[2], "y": 85.0, "label": "OCT JUMP"},
        {"type": "knob", "id": "DRIFT", "size": "M", "x": _K[3], "y": 85.0, "label": "DRIFT", "hot": True},

        {"type": "button", "id": "NEW", "x": 71.6, "y": 67.0, "label": "NEW"},
        {"type": "led", "id": "NEW_LED", "x": 76.0, "y": 67.0, "size": "S"},
        {"type": "button", "id": "MUTATE", "x": 71.6, "y": 77.0, "label": "MUTATE"},
        {"type": "button", "id": "DICE", "x": 71.6, "y": 87.0, "label": "DICE"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": _I[0], "y": 100.6, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": _I[1], "y": 100.6, "label": "RST"},
        {"type": "jack", "id": "FILL", "kind": "in", "x": _I[2], "y": 100.6, "label": "FILL"},
        {"type": "jack", "id": "VOCT_IN", "kind": "in", "x": _I[3], "y": 100.6, "label": "V/OCT"},
        {"type": "jack", "id": "NEW_IN", "kind": "in", "x": _I[4], "y": 100.6, "label": "NEW"},

        {"type": "plate", "x": 2.0, "y": 106.6, "w": 77.28, "h": 17.0},
        {"type": "label", "text": "CALL", "x": 20.4, "y": 109.2, "size": 1.7, "color": "cyan"},
        {"type": "label", "text": "RESPONSE", "x": 60.8, "y": 109.2, "size": 1.7, "color": "magenta"},
        {"type": "jack", "id": "C_VOCT", "kind": "out", "x": _O[0], "y": 118.6, "label": "V/OCT"},
        {"type": "jack", "id": "C_GATE", "kind": "out", "x": _O[1], "y": 118.6, "label": "GATE"},
        {"type": "jack", "id": "C_VEL", "kind": "out", "x": _O[2], "y": 118.6, "label": "VEL"},
        {"type": "jack", "id": "R_VOCT", "kind": "out", "x": _O[3], "y": 118.6, "label": "V/OCT"},
        {"type": "jack", "id": "R_GATE", "kind": "out", "x": _O[4], "y": 118.6, "label": "GATE"},
        {"type": "jack", "id": "R_VEL", "kind": "out", "x": _O[5], "y": 118.6, "label": "VEL"},
    ],
}
