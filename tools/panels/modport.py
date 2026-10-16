# MODPORT — 20 HP. Units: mm. See tools/panelgen.py for the item types.
# From the top: tracker screen; clock inputs + TEMPO / STEREO / LEVEL + PLAY; one
# mute button per channel (+ LOAD); output plate: per-channel GATE and V/OCT, L / R.
W = 20 * 5.08
CX0, CDX = 7.0, 10.95             # nine columns: channels 1-8, then L / R
COL = [CX0 + CDX * i for i in range(9)]

SPEC = {
    "slug": "Modport",
    "hp": 20,
    "title": "MODPORT",
    "subtitle": "protracker .mod · 8 ch",
    "items": [
        {"type": "screen", "id": "TRACKER", "x": 3.4, "y": 15.4, "w": W - 6.8, "h": 46.0},

        {"type": "jack", "id": "CLK", "kind": "in", "x": COL[0], "y": 72.0, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": COL[1], "y": 72.0, "label": "RST"},
        {"type": "jack", "id": "PLAY_IN", "kind": "in", "x": COL[2], "y": 72.0, "label": "PLAY"},
        {"type": "jack", "id": "NEXT", "kind": "in", "x": COL[3], "y": 72.0, "label": "NEXT"},
        {"type": "knob", "id": "TEMPO", "size": "M", "x": 52.0, "y": 72.0, "label": "TEMPO"},
        {"type": "knob", "id": "STEREO", "size": "M", "x": 65.5, "y": 72.0, "label": "STEREO"},
        {"type": "knob", "id": "LEVEL", "size": "M", "x": 79.0, "y": 72.0, "label": "LEVEL"},
        {"type": "pad", "id": "PLAY", "x": COL[8], "y": 72.6, "color": "cyan", "label": "PLAY"},
        {"type": "led", "id": "PLAY_LED", "x": 87.4, "y": 67.4, "size": "S"},

        {"type": "label", "text": "MUTE", "x": 2.9, "y": 79.6, "size": 1.6, "color": "dim", "anchor": "start"},
    ] + [
        {"type": "led", "id": f"MUTE_LED{i + 1}", "x": COL[i] + 3.3, "y": 86.0, "size": "S"} for i in range(8)
    ] + [
        {"type": "button", "id": f"MUTE{i + 1}", "x": COL[i] - 0.8, "y": 86.0, "label": str(i + 1)} for i in range(8)
    ] + [
        {"type": "button", "id": "LOAD", "x": COL[8], "y": 86.0, "label": "LOAD"},

        {"type": "plate", "x": 2.7, "y": 92.6, "w": W - 5.4, "h": 30.8},
    ] + [
        {"type": "jack", "id": f"GATE{i + 1}", "kind": "out", "x": COL[i], "y": 101.2, "label": f"G{i + 1}"} for i in range(8)
    ] + [
        {"type": "jack", "id": f"VOCT{i + 1}", "kind": "out", "x": COL[i], "y": 116.0, "label": f"V{i + 1}"} for i in range(8)
    ] + [
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": COL[8], "y": 101.2, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": COL[8], "y": 116.0, "label": "R"},
    ],
}
