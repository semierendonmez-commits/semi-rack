# DATUM — 14 HP. Units: mm. See tools/panelgen.py for the item types.
#
# Port of kdr_jdat.mxt: three data voices read 512-sample buffers of -1 / 0 / +1 written by a
# 120 ms metro, two drones read the same buffers. Rack adds FEED: any file supplies the writes.
# Top: the LCD (digit field + mlts heat), the three buffers as barcodes; source / hold row;
# metro knobs; levels; inputs; outputs (audio + the three gates and the clear trigger).
W = 14 * 5.08
X5 = [8.0, 21.8, 35.6, 49.4, 63.2]
X6 = [7.0, 18.4, 29.8, 41.2, 52.6, 64.0]

SPEC = {
    "slug": "Jdat",
    "hp": 14,
    "title": "DATUM",
    "subtitle": "data buffers · metro",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "LCD", "x": 3.4, "y": 15.6, "w": 64.3, "h": 26.0},
        {"type": "screen", "id": "BUF", "x": 3.4, "y": 44.2, "w": 64.3, "h": 15.6},

        {"type": "led", "id": "SRC_DATA", "x": 5.2, "y": 64.6, "size": "S", "label": "DATA"},
        {"type": "led", "id": "SRC_SINE", "x": 16.6, "y": 64.6, "size": "S", "label": "SINE"},
        {"type": "led", "id": "SRC_NOISE", "x": 28.0, "y": 64.6, "size": "S", "label": "NOISE"},
        {"type": "button", "id": "SRC", "x": 42.0, "y": 64.6},
        {"type": "led", "id": "HOLD_LED", "x": 51.0, "y": 64.6, "size": "S", "label": "HOLD"},
        {"type": "button", "id": "HOLD", "x": 66.0, "y": 64.6},

        {"type": "knob", "id": "RATE", "size": "M", "x": X5[0], "y": 77.0, "label": "RATE"},
        {"type": "knob", "id": "CLEAR", "size": "M", "x": X5[1], "y": 77.0, "label": "CLEAR", "hot": True},
        {"type": "knob", "id": "JUMP", "size": "M", "x": X5[2], "y": 77.0, "label": "JUMP"},
        {"type": "knob", "id": "DENS", "size": "M", "x": X5[3], "y": 77.0, "label": "DENS"},
        {"type": "knob", "id": "FEED", "size": "M", "x": X5[4], "y": 77.0, "label": "FEED", "hot": True},

        {"type": "knob", "id": "LVL1", "size": "S", "x": X6[0], "y": 89.4, "label": "D1"},
        {"type": "knob", "id": "LVL2", "size": "S", "x": X6[1], "y": 89.4, "label": "D2"},
        {"type": "knob", "id": "LVL3", "size": "S", "x": X6[2], "y": 89.4, "label": "D3"},
        {"type": "knob", "id": "DRONE", "size": "S", "x": X6[3], "y": 89.4, "label": "DRONE"},
        {"type": "knob", "id": "MASTER", "size": "S", "x": X6[4], "y": 89.4, "label": "MASTER"},
        {"type": "knob", "id": "LOOP", "size": "S", "x": X6[5], "y": 89.4, "label": "LOOP"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": X6[0], "y": 100.4, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": X6[1], "y": 100.4, "label": "RST"},
        {"type": "jack", "id": "RUN", "kind": "in", "x": X6[2], "y": 100.4, "label": "RUN"},
        {"type": "jack", "id": "CLR_IN", "kind": "in", "x": X6[3], "y": 100.4, "label": "CLR"},
        {"type": "jack", "id": "RATE_CV", "kind": "in", "x": X6[4], "y": 100.4, "label": "RATE"},
        {"type": "jack", "id": "BUS", "kind": "in", "bus": True, "x": X6[5], "y": 100.4, "label": "BUS·4"},

        {"type": "plate", "x": 2.7, "y": 107.6, "w": W - 5.4, "h": 15.6},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": X6[0], "y": 118.0, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": X6[1], "y": 118.0, "label": "R"},
        {"type": "jack", "id": "G1", "kind": "out", "x": X6[2], "y": 118.0, "label": "G1"},
        {"type": "jack", "id": "G2", "kind": "out", "x": X6[3], "y": 118.0, "label": "G2"},
        {"type": "jack", "id": "G3", "kind": "out", "x": X6[4], "y": 118.0, "label": "G3"},
        {"type": "jack", "id": "CLR_OUT", "kind": "out", "x": X6[5], "y": 118.0, "label": "CLR"},
    ],
}
