# LOOPGENOME — 16 HP. Units: mm. See tools/panelgen.py for the item types.
# Top: the loop (the break's slices over the bass line as a piano roll, the ball on the steps)
# and the joint walk's matrix (CALL / ANSWER / FILL) with the articulation row, the role and the
# form. RESET sends the ball home. Knobs: the walk (M row) and the drums / bass (S row).
_C6 = [9.6, 22.0, 34.4, 46.8, 59.2, 71.6]
_C7 = [7.6 + 11.0 * i for i in range(7)]

SPEC = {
    "slug": "Loopgenome",
    "hp": 16,
    "title": "LOOPGENOME",
    "subtitle": "drums + bass · one ball",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "LOOP", "x": 3.1, "y": 15.4, "w": 75.0, "h": 24.0},
        {"type": "screen", "id": "MATRIX", "x": 3.1, "y": 41.6, "w": 64.6, "h": 29.0},
        {"type": "button", "id": "RESET", "x": 73.0, "y": 50.0, "label": "RESET"},
        {"type": "led", "id": "HALT_LED", "x": 73.0, "y": 60.4, "size": "S", "label": "HALT", "labelPos": "above"},

        {"type": "knob", "id": "MUTATION", "size": "M", "x": 9.6, "y": 80.0, "label": "MUTATION", "hot": True},
        {"type": "knob", "id": "TEMP", "size": "M", "x": 25.1, "y": 80.0, "label": "TEMP"},
        {"type": "knob", "id": "COUPLE", "size": "M", "x": 40.6, "y": 80.0, "label": "COUPLE"},
        {"type": "knob", "id": "DRUMS", "size": "M", "x": 56.1, "y": 80.0, "label": "DRUMS"},
        {"type": "knob", "id": "BASS", "size": "M", "x": 71.6, "y": 80.0, "label": "BASS"},

        {"type": "knob", "id": "FEEL", "size": "S", "x": _C6[0], "y": 92.4, "label": "FEEL"},
        {"type": "knob", "id": "GHOSTS", "size": "S", "x": _C6[1], "y": 92.4, "label": "GHOSTS"},
        {"type": "knob", "id": "FORM", "size": "S", "x": _C6[2], "y": 92.4, "label": "FORM"},
        {"type": "knob", "id": "ANSWER", "size": "S", "x": _C6[3], "y": 92.4, "label": "ANSWER"},
        {"type": "knob", "id": "SWING", "size": "S", "x": _C6[4], "y": 92.4, "label": "SWING"},
        {"type": "knob", "id": "DRIVE", "size": "S", "x": _C6[5], "y": 92.4, "label": "DRIVE", "hot": True},

        {"type": "jack", "id": "CLK", "kind": "in", "x": _C6[0], "y": 103.4, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": _C6[1], "y": 103.4, "label": "RST"},
        {"type": "jack", "id": "HALT", "kind": "in", "x": _C6[2], "y": 103.4, "label": "HALT"},
        {"type": "jack", "id": "MUT_CV", "kind": "in", "x": _C6[3], "y": 103.4, "label": "MUT"},
        {"type": "jack", "id": "TEMP_CV", "kind": "in", "x": _C6[4], "y": 103.4, "label": "TEMP"},
        {"type": "jack", "id": "ROOT", "kind": "in", "x": _C6[5], "y": 103.4, "label": "ROOT"},

        {"type": "plate", "x": 2.7, "y": 108.0, "w": 75.88, "h": 15.4},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": _C7[0], "y": 118.0, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": _C7[1], "y": 118.0, "label": "R"},
        {"type": "jack", "id": "K_OUT", "kind": "out", "x": _C7[2], "y": 118.0, "label": "K"},
        {"type": "jack", "id": "S_OUT", "kind": "out", "x": _C7[3], "y": 118.0, "label": "S"},
        {"type": "jack", "id": "PITCH_OUT", "kind": "out", "x": _C7[4], "y": 118.0, "label": "V/OCT"},
        {"type": "jack", "id": "GATE_OUT", "kind": "out", "x": _C7[5], "y": 118.0, "label": "GATE"},
        {"type": "jack", "id": "STEP_OUT", "kind": "out", "x": _C7[6], "y": 118.0, "label": "STEP"},
    ],
}
