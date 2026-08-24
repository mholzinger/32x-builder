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


def parse_map(path, errs):
    """Parse one .map FILE, or record a readable error and return None.

    A contributor's first failure is usually 'this isn't a .map at all' (a
    description typed into the GitHub new-file box, say), so say that plainly
    instead of leaking a parser error about header keys."""
    base = os.path.relpath(path, ROOT)
    try:
        text = open(path).read()
    except OSError as ex:
        errs.append("%s: cannot read (%s)" % (base, ex)); return None
    # An empty or whitespace-only file is the signature of the editor's submit
    # dropping the map content (the large-map fallback used to open an empty
    # GitHub editor). Name that plainly instead of leaking a "missing 'name:'"
    # parser error, so the contributor knows to re-export and resubmit.
    if not text.strip():
        errs.append("%s: file is empty — the map content didn't make it into "
                    "the commit. Re-export the .map from the editor "
                    "(backrooms-32x-project.fly.dev) and resubmit; if you "
                    "pasted into the GitHub editor, make sure the paste landed "
                    "before you committed." % base)
        return None
    try:
        return mapfmt.parse(text)
    except mapfmt.MapFormatError as ex:
        if "[grid]" not in text:
            errs.append("%s: this doesn't look like a map file — it has no "
                        "[grid] section. Export a .map from the editor "
                        "(backrooms-32x-project.fly.dev) and commit that file. "
                        "(parser said: %s)" % (base, ex))
        else:
            errs.append("%s: %s" % (base, ex))
        return None


def _void_cells(m):
    """All void-exit ('X') cells as (x, y)."""
    g = m["grid"]; h = len(g); w = len(g[0]) if h else 0
    return [(x, y) for y in range(h) for x in range(w) if g[y][x] == 'X']

def _wall_voids(m):
    """Void cells that read as a MISSING WALL — on the map's outer edge, or with
    a wall next to them (a gap in a wall line). An 'X' floating in open floor is
    not that. The lobby's void (a gap in the playable-room wall) is wall-
    adjacent, so it qualifies; a stray 'X' in a room does not."""
    g = m["grid"]; h = len(g); w = len(g[0]) if h else 0
    out = []
    for (x, y) in _void_cells(m):
        if x == 0 or x == w - 1 or y == 0 or y == h - 1:
            out.append((x, y)); continue
        if any(0 <= x+dx < w and 0 <= y+dy < h and g[y+dy][x+dx] == '#'
               for dx, dy in ((1,0),(-1,0),(0,1),(0,-1))):
            out.append((x, y))
    return out


