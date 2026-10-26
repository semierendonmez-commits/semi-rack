#!/usr/bin/env python3
"""SEMI Family panel generator.

One layout spec per module (tools/panels/<slug>.py, a SPEC dict in millimetres)
produces everything that has to agree across platforms:

  res/panels/<Slug>.svg                     Rack faceplate (text converted to paths:
                                            Rack's nanosvg cannot render <text>)
  res/components/*.svg                      knobs, jacks, buttons, pads (shared set)
  src/layout/<Slug>Layout.hpp               constexpr positions for the C++ module
  metamodule/assets/panels/<Slug>.png       MetaModule faceplate, 240 px high, opaque
  metamodule/assets/components/*.png        component art at the same scale
  build/preview/<Slug>.png                  faceplate + components, for review

Usage:  python3 tools/panelgen.py [slug ...]      (no args = every spec)
"""
import importlib.util
import math
import os
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HP_MM = 5.08
H_MM = 128.5
MM_PX_MM = 240.0 / H_MM  # MetaModule: 240 px faceplate height

C = {
    "panel": "#121920", "edge": "#2A3540", "bezel": "#2B3540", "glass": "#05080A",
    "cyan": "#4FE3D6", "magenta": "#FF4F9A", "amber": "#E8B04A", "text": "#9DB5B2",
    "dim": "#5C7380", "plate": "#0F2B2A", "screw": "#7C858C", "screwEdge": "#4A535A",
    "slot": "#3E4952", "nut": "#9EA6AC", "nutEdge": "#5E676E", "hole": "#05080A",
    "busNut": "#FF4F9A", "busEdge": "#8E1440", "skirt": "#2B3540", "cap": "#0B1015",
}

KNOBS = {"L": (10.0, 7.2), "M": (8.6, 6.2), "S": (6.0, 4.3)}  # skirt, cap diameter
JACK_BOX = 8.0
BUTTON_BOX = 5.0
PAD_BOX = 8.0
LED_R = {"M": 1.6, "S": 1.1}  # radius of the drawn LED window (Rack light sits on top)


# --------------------------------------------------------------------------- fonts

class Font:
    def __init__(self, path):
        self.f = TTFont(path)
        self.gs = self.f.getGlyphSet()
        self.cmap = self.f.getBestCmap()
        self.upem = self.f["head"].unitsPerEm
        self.cap = getattr(self.f["OS/2"], "sCapHeight", 0) or self.upem * 0.7
        self.hmtx = self.f["hmtx"]

    def width(self, s, size, spacing=0.0):
        sc = size / self.upem
        w = 0.0
        for ch in s:
            g = self.cmap.get(ord(ch))
            if g:
                w += self.hmtx[g][0] * sc + spacing
        return max(0.0, w - spacing)

    def path(self, s, x, y, size, anchor="middle", spacing=0.0):
        """SVG path data for s; (x, y) = anchor point, y = vertical centre of caps."""
        sc = size / self.upem
        w = self.width(s, size, spacing)
        x0 = {"start": x, "middle": x - w / 2, "end": x - w}[anchor]
        base = y + self.cap * sc / 2
        out = []
        for ch in s:
            g = self.cmap.get(ord(ch))
            if not g:
                continue
            pen = SVGPathPen(self.gs)
            self.gs[g].draw(TransformPen(pen, (sc, 0, 0, -sc, x0, base)))
            d = pen.getCommands()
            if d:
                out.append(d)
            x0 += self.hmtx[g][0] * sc + spacing
        return " ".join(out)


FONTS = {}


def font(name):
    if name not in FONTS:
        FONTS[name] = Font(os.path.join(ROOT, "tools", "fonts", name))
    return FONTS[name]


def title_font():
    return font("ChakraPetch-Bold.ttf")


def label_font():
    return font("ShareTechMono-Regular.ttf")


def text_el(s, x, y, size, color, anchor="middle", f=None, spacing=0.0):
    f = f or label_font()
    d = f.path(s, x, y, size, anchor, spacing)
    return f'<path d="{d}" fill="{color}"/>' if d else ""


# --------------------------------------------------------------------------- components

def svg_doc(w, h, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:.3f}mm" height="{h:.3f}mm" '
            f'viewBox="0 0 {w:.3f} {h:.3f}">{body}</svg>\n')


MM_ONLY = {}   # component art that exists only as MetaModule PNG


