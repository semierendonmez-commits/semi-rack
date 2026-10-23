# NMS — 28 HP. Units: mm. See tools/panelgen.py for the item types.
# From the top: routing screen (patch, lanes = mixer channels with their module chains,
# cables, meters, position / pattern / energy); patch and pattern selectors, WANDER, ROLL,
# RUN; the 24 NMS macros (GENERATORS / SHAPES / SOUND rows) and TEMPO / WANDER / CHAOS /
# LEVEL; inputs; output plate: TRIG per channel, CLK, BUS, L / R.
W = 28 * 5.08
MX0, MDX = 8.0, 10.4
MACROS = [["PITCH", "RANGE", "MIC I", "MIC C", "MES I", "MES C", "MAC I", "MAC C"],
          ["MOD", "CMPLX", "ATK", "DEC", "ACC", "SPEED", "INT", "CHAR"],
          ["TONE", "RES", "CHAR", "NOISE", "HARM", "INST", "THICK", "SPACE"]]
IX0, IDX = 8.0, 10.9
OX0, ODX = 8.0, 11.3
INS = [("CLK", "CLK"), ("RST", "RST"), ("RUN_IN", "RUN"), ("VOCT", "V/OCT"), ("GATE", "GATE"),
       ("NEXT", "NEXT"), ("SEL", "SEL"), ("MOD_A", "MOD A"), ("MOD_B", "MOD B")]
OUTS = [(f"TRIG{i + 1}", f"T{i + 1}") for i in range(8)] + [("CLK_OUT", "CLK")]

SPEC = {
    "slug": "Nms",
    "hp": 28,
    "title": "MANIFOLD",
    "subtitle": "generative modular · 8 lanes",
    "items": [
        {"type": "screen", "id": "ROUTING", "x": 3.4, "y": 15.4, "w": W - 6.8, "h": 42.0},

        {"type": "label", "text": "PATCH", "x": 12.0, "y": 59.8, "size": 1.7, "color": "dim"},
        {"type": "button", "id": "PATCH_PREV", "x": 7.6, "y": 64.0},
        {"type": "button", "id": "PATCH_NEXT", "x": 16.4, "y": 64.0},
        {"type": "label", "text": "PATTERN", "x": 37.0, "y": 59.8, "size": 1.7, "color": "dim"},
        {"type": "button", "id": "PTN_PREV", "x": 28.2, "y": 64.0},
        {"type": "button", "id": "PTN_NEXT", "x": 37.0, "y": 64.0},
        {"type": "button", "id": "PTN_RND", "x": 45.8, "y": 64.0},
        {"type": "label", "text": "WANDER", "x": 59.0, "y": 59.8, "size": 1.7, "color": "dim"},
        {"type": "button", "id": "WANDER", "x": 57.0, "y": 64.0},
        {"type": "led", "id": "WANDER_LED", "x": 62.6, "y": 64.0, "size": "S"},
        {"type": "label", "text": "ROLL", "x": 74.0, "y": 59.8, "size": 1.7, "color": "magenta"},
        {"type": "button", "id": "ROLL", "x": 74.0, "y": 64.0},
        {"type": "pad", "id": "RUN", "x": 131.0, "y": 64.6, "color": "cyan"},
        {"type": "label", "text": "RUN", "x": 121.0, "y": 61.2, "size": 1.7, "color": "cyan"},
        {"type": "led", "id": "RUN_LED", "x": 121.0, "y": 65.6, "size": "S"},
    ] + [
        {"type": "knob", "id": f"M{r * 8 + i + 1}", "size": "S", "x": MX0 + MDX * i, "y": 76.0 + 11.0 * r, "label": MACROS[r][i]}
        for r in range(3) for i in range(8)
    ] + [
        {"type": "knob", "id": "TEMPO", "size": "M", "x": 98.0, "y": 78.0, "label": "TEMPO"},
        {"type": "knob", "id": "WSPEED", "size": "M", "x": 113.0, "y": 78.0, "label": "WANDER"},
        {"type": "knob", "id": "CHAOS", "size": "M", "x": 128.0, "y": 78.0, "label": "CHAOS", "hot": True},
        {"type": "knob", "id": "LEVEL", "size": "M", "x": 98.0, "y": 95.0, "label": "LEVEL"},
        {"type": "knob", "id": "BLEND", "size": "M", "x": 113.0, "y": 95.0, "label": "BLEND"},
        {"type": "knob", "id": "EVOLVE", "size": "M", "x": 128.0, "y": 95.0, "label": "EVOLVE", "hot": True},
    ] + [
        {"type": "jack", "id": n, "kind": "in", "x": IX0 + IDX * i, "y": 108.0, "label": l} for i, (n, l) in enumerate(INS)
    ] + [
        {"type": "plate", "x": 2.7, "y": 112.4, "w": W - 5.4, "h": 11.6 + 0.0},
    ] + [
        {"type": "jack", "id": n, "kind": "out", "x": OX0 + ODX * i, "y": 119.4, "label": l} for i, (n, l) in enumerate(OUTS)
    ] + [
        {"type": "jack", "id": "BUS", "kind": "out", "bus": True, "x": OX0 + ODX * 9, "y": 119.4, "label": "BUS·4"},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": OX0 + ODX * 10, "y": 119.4, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": OX0 + ODX * 11, "y": 119.4, "label": "R"},
    ],
}
