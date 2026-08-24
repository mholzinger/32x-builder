#!/usr/bin/env python3
"""Codegen: maps/*.map (+ registry.json) -> sh_src/custom_maps.c

Parses each .map via the shared format module (tools/mapfmt.py), resolves the
human values to engine numbers using registry.json, validates the caps, and
emits one C file of POD descriptors (custom_maps[]) that compiles into the ROM.
The C side (raycast_load_custom in raycast.c) replays each descriptor.

The Makefile runs this on any .map change. Run by hand with:
    python3 tools/gen_maps.py
    python3 tools/gen_maps.py --maps maps --registry registry.json --out sh_src/custom_maps.c
"""
import argparse, glob, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import mapfmt      # noqa: E402  (shared .map syntax)
import lint_maps   # noqa: E402  (build gate: maps + assets)


def die(msg):
    sys.stderr.write("gen_maps: error: %s\n" % msg)
    sys.exit(1)


def fxlit(v):
    """Readable compile-time FX() literal from a number."""
    s = ("%.4f" % float(v)).rstrip("0").rstrip(".")
    return "FX(%s)" % s


def resolve(path, reg):
    """Load a .map and resolve it to the numeric dict the emitter wants."""
    base = os.path.basename(path)
    try:
        m = mapfmt.parse(open(path).read())
    except mapfmt.MapFormatError as e:
        die("%s: %s" % (base, e))

    glyphs = reg["cells"]["glyphs"]
    facing = reg["facing"]
    pstyle, pheight, pcrawl = (reg["partition"][k] for k in ("style", "height", "crawl"))
    cdir = reg["crawl"]["dir"]
    face_axis = reg["decals"]["face_axis"]
    kinds = {k["id"]: k for k in reg["decals"]["kinds"]}
    lim = reg["limits"]
    w, h = m["w"], m["h"]

    if w > lim["max_dim"] or h > lim["max_dim"]:
        die("%s: size %dx%d exceeds max_dim %d" % (base, w, h, lim["max_dim"]))
    if len(m["grid"]) != h:
        die("%s: grid has %d rows, size says %d" % (base, len(m["grid"]), h))
    cells = []
    for ri, row in enumerate(m["grid"]):
        if len(row) != w:
            die("%s: grid row %d is %d wide, size says %d" % (base, ri, len(row), w))
        for ch in row:
            if ch not in glyphs:
                die("%s: grid row %d has unknown glyph %r" % (base, ri, ch))
            cells.append(glyphs[ch])

    sp = m["spawn"]
    if sp["facing"] not in facing:
        die("%s: unknown facing %r" % (base, sp["facing"]))
    spawn = (sp["x"], sp["y"], facing[sp["facing"]])

    parts = []
    pedges = []
    hclass = {0: 0, 192: 1, 96: 2}          # height value -> edge height class
    for p in m["partitions"]:
        for enum, table, what in ((p["style"], pstyle, "style"),
                                  (p["height"], pheight, "height"),
                                  (p["crawl"], pcrawl, "crawl")):
            if enum not in table:
                die("%s: unknown partition %s %r" % (base, what, enum))
        parts.append((p["x1"], p["y1"], p["x2"], p["y2"],
                      pstyle[p["style"]], pheight[p["height"]], pcrawl[p["crawl"]]))
        # Rasterize to cell edges (the first-class model): integer, axis-
        # aligned segments only — which is every partition ever authored.
        x1, y1, x2, y2 = p["x1"], p["y1"], p["x2"], p["y2"]
        if any(v != int(v) for v in (x1, y1, x2, y2)):
            die("%s: partition (%g,%g)->(%g,%g) has fractional endpoints — "
                "partitions live on cell edges (integer coordinates)"
                % (base, x1, y1, x2, y2))
        x1, y1, x2, y2 = int(x1), int(y1), int(x2), int(y2)
        if x1 != x2 and y1 != y2:
            die("%s: partition (%d,%d)->(%d,%d) is diagonal — partitions are "
                "axis-aligned" % (base, x1, y1, x2, y2))
        hv = pheight[p["height"]]
        if hv not in hclass:
            die("%s: partition height value %d has no edge class" % (base, hv))
        flags = 0x80 | (0x01 if pstyle[p["style"]] else 0) | (hclass[hv] << 1) \
              | (0x08 if pcrawl[p["crawl"]] else 0)
        # Collinear-wall flush: if the run shares its line with a wall face,
        # shift the slab so the faces align (0x20 = slab on the negative side,
        # face on the line; 0x10 = positive side). Mirrors the engine stamper.
        # Scan the run's own cells AND one continuation cell past each end:
        # a wall on exactly one side of the line means its face is collinear
        # with the run — flush the slab to that face. A wall on BOTH sides at
        # a continuation cell is a perpendicular tee (stays centered).
        if x1 == x2:                          # vertical: WEST edges on line x
            ww = we = False
            ya, yb = min(y1, y2), max(y1, y2)
            for yy in range(ya - 1, yb + 1):
                if not (0 <= yy < h): continue
                cw = x1 > 0 and cells[yy * w + (x1 - 1)] != 0
                ce = x1 < w and cells[yy * w + x1] != 0
                if cw and ce: continue        # tee / wall both sides
                if cw: ww = True
                if ce: we = True
            # slab goes on the side OPPOSITE the wall, face on the line
            if ww and not we:   flags |= 0x10   # wall west -> slab east
            elif we and not ww: flags |= 0x20   # wall east -> slab west
            for yy in range(ya, yb):
                pedges.append((x1, yy, flags))
        else:                                 # horizontal: NORTH edges on line y
            wn = ws = False
            xa, xb = min(x1, x2), max(x1, x2)
            for xx in range(xa - 1, xb + 1):
                if not (0 <= xx < w): continue
                cn = y1 > 0 and cells[(y1 - 1) * w + xx] != 0
                cs = y1 < h and cells[y1 * w + xx] != 0
                if cn and cs: continue
                if cn: wn = True
                if cs: ws = True
            if wn and not ws:   flags |= 0x10   # wall north -> slab south
            elif ws and not wn: flags |= 0x20   # wall south -> slab north
            for xx in range(xa, xb):
                pedges.append((xx, y1, flags | 0x40))

    decals = []
    for d in m["decals"]:
        if d["kind"] not in kinds:
            die("%s: unknown decal kind %r (add it to registry.json)" % (base, d["kind"]))
        if d["face"] not in face_axis:
            die("%s: unknown face %r" % (base, d["face"]))
        z = d.get("z", kinds[d["kind"]]["z"])
        # A free-standing billboard has no wall to align to, so `axis` is
        # meaningless for it — carry the real facing angle instead.
        decals.append((d["x"], d["y"], z, face_axis[d["face"]],
                       kinds[d["kind"]]["kind"], facing[d["face"]]))

    crawls = []
    ch = reg["crawl"]["h"]
    for c in m["crawls"]:
        if c["dir"] not in cdir:
            die("%s: unknown dir %r" % (base, c["dir"]))
        hname = c.get("h", "crawl")
        if hname not in ch:
            die("%s: unknown crawl h %r (named heights: %s)"
                % (base, hname, "/".join(sorted(ch))))
        dx, dy = cdir[c["dir"]]
        crawls.append((c["cx"], c["cy"], dx, dy, c["len"], ch[hname]))

    # Authored ceiling fixtures. Empty list => the engine falls back to its
    # procedural grid (init_lights), which is what every map did before this.
    lights = []
    for g_ in m["lights"]:
        cx, cy = int(g_["cx"]), int(g_["cy"])
        if not (0 <= cx < w and 0 <= cy < h):
            die("%s: light %d,%d is outside the %dx%d grid" % (base, cx, cy, w, h))
        lights.append((cx, cy))

    # Dark rooms: unlit rects. Empty => every cell lit as usual.
    dark = []
    for d in m["dark"]:
        for k in ("x0", "y0", "x1", "y1"):
            if not (0 <= d[k] < (w if k[0] == "x" else h)):
                die("%s: dark room %s=%d is outside the %dx%d grid" % (base, k, d[k], w, h))
        dark.append((d["x0"], d["y0"], d["x1"], d["y1"]))

    for cap, lst in (("max_partitions", parts), ("max_decals", decals),
                     ("max_crawl_runs", crawls), ("max_lights", lights),
                     ("max_dark_rooms", dark)):
        if len(lst) > lim[cap]:
            die("%s: %d items exceed %s %d" % (base, len(lst), cap, lim[cap]))
    if len(pedges) > lim.get("max_partition_edges", 255):
        die("%s: partitions rasterize to %d cell-edges (max %d per map — "
            "n_pedges is a uint8_t)"
            % (base, len(pedges), lim.get("max_partition_edges", 255)))

    roles = reg.get("roles", {})
    role = m.get("role", "community")
    if role not in roles:
        die("%s: unknown role %r (valid: %s)" % (base, role, ", ".join(sorted(roles))))
    # Transitive flush: runs sharing a LINE align with any flushed run on it.
    # (The lobby spotted run tees into a 2-thick wall — both-sided, so its own
    # scan stays centered — but it is collinear with the flushed chevron run;
    # without propagation the two faces jog 0.05 at the gap and the spotted
    # seam misses the wall corner.)
    lineflush = {}
    for (ex, ey, fl) in pedges:
        key = ("n", ey) if (fl & 0x40) else ("w", ex)
        if fl & 0x30:
            lineflush[key] = fl & 0x30
    if lineflush:
        pedges = [
            (ex, ey,
             (fl | lineflush.get(("n", ey) if (fl & 0x40) else ("w", ex), 0))
             if not (fl & 0x30) else fl)
            for (ex, ey, fl) in pedges
        ]
    raw_author = (m.get("author") or "").strip()
    author = reg.get("author_aliases", {}).get(raw_author.lower(), raw_author)
    if author.startswith("_"):        # skip the _doc key if it ever collides
        author = raw_author
    return {"name": m["name"], "author": author,
            "w": w, "h": h, "cells": cells, "parts": parts,
            "pedges": pedges,
            "next": (m.get("next") or "").strip(),
            "decals": decals, "crawls": crawls, "lights": lights, "dark": dark, "spawn": spawn,
            "lobby_ceiling": m["options"]["lobby_ceiling"],
            "place_outlets": m["options"]["place_outlets"],
            "place_exit_door": m["options"]["place_exit_door"],
            "role": role, "priority": roles[role]["priority"], "picker": roles[role]["picker"],
            "folder": roles[role]["folder"],
            "tier": roles[role].get("tier", "community"),
            "author_key": author.lower()}


