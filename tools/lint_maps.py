#!/usr/bin/env python3
"""Lint maps + assets — the build + CI gate.

Validates every maps/**/*.map (structure, role↔folder, spawn-on-floor, decals on
walls, partitions in-bounds, exit reachable from spawn, unique names) and the
sh_src texture assets (well-formed + the palette/registry resolve). Exits
non-zero with clear `path: message` lines on any failure.

  python3 tools/lint_maps.py        # standalone (CI)
gen_maps imports lint_all() so a bad map/asset also fails the ROM build.
"""
import glob, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import mapfmt          # noqa: E402
import export_assets   # noqa: E402


def _cell(m, glyphs, x, y):
    if x < 0 or y < 0 or x >= m["w"] or y >= m["h"]:
        return 1
    row = m["grid"][y]
    return glyphs.get(row[x], 1) if x < len(row) else 1


def _on_partition(m, dx, dy, tol=0.35):
    """True if (dx,dy) lies on a free-standing partition segment — decals mount
    on these (e.g. the lobby outlet on the 5,6->7,6 chevron) as well as on grid
    wall faces. Partitions are axis-aligned in practice; handle the diagonal
    case with a point-segment distance for completeness."""
    for p in m["partitions"]:
        x1, y1, x2, y2 = p["x1"], p["y1"], p["x2"], p["y2"]
        if abs(y1 - y2) < 1e-6:                      # horizontal
            if abs(dy - y1) <= tol and min(x1, x2) - tol <= dx <= max(x1, x2) + tol:
                return True
        elif abs(x1 - x2) < 1e-6:                    # vertical
            if abs(dx - x1) <= tol and min(y1, y2) - tol <= dy <= max(y1, y2) + tol:
                return True
        else:                                        # diagonal: point-segment dist
            vx, vy = x2 - x1, y2 - y1
            t = max(0.0, min(1.0, ((dx - x1) * vx + (dy - y1) * vy) / (vx * vx + vy * vy)))
            cx, cy = x1 + t * vx, y1 + t * vy
            if (dx - cx) ** 2 + (dy - cy) ** 2 <= tol * tol:
                return True
    return False


def _reachable(m, glyphs, sx, sy):
    """Flood-fill open cells from (sx,sy) -> set of reachable (x,y)."""
    if _cell(m, glyphs, sx, sy) != 0:
        return set()
    seen, stack = {(sx, sy)}, [(sx, sy)]
    while stack:
        x, y = stack.pop()
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if (nx, ny) not in seen and _cell(m, glyphs, nx, ny) == 0:
                seen.add((nx, ny)); stack.append((nx, ny))
    return seen


