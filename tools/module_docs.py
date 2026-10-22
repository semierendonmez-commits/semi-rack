#!/usr/bin/env python3
"""Builds the GitHub module pages and the README gallery.

  python3 tools/module_docs.py <describe.json>

describe.json is the output of build/describe_plugin (ports and params as the plugin reports them).
Per module it reads plugin.json (name, description, tags), metamodule/plugin-mm.json (control groups),
docs/intro/<Slug>.md (hand-written intro) and docs/images/<Slug>.png (live Rack screenshot, made by
tests/gui_shots.py), and writes docs/modules/<Slug>.md. The README gallery between the
<!-- gallery --> markers is regenerated from the same data.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
START, END = "<!-- gallery:start -->", "<!-- gallery:end -->"


def load(p):
    with open(os.path.join(ROOT, p)) as f:
        return json.load(f)


def hp_of(slug):
    """Panel width in HP from the Rack SVG (5.08 mm per HP)."""
    svg = open(os.path.join(ROOT, "res", "panels", slug + ".svg")).read(2000)
    m = re.search(r'width="([\d.]+)mm"', svg)
    return round(float(m.group(1)) / 5.08) if m else None


def fmt(v):
    return f"{v:g}" if abs(v) < 1e5 else f"{v:.3g}"


def clean(name):
    return name.replace("|", "/") or "—"


def control_rows(params, groups):
    """Params in the MetaModule group order (that is how the hardware lists them), the rest after."""
    by_name = {}
    for p in params:
        if p["name"]:
            by_name.setdefault(p["name"], p)
    rows, seen = [], set()
    for group, names in (groups or {}).items():
        for n in names:
            if n in by_name and n not in seen:
                p = by_name[n]
                rows.append((group, p))
                seen.add(n)
    for n, p in by_name.items():
        if n not in seen:
            rows.append(("", p))
    return rows


def page(m, mm, d, intro, hp):
    slug, name = m["slug"], m["name"]
    out = [f"# {name}", ""]
    out.append(f"*{m.get('description', '')}*")
    out.append("")
    facts = [f"{hp} HP" if hp else None, " · ".join(m.get("tags", [])) or None]
    out.append(" — ".join(x for x in facts if x))
    out.append("")
    if os.path.exists(os.path.join(ROOT, "docs", "images", slug + ".png")):
        out.append(f'<p align="center"><img src="../images/{slug}.png" alt="{name}" height="560"></p>')
        out.append("")
    out.append(intro.strip() if intro else "")
    out.append("")
    rows = control_rows(d["params"], mm.get("groups") if mm else None)
    if rows:
        out += ["## Controls", "", "| group | control | range | default |", "|---|---|---|---|"]
        for g, p in rows:
            out.append(f"| {g} | {clean(p['name'])} | {fmt(p['min'])} … {fmt(p['max'])} | {fmt(p['default'])} |")
        out.append("")
    ins = [n for n in d["inputs"] if n]
    outs = [n for n in d["outputs"] if n]
    if ins or outs:
        out += ["## Inputs and outputs", "", "| inputs | outputs |", "|---|---|"]
        for i in range(max(len(ins), len(outs))):
            out.append(f"| {clean(ins[i]) if i < len(ins) else ''} | {clean(outs[i]) if i < len(outs) else ''} |")
        out.append("")
    out.append("[← all modules](../../README.md#modules)")
    out.append("")
    return "\n".join(out)


def gallery(mods):
    cells = []
    for m in mods:
        slug, name = m["slug"], m["name"]
        img = f'<a href="docs/modules/{slug}.md"><img src="docs/images/{slug}.png" alt="{name}" height="300"></a>'
        cells.append(f'<td align="center" valign="bottom">{img}<br><a href="docs/modules/{slug}.md"><b>{name}</b></a></td>')
    lines = [START, "", "<table>"]
    per = 5
    for i in range(0, len(cells), per):
        lines.append("<tr>" + "".join(cells[i:i + per]) + "</tr>")
    lines += ["</table>", "", "| module | what it does |", "|---|---|"]
    for m in mods:
        lines.append(f"| [{m['name']}](docs/modules/{m['slug']}.md) | {m.get('description', '')} |")
    lines += ["", END]
    return "\n".join(lines)


def main(desc_path):
    pj = load("plugin.json")
    mm = {m["slug"]: m for m in load("metamodule/plugin-mm.json")["MetaModuleIncludedModules"]}
    desc = {d["slug"]: d for d in json.load(open(desc_path))}
    os.makedirs(os.path.join(ROOT, "docs", "modules"), exist_ok=True)
    for m in pj["modules"]:
        slug = m["slug"]
        ip = os.path.join(ROOT, "docs", "intro", slug + ".md")
        intro = open(ip).read() if os.path.exists(ip) else ""
        if not intro:
            print("no intro:", slug)
        text = page(m, mm.get(slug), desc[slug], intro, hp_of(slug))
        with open(os.path.join(ROOT, "docs", "modules", slug + ".md"), "w") as f:
            f.write(text)
    rp = os.path.join(ROOT, "README.md")
    readme = open(rp).read()
    g = gallery(pj["modules"])
    if START in readme:
        readme = readme[:readme.index(START)] + g + readme[readme.index(END) + len(END):]
    else:
        print("README has no gallery markers")
    with open(rp, "w") as f:
        f.write(readme)
    print(f"wrote {len(pj['modules'])} module pages")


if __name__ == "__main__":
    main(sys.argv[1])
