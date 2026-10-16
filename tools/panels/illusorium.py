# ILLUSORIUM — 20 HP. Units: mm. See tools/panelgen.py for the item types.
# 26 auditory illusions as one stereo effect, one at a time.
# Top: the illusion screen (name, who described it, a drawing of the percept, "you hear").
# Below: the parameter screen (what P1..P6 are for the selected illusion, with values).
# Knobs: ILLUSION selector + MIX on the left, P1..P6, GRAB (capture / restart / reseed).
_J = [8.6, 22.0, 35.4, 48.8, 62.2, 75.6, 89.0]

items = [
    {"type": "screen", "id": "ILLUSION", "x": 3.1, "y": 15.4, "w": 95.4, "h": 38.0},
    {"type": "screen", "id": "PARAMS", "x": 3.1, "y": 56.4, "w": 95.4, "h": 11.6},

    {"type": "knob", "id": "SELECT", "size": "L", "x": 11.0, "y": 78.4, "label": "ILLUSION"},
    {"type": "knob", "id": "MIX", "size": "M", "x": 11.0, "y": 93.6, "label": "MIX"},

    {"type": "knob", "id": "P1", "size": "M", "x": 30.0, "y": 78.4, "label": "P1"},
    {"type": "knob", "id": "P2", "size": "M", "x": 46.0, "y": 78.4, "label": "P2"},
    {"type": "knob", "id": "P3", "size": "M", "x": 62.0, "y": 78.4, "label": "P3"},
    {"type": "knob", "id": "P4", "size": "M", "x": 30.0, "y": 93.6, "label": "P4"},
    {"type": "knob", "id": "P5", "size": "M", "x": 46.0, "y": 93.6, "label": "P5"},
    {"type": "knob", "id": "P6", "size": "M", "x": 62.0, "y": 93.6, "label": "P6", "hot": True},

    {"type": "button", "id": "GRAB", "x": 80.0, "y": 78.4, "label": "GRAB"},
    {"type": "led", "id": "GRAB_LED", "x": 87.0, "y": 78.4, "size": "S"},
    {"type": "led", "id": "PHONES", "x": 78.2, "y": 86.0, "size": "S", "label": "PHONES", "labelColor": "magenta"},
    {"type": "led", "id": "ACTIVE", "x": 78.2, "y": 91.2, "size": "S", "label": "WET"},
    {"type": "led", "id": "CLK_LED", "x": 78.2, "y": 96.4, "size": "S", "label": "CLK"},
]
ins = [("IN_L", "IN L"), ("IN_R", "IN R"), ("CLK", "CLK"), ("RST", "RST"), ("GRAB_IN", "GRAB"), ("MOD", "MOD"), ("MIX_CV", "MIX")]
for i, (id_, lab) in enumerate(ins):
    items.append({"type": "jack", "id": id_, "kind": "in", "x": _J[i], "y": 104.8, "label": lab})

items.append({"type": "plate", "x": 2.7, "y": 110.2, "w": 96.2, "h": 14.0})
items.append({"type": "jack", "id": "OUT_L", "kind": "out", "x": 36.0, "y": 118.8, "label": "OUT L"})
items.append({"type": "jack", "id": "OUT_R", "kind": "out", "x": 65.6, "y": 118.8, "label": "OUT R"})

SPEC = {
    "slug": "Illusorium",
    "hp": 20,
    "title": "ILLUSORIUM",
    "subtitle": "26 auditory illusions",
    "subtitleColor": "magenta",
    "items": items,
}
