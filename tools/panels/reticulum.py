# RETICULUM — 16 HP. Units: mm. See tools/panelgen.py for the item types.
#
# From the top: Lissajous scope (left) and the node-matrix screen (right) with the
# modulator / gate-clock mode rows and the GATE pad under it; the eight node gains;
# the rete core (LEAK SCALE LVL) and the modulators (RND depth, RATE); the gate clock
# and the effects (FREE, ARTFX, BEADS, LEVEL); inputs; the output plate.
W = 16 * 5.08

N_X0, N_DX = 6.0, 9.9            # eight node knobs
M_X = [9.0, 24.8, 40.6, 56.4, 72.2]  # five medium knobs
S_X = M_X                        # five small knobs
J_X = [8.5, 24.6, 40.6, 56.7, 72.8]  # five input jacks

SPEC = {
    "slug": "Reticulum",
    "hp": 16,
    "title": "RETICULUM",
    "subtitle": "rete · 8 node noise net",
    "items": [
        # ---- screens
        {"type": "screen", "id": "SCOPE", "x": 3.4, "y": 15.6, "w": 34.0, "h": 34.0},
        {"type": "screen", "id": "MATRIX", "x": 41.0, "y": 15.6, "w": 36.9, "h": 19.4},
        {"type": "label", "text": "NODE MATRIX", "x": 41.0, "y": 37.4, "size": 1.6, "color": "dim", "anchor": "start"},
        {"type": "label", "text": "RHO|M| · SEED", "x": 77.9, "y": 37.4, "size": 1.6, "color": "dim", "anchor": "end"},

        # ---- modulator mode (OFF when none lit) and gate clock mode, GATE pad
        {"type": "led", "id": "LFO_RAND", "x": 41.4, "y": 41.6, "size": "S", "label": "RAND"},
        {"type": "led", "id": "LFO_WALK", "x": 49.4, "y": 41.6, "size": "S", "label": "WALK"},
        {"type": "led", "id": "LFO_DRUNK", "x": 57.4, "y": 41.6, "size": "S", "label": "DRUNK"},
        {"type": "button", "id": "LFO_MODE", "x": 66.8, "y": 41.6},
        {"type": "led", "id": "CLK_HAND", "x": 41.4, "y": 47.6, "size": "S", "label": "HAND"},
        {"type": "led", "id": "CLK_FREE", "x": 49.4, "y": 47.6, "size": "S", "label": "FREE"},
        {"type": "led", "id": "CLK_SYNC", "x": 57.4, "y": 47.6, "size": "S", "label": "SYNC"},
        {"type": "button", "id": "CLK_MODE", "x": 66.8, "y": 47.6},
        {"type": "pad", "id": "GATE", "x": 74.6, "y": 44.6, "color": "magenta"},
        {"type": "label", "text": "GATE", "x": 74.6, "y": 39.4, "size": 1.6, "color": "magenta"},

        # ---- node gains
    ] + [
        {"type": "knob", "id": f"N{i + 1}", "size": "S", "x": N_X0 + N_DX * i, "y": 59.0, "label": f"N{i + 1}"}
        for i in range(8)
    ] + [
        # ---- rete core and the modulators
        {"type": "knob", "id": "LEAK", "size": "M", "x": M_X[0], "y": 73.0, "label": "LEAK"},
        {"type": "knob", "id": "SCALE", "size": "M", "x": M_X[1], "y": 73.0, "label": "SCALE"},
        {"type": "knob", "id": "LVL", "size": "M", "x": M_X[2], "y": 73.0, "label": "LVL"},
        {"type": "knob", "id": "RND", "size": "M", "x": M_X[3], "y": 73.0, "label": "RND", "hot": True},
        {"type": "knob", "id": "RATE", "size": "M", "x": M_X[4], "y": 73.0, "label": "RATE"},

        # ---- gate clock, effects, level
        {"type": "knob", "id": "FREE", "size": "S", "x": S_X[0], "y": 86.0, "label": "CLK HZ"},
        {"type": "knob", "id": "FLUX", "size": "S", "x": S_X[1], "y": 86.0, "label": "FLUX"},
        {"type": "knob", "id": "ARTFX", "size": "S", "x": S_X[2], "y": 86.0, "label": "ARTFX"},
        {"type": "knob", "id": "BEADS", "size": "S", "x": S_X[3], "y": 86.0, "label": "BEADS"},
        {"type": "knob", "id": "LEVEL", "size": "S", "x": S_X[4], "y": 86.0, "label": "LEVEL"},

        # ---- inputs
        {"type": "jack", "id": "GATE_IN", "kind": "in", "x": J_X[0], "y": 99.0, "label": "GATE"},
        {"type": "jack", "id": "CLK_IN", "kind": "in", "x": J_X[1], "y": 99.0, "label": "CLK"},
        {"type": "jack", "id": "LEAK_CV", "kind": "in", "x": J_X[2], "y": 99.0, "label": "LEAK"},
        {"type": "jack", "id": "SCALE_CV", "kind": "in", "x": J_X[3], "y": 99.0, "label": "SCALE"},
        {"type": "jack", "id": "EXC_IN", "kind": "in", "x": J_X[4], "y": 99.0, "label": "EXC"},

        # ---- outputs
        {"type": "plate", "x": 2.7, "y": 107.6, "w": W - 5.4, "h": 15.6},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": 30.0, "y": 118.0, "label": "OUT L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": 51.3, "y": 118.0, "label": "OUT R"},
    ],
}
