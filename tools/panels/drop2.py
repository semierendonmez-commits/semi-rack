# DROP II — 16 HP. Units: mm. See tools/panelgen.py for the item types.
# Top: the drop view (drum lanes + bass roll, call / response, what changed). Then the five
# moves as LED + button toggles, the IDENTITY knob, variant, and NEW / PLAY / LEARN.
# Inputs: clock + the drop-1 lanes for LEARN. Outputs on the cyan plate.
_X = [6.6 + 9.73 * i for i in range(8)]      # eight jack columns
_MX = [9.0, 24.6, 40.6, 56.6, 72.2]           # five move buttons

SPEC = {
    "slug": "Drop2",
    "hp": 16,
    "title": "DROP II",
    "subtitle": "switch-up generator",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "VIEW", "x": 3.1, "y": 15.4, "w": 75.08, "h": 38.0},
    ] + [
        {"type": "button", "id": f"MOVE{i}", "x": _MX[i], "y": 63.0, "label": n}
        for i, n in enumerate(["HALF", "KICK", "LOCK", "FLIP", "SPLIT"])
    ] + [
        {"type": "led", "id": f"MOVE_LED{i}", "x": _MX[i] + 4.4, "y": 63.0, "size": "S"}
        for i in range(5)
    ] + [
        {"type": "knob", "id": "IDENTITY", "size": "L", "x": 12.0, "y": 80.0, "label": "IDENTITY", "hot": True},
        {"type": "knob", "id": "VARIANT", "size": "M", "x": 30.0, "y": 80.0, "label": "VARIANT"},
        {"type": "button", "id": "NEW", "x": 45.0, "y": 81.0, "label": "NEW"},
        {"type": "button", "id": "PLAY", "x": 57.0, "y": 81.0, "label": "DROP 1"},
        {"type": "led", "id": "PLAY_LED", "x": 57.0, "y": 85.6, "size": "S"},
        {"type": "button", "id": "LEARN", "x": 70.0, "y": 81.0, "label": "LEARN"},
        {"type": "led", "id": "LEARN_LED", "x": 70.0, "y": 85.6, "size": "S"},

        {"type": "jack", "id": "CLK", "kind": "in", "x": _X[0], "y": 99.0, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": _X[1], "y": 99.0, "label": "RST"},
        {"type": "jack", "id": "ID_CV", "kind": "in", "x": _X[2], "y": 99.0, "label": "ID"},
        {"type": "jack", "id": "KICK_IN", "kind": "in", "x": _X[3], "y": 99.0, "label": "KICK"},
        {"type": "jack", "id": "SNARE_IN", "kind": "in", "x": _X[4], "y": 99.0, "label": "SNR"},
        {"type": "jack", "id": "HAT_IN", "kind": "in", "x": _X[5], "y": 99.0, "label": "HAT"},
        {"type": "jack", "id": "BASS_VOCT", "kind": "in", "x": _X[6], "y": 99.0, "label": "B V/O"},
        {"type": "jack", "id": "BASS_GATE", "kind": "in", "x": _X[7], "y": 99.0, "label": "B GT"},

        {"type": "plate", "x": 2.0, "y": 106.6, "w": 77.28, "h": 17.0},
        {"type": "jack", "id": "KICK_OUT", "kind": "out", "x": _X[0], "y": 118.0, "label": "KICK"},
        {"type": "jack", "id": "SNARE_OUT", "kind": "out", "x": _X[1], "y": 118.0, "label": "SNR"},
        {"type": "jack", "id": "HAT_OUT", "kind": "out", "x": _X[2], "y": 118.0, "label": "HAT"},
        {"type": "jack", "id": "CALL_VOCT", "kind": "out", "x": _X[3], "y": 118.0, "label": "C V/O"},
        {"type": "jack", "id": "CALL_GATE", "kind": "out", "x": _X[4], "y": 118.0, "label": "C GT"},
        {"type": "jack", "id": "RESP_VOCT", "kind": "out", "x": _X[5], "y": 118.0, "label": "R V/O"},
        {"type": "jack", "id": "RESP_GATE", "kind": "out", "x": _X[6], "y": 118.0, "label": "R GT"},
        {"type": "jack", "id": "AUDIO", "kind": "out", "x": _X[7], "y": 118.0, "label": "KIT"},
    ],
}
