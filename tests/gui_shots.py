#!/usr/bin/env python3
"""Live GUI check + screenshots of every module in real VCV Rack (Free), under Xvfb.

For each module: a patch with the module on top and a small Fundamental feeder rack below
(CLK = LFO square at 1/16 of 172 BPM, GATE = LFO square at 1/4, AUDIO = plucked saw VCO->VCA<-ADSR),
the module's file fields pointed at test material (drum loop WAV / demo .mod / a text file).
Rack runs ~14 s, then: zoom to fit (F4), screenshot, crop the module by its panel colour,
right-click it (context menu must open), screenshot again. The Rack log is checked for crashes.

  python3 tests/gui_shots.py <RackFree dir> <rack user dir> <out dir> [Slug ...]
Needs: Xvfb, xdotool, ImageMagick `import`, Pillow; build/describe_plugin (see tests/describe_plugin.cpp).
"""
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import time

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PANEL = (0x12, 0x19, 0x20)
DISPLAY = ":77"
W, H = 2400, 1500


# ----------------------------------------------------------------- test material

def drum_wav(path, sr=48000, seconds=16 * 60 / 172):  # exactly 4 bars at 172 BPM
    n = int(sr * seconds)
    out = bytearray()
    seed = 1
    for i in range(n):
        t = i / sr
        beat = t * 172 / 60
        b = beat % 4
        def env(x, k): return math.exp(-k * x) if 0 <= x < 1 else 0.0
        kick = env(b, 14) + env(b - 1.5, 14)
        snare = env(b - 1, 12) + env(b - 3, 12) + 0.4 * env(b - 3.75, 20)
        hat = env(beat % 0.5, 55)
        seed = (seed * 1103515245 + 12345) & 0x7fffffff
        nz = seed / 0x3fffffff - 1.0
        s = 0.9 * kick * math.sin(2 * math.pi * (48 + 110 * env(b, 26)) * t) + 0.45 * snare * nz \
            + 0.12 * hat * nz + 0.25 * snare * math.sin(2 * math.pi * 190 * t)
        v = int(max(-1, min(1, s * 0.8)) * 32000)
        out += struct.pack('<hh', v, v)
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(out)) + b'WAVEfmt ' +
                struct.pack('<IHHIIHH', 16, 1, 2, sr, sr * 4, 4, 16) + b'data' + struct.pack('<I', len(out)) + out)