def lint_model(m, base, folder, reg, seen_names, errs):
    """Lint a parsed map MODEL. `base` labels the errors; `folder` is where the
    map lives (or will live — 'community' for editor submissions)."""
    def e(msg): errs.append("%s: %s" % (base, msg))
    glyphs, roles, lim = reg["cells"]["glyphs"], reg.get("roles", {}), reg["limits"]

    role = m.get("role", "community")
    if role not in roles:
        e("unknown role %r (valid: %s)" % (role, ", ".join(sorted(roles))))
    else:
        want = roles[role].get("folder")
        if want and folder != want:
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
                   ("max_crawl_runs", "crawls"), ("max_lights", "lights"),
                   ("max_dark_rooms", "dark")):
        if len(m[k]) > lim[cap]:
            e("%d %s exceed %s %d" % (len(m[k]), k, cap, lim[cap]))
    # Chairs are their own budget: each live-3D chair costs real per-frame time
    # and the frame is vblank-locked, so the cap is deliberately tight.
    n_chairs = sum(1 for d in m["decals"] if d.get("kind") == "chair")
    if "max_chairs" in lim and n_chairs > lim["max_chairs"]:
        e("%d chairs exceed max_chairs %d (far chairs are cheap directional "
          "billboards and only the nearest 3 render true-3D — the cap now "
          "tracks the engine's standup table, not per-chair render cost)"
          % (n_chairs, lim["max_chairs"]))
    # Desks get their own budget alongside chairs. A desk is CHEAPER to draw than
    # a chair (3 boxes / 18 faces vs 9 / 54) but it is physically much bigger, so
    # the cost that matters is screen fill, and its directional billboard set is
    # the widest asset in ROM — it alone eats 3280 of the 4096-byte shared decode
    # scratch. The cap is deliberately below what the frame could take so the
    # budget stays: 8 desks + 21 chairs + 3 neanderthals = 32, leaving 4 of
    # max_standups 36 spare for the next imported asset.
    n_desks = sum(1 for d in m["decals"] if d.get("kind") == "desk")
    if "max_desks" in lim and n_desks > lim["max_desks"]:
        e("%d desks exceed max_desks %d (desks share the engine's %d-slot "
          "standup table with chairs and neanderthals; the budget leaves 4 "
          "slots spare for the next imported asset)"
          % (n_desks, lim["max_desks"], lim.get("max_standups", 36)))
    # The PVM cart is the import those 4 spare slots were reserved for
    # (6 boxes / 36 faces — carved lighter than the chair after the first
    # 11-box cut showed on the frame counters). The cap stays at the
    # spare-slot count rather than growing the standup table: 8 desks +
    # 21 chairs + 3 neanderthals + 4 PVMs = max_standups 36.
    n_pvms = sum(1 for d in m["decals"] if d.get("kind") == "pvm")
    if "max_pvms" in lim and n_pvms > lim["max_pvms"]:
        e("%d PVMs exceed max_pvms %d (PVM carts fill the last %d spare "
          "slots of the engine's %d-slot standup table)"
          % (n_pvms, lim["max_pvms"], lim["max_pvms"],
             lim.get("max_standups", 36)))
    # EVERY free-standing object shares one engine array (raycast.c MAX_STANDUPS),
    # and the loader fills it in decal order then silently drops the rest. The
    # testbed lost all 7 of its desks that way: 3 neanderthals + 21 chairs filled
    # the old 24 slots exactly, so the tail vanished with no error anywhere. Count
    # the standalone kinds against the engine cap, not just chairs.
    standalone_ids = {k["id"] for k in reg["decals"]["kinds"] if k.get("standalone")}
    n_standup = sum(1 for d in m["decals"] if d.get("kind") in standalone_ids)
    if "max_standups" in lim and n_standup > lim["max_standups"]:
        e("%d free-standing objects exceed max_standups %d — the engine's "
          "standup table is a hard cap and the loader DROPS the overflow "
          "silently (raise MAX_STANDUPS in raycast.c and this limit together)"
          % (n_standup, lim["max_standups"]))
    edge_total = sum(int(abs(p["x2"] - p["x1"]) + abs(p["y2"] - p["y1"]))
                     for p in m["partitions"])
    # A void exit is a MISSING WALL cell on the border: an opening you walk out
    # through, floor and ceiling running past it into the expanse. One floating
    # in the middle of a room is not that. (For a dark room, use Dark Rooms.)
    interior = [c for c in _void_cells(m) if c not in _wall_voids(m)]
    if interior:
        e("void exit cell%s at %s must be on the map border (a missing wall "
          "cell you walk out through), not floating in a room. For a dark area "
          "use a Dark Room instead."
          % ("s" if len(interior) > 1 else "",
             ", ".join("(%d,%d)" % c for c in interior[:5])))

    edge_cap = lim.get("max_partition_edges", 255)
    if edge_total > edge_cap:
        e("partitions rasterize to %d cell-edges (max %d per map — n_pedges is "
          "a uint8_t). Long runs cost edges even when the segment count is low."
          % (edge_total, edge_cap))

    sp = m["spawn"]; sx, sy = int(sp["x"]), int(sp["y"])
    if _cell(m, glyphs, sx, sy) != 0:
        e("spawn (%g,%g) is not on open floor" % (sp["x"], sp["y"]))

    for p in m["partitions"]:
        for cx, cy in ((p["x1"], p["y1"]), (p["x2"], p["y2"])):
            if not (0 <= cx <= m["w"] and 0 <= cy <= m["h"]):
                e("partition endpoint (%g,%g) out of bounds" % (cx, cy))
        if str(p.get("crawl", "no")).lower() == "yes":
            e("crawl-under beams are RETIRED (no map ever shipped one; crawl "
              "gameplay lives in the [crawl] duct system) — use height=low/half "
              "or a duct instead")
        # First-class (edge) partitions: integer, axis-aligned segments only.
        if any(v != int(v) for v in (p["x1"], p["y1"], p["x2"], p["y2"])):
            e("partition (%g,%g)->(%g,%g) has fractional endpoints — partitions "
              "sit on cell edges (whole-number coordinates)"
              % (p["x1"], p["y1"], p["x2"], p["y2"]))
        elif p["x1"] != p["x2"] and p["y1"] != p["y2"]:
            e("partition (%g,%g)->(%g,%g) is diagonal — partitions are axis-aligned"
              % (p["x1"], p["y1"], p["x2"], p["y2"]))

    # Every decal must name an asset THIS REPO has, and a map that ships in the
    # flagship ROM (core/curated tiers) may only use first-party assets. The
    # editor lets an author bake a sprite and place it in the same session, so a
    # submitted map can reference an asset whose own PR never landed — that map
    # used to pass this lint and then break the ROM build with a codegen error
    # nobody but the maintainer ever saw.
    kind_ids = {k["id"] for k in reg.get("decals", {}).get("kinds", [])}
    sprite_tier = {s["id"]: s.get("tier", "core")
                   for s in reg.get("assets", {}).get("sprites", [])}
    map_tier = roles.get(role, {}).get("tier", "community")
    for kd in sorted({d.get("kind") for d in m["decals"]}):   # once per KIND, not per decal
        if kd not in kind_ids:
            e("decal kind %r is not in the game yet — if you baked it in the "
              "editor, submit the sprite first and place it once that PR is "
              "merged" % kd)
        elif map_tier != "community" and sprite_tier.get(kd) == "community":
            e("%s is a community asset, so it can't be used by a %s map "
              "(community assets ship only in the community ROM)" % (kd, map_tier))

    standalone_ids = {k["id"] for k in reg.get("decals", {}).get("kinds", [])
                      if k.get("standalone")}
    for d in m["decals"]:
        if d.get("kind") in standalone_ids:     # free-standing, not wall-mounted
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

    # Story chain (next:): the exit door IS the transition, so a chained map
    # must have one; a map can't chain to itself. Dangling targets + cycles are
    # cross-map properties checked in lint_all / the editor submit gate.
    nxt = (m.get("next") or "").strip()
    if nxt:
        has_door = (any(d.get("kind") == "door" for d in m["decals"])
                    or m["options"].get("place_exit_door"))
        if not (has_door or _wall_voids(m)):
            e("next: %s needs an exit the player can reach — an exit door, "
              "place_exit_door: 1, or a void opening on the border (a missing "
              "wall cell you walk out through)." % nxt)
        if nxt.upper() == (m.get("name") or "").upper():
            e("next: points at itself")


