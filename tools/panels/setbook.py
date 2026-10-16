# SETBOOK — 24 HP. Units: mm. See tools/panelgen.py for the item types.
# Left: scene cards + cue lane screens, 8 scene buttons (LED above), 8 channel knobs, 8 channel inputs,
# 8 outputs. Right: morph screen, STORE / LIVE / PAD, TIME / CURVE / THRESH, BUS + SCN / RCL / NXT / X / Y,
# BUS OUT, END and MORPH outputs.
_items = [
    {"type": "screen", "id": "CARDS", "x": 3.5, "y": 14.6, "w": 76.0, "h": 31.0},
    {"type": "screen", "id": "LANE", "x": 3.5, "y": 48.2, "w": 76.0, "h": 10.0},
    {"type": "screen", "id": "INFO", "x": 82.4, "y": 14.6, "w": 36.0, "h": 43.6},
]
for i in range(8):
    x = 7.0 + i * 9.6
    _items.append({"type": "led", "id": f"SCENE{i + 1}_LED", "x": x, "y": 63.0, "size": "S", "label": str(i + 1), "labelPos": "above"})
    _items.append({"type": "button", "id": f"SCENE{i + 1}", "x": x, "y": 68.0})
    _items.append({"type": "knob", "id": f"CH{i + 1}", "size": "S", "x": x, "y": 79.0, "label": f"CH{i + 1}"})
    _items.append({"type": "jack", "id": f"IN{i + 1}", "kind": "in", "x": x, "y": 91.5, "label": f"IN{i + 1}"})
_R = [87.0, 99.0, 111.0]
_items += [
    {"type": "pad", "id": "STORE", "x": _R[0], "y": 67.0, "color": "magenta", "label": "STORE"},
    {"type": "button", "id": "LIVE", "x": _R[1], "y": 67.0, "label": "LIVE"},
    {"type": "button", "id": "PAD", "x": _R[2] - 1.6, "y": 67.0, "label": "PAD"},
    {"type": "led", "id": "PAD_LED", "x": _R[2] + 3.2, "y": 67.0, "size": "S"},
    {"type": "knob", "id": "TIME", "size": "M", "x": _R[0], "y": 79.0, "label": "TIME"},
    {"type": "knob", "id": "CURVE", "size": "S", "x": _R[1], "y": 79.0, "label": "CURVE"},
    {"type": "knob", "id": "THRESH", "size": "S", "x": _R[2], "y": 79.0, "label": "THRESH"},
    {"type": "jack", "id": "BUS_IN", "kind": "in", "bus": True, "x": _R[0], "y": 91.5, "label": "BUS·4"},
    {"type": "jack", "id": "SCN", "kind": "in", "x": _R[1], "y": 91.5, "label": "SCN"},
    {"type": "jack", "id": "RCL", "kind": "in", "x": _R[2], "y": 91.5, "label": "RCL"},
    {"type": "jack", "id": "NXT", "kind": "in", "x": _R[0], "y": 104.0, "label": "NXT"},
    {"type": "jack", "id": "X", "kind": "in", "x": _R[1], "y": 104.0, "label": "X"},
    {"type": "jack", "id": "Y", "kind": "in", "x": _R[2], "y": 104.0, "label": "Y"},
    {"type": "plate", "x": 2.7, "y": 109.4, "w": 116.5, "h": 14.4},
]
for i in range(8):
    _items.append({"type": "jack", "id": f"OUT{i + 1}", "kind": "out", "x": 7.0 + i * 9.6, "y": 119.2, "label": f"OUT{i + 1}"})
_items += [
    {"type": "jack", "id": "BUS_OUT", "kind": "out", "bus": True, "x": _R[0], "y": 119.2, "label": "BUS·4"},
    {"type": "jack", "id": "END", "kind": "out", "x": _R[1], "y": 119.2, "label": "END"},
    {"type": "jack", "id": "MORPH", "kind": "out", "x": _R[2], "y": 119.2, "label": "MORPH"},
]

SPEC = {
    "slug": "Setbook",
    "hp": 24,
    "title": "SETBOOK",
    "subtitle": "scene memory",
    "subtitleColor": "dim",
    "items": _items,
}
