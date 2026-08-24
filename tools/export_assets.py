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
              "STIPPLE_BASE", "HANDLE_BASE", "FRAME_BASE", "WOODTOP_BASE",
              "COMM_BASE",
              "LOWCEIL_COLOR",
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


def _mood_ramps(src, d, pal, SL, FOG):
    """Reproduce raycast_pal_apply(): WALL/FLOOR/CEIL/LIGHT ramps derived from the
    baked g_anchor[] anchors through pal_effective() (warmth/sat) then the fog
    MIX, exactly as the ROM paints CRAM at full brightness (lvl == FADE_STEPS).
    Silently no-ops if the anchors can't be parsed — matches the pre-engine
    behavior rather than throwing."""
    m = re.search(r'g_anchor\[PSURF_N\]\[3\]\s*=\s*\{(.*?)\}\s*;', src, re.S)
    if not m:
        return
    trips = re.findall(r'\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}', m.group(1))
    if len(trips) < 4:
        return
    anchors = [[int(a), int(b), int(c)] for a, b, c in trips[:4]]

    def _first(pat, default):
        mm = re.search(pat, src)
        return int(mm.group(1)) if mm else default
    warmth = _first(r'g_pal_warmth\s*=\s*(-?\d+)', 0)
    sat    = _first(r'g_pal_sat\s*=\s*(-?\d+)', 100)

    def clamp5(v):
        return 0 if v < 0 else (31 if v > 31 else v)

    def effective(r, g, b):                       # pal_effective() at defaults
        luma = (r + g + b) // 3
        return (clamp5(luma + (r - luma) * sat // 100 + warmth),
                clamp5(luma + (g - luma) * sat // 100),
                clamp5(luma + (b - luma) * sat // 100 - warmth))

    def mix(bright, fog, i):                      # MIX(bright, fog, i)
        return (bright * (SL - i) + fog * i) // SL

    for surf, key in ((0, "WALL_BASE"), (1, "FLOOR_BASE"), (2, "CEIL_BASE")):
        base = d.get(key)
        if base is None:
            continue
        er, eg, eb = effective(*anchors[surf])
        for i in range(SL):
            pal[base + i] = [mix(er, FOG[0], i), mix(eg, FOG[1], i), mix(eb, FOG[2], i)]

    lbase = d.get("LIGHT_BASE")                    # 4 flicker states, lrat ratios
    if lbase is not None:
        lr, lg, lb = effective(*anchors[3])
        for i, rat in enumerate((100, 93, 79, 62)):
            pal[lbase + i] = [lr * rat // 100, lg * rat // 100, lb * rat // 100]


def build_palette(src, SRC_PATH_HACK=''):
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

    # The four MOOD ramps (wall/floor/ceiling/light) no longer live in
    # build_palette() — they're painted at runtime by raycast_pal_apply() from
    # the tunable g_anchor[] table (the COLOR tab). Reproduce them here at the
    # shipped warmth/sat, or the four biggest surfaces fall through to the [0,0,0]
    # default and the preview renders them pure BLACK (so dark rooms, which push
    # DARK_ROOM_SHADE deeper, read as a black screen instead of the 32X's faint
    # fog-lit grey). See raycast.c: pal_effective() + raycast_pal_apply().
    _mood_ramps(src, d, pal, SL, FOG)

    # Countertop wood ramp: Hw32xSetBGColor(WOODTOP_BASE + i, (R*(7-i)+FOG_R*i)/7, ...)
    # 8 entries, a distinct /7 blend the generic MIX loop above doesn't match.
    wm = re.search(
        r'Hw32xSetBGColor\(\s*WOODTOP_BASE\s*\+\s*i,\s*'
        r'\((\d+)\s*\*\s*\(7\s*-\s*i\)\s*\+\s*FOG_R\s*\*\s*i\)\s*/\s*7,\s*'
        r'\((\d+)\s*\*\s*\(7\s*-\s*i\)\s*\+\s*FOG_G\s*\*\s*i\)\s*/\s*7,\s*'
        r'\((\d+)\s*\*\s*\(7\s*-\s*i\)\s*\+\s*FOG_B\s*\*\s*i\)\s*/\s*7\)', body)
    wbase = d.get("WOODTOP_BASE")
    if wm and wbase is not None:
        wr, wg, wb = int(wm.group(1)), int(wm.group(2)), int(wm.group(3))
        for i in range(8):
            pal[wbase + i] = [(wr * (7 - i) + FOG[0] * i) // 7,
                              (wg * (7 - i) + FOG[1] * i) // 7,
                              (wb * (7 - i) + FOG[2] * i) // 7]

    # GENERIC linear ramp loops: Hw32xSetBGColor(BASE + i, (A*(N-i)+B*i)/N, ...)
    # with numeric endpoints — the COMM_BASE community ramp and any future
    # bright->dark sweep. (The WOODTOP/FOG blend keeps its own matcher above.)
    for mm in re.finditer(
            r'Hw32xSetBGColor\(\s*(\w+)\s*\+\s*i,\s*'
            r'\((\d+)\s*\*\s*\((\d+)\s*-\s*i\)\s*\+\s*(\d+)\s*\*\s*i\)\s*/\s*\d+,\s*'
            r'\((\d+)\s*\*\s*\(\d+\s*-\s*i\)\s*\+\s*(\d+)\s*\*\s*i\)\s*/\s*\d+,\s*'
            r'\((\d+)\s*\*\s*\(\d+\s*-\s*i\)\s*\+\s*(\d+)\s*\*\s*i\)\s*/\s*\d+\)', body):
        base = d.get(mm.group(1))
        if base is None:
            continue
        n = int(mm.group(3))
        a = (int(mm.group(2)), int(mm.group(5)), int(mm.group(7)))
        b = (int(mm.group(4)), int(mm.group(6)), int(mm.group(8)))
        for i in range(n + 1):
            if 0 <= base + i < 256:
                pal[base + i] = [(a[c] * (n - i) + b[c] * i) // n for c in range(3)]

    # Community sprite palettes: the generated comm_pal.h rows (idx, r, g, b)
    # painted verbatim by build_palette — parse them the same way.
    cp = os.path.join(os.path.dirname(SRC_PATH_HACK), "comm_pal.h")
    if os.path.exists(cp):
        for mm in re.finditer(r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}',
                              open(cp).read()):
            idx = int(mm.group(1))
            if 0 < idx < 256:
                pal[idx] = [int(mm.group(2)), int(mm.group(3)), int(mm.group(4))]

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

    # COMMUNITY sprites: registry-driven, so bake_sprite.py additions export
    # with no edits here (the hardcoded specs above are the four originals).
    # Offset decode against the sprite's ramp base; world dims ride along so
    # the editor walkthrough can billboard them at true scale.
    reg_p = os.path.join(repo_root, "registry.json")
    if os.path.exists(reg_p):
        reg = json.load(open(reg_p))
        for s in reg.get("assets", {}).get("sprites", []):
            sid = s["id"]
            if sid in ("outlet", "door", "neanderthal", "chair") or sid in sprites:
                continue
            base = s.get("base")
            if not isinstance(base, int):
                base = d.get(base or "")
            if base is None or s.get("decode") != "offset":
                continue
            p = os.path.join(sh, s["tex"])
            if not os.path.exists(p):
                continue
            off = 0 if isinstance(s.get("base"), int) else -1   # arena: base+v
            spr = decode_sprite(p, "HW",
                                lambda v, b=base, o=off: -1 if v == 0 else b + v + o)
            spr["world_h"]  = s.get("world_h", 1.0)
            spr["world_hw"] = s.get("world_hw", 0.25)
            sprites[sid] = spr

    return {
        "palette": build_palette(src, os.path.join(sh, "raycast.c")),
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
