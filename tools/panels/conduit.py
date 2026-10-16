# CONDUIT — 20 HP. Units: mm. See tools/panelgen.py for the item types.
# From the top: pin matrix + PIN screens, the pin editor (row / column select, the five pin
# controls, PIN button, MASTER), inputs (BUS, CLK, 4 triggers, 4 CVs), depth per output, output plate.
_items = [
    {"type": "screen", "id": "MATRIX", "x": 3.5, "y": 14.6, "w": 63.0, "h": 50.0},
    {"type": "screen", "id": "PIN", "x": 69.6, "y": 14.6, "w": 28.5, "h": 50.0},
]
for i, (k, lab) in enumerate([("ROW", "ROW"), ("COL", "COL"), ("AMOUNT", "AMT"), ("CURVE", "CURVE"),
                              ("DELAY", "DELAY"), ("PROB", "PROB"), ("LEN", "LEN")]):
    _items.append({"type": "knob", "id": k, "size": "S", "x": 7.5 + i * 10.0, "y": 73.0, "label": lab})
_items += [
    {"type": "button", "id": "PINBTN", "x": 79.0, "y": 73.0, "label": "PIN"},
    {"type": "led", "id": "PIN_LED", "x": 79.0, "y": 78.4, "size": "S"},
    {"type": "knob", "id": "MASTER", "size": "M", "x": 92.6, "y": 73.0, "label": "MASTER"},
    {"type": "jack", "id": "BUS_IN", "kind": "in", "bus": True, "x": 7.0, "y": 88.0, "label": "BUS·4"},
    {"type": "jack", "id": "CLK", "kind": "in", "x": 16.6, "y": 88.0, "label": "CLK"},
]
for i in range(4):
    _items.append({"type": "jack", "id": f"TRG{i + 1}", "kind": "in", "x": 26.2 + i * 9.6, "y": 88.0, "label": f"TRG{i + 1}"})
for i in range(4):
    _items.append({"type": "jack", "id": f"CV{i + 1}", "kind": "in", "x": 64.6 + i * 9.6, "y": 88.0, "label": f"CV{i + 1}"})
_items.append({"type": "plate", "x": 2.7, "y": 107.6, "w": 96.2, "h": 15.6})
for i in range(8):
    x = 7.0 + i * 10.9
    _items.append({"type": "knob", "id": f"DEPTH{i + 1}", "size": "S", "x": x, "y": 100.6})
    _items.append({"type": "jack", "id": f"OUT{i + 1}", "kind": "out", "x": x, "y": 118.0, "label": f"OUT{i + 1}"})
_items.append({"type": "label", "text": "DEPTH", "x": 94.2, "y": 100.6, "size": 1.9, "color": "dim"})
_items.append({"type": "jack", "id": "BUS_OUT", "kind": "out", "bus": True, "x": 94.2, "y": 118.0, "label": "BUS·4"})

SPEC = {
    "slug": "Conduit",
    "hp": 20,
    "title": "CONDUIT",
    "subtitle": "patch bay",
    "subtitleColor": "dim",
    "items": _items,
}