# ---------------- build profiles (which tiers land in which ROM) -------------
#
# One repo, three ROMs. The flagship ships core + curated (canon plus the maps
# by project authors and the outside maps the maintainer promoted). The
# community ROM ships everything. An author profile ships the flagship PLUS
# that one contributor's own maps — their personal build, so a first-time
# mapper gets a ROM with their level in it without their work landing in the
# game everyone downloads.
PROFILE_DOC = "core (flagship) | community (everything) | author:<handle>"


def profile_tiers(profile):
    if profile in ("core", "flagship"):
        return {"core", "curated"}, None
    if profile in ("community", "all"):
        return {"core", "curated", "community"}, None
    if profile.startswith("author:"):
        who = profile.split(":", 1)[1].strip().lower()
        if not who:
            die("--profile author: needs a handle, e.g. author:doublek")
        return {"core", "curated"}, who
    die("unknown --profile %r (valid: %s)" % (profile, PROFILE_DOC))


def select(maps, profile):
    """Filter resolved maps down to the ones this profile ships, and label the
    build. The label is emitted into custom_maps.c (so it costs no Makefile
    plumbing) and shown on the title/CREDITS screen — a community ROM should
    never be mistaken for the flagship."""
    tiers, who = profile_tiers(profile)
    kept = [m for m in maps
            if m["tier"] in tiers or (who and m["author_key"] == who)]
    if who:
        mine = [m for m in kept if m["tier"] == "community"]
        if not mine:
            die("no community maps by author %r (checked the author: field, "
                "folded through registry author_aliases)" % who)
        label = "BUILD FOR %s" % mine[0]["author"].upper()[:20]
    elif "community" in tiers:
        label = "COMMUNITY BUILD"
    else:
        label = ""
    return kept, label