def component_svgs():
    comps = {}
    for size, (sd, cd) in KNOBS.items():
        r = sd / 2
        comps[f"Knob{size}_skirt"] = svg_doc(sd, sd, f'<circle cx="{r}" cy="{r}" r="{r}" fill="{C["skirt"]}"/>')
        for suffix, col in (("", C["cyan"]), ("_hot", C["magenta"])):
            cr = cd / 2
            lw = max(0.45, sd * 0.06)
            body = (f'<circle cx="{r}" cy="{r}" r="{cr}" fill="{C["cap"]}"/>'
                    f'<line x1="{r}" y1="{r - cr + lw * 0.9:.3f}" x2="{r}" y2="{r - cr * 0.28:.3f}" '
                    f'stroke="{col}" stroke-width="{lw:.3f}" stroke-linecap="round"/>')
            comps[f"Knob{size}_cap{suffix}"] = svg_doc(sd, sd, body)
            # MetaModule draws a knob as ONE image rotated with the value (no fixed bg layer),
            # so it gets skirt + cap + pointer flattened. PNG only: Rack keeps the two layers.
            # The pointer is drawn bolder there: from the skirt's edge to near the centre, ~2 px wide
            # on the 144 ppi screen, so the position reads at a glance on a 11-19 px knob.
            mlw = max(1.0, sd * 0.13)
            flat = (f'<circle cx="{r}" cy="{r}" r="{r}" fill="{C["skirt"]}"/>'
                    f'<circle cx="{r}" cy="{r}" r="{cr}" fill="{C["cap"]}"/>'
                    f'<line x1="{r}" y1="{mlw * 0.6:.3f}" x2="{r}" y2="{r - cr * 0.15:.3f}" '
                    f'stroke="{col}" stroke-width="{mlw:.3f}" stroke-linecap="butt"/>')
            MM_ONLY[f"Knob{size}_mm{suffix}"] = svg_doc(sd, sd, flat)
    j = JACK_BOX / 2
    for name, nut, edge in (("Jack", C["nut"], C["nutEdge"]), ("JackBus", C["busNut"], C["busEdge"])):
        comps[name] = svg_doc(JACK_BOX, JACK_BOX,
                              f'<circle cx="{j}" cy="{j}" r="2.85" fill="{edge}"/>'
                              f'<circle cx="{j}" cy="{j}" r="2.5" fill="{nut}"/>'
                              f'<circle cx="{j}" cy="{j}" r="1.2" fill="{C["hole"]}"/>')
    b = BUTTON_BOX / 2
    for i, inner in enumerate((C["bezel"], C["slot"])):
        comps[f"Button_{i}"] = svg_doc(BUTTON_BOX, BUTTON_BOX,
                                       f'<circle cx="{b}" cy="{b}" r="2.4" fill="{C["nutEdge"]}"/>'
                                       f'<circle cx="{b}" cy="{b}" r="1.85" fill="{inner}"/>')
    for name, col, deep in (("PadMagenta", C["magenta"], "#2A1220"), ("PadCyan", C["cyan"], C["plate"])):
        for i, fill in enumerate((deep, col)):
            comps[f"{name}_{i}"] = svg_doc(PAD_BOX, PAD_BOX,
                                           f'<rect x="0.3" y="0.3" width="{PAD_BOX - 0.6}" height="{PAD_BOX - 0.6}" '
                                           f'rx="1" fill="{fill}" stroke="{col}" stroke-width="0.6"/>')
    return comps


# --------------------------------------------------------------------------- panels

def screws(w):
    xs = [7.62] if w <= 10 * HP_MM else [7.62, w - 7.62]
    out = []
    for x in xs:
        for y in (2.54, H_MM - 2.54):
            out.append(f'<circle cx="{x:.3f}" cy="{y}" r="1.25" fill="{C["screw"]}" stroke="{C["screwEdge"]}" stroke-width="0.15"/>'
                       f'<rect x="{x - 0.7:.3f}" y="{y - 0.17:.3f}" width="1.4" height="0.34" fill="{C["slot"]}" '
                       f'transform="rotate({(x * 37 + y * 11) % 180:.0f} {x:.3f} {y})"/>')
    return "".join(out)


def label_above(item, text, color, gap=1.1, size=1.9):
    r = item_radius(item)
    return text_el(text, item["x"], item["y"] - r - gap, size, color)


def item_radius(item):
    t = item["type"]
    if t == "knob":
        return KNOBS[item.get("size", "M")][0] / 2
    if t == "jack":
        return JACK_BOX / 2 - 0.9
    if t == "button":
        return BUTTON_BOX / 2 - 0.3
    if t == "pad":
        return PAD_BOX / 2
    if t == "led":
        return LED_R[item.get("size", "M")]
    return 0


