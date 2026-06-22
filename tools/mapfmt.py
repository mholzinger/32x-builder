"""Shared reader/writer for the .map level format.

The ONE place that knows the .map *syntax* (sections, line shapes, the ASCII
grid). Used by both tools/gen_maps.py (the ROM codegen) and tools/map-editor
(the web editor) so the two can never disagree about the format.

  parse(text)      -> model dict (JSON-friendly, human values kept verbatim:
                      glyph chars, enum NAMES like "spotted", floats)
  serialize(model) -> canonical .map text

Semantic resolution (glyph -> cell int, enum -> engine number, facing -> angle)
is NOT done here — that needs registry.json and lives in gen_maps.py. This
module is registry-agnostic on purpose, so the editor can round-trip a file it
doesn't fully "understand" yet (e.g. a new decal kind).
"""


class MapFormatError(Exception):
    pass


def _strip_comment(s):
    i = s.find("#")
    return s[:i] if i >= 0 else s


def _kv(tokens):
    """['a','k=v',...] -> (positional list, {k:v})."""
    pos, kw = [], {}
    for t in tokens:
        if "=" in t:
            k, _, v = t.partition("=")
            kw[k.strip()] = v.strip()
        elif t:
            pos.append(t)
    return pos, kw


def new_model(name="UNTITLED", w=16, h=16):
    """A blank model: all-wall border, open interior, centred south spawn."""
    grid = []
    for y in range(h):
        if y == 0 or y == h - 1:
            grid.append("#" * w)
        else:
            grid.append("#" + "." * (w - 2) + "#")
    return {
        "name": name, "w": w, "h": h,
        "spawn": {"x": w / 2.0, "y": h - 2.5, "facing": "N"},
        "grid": grid, "crawls": [], "partitions": [], "decals": [], "lights": [],
        "options": {"place_outlets": 0, "place_exit_door": 0, "lobby_ceiling": 0},
    }


def parse(text):
    m = {"name": None, "w": None, "h": None, "spawn": None,
         "grid": [], "crawls": [], "partitions": [], "decals": [], "lights": [],
         "options": {"place_outlets": 0, "place_exit_door": 0, "lobby_ceiling": 0}}
    section = None

    def err(n, msg):
        raise MapFormatError("line %d: %s" % (n, msg))

    for n, raw in enumerate(text.split("\n"), 1):
        line = raw.rstrip("\r")
        if section == "grid":
            t = line.rstrip()
            if t == "":
                section = None
                continue
            if t.lstrip().startswith("["):
                section = t.strip().strip("[]").split()[0].lower()
                continue
            m["grid"].append(t)
            continue
        line = _strip_comment(line).strip()
        if not line:
            continue
        if line.startswith("["):
            section = line.strip("[]").split()[0].lower()
            continue

        if section is None:
            key, _, val = line.partition(":")
            key, val = key.strip().lower(), val.strip()
            if key == "name":
                m["name"] = val
            elif key == "size":
                try:
                    w, h = (int(x) for x in val.lower().split("x"))
                except ValueError:
                    err(n, "bad size %r (want WxH)" % val)
                m["w"], m["h"] = w, h
            elif key == "spawn":
                pos, kw = _kv(val.replace(",", " ").split())
                if len(pos) < 2:
                    err(n, "spawn needs x y")
                m["spawn"] = {"x": float(pos[0]), "y": float(pos[1]),
                              "facing": kw.get("facing", "S").upper()}
            else:
                err(n, "unknown header key %r" % key)

        elif section == "crawl":
            pos, kw = _kv(line.replace(",", " ").split())
            if len(pos) < 2:
                err(n, "crawl needs cx cy")
            m["crawls"].append({"cx": int(pos[0]), "cy": int(pos[1]),
                                "dir": kw.get("dir", "S").upper(),
                                "len": int(kw.get("len", "1"))})

        elif section in ("partition", "partitions"):
            left, sep, right = line.partition("->")
            if not sep:
                err(n, "partition needs 'x1,y1 -> x2,y2'")
            try:
                x1, y1 = (float(x) for x in left.replace(",", " ").split())
            except ValueError:
                err(n, "bad partition start")
            rp, rkw = _kv(right.split())
            if "," in rp[0]:
                x2, y2 = (float(x) for x in rp[0].split(","))
            elif len(rp) >= 2:
                x2, y2 = float(rp[0]), float(rp[1])
            else:
                err(n, "partition needs an end point")
            m["partitions"].append({"x1": x1, "y1": y1, "x2": x2, "y2": y2,
                                    "style": rkw.get("style", "chevron"),
                                    "height": rkw.get("height", "full"),
                                    "crawl": rkw.get("crawl", "no")})

        elif section in ("decal", "decals"):
            pos, kw = _kv(line.replace(",", " ").split())
            if len(pos) < 3:
                err(n, "decal needs 'kind x y'")
            d = {"kind": pos[0].lower(), "x": float(pos[1]), "y": float(pos[2]),
                 "face": kw.get("face", "S").upper()}
            if "z" in kw:
                d["z"] = float(kw["z"])
            m["decals"].append(d)

        elif section in ("light", "lights"):
            pos, kw = _kv(line.replace(",", " ").split())
            if len(pos) < 2:
                err(n, "light needs cx cy")
            m["lights"].append({"cx": int(pos[0]), "cy": int(pos[1])})

        elif section == "options":
            key, _, val = line.partition(":")
            key = key.strip().lower()
            if key in m["options"]:
                m["options"][key] = int(val.strip())
            else:
                err(n, "unknown option %r" % key)
        else:
            err(n, "content outside a known [section]")

    if not m["name"]:
        raise MapFormatError("missing 'name:'")
    if not m["w"] or not m["h"]:
        raise MapFormatError("missing 'size:'")
    if m["spawn"] is None:
        raise MapFormatError("missing 'spawn:'")
    return m