def emit(maps, out_path, label=""):
    L = ['/* AUTO-GENERATED by tools/gen_maps.py from the maps directory.',
         ' * Do NOT edit by hand. Re-run the build to regenerate. */',
         '#include "custom_maps.h"', '']
    total = 0
    for i, m in enumerate(maps):
        p = "map%02d" % i
        L.append("static const uint8_t %s_grid[] = {" % p)
        for r in range(m["h"]):
            row = m["cells"][r * m["w"]:(r + 1) * m["w"]]
            L.append("    " + ",".join(str(c) for c in row) + ",")
        L.append("};")
        total += m["w"] * m["h"]
        if m["pedges"]:
            L.append("static const cm_pedge_t %s_pedges[] = {" % p)
            for (ex, ey, ef) in m["pedges"]:
                L.append("    { %d,%d,0x%02x }," % (ex, ey, ef))
            L.append("};")
        if m["decals"]:
            L.append("static const cm_decal_t %s_decals[] = {" % p)
            for (x, y, z, ax, kd, fa) in m["decals"]:
                L.append("    { %s,%s,%s, %d,%d,%d }," % (fxlit(x), fxlit(y), fxlit(z), ax, kd, fa))
            L.append("};")
        if m["crawls"]:
            L.append("static const cm_crawl_t %s_crawls[] = {" % p)
            for (cx, cy, dx, dy, ln, hv) in m["crawls"]:
                L.append("    { %d,%d,%d,%d,%d,%d }," % (cx, cy, dx, dy, ln, hv))  # dx,dy signed
            L.append("};")
        if m["dark"]:
            L.append("static const cm_dark_t %s_dark[] = {" % p)
            for (x0, y0, x1, y1) in m["dark"]:
                L.append("    { %d,%d,%d,%d }," % (x0, y0, x1, y1))
            L.append("};")
        if m["lights"]:
            L.append("static const cm_light_t %s_lights[] = {" % p)
            for (cx, cy) in m["lights"]:
                L.append("    { %d,%d }," % (cx, cy))
            L.append("};")
        L.append("")
    if maps:
        L.append("const custom_map_t custom_maps[] = {")
        for i, m in enumerate(maps):
            p = "map%02d" % i
            sx, sy, sa = m["spawn"]
            pedges = ("%s_pedges,%d" % (p, len(m["pedges"]))) if m["pedges"] else "0,0"
            decals = ("%s_decals,%d" % (p, len(m["decals"]))) if m["decals"] else "0,0"
            crawls = ("%s_crawls,%d" % (p, len(m["crawls"]))) if m["crawls"] else "0,0"
            lights = ("%s_lights,%d" % (p, len(m["lights"]))) if m["lights"] else "0,0"
            dark   = ("%s_dark,%d"   % (p, len(m["dark"])))   if m["dark"]   else "0,0"
            L.append('    { "%s", "%s", %d,%d, %s_grid, %s, %s, %s, %s, %s, %s,%s,%d, %d,%d,%d, %d },' %
                     (m["name"][:16], m["author"][:20], m["w"], m["h"], p, pedges, decals, crawls, lights, dark,
                      fxlit(sx), fxlit(sy), sa,
                      m["lobby_ceiling"], m["place_outlets"], m["place_exit_door"],
                      m["next_idx"]))
        L.append("};")
        L.append("const int custom_map_count = (int)(sizeof custom_maps / sizeof custom_maps[0]);")
        # pickable maps are ordered first (by role priority), so the in-game
        # picker just bounds on this; the lobby sits past it (not selectable).
        L.append("const int custom_pick_count = %d;" % sum(1 for m in maps if m.get("picker")))
        # Role-priority ordering gives the start menu clean index boundaries —
        # one contiguous block per TIER, so the engine just bounds on counts:
        #   [0, start)          starter maps          ("-- START MAPS --")
        #   [start, core)       play + test maps      ("-- TEST --")
        #   [core, curated)     curated maps          ("-- MAPS --" / "-- STORIES --")
        #   [curated, pick)     community maps        ("-- COMMUNITY --")
        L.append("const int custom_start_count = %d;" %
                 sum(1 for m in maps if m.get("picker") and m["role"] == "starter"))
        L.append("const int custom_core_count = %d;" %
                 sum(1 for m in maps if m.get("picker") and m["tier"] == "core"))
        L.append("const int custom_curated_count = %d;" %
                 sum(1 for m in maps if m.get("picker") and m["tier"] == "curated"))
    else:
        L.append("/* no maps found */")
        L.append("const custom_map_t custom_maps[1] = {{0}};")
        L.append("const int custom_map_count = 0;")
        L.append("const int custom_pick_count = 0;")
        L.append("const int custom_start_count = 0;")
        L.append("const int custom_core_count = 0;")
        L.append("const int custom_curated_count = 0;")
    # Which ROM this is. "" on the flagship (nothing to announce); the start
    # menu and CREDITS print it, so a community or personal build says so on
    # screen instead of looking like the release everyone else is playing.
    L.append('const char custom_build_label[] = "%s";' % label.replace('"', ""))
    L.append("")
    text = "\n".join(L)

    old = None
    if os.path.exists(out_path):
        with open(out_path) as fh:
            old = fh.read()
    if old != text:
        with open(out_path, "w") as fh:
            fh.write(text)
    print("gen_maps: %d map(s), %d grid bytes -> %s%s" %
          (len(maps), total, out_path, "" if old != text else " (unchanged)"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--maps", default=os.path.join(ROOT, "maps"))
    ap.add_argument("--registry", default=os.path.join(ROOT, "registry.json"))
    ap.add_argument("--out", default=os.path.join(ROOT, "sh_src", "custom_maps.c"))
    ap.add_argument("--profile", default="core",
                    help="which ROM to generate: " + PROFILE_DOC)
    args = ap.parse_args()

    with open(args.registry) as fh:
        reg = json.load(fh)

    # Gate the build on the shared lint (maps + assets). The same tool runs in
    # CI on every PR; failing here keeps a broken map/asset out of the ROM.
    errs = lint_maps.lint_all(args.maps, reg, os.path.join(ROOT, "sh_src"))
    if errs:
        for x in errs:
            sys.stderr.write("gen_maps: lint: %s\n" % x)
        die("%d lint problem(s) — fix the map(s)/asset(s) above" % len(errs))

    files = glob.glob(os.path.join(args.maps, "**", "*.map"), recursive=True)
    every = [resolve(p, reg) for p in files]
    all_names = {m["name"].upper() for m in every}
    maps, label = select(every, args.profile)
    # order: by role priority (pickable first, lobby last), then name -> stable
    maps.sort(key=lambda m: (m["priority"], m["name"]))
    # Story chains: resolve each map's `next:` NAME to its post-sort index so
    # the exit door can jump straight to custom_maps[next_map] at runtime.
    by_name = {m["name"].upper(): i for i, m in enumerate(maps)}
    for m in maps:
        if m["next"]:
            key = m["next"].upper()
            if key not in all_names:
                die("%s: next: %r does not name a map in this repo" % (m["name"], m["next"]))
            tgt = by_name.get(key)
            if tgt is None:
                # The chain leaves this ROM (a curated map linking a community
                # chapter). Not an error — the exit falls back to procgen, and
                # the community/author build has the whole story.
                sys.stderr.write("gen_maps: %s: next: %r is not in the %s profile — "
                                 "its exit falls back to procedural\n"
                                 % (m["name"], m["next"], args.profile))
                m["next_idx"] = -1
                continue
            if maps[tgt] is m:
                die("%s: next: points at itself" % m["name"])
            m["next_idx"] = tgt
        else:
            m["next_idx"] = -1
    emit(maps, args.out, label)


if __name__ == "__main__":
    main()
