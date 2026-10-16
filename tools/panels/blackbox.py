# BLACKBOX — 20 HP. Units: mm. See tools/panelgen.py for the item types.
# From the top: tape timeline + readout screens, transport (REC / PLAY / RECALL / MARK with their
# trigger inputs, CURSOR and ZOOM), the recorded inputs (BUS + 4 CV + 4 GATE), the replay outputs.
_items = [
    {"type": "screen", "id": "TAPE", "x": 3.5, "y": 14.6, "w": 94.6, "h": 50.0},
    {"type": "screen", "id": "READOUT", "x": 3.5, "y": 67.4, "w": 94.6, "h": 9.0},

    {"type": "button", "id": "REC", "x": 6.5, "y": 87.0, "label": "REC"},
    {"type": "led", "id": "REC_LED", "x": 11.2, "y": 87.0, "size": "S"},
    {"type": "jack", "id": "REC_IN", "kind": "in", "x": 18.0, "y": 87.0, "label": "TRG"},
    {"type": "button", "id": "PLAY", "x": 27.5, "y": 87.0, "label": "PLAY"},
    {"type": "led", "id": "PLAY_LED", "x": 32.2, "y": 87.0, "size": "S"},
    {"type": "jack", "id": "PLAY_IN", "kind": "in", "x": 39.0, "y": 87.0, "label": "TRG"},
    {"type": "button", "id": "RECALL", "x": 48.5, "y": 87.0, "label": "RECALL"},
    {"type": "led", "id": "RECALL_LED", "x": 53.2, "y": 87.0, "size": "S"},
    {"type": "button", "id": "MARK", "x": 61.5, "y": 87.0, "label": "MARK"},
    {"type": "jack", "id": "MARK_IN", "kind": "in", "x": 68.5, "y": 87.0, "label": "TRG"},
    {"type": "knob", "id": "CURSOR", "size": "M", "x": 80.5, "y": 87.0, "label": "CURSOR"},
    {"type": "knob", "id": "ZOOM", "size": "M", "x": 93.0, "y": 87.0, "label": "ZOOM"},
]
_names = ["BUS·4", "CV1", "CV2", "CV3", "CV4", "G1", "G2", "G3", "G4"]
_ids = ["BUS", "CV1", "CV2", "CV3", "CV4", "G1", "G2", "G3", "G4"]
for i, (n, lab) in enumerate(zip(_ids, _names)):
    _items.append({"type": "jack", "id": f"{n}_IN", "kind": "in", "bus": i == 0, "x": 7.0 + i * 10.9, "y": 101.0, "label": lab})
_items.append({"type": "plate", "x": 2.7, "y": 107.6, "w": 96.2, "h": 15.6})
for i, (n, lab) in enumerate(zip(_ids, _names)):
    _items.append({"type": "jack", "id": f"{n}_OUT", "kind": "out", "bus": i == 0, "x": 7.0 + i * 10.9, "y": 118.0, "label": lab})

SPEC = {
    "slug": "Blackbox",
    "hp": 20,
    "title": "BLACKBOX",
    "subtitle": "flight recorder",
    "subtitleColor": "dim",
    "items": _items,
}
