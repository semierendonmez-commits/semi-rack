# FLOATER — 32 HP. Units: mm. See tools/panelgen.py for the item types.
#
# Left: the eight lanes (LANES screen: steps, LOCK's flips, conditions, the playhead, hits) with a
# select button and a hit LED per lane; under it the selected lane's ring (STEPS .. VARY) and its
# module (TUNE .. SEND, names on the screen follow the model) with the STRIKE pad.
# Right: the REEL (tape, splices, genes) and the PIN matrix (10 sources x 14 jacks, live traces);
# the clock / reel / fx knobs, NEW PATCH and MUTATE pads.
# Bottom: inputs; the plate: L R, a gate per lane, CV (a matrix source).
W = 32 * 5.08

ROW_Y0, ROW_H = 15.4, 46.0 / 8   # lane rows of the LANES screen
KX = [9.6, 22.2, 34.8, 47.4, 60.0, 72.6, 85.2, 97.8]   # eight columns on the left
RX = [115.6, 128.4, 141.2, 154.0]                      # four columns on the right
JX = [9.0 + 14.18 * i for i in range(11)]              # eleven jacks across

lanes = ["KICK", "MESH", "FM", "OPTO", "HATS", "ZAP", "STRK", "SCAT"]

SPEC = {
    "slug": "Floater",
    "hp": 32,
    "title": "FLOATER",
    "subtitle": "a floater case · one clock · eight modules",
    "subtitleColor": "dim",
    "items": [
        {"type": "screen", "id": "LANES", "x": 14.8, "y": ROW_Y0, "w": 92.4, "h": 46.0},
        {"type": "screen", "id": "REEL", "x": 110.6, "y": ROW_Y0, "w": 48.6, "h": 21.0},
        {"type": "screen", "id": "PINS", "x": 110.6, "y": 39.4, "w": 48.6, "h": 22.0},
    ]
    + [{"type": "button", "id": f"SEL{i + 1}", "x": 4.4, "y": ROW_Y0 + ROW_H * (i + 0.5)} for i in range(8)]
    + [{"type": "led", "id": f"HIT{i + 1}", "x": 10.2, "y": ROW_Y0 + ROW_H * (i + 0.5), "size": "S"} for i in range(8)]
    + [
        # ---- the selected lane's ring
        {"type": "knob", "id": "STEPS", "size": "M", "x": KX[0], "y": 70.6, "label": "STEPS"},
        {"type": "knob", "id": "HITS", "size": "M", "x": KX[1], "y": 70.6, "label": "HITS"},
        {"type": "knob", "id": "OFFSET", "size": "M", "x": KX[2], "y": 70.6, "label": "OFFSET"},
        {"type": "knob", "id": "DIV", "size": "M", "x": KX[3], "y": 70.6, "label": "DIV"},
        {"type": "knob", "id": "CHANCE", "size": "M", "x": KX[4], "y": 70.6, "label": "CHANCE"},
        {"type": "knob", "id": "BURST", "size": "M", "x": KX[5], "y": 70.6, "label": "BURST", "hot": True},
        {"type": "knob", "id": "LOCK", "size": "M", "x": KX[6], "y": 70.6, "label": "LOCK"},
        {"type": "knob", "id": "VARY", "size": "M", "x": KX[7], "y": 70.6, "label": "VARY"},
        # ---- the selected lane's module
        {"type": "knob", "id": "TUNE", "size": "M", "x": KX[0], "y": 86.6, "label": "TUNE"},
        {"type": "knob", "id": "DECAY", "size": "M", "x": KX[1], "y": 86.6, "label": "DECAY"},
        {"type": "knob", "id": "COLOR", "size": "M", "x": KX[2], "y": 86.6, "label": "COLOR"},
        {"type": "knob", "id": "HARM", "size": "M", "x": KX[3], "y": 86.6, "label": "HARM"},
        {"type": "knob", "id": "MORPH", "size": "M", "x": KX[4], "y": 86.6, "label": "MORPH"},
        {"type": "knob", "id": "LEVEL", "size": "M", "x": KX[5], "y": 86.6, "label": "LEVEL"},
        {"type": "knob", "id": "SEND", "size": "M", "x": KX[6], "y": 86.6, "label": "SEND"},
        {"type": "pad", "id": "STRIKE", "x": KX[7], "y": 86.6, "color": "cyan", "label": "STRIKE"},

        # ---- clock, reel, fx
        {"type": "knob", "id": "BPM", "size": "M", "x": RX[0], "y": 70.6, "label": "BPM"},
        {"type": "knob", "id": "ROTATE", "size": "M", "x": RX[1], "y": 70.6, "label": "ROTATE"},
        {"type": "knob", "id": "DLY", "size": "M", "x": RX[2], "y": 70.6, "label": "ZIGZAG"},
        {"type": "knob", "id": "SPACE", "size": "M", "x": RX[3], "y": 70.6, "label": "SPACE"},
        {"type": "knob", "id": "CHAOS", "size": "L", "x": RX[0], "y": 86.6, "label": "REEL CHAOS", "hot": True},
        {"type": "knob", "id": "VARI", "size": "M", "x": RX[1], "y": 86.6, "label": "VARI"},
        {"type": "pad", "id": "NEW", "x": RX[2], "y": 86.6, "color": "magenta", "label": "NEW"},
        {"type": "pad", "id": "MUTATE", "x": RX[3], "y": 86.6, "color": "cyan", "label": "MUTATE"},

        # ---- inputs, RUN, SWING, TURNS
        {"type": "jack", "id": "CLK", "kind": "in", "x": JX[0], "y": 101.6, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": JX[1], "y": 101.6, "label": "RST"},
        {"type": "jack", "id": "RUN_IN", "kind": "in", "x": JX[2], "y": 101.6, "label": "RUN"},
        {"type": "jack", "id": "STRK_IN", "kind": "in", "x": JX[3], "y": 101.6, "label": "STRIKE"},
        {"type": "jack", "id": "NEW_IN", "kind": "in", "x": JX[4], "y": 101.6, "label": "NEW"},
        {"type": "jack", "id": "MUT_IN", "kind": "in", "x": JX[5], "y": 101.6, "label": "MUTATE"},
        {"type": "jack", "id": "CHAOS_IN", "kind": "in", "x": JX[6], "y": 101.6, "label": "CHAOS"},
        {"type": "jack", "id": "ROT_IN", "kind": "in", "x": JX[7], "y": 101.6, "label": "ROTATE"},
        {"type": "button", "id": "RUN", "x": 117.4, "y": 101.6, "label": "RUN"},
        {"type": "led", "id": "RUN_LED", "x": 122.6, "y": 101.6, "size": "S"},
        {"type": "knob", "id": "SWING", "size": "S", "x": 137.6, "y": 101.6, "label": "SWING"},
        {"type": "knob", "id": "TURNS", "size": "S", "x": 151.2, "y": 101.6, "label": "TURNS"},

        {"type": "plate", "x": 2.7, "y": 108.0, "w": W - 5.4, "h": 15.6},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": JX[0], "y": 118.2, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": JX[1], "y": 118.2, "label": "R"},
    ]
    + [{"type": "jack", "id": f"GATE{i + 1}", "kind": "out", "x": JX[2 + i], "y": 118.2, "label": lanes[i]} for i in range(8)]
    + [{"type": "jack", "id": "CV_OUT", "kind": "out", "x": JX[10], "y": 118.2, "label": "CV"}],
}