def demo_mod(path):
    """8-channel ProTracker module: bass, lead, pad, drums, so the tracker screen has life."""
    d = bytearray(1084)
    d[0:9] = b'semi demo'
    samples = []
    def square(n): return bytes(((100 if (i // (n // 2)) % 2 == 0 else -100) & 0xff) for i in range(n))
    def saw(n): return bytes((int(-120 + 240 * i / n) & 0xff) for i in range(n))
    def noise(n):
        s, b = 7, bytearray()
        for i in range(n):
            s = (s * 1103515245 + 12345) & 0x7fffffff
            b.append(int(((s >> 16) % 200 - 100) * max(0.0, 1 - i / n)) & 0xff)
        return bytes(b)
    def kick(n): return bytes((int(110 * math.sin(2 * math.pi * 2.5 * i ** 0.82 / 64) * max(0, 1 - i / n)) & 0xff) for i in range(n))
    samples = [("bass", square(64), True), ("lead", saw(64), True), ("kick", kick(2400), False),
               ("snare", noise(1800), False), ("hat", noise(500), False)]
    for k, (name, data, loop) in enumerate(samples):
        p = 20 + k * 30
        d[p:p + len(name)] = name.encode()
        words = len(data) // 2
        d[p + 22:p + 24] = struct.pack('>H', words)
        d[p + 25] = 64 if k < 2 else 56
        d[p + 26:p + 28] = struct.pack('>H', 0)
        d[p + 28:p + 30] = struct.pack('>H', words if loop else 1)
    song = [0, 1, 0, 2]
    d[950] = len(song)
    d[951] = 0x7F
    for i, s in enumerate(song): d[952 + i] = s
    d[1080:1084] = b'8CHN'
    per = {'C-2': 428, 'D-2': 381, 'E-2': 339, 'F-2': 320, 'G-2': 285, 'A-2': 254, 'C-3': 214, 'D-3': 190,
           'E-3': 170, 'G-3': 143, 'A-3': 127, 'C-1': 856, 'G-1': 570, 'A-1': 508, 'F-1': 640}
    bassline = ['A-1', None, 'A-1', None, 'C-2', None, 'A-1', 'G-1', 'F-1', None, 'F-1', None, 'G-1', None, 'C-2', 'D-2']
    leads = [['A-3', None, None, 'G-3', None, 'E-3', None, None, 'C-3', None, 'D-3', None, 'E-3', None, None, None],
             ['E-3', None, 'G-3', None, 'A-3', None, None, None, 'G-3', None, 'E-3', None, 'D-3', None, 'C-3', None],
             ['C-3', 'D-3', 'E-3', None, 'G-3', None, 'A-3', None, 'G-3', 'E-3', None, 'D-3', 'C-3', None, None, None]]
    pats = bytearray()
    for p in range(3):
        for r in range(64):
            for c in range(8):
                note, smp, eff, prm = None, 0, 0, 0
                step = r % 16
                if c == 0 and bassline[step]: note, smp = bassline[step], 1
                if c == 1 and leads[p][step]: note, smp = leads[p][step], 2
                if c == 2 and step in (0, 6, 10): note, smp = 'C-2', 3
                if c == 3 and step in (4, 12): note, smp = 'C-2', 4
                if c == 4 and step % 2 == 0: note, smp = 'C-3', 5
                if c == 5 and r % 8 == 0 and p == 1: note, smp, eff, prm = 'E-2', 2, 0xC, 24
                if c == 6 and r % 32 == 0: note, smp, eff, prm = 'A-2', 1, 0xC, 20
                if c == 7 and step == 15 and p == 2: note, smp = 'C-2', 4
                period = per[note] if note else 0
                pats += bytes([(smp & 0xF0) | (period >> 8), period & 0xFF, ((smp & 15) << 4) | eff, prm])
    with open(path, 'wb') as f:
        f.write(bytes(d) + bytes(pats) + b''.join(s for _, s, _ in samples))


# ----------------------------------------------------------------- patches

def lower(s): return s.lower()


def classify(name):
    n = name.lower()
    if 'bus' in n: return 'bus'
    if n.startswith('reset') or n == 'rst': return 'reset'
    if 'clock' in n or n.startswith('clk') or n == 'clk': return 'clock'
    if n.startswith('reset') or n == 'rst': return 'reset'
    if any(k in n for k in ('left', 'audio', 'in l', 'input (l', 'sidechain', 'break audio')) or n in ('in', 'audio in', 'left input'):
        return 'audio'
    if n.startswith('right') or 'in r' in n or 'audio r' in n: return 'audio'
    if any(n.startswith(k) for k in ('gate', 'trig', 'strike', 'hit')) or 'trigger (' in n and 'new' not in n:
        return 'gate'
    return None


def set_paths(j, mapping):
    n = 0
    if isinstance(j, dict):
        for k, v in list(j.items()):
            kl = k.lower()
            if isinstance(v, str) and any(t in kl for t in ('path', 'file', 'wav', 'sample')):
                j[k] = mapping
                n += 1
            else:
                n += set_paths(v, mapping)
    elif isinstance(j, list):
        for v in j: n += set_paths(v, mapping)
    return n


def feeder_modules(fund, x0, y):
    """LFO clock, LFO gate, VCO -> VCA-1 <- ADSR. Returns modules, ids of outputs."""
    def pidx(slug, name):
        m = next(m for m in fund if m['slug'] == slug)
        return [p['name'] for p in m['params']].index(name)
    mods = [
        {"id": 9001, "plugin": "Fundamental", "model": "LFO", "params": [{"id": pidx('LFO', 'Frequency'), "value": math.log2(172 / 60 * 4)}], "pos": [x0, y]},
        {"id": 9002, "plugin": "Fundamental", "model": "LFO", "params": [{"id": pidx('LFO', 'Frequency'), "value": math.log2(172 / 60)}], "pos": [x0 + 9, y]},
        {"id": 9003, "plugin": "Fundamental", "model": "VCO", "params": [{"id": pidx('VCO', 'Frequency'), "value": -24.0}], "pos": [x0 + 18, y]},
        {"id": 9004, "plugin": "Fundamental", "model": "ADSR", "params": [{"id": 0, "value": 0.0}, {"id": 1, "value": 0.35}, {"id": 2, "value": 0.15}, {"id": 3, "value": 0.25}], "pos": [x0 + 28, y]},
        {"id": 9005, "plugin": "Fundamental", "model": "VCA-1", "params": [], "pos": [x0 + 37, y]},
        {"id": 9006, "plugin": "Fundamental", "model": "Scope", "params": [], "pos": [x0 + 41, y]},
    ]
    cables = [
        (9001, 3, 9004, 4),   # clock square -> ADSR gate
        (9004, 0, 9005, 0),   # env -> VCA cv
        (9003, 2, 9005, 1),   # saw -> VCA in
    ]
    srcs = {"clock": (9001, 3), "gate": (9002, 3), "audio": (9005, 0)}
    return mods, cables, srcs


def make_patch(desc, fund, material):
    slug = desc['slug']
    data = desc.get('data')
    if isinstance(data, (dict, list)):
        data = json.loads(json.dumps(data))
        path = material['mod'] if slug == 'Modport' else material['text'] if slug in (
            'Pindata', 'Ikedat', 'Floatdat', 'Jdat', 'Datascreen', 'TstIngp') else material['wav']
        set_paths(data, path)
    target = {"id": 1, "plugin": "SemiFamily", "model": slug, "version": "2.0.0", "params": [], "pos": [0, 0]}
    if data is not None:
        target["data"] = data
    mods, cables, srcs = feeder_modules(fund, 0, 1)
    cid = 1
    out_cables = [{"id": cid + i, "outputModuleId": a, "outputId": b, "inputModuleId": c, "inputId": d}
                  for i, (a, b, c, d) in enumerate(cables)]
    cid += len(out_cables)
    for i, name in enumerate(desc['inputs']):
        k = classify(name)
        if k in srcs:
            m, o = srcs[k]
            out_cables.append({"id": cid, "outputModuleId": m, "outputId": o, "inputModuleId": 1, "inputId": i})
            cid += 1
    # the module's first output on a scope: the full screenshot shows whether it really sounds
    if desc['outputs']:
        for k in range(min(2, len(desc['outputs']))):
            out_cables.append({"id": cid, "outputModuleId": 1, "outputId": k, "inputModuleId": 9006, "inputId": k})
            cid += 1
    return {"version": "2.6.6", "modules": [target] + mods, "cables": out_cables}


# ----------------------------------------------------------------- running Rack

def sh(cmd, **kw):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, **kw)


def crop_panel(img):
    """Bounding box of the SEMI panel colour (the only SEMI module in the patch)."""
    px = img.convert('RGB').load()
    w, h = img.size
    xs, ys = [], []
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            r, g, b = px[x, y]
            if abs(r - PANEL[0]) < 3 and abs(g - PANEL[1]) < 3 and abs(b - PANEL[2]) < 3:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    xs.sort(); ys.sort()
    # robust: ignore stray pixels
    lo = lambda v: v[len(v) // 500]
    hi = lambda v: v[-1 - len(v) // 500]
    return (max(0, lo(xs) - 3), max(0, lo(ys) - 3), min(w, hi(xs) + 4), min(h, hi(ys) + 4))


def run_one(rack_dir, user, out, desc, fund, material, wait=14):
    slug = desc['slug']
    # fresh user state every run (no crash-recovery dialog, no tips, no update check)
    for f in ('autosave', 'settings.json', 'log.txt', 'screenshots'):
        p = os.path.join(user, f)
        shutil.rmtree(p, ignore_errors=True) if os.path.isdir(p) else (os.path.exists(p) and os.remove(p))
    json.dump({"autoCheckUpdates": False, "showTipsOnLaunch": False, "windowSize": [W, H], "windowPos": [0, 0],
               "windowMaximized": False, "cableOpacity": 0.35, "verifyLibrary": False, "tipIndex": -1, "haveTips": False},
              open(os.path.join(user, 'settings.json'), 'w'))
    patch = make_patch(desc, fund, material)
    ppath = os.path.join(out, f'_{slug}.vcv')
    json.dump(patch, open(ppath, 'w'))
    env = dict(os.environ, DISPLAY=DISPLAY)
    proc = subprocess.Popen(['./Rack', '-u', user, ppath], cwd=rack_dir, env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    res = {"slug": slug, "ok": True, "notes": []}
    try:
        time.sleep(wait)
        if proc.poll() is not None:
            res["ok"] = False
            res["notes"].append(f"Rack exited early ({proc.returncode})")
            return res
        sh(f'DISPLAY={DISPLAY} xdotool search --name "VCV Rack" windowactivate --sync 2>/dev/null; '
           f'DISPLAY={DISPLAY} xdotool mousemove 5 5 key F4')
        time.sleep(2.5)
        full = os.path.join(out, f'_{slug}_full.png')
        sh(f'DISPLAY={DISPLAY} import -window root {full}')
        img = Image.open(full)
        box = crop_panel(img)
        if not box:
            res["ok"] = False
            res["notes"].append("panel not found on screen")
            return res
        img.crop(box).save(os.path.join(out, f'{slug}.png'))
        # context menu
        cx, cy = (box[0] + box[2]) // 2, box[1] + (box[3] - box[1]) // 6
        sh(f'DISPLAY={DISPLAY} xdotool mousemove {cx} {cy} click 3')
        time.sleep(1.5)
        menu = os.path.join(out, f'_{slug}_menu.png')
        sh(f'DISPLAY={DISPLAY} import -window root {menu}')
        a, b = Image.open(full).convert('RGB'), Image.open(menu).convert('RGB')
        diff = sum(1 for p, q in zip(a.resize((240, 150)).get_flattened_data(), b.resize((240, 150)).get_flattened_data()) if p != q)
        res["menuPixels"] = diff
        if diff < 150:
            res["notes"].append("context menu did not visibly open")
        sh(f'DISPLAY={DISPLAY} xdotool key Escape')
        time.sleep(0.5)
        if proc.poll() is not None:
            res["ok"] = False
            res["notes"].append("Rack exited after the context menu")
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        log = open(os.path.join(user, 'log.txt'), errors='replace').read() if os.path.exists(os.path.join(user, 'log.txt')) else ''
        bad = [l for l in log.splitlines() if (' fatal ' in l and 'signal 9' not in l and 'signal 15' not in l)
               or ('warn' in l and 'SemiFamily' in l) or ('Could not' in l)]
        if bad:
            res["notes"] += bad[:4]
            if any(' fatal ' in l for l in bad):
                res["ok"] = False
    return res


def main(argv):
    rack_dir, user, out = argv[:3]
    want = set(argv[3:])
    os.makedirs(out, exist_ok=True)
    semi = json.loads(sh(f'{ROOT}/build/describe_plugin {user}/plugins-lin-x64/SemiFamily').stdout)
    fund = json.loads(sh(f'{ROOT}/build/describe_plugin {user}/plugins-lin-x64/Fundamental').stdout)
    material = {"wav": os.path.join(out, '_drums.wav'), "mod": os.path.join(out, '_demo.mod'),
                "text": os.path.join(ROOT, 'README.md')}
    drum_wav(material['wav'])
    demo_mod(material['mod'])
    xv = subprocess.Popen(['Xvfb', DISPLAY, '-screen', '0', f'{W}x{H}x24', '-nolisten', 'tcp'],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    results = []
    try:
        for d in semi:
            if want and d['slug'] not in want:
                continue
            r = run_one(rack_dir, user, out, d, fund, material)
            results.append(r)
            print(f"{r['slug']:14s} {'OK ' if r['ok'] else 'BAD'} menu={r.get('menuPixels', '-')} {' | '.join(r['notes'])}", flush=True)
    finally:
        xv.kill()
    json.dump(results, open(os.path.join(out, '_results.json'), 'w'), indent=1)


if __name__ == '__main__':
    main(sys.argv[1:])
