# VERTEX — 24 HP. Units: mm. See tools/panelgen.py for the item types.
# Rebuild of Vertex 1.3 by aorist (a DFAM-style percussion voice).
# Top: the four sequencer rows (PITCH / VEL / MOD 1 / MOD 2) and the scope + envelopes.
# Strip: OSC 1 / OSC 2 shape (LED + button), which rows the step knobs edit, RUN.
# Knobs: oscillators + noise + FM (row A), envelopes + filter (row B), 2 x 8 step knobs.
_M = [9.0, 23.5, 38.0, 52.5, 67.0, 81.5, 96.0, 110.5]          # 8 columns, M knobs and jacks
_S = [21.0, 33.6, 46.2, 58.8, 71.4, 84.0, 96.6, 109.2]          # 8 step columns

_SY = 46.2        # selector strip
_A, _B = 57.0, 71.0
_R1, _R2 = 82.6, 93.6
_IN = 104.2

items = [
    {"type": "screen", "id": "SEQ", "x": 3.1, "y": 15.4, "w": 76.0, "h": 24.6},
    {"type": "screen", "id": "SCOPE", "x": 82.2, "y": 15.4, "w": 36.6, "h": 24.6},
]

shapes = ["TRI", "SQU", "SIN", "SAW"]
for o, x0 in ((1, 4.6), (2, 41.6)):
    for i, n in enumerate(shapes):
        items.append({"type": "led", "id": f"O{o}_{n}", "x": x0 + i * 7.2, "y": _SY, "size": "S", "label": n})
    items.append({"type": "button", "id": f"O{o}_SHAPE", "x": x0 + 31.0, "y": _SY, "label": f"OSC {o}"})

items += [
    {"type": "led", "id": "PAGE_PV", "x": 81.0, "y": _SY, "size": "S", "label": "P/V"},
    {"type": "led", "id": "PAGE_M", "x": 90.0, "y": _SY, "size": "S", "label": "M1/M2"},
    {"type": "button", "id": "PAGE", "x": 102.6, "y": _SY, "label": "ROWS"},
    {"type": "button", "id": "RUN", "x": 112.0, "y": _SY, "label": "RUN"},
    {"type": "led", "id": "RUN_LED", "x": 117.4, "y": _SY, "size": "S"},
]

row_a = [("O1_FREQ", "OSC 1", False), ("O1_EG", "EG 1", False), ("O1_LVL", "LVL 1", False), ("NOISE", "NOISE", False),
         ("O2_FREQ", "OSC 2", False), ("O2_EG", "EG 2", False), ("O2_LVL", "LVL 2", False), ("FM", "FM", True)]
row_b = [("ATTACK", "ATTACK", False), ("P_DECAY", "VCO DEC", False), ("A_DECAY", "VCA DEC", False), ("VOLUME", "VOLUME", False),
         ("CUTOFF", "CUTOFF", False), ("RES", "RESO", True), ("F_ENV", "VCF EG", False), ("F_DECAY", "VCF DEC", False)]
for (i, (id_, lab, hot)) in enumerate(row_a):
    items.append({"type": "knob", "id": id_, "size": "M", "x": _M[i], "y": _A, "label": lab, "hot": hot})
for (i, (id_, lab, hot)) in enumerate(row_b):
    items.append({"type": "knob", "id": id_, "size": "M", "x": _M[i], "y": _B, "label": lab, "hot": hot})

items.append({"type": "knob", "id": "TEMPO", "size": "S", "x": 7.0, "y": _R2, "label": "TEMPO"})
items.append({"type": "label", "text": "A", "x": 14.6, "y": _R1, "size": 1.9, "color": "dim"})
items.append({"type": "label", "text": "B", "x": 14.6, "y": _R2, "size": 1.9, "color": "dim"})
for i in range(8):
    items.append({"type": "knob", "id": f"STEP_A{i + 1}", "size": "S", "x": _S[i], "y": _R1})
    items.append({"type": "led", "id": f"STEP_LED{i + 1}", "x": _S[i], "y": (_R1 + _R2) / 2, "size": "S"})
    items.append({"type": "knob", "id": f"STEP_B{i + 1}", "size": "S", "x": _S[i], "y": _R2})

ins = [("CLK", "CLK"), ("RST", "RST"), ("TRIG", "TRIG"), ("VOCT", "V/OCT"), ("VEL", "VEL"), ("IN", "IN"),
       ("VCF_CV", "VCF"), ("FM_CV", "FM")]
for i, (id_, lab) in enumerate(ins):
    items.append({"type": "jack", "id": id_, "kind": "in", "x": _M[i], "y": _IN, "label": lab})

items.append({"type": "plate", "x": 2.7, "y": 108.6, "w": 116.52, "h": 15.0})
outs = [("OUT", "OUT"), ("TRIG_OUT", "TRIG"), ("PITCH_OUT", "PITCH"), ("VEL_OUT", "VEL"), ("AMP_EG", "VCA EG"), ("FLT_EG", "VCF EG")]
for i, (id_, lab) in enumerate(outs):
    items.append({"type": "jack", "id": id_, "kind": "out", "x": 12.0 + i * 19.6, "y": 118.4, "label": lab})

SPEC = {
    "slug": "Vertex",
    "hp": 24,
    "title": "VERTEX",
    "subtitle": "after Vertex 1.3 by aorist",
    "subtitleColor": "magenta",
    "items": items,
}
