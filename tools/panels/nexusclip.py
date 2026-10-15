# NEXUSCLIP — 10 HP. Units: mm. See tools/panelgen.py for the item types.
SPEC = {
    "slug": "Nexusclip",
    "hp": 10,
    "title": "NEXUSCLIP",
    "subtitle": "CTZ",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "TRANSFER", "x": 3.4, "y": 15.6, "w": 44.0, "h": 25.0},

        {"type": "led", "id": "MODE_CLIP", "x": 5.2, "y": 46.0, "size": "S", "label": "CLIP"},
        {"type": "led", "id": "MODE_SPEC", "x": 17.6, "y": 46.0, "size": "S", "label": "SPEC"},
        {"type": "led", "id": "MODE_LIMIT", "x": 30.0, "y": 46.0, "size": "S", "label": "LIMIT"},
        {"type": "button", "id": "MODE", "x": 45.0, "y": 46.0},

        {"type": "knob", "id": "DRIVE", "size": "M", "x": 10.2, "y": 60.0, "label": "DRIVE"},
        {"type": "knob", "id": "THRESH", "size": "M", "x": 25.4, "y": 60.0, "label": "THRESH"},
        {"type": "knob", "id": "SHAPE", "size": "M", "x": 40.6, "y": 60.0, "label": "SHAPE"},
        {"type": "knob", "id": "SOFT", "size": "M", "x": 10.2, "y": 77.0, "label": "SOFT"},
        {"type": "knob", "id": "SAT", "size": "M", "x": 25.4, "y": 77.0, "label": "SAT", "hot": True},
        {"type": "knob", "id": "OUT", "size": "M", "x": 40.6, "y": 77.0, "label": "OUT"},

        {"type": "jack", "id": "IN_L", "kind": "in", "x": 7.55, "y": 99.0, "label": "IN L"},
        {"type": "jack", "id": "IN_R", "kind": "in", "x": 19.45, "y": 99.0, "label": "IN R"},
        {"type": "jack", "id": "DRIVE_CV", "kind": "in", "x": 31.35, "y": 99.0, "label": "DRV"},
        {"type": "jack", "id": "THRESH_CV", "kind": "in", "x": 43.25, "y": 99.0, "label": "THR"},

        {"type": "plate", "x": 2.7, "y": 107.6, "w": 45.4, "h": 15.6},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": 17.0, "y": 118.0, "label": "OUT L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": 33.8, "y": 118.0, "label": "OUT R"},
    ],
}