ARENA_BASE, ARENA_END, ARENA_STRIDE = 144, 256, 8


def arena_usage(sprites):
    """(used, capacity) sprite slots in the community CRAM arena."""
    used = [s for s in sprites if isinstance(s.get("base"), int)]
    return len(used), (ARENA_END - ARENA_BASE) // ARENA_STRIDE


def _lint_palette_arena(sprites, errs):
    """The community palette arena is the REAL ceiling on a shared ROM, and it
    is much lower than ROM size suggests: 14 sprite slots, 8 CRAM entries each.
    One contributor's set of decals took four. A shared "everybody's work" ROM
    therefore fills after a handful of people — which is exactly why the editor
    points contributors at their own fork, where all 14 slots are theirs.

    Without this check the wall arrives as a cryptic codegen error on whoever's
    PR happens to be the straw, or worse, as sprites rendering in each other's
    colours."""
    used, cap = arena_usage(sprites)
    if used > cap:
        errs.append(
            "assets: %d sprites with their own palette, but the community CRAM "
            "arena holds %d (%d..%d, %d entries each). A shared ROM cannot take "
            "more — promote a sprite to a first-party ramp, drop one, or let "
            "the author keep it in their own fork where the whole arena is "
            "theirs." % (used, cap, ARENA_BASE, ARENA_END - 1, ARENA_STRIDE))
    seen = {}
    for s in sprites:
        b = s.get("base")
        if not isinstance(b, int):
            continue
        if b in seen:
            errs.append("assets: %s and %s share palette base %d — their colours "
                        "would overwrite each other" % (seen[b], s.get("id", "?"), b))
        seen[b] = s.get("id", "?")
        if b < ARENA_BASE or b + ARENA_STRIDE > ARENA_END:
            errs.append("assets: %s base %d is outside the community arena %d..%d"
                        % (s.get("id", "?"), b, ARENA_BASE, ARENA_END - 1))


