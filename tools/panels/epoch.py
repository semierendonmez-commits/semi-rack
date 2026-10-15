# EPOCH — 12 HP. Units: mm. See tools/panelgen.py for the item types.
# From the top: timeline screen, readout screen, plan selector (group LEDs + button),
# LENGTH / SCALE / SMOOTH / SILENCE, clock inputs + RUN pad, output plate.
SPEC = {
    "slug": "Epoch",
    "hp": 12,
    "title": "EPOCH",
    "subtitle": "long clock",
    "subtitleColor": "dim",
    "items": [
        {"type": "screen", "id": "TIMELINE", "x": 3.48, "y": 15.4, "w": 54.0, "h": 32.0},
        {"type": "screen", "id": "READOUT", "x": 3.48, "y": 50.0, "w": 54.0, "h": 13.0},

        {"type": "led", "id": "SRC_DNB", "x": 5.0, "y": 68.6, "size": "S", "label": "DNB"},
        {"type": "led", "id": "SRC_UKG", "x": 14.2, "y": 68.6, "size": "S", "label": "UKG"},
        {"type": "led", "id": "SRC_MSR", "x": 23.4, "y": 68.6, "size": "S", "label": "MSR"},
        {"type": "led", "id": "SRC_FORM", "x": 32.6, "y": 68.6, "size": "S", "label": "FORM"},
        {"type": "led", "id": "SRC_REF", "x": 42.6, "y": 68.6, "size": "S", "label": "REF"},
        {"type": "button", "id": "PLAN", "x": 54.6, "y": 68.6},

        {"type": "knob", "id": "LENGTH", "size": "M", "x": 9.0, "y": 82.0, "label": "LENGTH"},
        {"type": "knob", "id": "SCALE", "size": "M", "x": 23.3, "y": 82.0, "label": "SCALE"},
        {"type": "knob", "id": "SMOOTH", "size": "M", "x": 37.6, "y": 82.0, "label": "SMOOTH"},
        {"type": "knob", "id": "SILENCE", "size": "M", "x": 51.9, "y": 82.0, "label": "SILENCE", "hot": True},

        {"type": "jack", "id": "CLK", "kind": "in", "x": 7.6, "y": 99.0, "label": "CLK"},
        {"type": "jack", "id": "RUN_IN", "kind": "in", "x": 18.6, "y": 99.0, "label": "RUN"},
        {"type": "jack", "id": "RST", "kind": "in", "x": 29.6, "y": 99.0, "label": "RST"},
        {"type": "pad", "id": "RUN", "x": 44.0, "y": 99.0, "color": "cyan", "label": "RUN"},
        {"type": "led", "id": "RUN_LED", "x": 54.6, "y": 99.0, "size": "S"},

        {"type": "plate", "x": 2.7, "y": 107.6, "w": 55.56, "h": 15.6},
        {"type": "jack", "id": "BUS", "kind": "out", "bus": True, "x": 8.2, "y": 118.0, "label": "BUS·4"},
        {"type": "jack", "id": "NRG", "kind": "out", "x": 19.25, "y": 118.0, "label": "NRG"},
        {"type": "jack", "id": "DENS", "kind": "out", "x": 30.3, "y": 118.0, "label": "DENS"},
        {"type": "jack", "id": "CHS", "kind": "out", "x": 41.35, "y": 118.0, "label": "CHS"},
        {"type": "jack", "id": "DUCK", "kind": "out", "x": 52.4, "y": 118.0, "label": "DUCK"},
    ],
}
