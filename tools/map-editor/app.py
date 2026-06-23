#!/usr/bin/env python3
"""Backrooms 32X map editor — Flask backend.

Serves the canvas editor and reads/writes the repo's maps/*.map files through
the SHARED format module (tools/mapfmt.py), so what the editor saves is exactly
what the ROM codegen (tools/gen_maps.py) reads. The element palette is driven by
the same registry.json the build uses, so new elements show up automatically.

Run:  python3 app.py          (needs Flask; reads ../../maps + ../../registry.json)
  or: docker compose up       (mounts the repo at /repo)
"""
import json, os, re, sys

import config
sys.path.insert(0, config.TOOLS_DIR)
import mapfmt          # noqa: E402  (shared .map syntax — single source of truth)
import export_assets   # noqa: E402  (palette + textures from the ROM source)

from flask import Flask, jsonify, request, render_template

app = Flask(__name__)


def _safe(name):
    """Filename-safe map slug (no path traversal)."""
    return re.sub(r"[^a-zA-Z0-9_-]", "", name or "")[:32] or "untitled"


def _roles():
    try:
        with open(config.REGISTRY) as fh:
            return json.load(fh).get("roles", {})
    except Exception:
        return {}


def _iter_map_files():
    """Every maps/**/*.map across the role folders -> (folder, name, fullpath)."""
    out = []
    for root, _dirs, files in os.walk(config.MAPS_DIR):
        folder = "" if root == config.MAPS_DIR else os.path.basename(root)
        for f in files:
            if f.endswith(".map"):
                out.append((folder, f[:-4], os.path.join(root, f)))
    return out


def _find_map(name):
    """Resolve a map by slug across the role folders (for load)."""
    slug = _safe(name).lower()
    for _folder, n, path in _iter_map_files():
        if n.lower() == slug:
            return path
    return None


def _community_path(name):
    return os.path.join(config.MAPS_DIR, "community", _safe(name) + ".map")


def _list_maps():
    """[{name, role, folder, protected}] — the editor shows a lock on protected
    (core) maps and clones them on edit."""
    roles = _roles()
    maps = []
    for folder, name, path in sorted(_iter_map_files(), key=lambda t: (t[0], t[1])):
        role = "community"
        try:
            role = mapfmt.parse(open(path).read()).get("role", "community")
        except Exception:
            pass
        maps.append({"name": name, "role": role, "folder": folder,
                     "protected": bool(roles.get(role, {}).get("protected"))})
    return maps


@app.after_request
def _no_cache(resp):
    resp.headers["Cache-Control"] = "no-store"
    return resp


@app.route("/health")
def health():
    return jsonify({"status": "ok"})


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/registry")
def registry():
    with open(config.REGISTRY) as fh:
        return jsonify(json.load(fh))


@app.route("/assets")
def assets():
    """The ROM's actual palette (+ base indices) so the preview renders in the
    game's real colors. Parsed live from raycast.c, so it tracks palette tweaks."""
    try:
        return jsonify(export_assets.build_assets(config.REPO_ROOT))
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/config")
def client_config():
    """The client adapts its UI to this: on a hosted (read-only) instance the
    Save-to-disk button hides and Export (download) is the way out."""
    return jsonify({"readonly": config.READONLY})


@app.route("/maps")
def maps_list():
    return jsonify({"maps": _list_maps()})


@app.route("/maps/<name>")
def map_get(name):
    path = _find_map(name)
    if not path:
        return jsonify({"error": "not found"}), 404
    try:
        model = mapfmt.parse(open(path).read())
    except mapfmt.MapFormatError as e:
        return jsonify({"error": str(e)}), 400
    return jsonify({"model": model})


def _serialize_or_400(model):
    """Serialize via the shared format module with a round-trip guard.
    Returns (text, None) or (None, error_message)."""
    if not isinstance(model, dict):
        return None, "expected a JSON model"
    model["name"] = (model.get("name") or "untitled").upper()[:16]
    try:
        text = mapfmt.serialize(model)
        mapfmt.parse(text)            # what we emit must parse back cleanly
        return text, None
    except (mapfmt.MapFormatError, KeyError, TypeError) as e:
        return None, "serialize failed: %s" % e


@app.route("/export", methods=["POST"])
def map_export():
    """Stateless: serialize the posted model and hand it back as a .map file
    download. Never touches the filesystem — this is the save path on the hosted
    editor (session stays in the browser; the file lands on the user's disk)."""
    model = request.get_json(force=True, silent=True)
    text, err = _serialize_or_400(model)
    if err:
        return jsonify({"error": err}), 400
    name = _safe((model or {}).get("name")) or "untitled"
    resp = app.response_class(text, mimetype="text/plain")
    resp.headers["Content-Disposition"] = 'attachment; filename="%s.map"' % name.lower()
    resp.headers["Cache-Control"] = "no-store"
    return resp


@app.route("/parse", methods=["POST"])
def map_parse():
    """Stateless: parse raw .map text (a file the user picked off their disk)
    into a model via the shared parser. No filesystem write."""
    text = request.get_data(as_text=True)
    try:
        return jsonify({"model": mapfmt.parse(text)})
    except mapfmt.MapFormatError as e:
        return jsonify({"error": str(e)}), 400


@app.route("/maps/<name>", methods=["POST"])
def map_save(name):
    """Save-to-disk — LOCAL DEV ONLY (the hosted instance is read-only). Writes
    only into maps/community/; a protected (core) map is cloned, never
    overwritten, so canon is immutable here exactly as it is in CI/CODEOWNERS."""
    if config.READONLY:
        return jsonify({"error": "This hosted editor is read-only. Use Export to "
                                 "download the .map to your computer, then open a PR."}), 403
    model = request.get_json(force=True, silent=True)
    if not isinstance(model, dict):
        return jsonify({"error": "expected a JSON model"}), 400
    model["name"] = (model.get("name") or _safe(name)).upper()[:16]

    roles = _roles()
    role = (model.get("role") or "community").lower()
    cloned = False
    if roles.get(role, {}).get("protected"):     # clone-on-edit: fork to community
        model["role"] = "community"
        model["author"] = model.get("author") or os.environ.get("MAP_AUTHOR", "")
        cloned = True

    # Reject a name that collides with a DIFFERENT canonical map (clones must be
    # renamed) — keeps map names unique, which the lint also enforces.
    existing = _find_map(model["name"])
    if existing and os.path.basename(os.path.dirname(existing)) != "community":
        return jsonify({"error": "name %r is a protected map — choose a new name "
                                 "for your copy." % model["name"]}), 409

    text, err = _serialize_or_400(model)
    if err:
        return jsonify({"error": err}), 400
    out = _community_path(model["name"])
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as fh:
        fh.write(text)
    return jsonify({"ok": True, "name": _safe(model["name"]), "cloned": cloned,
                    "folder": "community", "text": text})


@app.route("/new")
def map_new():
    try:
        w = max(4, min(64, int(request.args.get("w", 16))))
        h = max(4, min(64, int(request.args.get("h", 16))))
    except ValueError:
        return jsonify({"error": "bad w/h"}), 400
    name = _safe(request.args.get("name", "untitled")).upper()[:16]
    return jsonify({"model": mapfmt.new_model(name, w, h)})


if __name__ == "__main__":
    print("map-editor: repo=%s  maps=%s" % (config.REPO_ROOT, config.MAPS_DIR))
    app.run(host=config.HOST, port=config.PORT, debug=config.DEBUG)
