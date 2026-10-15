# TURNAROUND — 14 HP (stereo). Units: mm. See tools/panelgen.py for the item types.
# Width 71.12 mm. Knob columns 12.5 / 35.56 / 58.62; five jack columns, 13.53 apart.
_LED_X0, _LED_DX = 30.6, 5.0
_J = [8.5 + 13.53 * k for k in range(5)]

SPEC = {
    "slug": "Turnaround",
    "hp": 14,
    "title": "TURNAROUND",
    "subtitle": "phrase-end · stereo",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "MAP", "x": 3.48, "y": 15.6, "w": 64.16, "h": 24.0},

        {"type": "label", "text": "EDIT BUDGET", "x": 3.0, "y": 45.6, "size": 1.8, "color": "dim", "anchor": "start"},
    ] + [
        {"type": "led", "id": f"BUDGET_LED{i}", "x": _LED_X0 + _LED_DX * i, "y": 45.6, "size": "S"}
        for i in range(8)
    ] + [
        {"type": "knob", "id": "BUDGET", "size": "M", "x": 12.5, "y": 59.0, "label": "BUDGET"},
        {"type": "knob", "id": "ROLL", "size": "M", "x": 35.56, "y": 59.0, "label": "ROLL"},
        {"type": "knob", "id": "WALL", "size": "M", "x": 58.62, "y": 59.0, "label": "WALL", "hot": True},
        {"type": "knob", "id": "GAP", "size": "M", "x": 12.5, "y": 77.0, "label": "GAP"},
        {"type": "knob", "id": "DEPTH", "size": "M", "x": 35.56, "y": 77.0, "label": "DEPTH"},
        {"type": "knob", "id": "VARY", "size": "M", "x": 58.62, "y": 77.0, "label": "VARY"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": _J[0], "y": 99.0, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": _J[1], "y": 99.0, "label": "RST"},
        {"type": "jack", "id": "BUS_IN", "kind": "in", "bus": True, "x": _J[2], "y": 99.0, "label": "BUS·4"},
        {"type": "jack", "id": "IN_L", "kind": "in", "x": _J[3], "y": 99.0, "label": "IN L"},
        {"type": "jack", "id": "IN_R", "kind": "in", "x": _J[4], "y": 99.0, "label": "IN R"},

        {"type": "plate", "x": 2.7, "y": 107.6, "w": 65.72, "h": 15.6},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": _J[0], "y": 118.0, "label": "OUT L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": _J[1], "y": 118.0, "label": "OUT R"},
        {"type": "jack", "id": "TURN", "kind": "out", "x": _J[2], "y": 118.0, "label": "TURN"},
        {"type": "jack", "id": "ROLL_GATE", "kind": "out", "x": _J[3], "y": 118.0, "label": "ROLL"},
        {"type": "jack", "id": "BUS_OUT", "kind": "out", "bus": True, "x": _J[4], "y": 118.0, "label": "BUS·4"},
    ],
}
