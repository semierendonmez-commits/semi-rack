# TURNAROUND — 12 HP. Units: mm. See tools/panelgen.py for the item types.
# Width 60.96 mm. Columns for knobs: 11.0 / 30.48 / 49.96.
_LED_X0, _LED_DX = 25.2, 4.3

SPEC = {
    "slug": "Turnaround",
    "hp": 12,
    "title": "TURNAROUND",
    "subtitle": "phrase-end",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "MAP", "x": 3.48, "y": 15.6, "w": 54.0, "h": 24.0},

        {"type": "label", "text": "EDIT BUDGET", "x": 3.0, "y": 45.6, "size": 1.8, "color": "dim", "anchor": "start"},
    ] + [
        {"type": "led", "id": f"BUDGET_LED{i}", "x": _LED_X0 + _LED_DX * i, "y": 45.6, "size": "S"}
        for i in range(8)
    ] + [
        {"type": "knob", "id": "BUDGET", "size": "M", "x": 11.0, "y": 59.0, "label": "BUDGET"},
        {"type": "knob", "id": "ROLL", "size": "M", "x": 30.48, "y": 59.0, "label": "ROLL"},
        {"type": "knob", "id": "WALL", "size": "M", "x": 49.96, "y": 59.0, "label": "WALL", "hot": True},
        {"type": "knob", "id": "GAP", "size": "M", "x": 11.0, "y": 77.0, "label": "GAP"},
        {"type": "knob", "id": "DEPTH", "size": "M", "x": 30.48, "y": 77.0, "label": "DEPTH"},
        {"type": "knob", "id": "VARY", "size": "M", "x": 49.96, "y": 77.0, "label": "VARY"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": 8.5, "y": 99.0, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": 23.15, "y": 99.0, "label": "RST"},
        {"type": "jack", "id": "BUS_IN", "kind": "in", "bus": True, "x": 37.8, "y": 99.0, "label": "BUS·4"},
        {"type": "jack", "id": "IN", "kind": "in", "x": 52.45, "y": 99.0, "label": "IN"},

        {"type": "plate", "x": 2.7, "y": 107.6, "w": 55.56, "h": 15.6},
        {"type": "jack", "id": "OUT", "kind": "out", "x": 8.5, "y": 118.0, "label": "OUT"},
        {"type": "jack", "id": "TURN", "kind": "out", "x": 23.15, "y": 118.0, "label": "TURN"},
        {"type": "jack", "id": "ROLL_GATE", "kind": "out", "x": 37.8, "y": 118.0, "label": "ROLL"},
        {"type": "jack", "id": "BUS_OUT", "kind": "out", "bus": True, "x": 52.45, "y": 118.0, "label": "BUS·4"},
    ],
}
