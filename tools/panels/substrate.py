# SUBSTRATE — 14 HP. Units: mm. See tools/panelgen.py for the item types.
# Top: low.end map (the tracked f0 over time, the mono zone under SPLIT x f0, a tick per repaired
# edit); below it phase.scope (the input's low band and the rebuilt sub, three periods, CORR).
# Knobs: track / sub (row A), bands / repair / output (row B). Inputs above the cyan plate.
_X = [9.0, 22.3, 35.6, 48.9, 62.2]

SPEC = {
    "slug": "Substrate",
    "hp": 14,
    "title": "SUBSTRATE",
    "subtitle": "low end, sorted",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "MAP", "x": 3.2, "y": 15.4, "w": 64.7, "h": 25.0},
        {"type": "screen", "id": "SCOPE", "x": 3.2, "y": 42.4, "w": 64.7, "h": 19.0},

        {"type": "button", "id": "REPAIR", "x": _X[0], "y": 68.0, "label": "REPAIR"},
        {"type": "led", "id": "REPAIR_LED", "x": _X[0] + 5.6, "y": 68.0, "size": "S"},
        {"type": "button", "id": "MODE", "x": _X[2], "y": 68.0, "label": "PHASE"},
        {"type": "button", "id": "MON", "x": _X[4], "y": 68.0, "label": "MONITOR"},
        {"type": "led", "id": "MON_LED", "x": _X[4] - 5.6, "y": 68.0, "size": "S"},

        {"type": "knob", "id": "SPLIT", "size": "M", "x": _X[0], "y": 80.4, "label": "SPLIT"},
        {"type": "knob", "id": "SUB", "size": "M", "x": _X[1], "y": 80.4, "label": "SUB"},
        {"type": "knob", "id": "PHASE", "size": "M", "x": _X[2], "y": 80.4, "label": "PHASE"},
        {"type": "knob", "id": "FOLLOW", "size": "M", "x": _X[3], "y": 80.4, "label": "FOLLOW"},
        {"type": "knob", "id": "STAB", "size": "M", "x": _X[4], "y": 80.4, "label": "STABILIZE"},

        {"type": "knob", "id": "RESID", "size": "M", "x": _X[0], "y": 93.4, "label": "RESIDUAL"},
        {"type": "knob", "id": "UPPER", "size": "M", "x": _X[1], "y": 93.4, "label": "UPPER"},
        {"type": "knob", "id": "WIDTH", "size": "M", "x": _X[2], "y": 93.4, "label": "WIDTH"},
        {"type": "knob", "id": "SENS", "size": "M", "x": _X[3], "y": 93.4, "label": "SENS", "hot": True},
        {"type": "knob", "id": "OUT", "size": "M", "x": _X[4], "y": 93.4, "label": "OUT"},

        {"type": "jack", "id": "IN_L", "kind": "in", "x": _X[0], "y": 104.6, "label": "IN L"},
        {"type": "jack", "id": "IN_R", "kind": "in", "x": _X[1], "y": 104.6, "label": "IN R"},
        {"type": "jack", "id": "VOCT", "kind": "in", "x": _X[2], "y": 104.6, "label": "V/OCT"},
        {"type": "jack", "id": "SPLIT_CV", "kind": "in", "x": _X[3], "y": 104.6, "label": "SPLIT"},
        {"type": "jack", "id": "SUB_CV", "kind": "in", "x": _X[4], "y": 104.6, "label": "SUB"},

        {"type": "plate", "x": 2.7, "y": 109.0, "w": 65.72, "h": 14.6},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": 10.5, "y": 118.6, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": 24.5, "y": 118.6, "label": "R"},
        {"type": "jack", "id": "SUB_OUT", "kind": "out", "x": 46.5, "y": 118.6, "label": "SUB"},
        {"type": "jack", "id": "F0", "kind": "out", "x": 60.5, "y": 118.6, "label": "F0"},
    ],
}