def _num(v):
    """Compact number: integers without a trailing .0, floats trimmed."""
    if isinstance(v, float):
        if v == int(v):
            return str(int(v))
        return ("%.4f" % v).rstrip("0").rstrip(".")
    return str(v)


def serialize(model):
    m = model
    L = ["# %s.map" % str(m["name"]).lower(),
         "name: %s" % m["name"],
         "size: %dx%d" % (m["w"], m["h"]),
         "spawn: %s, %s facing=%s" % (_num(m["spawn"]["x"]), _num(m["spawn"]["y"]),
                                      m["spawn"]["facing"]),
         "", "[grid]"]
    L.extend(m["grid"])
    L.append("")
    if m["crawls"]:
        L.append("[crawl]")
        for c in m["crawls"]:
            L.append("%d,%d dir=%s len=%d" % (c["cx"], c["cy"], c["dir"], c["len"]))
        L.append("")
    if m["partitions"]:
        L.append("[partitions]")
        for p in m["partitions"]:
            L.append("%s,%s -> %s,%s  style=%s height=%s crawl=%s" % (
                _num(p["x1"]), _num(p["y1"]), _num(p["x2"]), _num(p["y2"]),
                p["style"], p["height"], p.get("crawl", "no")))
        L.append("")
    if m["decals"]:
        L.append("[decals]")
        for d in m["decals"]:
            z = (" z=%s" % _num(d["z"])) if "z" in d and d["z"] is not None else ""
            L.append("%-6s %s,%s face=%s%s" % (d["kind"], _num(d["x"]), _num(d["y"]),
                                               d["face"], z))
        L.append("")
    if m.get("lights"):
        L.append("[lights]")
        for g in m["lights"]:
            L.append("%d,%d" % (g["cx"], g["cy"]))
        L.append("")
    L.append("[options]")
    for k in ("place_outlets", "place_exit_door", "lobby_ceiling"):
        L.append("%s: %d" % (k, m["options"][k]))
    return "\n".join(L) + "\n"
