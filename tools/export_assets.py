#!/usr/bin/env python3
"""Export the engine's visual assets to JSON for the web map-editor preview.

Phase A: the PALETTE. Parses build_palette() out of sh_src/raycast.c (the MIX
ramp loops + the explicit Hw32xSetBGColor calls) and reproduces the 256-entry
palette as 8-bit RGB, plus the base-index constants the raycaster needs. Parsing
(not hardcoding) keeps the preview in lock-step with palette tweaks in the ROM.

  python3 tools/export_assets.py                 # writes tools/map-editor/static/assets.json
  from export_assets import build_assets; build_assets(repo_root)   # -> dict (Flask /assets)
"""
import argparse, json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# base-index constants the JS raycaster references by name
WANT_BASES = ["WALL_BASE", "FLOOR_BASE", "CEIL_BASE", "LIGHT_BASE", "NEANDER_BASE",
              "OUTLET_BASE", "PARTITION_BASE", "DOOR_BASE", "DOOR_DARK_BASE",
              "STIPPLE_BASE", "HANDLE_BASE", "FRAME_BASE", "LOWCEIL_COLOR",
              "LOWCEIL_SEAM", "SHADE_LEVELS", "CEIL_GRID_DENSITY",
              "LIGHT_BOOST_MAX", "CRAWL_CEIL_H", "CEIL_H_FULL"]


def _defines(src):
    d = {}
    for m in re.finditer(r'#define\s+(\w+)\s+(\d+)\b', src):
        d.setdefault(m.group(1), int(m.group(2)))
    return d


def _resolve(expr, d):
    total = 0
    for tok in expr.split('+'):
        tok = tok.strip()
        if tok.isdigit():
            total += int(tok)
        elif tok in d:
            total += d[tok]
        else:
            return None
    return total