def lint_map(path, reg, seen_names, errs):
    base = os.path.relpath(path, ROOT)
    def e(msg): errs.append("%s: %s" % (base, msg))
    try:
        m = mapfmt.parse(open(path).read())
    except mapfmt.MapFormatError as ex:
        e(str(ex)); return
    glyphs, roles, lim = reg["cells"]["glyphs"], reg.get("roles", {}), reg["limits"]

    role = m.get("role", "community")
    if role not in roles:
        e("unknown role %r (valid: %s)" % (role, ", ".join(sorted(roles))))
    else:
        want = roles[role].get("folder")
        if want and os.path.basename(os.path.dirname(path)) != want:
            e("role %r belongs in maps/%s/" % (role, want))

    name = (m.get("name") or "").strip()
    if not name:
        e("missing name:")
    elif len(name) > 16:
        e("name %r exceeds 16 chars" % name)
    key = name.upper()
    if key and key in seen_names:
        e("duplicate map name %r (also %s)" % (name, seen_names[key]))
    elif key:
        seen_names[key] = base

    if not (1 <= m["w"] <= lim["max_dim"] and 1 <= m["h"] <= lim["max_dim"]):
        e("size %dx%d out of range (1..%d)" % (m["w"], m["h"], lim["max_dim"]))
    if len(m["grid"]) != m["h"]:
        e("grid has %d rows, size says %d" % (len(m["grid"]), m["h"]))
    for ri, row in enumerate(m["grid"]):
        if len(row) != m["w"]:
            e("grid row %d width %d != %d" % (ri, len(row), m["w"]))
        for ch in set(row):
            if ch not in glyphs:
                e("grid row %d unknown glyph %r" % (ri, ch))

    for cap, k in (("max_partitions", "partitions"), ("max_decals", "decals"),
                   ("max_crawl_runs", "crawls")):
        if len(m[k]) > lim[cap]:
            e("%d %s exceed %s %d" % (len(m[k]), k, cap, lim[cap]))

    sp = m["spawn"]; sx, sy = int(sp["x"]), int(sp["y"])
    if _cell(m, glyphs, sx, sy) != 0:
        e("spawn (%g,%g) is not on open floor" % (sp["x"], sp["y"]))

    for p in m["partitions"]:
        for cx, cy in ((p["x1"], p["y1"]), (p["x2"], p["y2"])):
            if not (0 <= cx <= m["w"] and 0 <= cy <= m["h"]):
                e("partition endpoint (%g,%g) out of bounds" % (cx, cy))

    for d in m["decals"]:
        if d.get("kind") == "neanderthal":      # free-standing, not wall-mounted
            continue
        fx, fy = int(d["x"]), int(d["y"])
        adj = [(fx, fy), (fx - 1, fy)] if d.get("face") in ("W", "E") else [(fx, fy), (fx, fy - 1)]
        on_grid = any(_cell(m, glyphs, cx, cy) == 1 for cx, cy in adj)
        if not on_grid and not _on_partition(m, d["x"], d["y"]):
            e("decal %s at (%g,%g) is not on a wall or partition" % (d.get("kind"), d["x"], d["y"]))

    reach = _reachable(m, glyphs, sx, sy)
    if not reach:
        e("spawn is walled in (no reachable floor)")
    for d in m["decals"]:
        if d.get("kind") != "door":
            continue
        fx, fy = int(d["x"]), int(d["y"])
        adj = [(fx, fy), (fx - 1, fy), (fx, fy - 1), (fx, fy + 1), (fx + 1, fy)]
        if reach and not any((cx, cy) in reach for cx, cy in adj):
            e("exit door at (%g,%g) is unreachable from spawn" % (d["x"], d["y"]))


def lint_assets(reg, sh_dir, errs):
    try:
        export_assets.build_assets(ROOT)        # resolves palette + registry + sprites
    except Exception as ex:
        errs.append("assets: build_assets failed: %s" % ex); return
    for fn in ("wall_tex.h", "partition_tex.h", "outlet_tex.h", "door_tex.h", "neander_tex.h"):
        p = os.path.join(sh_dir, fn)
        if not os.path.exists(p):
            errs.append("assets: missing %s" % fn); continue
        try:
            w, h, data = export_assets._tex_raw(p)
        except Exception as ex:
            errs.append("assets: %s parse failed: %s" % (fn, ex)); continue
        if len(data) != w * h:
            errs.append("assets: %s has %d values, expected %dx%d=%d" % (fn, len(data), w, h, w * h))


def lint_all(maps_dir, reg, sh_dir):
    errs, seen = [], {}
    paths = sorted(glob.glob(os.path.join(maps_dir, "**", "*.map"), recursive=True))
    for path in paths:
        lint_map(path, reg, seen, errs)
    pickable = sum(1 for p in paths
                   if reg.get("roles", {}).get(mapfmt.parse(open(p).read()).get("role", "community"), {}).get("picker"))
    if paths and pickable == 0:
        errs.append("maps: no selectable (picker) map exists")
    lint_assets(reg, sh_dir, errs)
    return errs


def main():
    with open(os.path.join(ROOT, "registry.json")) as fh:
        reg = json.load(fh)
    errs = lint_all(os.path.join(ROOT, "maps"), reg, os.path.join(ROOT, "sh_src"))
    for x in errs:
        sys.stderr.write("lint: %s\n" % x)
    if errs:
        sys.stderr.write("lint: %d problem(s) — FAILED\n" % len(errs))
        sys.exit(1)
    print("lint: OK (maps + assets)")


if __name__ == "__main__":
    main()
