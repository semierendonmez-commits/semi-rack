#!/usr/bin/env python3
"""Merge docs/meta/<Slug>.json (written by each port) into plugin.json and metamodule/plugin-mm.json.

Each meta file: {"plugin": {"slug","name","description","tags"}, "mm": {"slug","name","displayName","groups",...}}
Module order in both manifests is kept; unknown slugs are appended.
"""
import glob
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load(p):
    with open(p) as f:
        return json.load(f)


def save(p, d):
    with open(p, "w") as f:
        json.dump(d, f, indent=2, ensure_ascii=False)
        f.write("\n")


def upsert(lst, entry):
    for i, m in enumerate(lst):
        if m.get("slug") == entry["slug"]:
            merged = dict(m)
            merged.update({k: v for k, v in entry.items() if v not in ("", [], None)})
            lst[i] = merged
            return
    lst.append(entry)


def main():
    pj_path = os.path.join(ROOT, "plugin.json")
    mm_path = os.path.join(ROOT, "metamodule", "plugin-mm.json")
    pj, mm = load(pj_path), load(mm_path)
    n = 0
    for f in sorted(glob.glob(os.path.join(ROOT, "docs", "meta", "*.json"))):
        d = load(f)
        if "plugin" in d:
            upsert(pj["modules"], d["plugin"])
        if "mm" in d:
            upsert(mm["MetaModuleIncludedModules"], d["mm"])
        n += 1
    save(pj_path, pj)
    save(mm_path, mm)
    print(f"merged {n} meta files")


if __name__ == "__main__":
    main()