def lint_assets(reg, sh_dir, errs):
    try:
        export_assets.build_assets(ROOT)        # resolves palette + registry + sprites
    except Exception as ex:
        errs.append("assets: build_assets failed: %s" % ex); return

    # The sprite ASSET CATALOG (registry "assets") drives the codegen'd
    # sprite_defs[] table — validate every entry + that its _tex.h exist and are
    # well-formed, so a bad asset fails the build/PR (same gate as maps).
    sprites = reg.get("assets", {}).get("sprites", [])
    _lint_palette_arena(sprites, errs)
    kinds_seen, tex_files = {}, set(["wall_tex.h", "partition_tex.h"])
    for s in sprites:
        sid = s.get("id", "?")
        for k in ("id", "kind", "mount", "sym", "base", "decode", "tex"):
            if k not in s:
                errs.append("assets: sprite %s missing %r" % (sid, k))
        if s.get("mount") not in (None, "wall", "billboard"):
            errs.append("assets: sprite %s bad mount %r" % (sid, s["mount"]))
        if s.get("decode") not in (None, "offset", "door"):
            errs.append("assets: sprite %s bad decode %r" % (sid, s["decode"]))
        for f in s.get("flags", []):
            if f not in ("animated", "lod", "standalone", "colmajor", "artpal"):
                errs.append("assets: sprite %s bad flag %r" % (sid, f))
        if "kind" in s:
            if s["kind"] in kinds_seen:
                errs.append("assets: kind %d shared by %s and %s"
                            % (s["kind"], kinds_seen[s["kind"]], sid))
            kinds_seen[s["kind"]] = sid
        for t in (s.get("tex"), s.get("tex_hi")):
            if t:
                tex_files.add(t)

    for fn in sorted(tex_files):
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
    # Parse ONCE, up front. This used to parse every map three times (here, the
    # story-chain scan, the pickable count) and the last one was unguarded — so
    # a single unparseable file crashed the whole lint with a traceback AFTER
    # the readable error had already been recorded. Contributors saw a Python
    # stack dump instead of "your file isn't a map". Files that fail to parse
    # are reported and then skipped by everything downstream.
    models = {}
    for path in paths:
        m = parse_map(path, errs)
        if m is not None:
            models[path] = m
    for path, m in models.items():
        lint_model(m, os.path.relpath(path, ROOT),
                   os.path.basename(os.path.dirname(path)), reg, seen, errs)
    # Story chains across the whole tree: every next: target must exist, and
    # following the links must never loop (stories END; procgen is the infinite).
    nxt_of, file_of = {}, {}
    for p, mm in models.items():
        nm = (mm.get("name") or "").upper()
        file_of[nm] = os.path.relpath(p, ROOT)
        if (mm.get("next") or "").strip():
            nxt_of[nm] = mm["next"].strip().upper()
    for nm, tgt in sorted(nxt_of.items()):
        if tgt not in file_of:
            errs.append("%s: next: %r does not name any map in the tree"
                        % (file_of.get(nm, nm), tgt))
    for start in sorted(nxt_of):
        seen_chain, cur = set(), start
        while cur in nxt_of:
            if cur in seen_chain:
                errs.append("%s: story chain loops (%s -> ... -> %s)"
                            % (file_of.get(start, start), start, cur))
                break
            seen_chain.add(cur)
            cur = nxt_of[cur]

    pickable = sum(1 for m in models.values()
                   if reg.get("roles", {}).get(m.get("role", "community"), {}).get("picker"))
    if models and pickable == 0:
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
