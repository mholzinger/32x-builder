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


def _map_path(name):
    return os.path.join(config.MAPS_DIR, _safe(name) + ".map")


def _list_maps():
    if not os.path.isdir(config.MAPS_DIR):
        return []
    return sorted(f[:-4] for f in os.listdir(config.MAPS_DIR) if f.endswith(".map"))


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


@app.route("/maps")
def maps_list():
    return jsonify({"maps": _list_maps()})


@app.route("/maps/<name>")
def map_get(name):
    path = _map_path(name)
    if not os.path.exists(path):
        return jsonify({"error": "not found"}), 404
    try:
        model = mapfmt.parse(open(path).read())
    except mapfmt.MapFormatError as e:
        return jsonify({"error": str(e)}), 400
    return jsonify({"model": model})


@app.route("/maps/<name>", methods=["POST"])
def map_save(name):
    model = request.get_json(force=True, silent=True)
    if not isinstance(model, dict):
        return jsonify({"error": "expected a JSON model"}), 400
    model["name"] = (model.get("name") or _safe(name)).upper()[:16]
    try:
        text = mapfmt.serialize(model)
        # round-trip guard: what we wrote must parse back cleanly
        mapfmt.parse(text)
    except (mapfmt.MapFormatError, KeyError, TypeError) as e:
        return jsonify({"error": "serialize failed: %s" % e}), 400
    os.makedirs(config.MAPS_DIR, exist_ok=True)
    with open(_map_path(name), "w") as fh:
        fh.write(text)
    return jsonify({"ok": True, "name": _safe(name), "text": text})


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
