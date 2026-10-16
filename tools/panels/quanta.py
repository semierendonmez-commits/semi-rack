# QUANTA — 24 HP. Units: mm. See tools/panelgen.py for the item types.
# Microsound particle synth (Curtis Roads) + Ikeda-style arrangement generator.
# Top: the particle cloud (every spawned particle: time x pitch, size = amplitude, colour = type)
# and the barcode (the piece: 8 channel rows x blocks, energy, fills, playhead).
# Strip: particle TYPE (LED + button), ARR on/off, REGEN (new seed), CLK / SMP / FILL lamps.
# Row A: the voice's particle sound. Row B: the generator, perform FX, output. Row C: sync,
# window and the voice envelope, the HOLD pad for the perform FX.
_M = [9.0, 23.5, 38.0, 52.5, 67.0, 81.5, 96.0, 110.5]

_SY = 51.0
_A, _B, _C = 62.4, 77.0, 90.6
_IN = 103.8

items = [
    {"type": "screen", "id": "CLOUD", "x": 3.1, "y": 15.4, "w": 66.0, "h": 30.0},
    {"type": "screen", "id": "BARCODE", "x": 72.6, "y": 15.4, "w": 46.2, "h": 30.0},
]
for i, n in enumerate(["GRAIN", "GLIS", "GLET", "TRAIN", "PULS"]):
    items.append({"type": "led", "id": f"T_{n}", "x": 4.6 + i * 8.7, "y": _SY, "size": "S", "label": n})
items += [
    {"type": "button", "id": "TYPE", "x": 50.0, "y": _SY, "label": "TYPE"},
    {"type": "led", "id": "ARR_LED", "x": 57.8, "y": _SY, "size": "S"},
    {"type": "button", "id": "ARR", "x": 63.6, "y": _SY, "label": "ARR"},
    {"type": "button", "id": "REGEN", "x": 74.0, "y": _SY, "label": "REGEN"},
    {"type": "led", "id": "CLK_LED", "x": 82.0, "y": _SY, "size": "S", "label": "CLK"},
    {"type": "led", "id": "SMP_LED", "x": 93.0, "y": _SY, "size": "S", "label": "SMP"},
    {"type": "led", "id": "FILL_LED", "x": 104.0, "y": _SY, "size": "S", "label": "FILL", "labelColor": "magenta"},
]

row_a = [("DENSITY", "DENSITY", False), ("DUR", "DUR", False), ("SHAPE", "SHAPE", False), ("MASK", "MASK", False),
         ("JITTER", "JITTER", True), ("SPREAD", "SPREAD", False), ("TUNE", "TUNE", False), ("WAVE", "WAVE", False)]
row_b = [("FORM", "FORM", False), ("EXTREM", "EXTREM", True), ("SILENCE", "SILENCE", False), ("FILLS", "FILLS", False),
         ("MOTION", "MOTION", False), ("ARR_LVL", "ARR LVL", False), ("FX", "FX", True), ("OUT", "OUT", False)]
for i, (id_, lab, hot) in enumerate(row_a):
    items.append({"type": "knob", "id": id_, "size": "M", "x": _M[i], "y": _A, "label": lab, "hot": hot})
for i, (id_, lab, hot) in enumerate(row_b):
    items.append({"type": "knob", "id": id_, "size": "M", "x": _M[i], "y": _B, "label": lab, "hot": hot})
for i, (id_, lab) in enumerate([("SYNC", "SYNC"), ("WINDOW", "WINDOW"), ("ATK", "ATK"), ("DEC", "DEC"), ("SUS", "SUS"), ("REL", "REL")]):
    items.append({"type": "knob", "id": id_, "size": "S", "x": _M[i], "y": _C, "label": lab})
items.append({"type": "pad", "id": "HOLD", "x": 103.2, "y": _C, "color": "magenta", "label": "FX HOLD"})

ins = [("VOCT", "V/OCT"), ("GATE", "GATE"), ("CLK", "CLK"), ("RST", "RST"), ("DENS_CV", "DENS"), ("DUR_CV", "DUR"),
       ("FX_GATE", "FX"), ("REGEN_IN", "REGEN")]
for i, (id_, lab) in enumerate(ins):
    items.append({"type": "jack", "id": id_, "kind": "in", "x": _M[i], "y": _IN, "label": lab})

items.append({"type": "plate", "x": 2.7, "y": 108.6, "w": 116.52, "h": 15.0})
outs = [("OUT_L", "L"), ("OUT_R", "R"), ("BLK", "BLOCK"), ("NRG", "ENERGY"), ("EOC", "END")]
for i, (id_, lab) in enumerate(outs):
    items.append({"type": "jack", "id": id_, "kind": "out", "x": 14.0 + i * 23.4, "y": 118.4, "label": lab})

SPEC = {
    "slug": "Quanta",
    "hp": 24,
    "title": "QUANTA",
    "subtitle": "microsound · after Roads / Ikeda",
    "subtitleColor": "magenta",
    "items": items,
}
