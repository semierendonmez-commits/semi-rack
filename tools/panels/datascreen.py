# DATASCREEN — 24 HP. Units: mm. See tools/panelgen.py for the item types.
# The screen is the product: one big glass rendering the family's 26 scenes (PINDATA 01-12,
# FLOATDAT films 13-20, case 21-26) from the audio in, the BUS cable and the event inputs.
# Knobs: the picture (scene, variant, change, density, react, speed); NEXT / FREEZE / FLASH.
# Inputs: audio L/R, CLK, RST, TRIG (poly lanes), STEP (a sibling's STEP CV), HEAD, SCENE CV, BUS.
# Out plate: audio thru, CUT trigger, BUS (thru, SCENE channel = this screen's scene).
_K = [10.0, 27.0, 44.0, 61.0, 78.0, 95.0]
_J = [9.0, 21.8, 34.6, 47.4, 60.2, 73.0, 85.8, 98.6, 111.4]
_O = [12.0, 27.0, 42.0, 96.0, 111.0]

SPEC = {
    "slug": "Datascreen",
    "hp": 24,
    "title": "DATASCREEN",
    "subtitle": "pindata · floatdat · scenes",
    "subtitleColor": "magenta",
    "items": [
        {"type": "screen", "id": "SCREEN", "x": 3.1, "y": 15.4, "w": 115.7, "h": 62.0},

        {"type": "knob", "id": "SCENE", "size": "M", "x": _K[0], "y": 88.0, "label": "SCENE"},
        {"type": "knob", "id": "VARIANT", "size": "M", "x": _K[1], "y": 88.0, "label": "VARIANT"},
        {"type": "knob", "id": "CHANGE", "size": "M", "x": _K[2], "y": 88.0, "label": "CHANGE"},
        {"type": "knob", "id": "DENSITY", "size": "M", "x": _K[3], "y": 88.0, "label": "DENSITY"},
        {"type": "knob", "id": "REACT", "size": "M", "x": _K[4], "y": 88.0, "label": "REACT", "hot": True},
        {"type": "knob", "id": "SPEED", "size": "M", "x": _K[5], "y": 88.0, "label": "SPEED"},

        {"type": "button", "id": "NEXT", "x": 108.0, "y": 83.6, "label": "NEXT"},
        {"type": "button", "id": "FREEZE", "x": 108.0, "y": 93.6, "label": "FRZ"},
        {"type": "led", "id": "FREEZE_LED", "x": 114.6, "y": 93.6, "size": "S"},
        {"type": "button", "id": "FLASH", "x": 115.6, "y": 83.6, "label": "FLSH"},

        {"type": "jack", "id": "IN_L", "kind": "in", "x": _J[0], "y": 105.0, "label": "IN L"},
        {"type": "jack", "id": "IN_R", "kind": "in", "x": _J[1], "y": 105.0, "label": "IN R"},
        {"type": "jack", "id": "CLK", "kind": "in", "x": _J[2], "y": 105.0, "label": "CLK"},
        {"type": "jack", "id": "RST", "kind": "in", "x": _J[3], "y": 105.0, "label": "RST"},
        {"type": "jack", "id": "TRIG", "kind": "in", "x": _J[4], "y": 105.0, "label": "TRIG"},
        {"type": "jack", "id": "STEP", "kind": "in", "x": _J[5], "y": 105.0, "label": "STEP"},
        {"type": "jack", "id": "HEAD", "kind": "in", "x": _J[6], "y": 105.0, "label": "HEAD"},
        {"type": "jack", "id": "SCENE_CV", "kind": "in", "x": _J[7], "y": 105.0, "label": "SCENE"},
        {"type": "jack", "id": "BUS_IN", "kind": "in", "bus": True, "x": _J[8], "y": 105.0, "label": "BUS"},

        {"type": "plate", "x": 2.7, "y": 109.6, "w": 116.5, "h": 14.0},
        {"type": "jack", "id": "OUT_L", "kind": "out", "x": _O[0], "y": 118.6, "label": "L"},
        {"type": "jack", "id": "OUT_R", "kind": "out", "x": _O[1], "y": 118.6, "label": "R"},
        {"type": "jack", "id": "CUT", "kind": "out", "x": _O[2], "y": 118.6, "label": "CUT"},
        {"type": "label", "text": "26 SCENES · AUDIO · EVENTS · BUS", "x": 69.0, "y": 116.6, "size": 1.9, "color": "cyan"},
        {"type": "label", "text": "DATA 01-12 · FILMS 13-20 · CASE 21-26", "x": 69.0, "y": 120.6, "size": 1.6},
        {"type": "jack", "id": "BUS_OUT", "kind": "out", "bus": True, "x": _O[4], "y": 118.6, "label": "BUS·4"},
    ],
}