def build_panel(spec):
    w = spec["hp"] * HP_MM
    body = [f'<rect x="0" y="0" width="{w:.3f}" height="{H_MM}" fill="{C["panel"]}"/>',
            f'<rect x="0.1" y="0.1" width="{w - 0.2:.3f}" height="{H_MM - 0.2}" fill="none" stroke="{C["edge"]}" stroke-width="0.2"/>',
            screws(w)]
    m = 2.7  # side margin
    ty = 9.6
    body.append(text_el(spec["title"], m, ty, 4.1, C["cyan"], "start", title_font(), 0.35))
    if spec.get("subtitle"):
        body.append(text_el(spec["subtitle"], w - m, ty + 0.4, 1.9, C[spec.get("subtitleColor", "dim")], "end"))
    body.append(f'<rect x="{m}" y="{ty + 3.0:.2f}" width="{w - 2 * m:.3f}" height="0.34" fill="{C["cyan"]}"/>')

    for it in spec["items"]:
        t = it["type"]
        if t == "screen":
            x, y, sw, sh = it["x"], it["y"], it["w"], it["h"]
            body.append(f'<rect x="{x - 0.7:.3f}" y="{y - 0.7:.3f}" width="{sw + 1.4:.3f}" height="{sh + 1.4:.3f}" rx="0.7" fill="{C["bezel"]}"/>')
            body.append(f'<rect x="{x:.3f}" y="{y:.3f}" width="{sw:.3f}" height="{sh:.3f}" rx="0.35" fill="{C["glass"]}"/>')
        elif t == "plate":
            body.append(f'<rect x="{it["x"]:.3f}" y="{it["y"]:.3f}" width="{it["w"]:.3f}" height="{it["h"]:.3f}" rx="0.7" '
                        f'fill="{C["plate"]}" stroke="{C["cyan"]}" stroke-width="0.17"/>')
        elif t == "label":
            body.append(text_el(it["text"], it["x"], it["y"], it.get("size", 1.9), C[it.get("color", "text")],
                                it.get("anchor", "middle")))
        elif t == "box":
            body.append(f'<rect x="{it["x"]:.3f}" y="{it["y"]:.3f}" width="{it["w"]:.3f}" height="{it["h"]:.3f}" rx="0.5" '
                        f'fill="{C.get(it.get("fill", "glass"), it.get("fill"))}" stroke="{C["bezel"]}" stroke-width="0.17"/>')

    # second pass: labels and LED windows on top of plates
    for it in spec["items"]:
        t = it["type"]
        if t in ("knob", "jack", "button", "pad") and it.get("label"):
            if t == "jack":
                col = C["magenta"] if it.get("bus") else (C["cyan"] if it.get("kind") == "out" else C["text"])
            elif t == "pad":
                col = C[it.get("color", "magenta")]
            else:
                col = C["text"]
            if it.get("labelPos") == "below":
                r = item_radius(it)
                body.append(text_el(it["label"], it["x"], it["y"] + r + 1.9, 1.9, col))
            elif it.get("labelPos") == "right":
                r = item_radius(it)
                body.append(text_el(it["label"], it["x"] + r + 1.0, it["y"], 1.9, col, "start"))
            else:
                body.append(label_above(it, it["label"], col))
        elif t == "led":
            r = LED_R[it.get("size", "M")]
            body.append(f'<circle cx="{it["x"]:.3f}" cy="{it["y"]:.3f}" r="{r:.3f}" fill="{C["bezel"]}"/>')
            if it.get("label"):
                col = C[it.get("labelColor", "dim")]
                if it.get("labelPos") == "above":
                    body.append(text_el(it["label"], it["x"], it["y"] - r - 1.3, 1.7, col))
                else:
                    body.append(text_el(it["label"], it["x"] + r + 0.9, it["y"], 1.8, col, "start"))
    return svg_doc(w, H_MM, "".join(body))


# --------------------------------------------------------------------------- layout header

def cname(s):
    return "".join(ch if ch.isalnum() else "_" for ch in s.upper())


