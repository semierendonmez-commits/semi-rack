# FLOATDAT — 24 HP. Units: mm. See tools/panelgen.py for the item types.
#
# Top: the datamatic VISUAL (the file's bits as an Ikeda-style scene, cut and flashed by the lanes), the
# PINBALL matrix (17 x 17, the ball's path) and the DATA strip (file, type, entropy, the read head).
# Under it the eight lane LEDs. Knobs: the four layers' levels, MASTER, BPM, TIME MULT; SWING, PATTERN,
# SCAN RATE; pads RESET, DATA PATCH, MUTATE (FLT), SCENE. Inputs; the plate: L R and a gate per lane.
W = 24 * 5.08
K7 = [9.0, 25.6, 42.2, 58.8, 75.4, 92.0, 108.6]
J8 = [7.6, 22.0, 36.4, 50.8, 65.2, 79.6, 94.0, 108.4]
J10 = [7.4 + 11.9 * i for i in range(10)]

SPEC = {
    "slug": "Floatdat",
    "hp": 24,
    "title": "FLOATDAT",
    "subtitle": "ikedar x floater x vortessa x jdat x pinball",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "VISUAL", "x": 3.1, "y": 15.4, "w": 74.0, "h": 44.0},
        {"type": "screen", "id": "PINBALL", "x": 80.4, "y": 15.4, "w": 38.4, "h": 30.0},
        {"type": "screen", "id": "DATA", "x": 80.4, "y": 48.0, "w": 38.4, "h": 11.4},
    ]
    + [{"type": "led", "id": f"LANE{i + 1}", "x": 7.0 + 9.6 * i, "y": 64.4, "size": "S", "label": f"T{i + 1}"} for i in range(8)]
    + [
        {"type": "led", "id": "RUN_LED", "x": 86.0, "y": 64.4, "size": "S", "label": "RUN"},
        {"type": "button", "id": "RUN", "x": 100.0, "y": 64.4},
        {"type": "led", "id": "LOAD_LED", "x": 106.0, "y": 64.4, "size": "S", "label": "DATA"},

        {"type": "knob", "id": "IKD", "size": "M", "x": K7[0], "y": 76.6, "label": "IKD"},
        {"type": "knob", "id": "FLT", "size": "M", "x": K7[1], "y": 76.6, "label": "FLT"},
        {"type": "knob", "id": "VTX", "size": "M", "x": K7[2], "y": 76.6, "label": "VTX"},
        {"type": "knob", "id": "JDAT", "size": "M", "x": K7[3], "y": 76.6, "label": "JDAT"},
        {"type": "knob", "id": "MASTER", "size": "M", "x": K7[4], "y": 76.6, "label": "MASTER"},
        {"type": "knob", "id": "BPM", "size": "M", "x": K7[5], "y": 76.6, "label": "BPM"},
        {"type": "knob", "id": "MULT", "size": "M", "x": K7[6], "y": 76.6, "label": "TIME MULT"},

        {"type": "knob", "id": "SWING", "size": "M", "x": K7[0], "y": 90.4, "label": "SWING"},
        {"type": "knob", "id": "PATLEN", "size": "M", "x": K7[1], "y": 90.4, "label": "PATTERN"},
        {"type": "knob", "id": "RATE", "size": "M", "x": K7[2], "y": 90.4, "label": "SCAN RATE"},
        {"type": "pad", "id": "RESET", "x": K7[3], "y": 90.4, "color": "cyan", "label": "RESET"},
        {"type": "pad", "id": "DPATCH", "x": K7[4], "y": 90.4, "color": "magenta", "label": "DATA PATCH"},
        {"type": "pad", "id": "MUTATE", "x": K7[5], "y": 90.4, "color": "cyan", "label": "MUTATE"},
        {"type": "pad", "id": "SCENE", "x": K7[6], "y": 90.4, "color": "cyan", "label": "SCENE"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": J8[0], "y": 102.4, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": J8[1], "y": 102.4, "label": "RST"},
        {"type": "jack", "id": "RUN_IN", "kind": "in", "x": J8[2], "y": 102.4, "label": "RUN"},
        {"type": "jack", "id": "LANES_IN", "kind": "in", "x": J8[3], "y": 102.4, "label": "LANES"},
        {"type": "jack", "id": "RESET_IN", "kind": "in", "x": J8[4], "y": 102.4, "label": "RESET"},
        {"type": "jack", "id": "DPATCH_IN", "kind": "in", "x": J8[5], "y": 102.4, "label": "D.PATCH"},
        {"type": "jack", "id": "SCENE_IN", "kind": "in", "x": J8[6], "y": 102.4, "label": "SCENE"},
        {"type": "jack", "id": "MULT_IN", "kind": "in", "x": J8[7], "y": 102.4, "label": "MULT"},

        {"type": "plate", "x": 2.7, "y": 108.0, "w": W - 5.4, "h": 15.6},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": J10[0], "y": 118.2, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": J10[1], "y": 118.2, "label": "R"},
    ]
    + [{"type": "jack", "id": f"GATE{i + 1}", "kind": "out", "x": J10[2 + i], "y": 118.2, "label": f"T{i + 1}"} for i in range(8)],
}