def build_palette(src):
    """Reproduce build_palette()'s 256-entry palette as 8-bit [[r,g,b],...]."""
    d = _defines(src)
    SL = d.get("SHADE_LEVELS", 16)
    FOG = (d.get("FOG_R", 8), d.get("FOG_G", 8), d.get("FOG_B", 8))
    m = re.search(r'static void build_palette\(void\)\s*\{(.*?)\n\}', src, re.S)
    if not m:
        raise RuntimeError("build_palette() not found in raycast.c")
    body = m.group(1)
    pal = [[0, 0, 0] for _ in range(256)]

    # explicit calls (integer channels)
    for mm in re.finditer(
            r'Hw32xSetBGColor\(\s*([A-Za-z0-9_ +]+?)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)', body):
        idx = _resolve(mm.group(1), d)
        if idx is not None and 0 <= idx < 256:
            pal[idx] = [int(mm.group(2)), int(mm.group(3)), int(mm.group(4))]

    # MIX ramp loops: Hw32xSetBGColor(BASE + i, MIX(br,FOG_R,i), MIX(bg,FOG_G,i), MIX(bb,FOG_B,i))
    for mm in re.finditer(
            r'Hw32xSetBGColor\(\s*(\w+)\s*\+\s*i,\s*'
            r'MIX\((\d+),\s*FOG_R,\s*i\),\s*MIX\((\d+),\s*FOG_G,\s*i\),\s*MIX\((\d+),\s*FOG_B,\s*i\)\)', body):
        base = d.get(mm.group(1))
        if base is None:
            continue
        br, bg, bb = int(mm.group(2)), int(mm.group(3)), int(mm.group(4))
        for i in range(SL):
            pal[base + i] = [(br * (SL - i) + FOG[0] * i) // SL,
                             (bg * (SL - i) + FOG[1] * i) // SL,
                             (bb * (SL - i) + FOG[2] * i) // SL]

    # 32X CRAM is 5-bit/channel -> scale to 8-bit
    return [[min(255, c * 255 // 31) for c in rgb] for rgb in pal]


def parse_tex(path):
    """Parse a *_tex.h header -> {w,h,data} (flat, x-major: data[x*h+y]).
    These textures are column-major [WIDTH][HEIGHT] of small values (shade
    offsets into a base ramp / dot density), so the flat C order is x-major."""
    src = open(path).read()
    w = int(re.search(r'(\w+)_WIDTH\s+(\d+)', src).group(2))
    h = int(re.search(r'(\w+)_HEIGHT\s+(\d+)', src).group(2))
    body = re.search(r'=\s*\{(.*)\}\s*;', src, re.S).group(1)
    data = [int(x) for x in re.findall(r'\d+', body)][:w * h]
    return {"w": w, "h": h, "data": data}


def _tex_raw(path):
    src = open(path).read()
    w = int(re.search(r'(\w+)_WIDTH\s+(\d+)', src).group(2))
    h = int(re.search(r'(\w+)_HEIGHT\s+(\d+)', src).group(2))
    body = re.search(r'=\s*\{(.*)\}\s*;', src, re.S).group(1)
    return w, h, [int(x) for x in re.findall(r'\d+', body)][:w * h]


def decode_sprite(path, order, decode):
    """-> {w,h,px} where px is ROW-MAJOR palette indices, -1 = transparent.
    order 'WH' = column-major source [x*h+y]; 'HW' = row-major [y*w+x]."""
    w, h, data = _tex_raw(path)
    px = [0] * (w * h)
    for y in range(h):
        for x in range(w):
            v = data[x * h + y] if order == "WH" else data[y * w + x]
            px[y * w + x] = decode(v)
    return {"w": w, "h": h, "px": px}


def build_assets(repo_root=ROOT):
    sh = os.path.join(repo_root, "sh_src")
    src = open(os.path.join(sh, "raycast.c")).read()
    d = _defines(src)
    textures = {}
    for fname, key, base in [("wall_tex.h", "wall", "WALL_BASE"),
                             ("partition_tex.h", "partition", "PARTITION_BASE")]:
        p = os.path.join(sh, fname)
        if os.path.exists(p):
            t = parse_tex(p)
            t["base"] = d.get(base, 1)
            textures[key] = t

    OUT, DOOR, STIP, HAND, FRAME, NEAN = (d.get(k) for k in
        ("OUTLET_BASE", "DOOR_BASE", "STIPPLE_BASE", "HANDLE_BASE", "FRAME_BASE", "NEANDER_BASE"))
    def dec_door(v):      # door_tex 0..18 -> palette (the dlut mapping), 0 = transparent
        if v == 0: return -1
        if v <= 8:  return DOOR + (v - 1)        # 1..5 body grey, 6..8 green/white EXIT
        if v <= 13: return STIP + (v - 9)        # stipple dapple
        if v <= 17: return HAND + (v - 14)       # bronze handle
        return FRAME + 4                          # lit jamb
    sprites = {}
    specs = [("outlet_tex.h", "outlet", "HW", lambda v: OUT + v),
             ("door_tex.h",   "door",   "WH", dec_door),
             ("neander_tex.h", "neander", "HW", lambda v: -1 if v == 0 else NEAN + v)]
    for fname, key, order, fn in specs:
        p = os.path.join(sh, fname)
        if os.path.exists(p):
            sprites[key] = decode_sprite(p, order, fn)

    return {
        "palette": build_palette(src),
        "bases": {k: d[k] for k in WANT_BASES if k in d},
        "textures": textures,
        "sprites": sprites,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=ROOT)
    ap.add_argument("--out", default=os.path.join(ROOT, "tools", "map-editor", "static", "assets.json"))
    args = ap.parse_args()
    assets = build_assets(args.repo)
    with open(args.out, "w") as fh:
        json.dump(assets, fh)
    print("export_assets: %d palette entries, bases=%s -> %s" %
          (len(assets["palette"]), ",".join(assets["bases"]), args.out))


if __name__ == "__main__":
    main()