def build_header(spec):
    slug = spec["slug"]
    lines = ["#pragma once", f"// Generated by tools/panelgen.py from tools/panels/{slug.lower()}.py — do not edit.",
             "// All values in millimetres; centres for knobs/jacks/leds/buttons/pads, top-left + size for screens.",
             "", "namespace semi {", "namespace layout {", f"namespace {slug.lower()} {{", "",
             "struct P { float x, y; };", "struct R { float x, y, w, h; };", "",
             f"constexpr float kWidthHp = {spec['hp']};", ""]
    seen = set()
    for it in spec["items"]:
        if "id" not in it:
            continue
        n = cname(it["id"])
        if n in seen:
            raise SystemExit(f"{slug}: duplicate id {it['id']}")
        seen.add(n)
        if it["type"] == "screen":
            lines.append(f"constexpr R {n}{{{it['x']:.3f}f, {it['y']:.3f}f, {it['w']:.3f}f, {it['h']:.3f}f}};")
        else:
            lines.append(f"constexpr P {n}{{{it['x']:.3f}f, {it['y']:.3f}f}};  // {it['type']}")
    lines += ["", f"}} // namespace {slug.lower()}", "} // namespace layout", "} // namespace semi", ""]
    return "\n".join(lines)


# --------------------------------------------------------------------------- rendering

def render_png(svg_text, out, height_px=None, width_px=None):
    import cairosvg
    os.makedirs(os.path.dirname(out), exist_ok=True)
    kw = {}
    if height_px:
        kw["output_height"] = int(round(height_px))
    if width_px:
        kw["output_width"] = int(round(width_px))
    cairosvg.svg2png(bytestring=svg_text.encode(), write_to=out, **kw)


def preview_svg(spec, panel_svg, comps):
    """Panel + components composited (for review and the MetaModule look-check)."""
    w = spec["hp"] * HP_MM
    inner = panel_svg.split(">", 1)[1].rsplit("</svg>", 1)[0]
    parts = [inner]

    def place(name, cx, cy, size):
        sub = comps[name].split(">", 1)[1].rsplit("</svg>", 1)[0]
        parts.append(f'<g transform="translate({cx - size / 2:.3f} {cy - size / 2:.3f})">{sub}</g>')

    for it in spec["items"]:
        t = it["type"]
        if t == "knob":
            s = it.get("size", "M")
            place(f"Knob{s}_skirt", it["x"], it["y"], KNOBS[s][0])
            place(f"Knob{s}_cap" + ("_hot" if it.get("hot") else ""), it["x"], it["y"], KNOBS[s][0])
        elif t == "jack":
            place("JackBus" if it.get("bus") else "Jack", it["x"], it["y"], JACK_BOX)
        elif t == "button":
            place("Button_0", it["x"], it["y"], BUTTON_BOX)
        elif t == "pad":
            place(("PadCyan" if it.get("color") == "cyan" else "PadMagenta") + "_0", it["x"], it["y"], PAD_BOX)
    return svg_doc(w, H_MM, "".join(parts))


def load_spec(path):
    s = importlib.util.spec_from_file_location("spec", path)
    m = importlib.util.module_from_spec(s)
    s.loader.exec_module(m)
    return m.SPEC


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


def main(argv):
    comps = component_svgs()
    for name, svg in comps.items():
        write(os.path.join(ROOT, "res", "components", name + ".svg"), svg)
        size_mm = float(svg.split('width="')[1].split("mm")[0])
        render_png(svg, os.path.join(ROOT, "metamodule", "assets", "components", name + ".png"),
                   height_px=size_mm * MM_PX_MM)
    for name, svg in MM_ONLY.items():
        size_mm = float(svg.split('width="')[1].split("mm")[0])
        render_png(svg, os.path.join(ROOT, "metamodule", "assets", "components", name + ".png"),
                   height_px=size_mm * MM_PX_MM)

    spec_dir = os.path.join(ROOT, "tools", "panels")
    files = sorted(f for f in os.listdir(spec_dir) if f.endswith(".py") and not f.startswith("_"))
    want = {a.lower() for a in argv}
    for fn in files:
        spec = load_spec(os.path.join(spec_dir, fn))
        if want and spec["slug"].lower() not in want:
            continue
        slug = spec["slug"]
        panel = build_panel(spec)
        write(os.path.join(ROOT, "res", "panels", slug + ".svg"), panel)
        write(os.path.join(ROOT, "src", "layout", slug + "Layout.hpp"), build_header(spec))
        render_png(panel, os.path.join(ROOT, "metamodule", "assets", "panels", slug + ".png"), height_px=240)
        render_png(preview_svg(spec, panel, comps), os.path.join(ROOT, "build", "preview", slug + ".png"),
                   height_px=760)
        print(f"{slug}: {spec['hp']} HP, {len(spec['items'])} items")


if __name__ == "__main__":
    main(sys.argv[1:])
