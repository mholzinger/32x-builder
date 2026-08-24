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
import bake_sprite     # noqa: E402  (community standee bake — shared with the CLI)
import export_assets   # noqa: E402  (palette + textures from the ROM source)
import lint_maps       # noqa: E402  (the same gate CI runs — lint before submit)

import github_pr        # noqa: E402  (OAuth + fork/branch/PR — pipeline Phase 3)

from flask import Flask, jsonify, request, render_template, redirect, session

app = Flask(__name__)
# Signs the session cookie that carries a signed-in user's OWN GitHub token
# between requests (Phase 3). Unset -> OAuth routes disable themselves and the
# editor uses the zero-auth pre-filled-URL submit flow.
app.secret_key = config.SESSION_SECRET or None


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
    """[{name, title, role, folder, protected}] — the editor shows a lock on
    protected (core) maps and clones them on edit.

    `name` is the FILE stem (the /maps/<name> key); `title` is the map's
    `name:` header. They differ ("segapowerbase" vs "SEGA POWER BASE"), and
    story chains resolve by TITLE (gen_maps matches `next:` against the header
    name, uppercased) — so the editor's next-map picker needs the title, not
    the filename. Same parse as the role lookup, so it costs nothing extra."""
    roles = _roles()
    maps = []
    for folder, name, path in sorted(_iter_map_files(), key=lambda t: (t[0], t[1])):
        role, title = "community", ""
        try:
            m = mapfmt.parse(open(path).read())
            role = m.get("role", "community")
            title = m.get("name") or ""
        except Exception:
            pass
        maps.append({"name": name, "title": title, "role": role, "folder": folder,
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
    Save-to-disk button hides and Export (download) is the way out; when OAuth
    is configured the Submit button upgrades to the in-editor PR flow."""
    return jsonify({"readonly": config.READONLY,
                    "github_oauth": config.OAUTH_ENABLED,
                    "github_repo": config.GITHUB_REPO})


# ---------------- Phase 3: GitHub sign-in + in-editor PR ----------------

@app.route("/auth/login")
def auth_login():
    if not config.OAUTH_ENABLED:
        return jsonify({"error": "GitHub OAuth is not configured on this instance"}), 501
    state = os.urandom(16).hex()
    session["oauth_state"] = state
    redirect_uri = request.url_root.rstrip("/") + "/auth/callback"
    # fly terminates TLS ahead of us; the public callback must be https.
    if request.headers.get("X-Forwarded-Proto") == "https":
        redirect_uri = redirect_uri.replace("http://", "https://", 1)
    return redirect(github_pr.authorize_url(config.GITHUB_CLIENT_ID, redirect_uri, state))


@app.route("/auth/callback")
def auth_callback():
    if not config.OAUTH_ENABLED:
        return jsonify({"error": "OAuth not configured"}), 501
    if request.args.get("state") != session.pop("oauth_state", None):
        return jsonify({"error": "state mismatch — retry sign-in"}), 400
    code = request.args.get("code", "")
    try:
        token = github_pr.exchange_code(config.GITHUB_CLIENT_ID,
                                        config.GITHUB_CLIENT_SECRET, code)
        login = github_pr.whoami(token)
    except github_pr.GitHubError as e:
        return jsonify({"error": str(e)}), 502
    session["gh_token"] = token
    session["gh_login"] = login
    return redirect("/")


@app.route("/auth/user")
def auth_user():
    return jsonify({"login": session.get("gh_login") or None,
                    "oauth": config.OAUTH_ENABLED})


@app.route("/auth/logout", methods=["POST"])
def auth_logout():
    session.clear()
    return jsonify({"ok": True})


@app.route("/submit_pr", methods=["POST"])
def submit_pr():
    """Signed-in submit: lint, then fork/branch/commit/PR AS THE USER. The map
    arrives as a normal GitHub PR under their identity; CI takes it from there."""
    token, login = session.get("gh_token"), session.get("gh_login")
    if not (token and login):
        return jsonify({"error": "not signed in"}), 401
    model = request.get_json(force=True, silent=True)
    model, text, errs, target, is_update = _lint_submission(model)
    if errs:
        return jsonify({"ok": False, "errors": errs}), 400
    what = ("Map update" if is_update else "Community map")
    lead = ("Update to **%s** by @%s" if is_update else
            "New community map **%s** by @%s") % (model["name"], login)
    try:
        pr = github_pr.open_map_pr(
            token, login, config.GITHUB_REPO, config.GITHUB_BRANCH,
            target, text,
            title="%s: %s" % (what, model["name"]),
            body=(lead + ", submitted from the "
                  "[hosted map editor](https://backrooms-32x-project.fly.dev/).\n\n"
                  "CI lints it and builds the ROM; once merged it ships in the "
                  "next `build-N` release automatically."))
    except github_pr.GitHubError as e:
        return jsonify({"ok": False, "errors": [str(e)]}), 502
    return jsonify({"ok": True, "pr_url": pr["url"], "pr_number": pr["number"],
                    "existing": pr["existing"]})


# ---------------- the contributor's own fork ----------------
#
# Submitting upstream is for work you want in the MAIN game, and that is a
# curated decision. A fork is the contributor's own copy, where their maps and
# sprites are their own work and the whole budget — the community CRAM arena,
# the sprite kind numbering, the ROM — belongs to them. Their fork's CI builds
# them a ROM on every push.

@app.route("/fork/status")
def fork_status_route():
    token, login = session.get("gh_token"), session.get("gh_login")
    if not (token and login):
        return jsonify({"error": "not signed in"}), 401
    try:
        return jsonify(github_pr.fork_status(token, login, config.GITHUB_REPO))
    except github_pr.GitHubError as e:
        return jsonify({"error": str(e)}), 502


@app.route("/fork/create", methods=["POST"])
def fork_create():
    token, login = session.get("gh_token"), session.get("gh_login")
    if not (token and login):
        return jsonify({"error": "not signed in"}), 401
    try:
        fork = github_pr.ensure_fork(token, login, config.GITHUB_REPO,
                                     config.GITHUB_BRANCH)
    except github_pr.GitHubError as e:
        return jsonify({"error": str(e)}), 502
    return jsonify({"ok": True, "fork": fork,
                    "repo_url": "https://github.com/%s" % fork,
                    "actions_url": "https://github.com/%s/actions" % fork})


@app.route("/fork/assets")
def fork_assets():
    """The signed-in contributor's OWN sprites, read from their fork.

    Without this, a fork is write-only: you bake a sprite, save it to your copy,
    come back tomorrow and the editor (which only knows the upstream registry
    baked into this image) has never heard of it — so you can't place it, can't
    preview it, and can't build a map that uses it. Their fork is the source of
    truth for what THEIR ROM contains, so that is what their palette should show.

    Returns entries shaped exactly like a fresh bake, so the client injects them
    with the same code path it uses for try-before-submit."""
    token, login = session.get("gh_token"), session.get("gh_login")
    if not (token and login):
        return jsonify({"error": "not signed in"}), 401
    fork = github_pr.fork_name(login, config.GITHUB_REPO)
    if fork == config.GITHUB_REPO:
        return jsonify({"fork": fork, "sprites": [], "is_upstream": True})
    reg_text = github_pr.get_file(token, fork, "registry.json", config.GITHUB_BRANCH)
    if reg_text is None:
        return jsonify({"fork": fork, "sprites": [], "no_fork": True})
    try:
        reg = json.loads(reg_text)
    except ValueError:
        return jsonify({"error": "your fork's registry.json is not valid JSON"}), 400

    # Only what THIS instance doesn't already have: everything else is already
    # in the palette from the image's own registry.
    with open(config.REGISTRY) as fh:
        have = {s["id"] for s in json.load(fh)["assets"]["sprites"]}
    kinds = {k["id"]: k for k in reg.get("decals", {}).get("kinds", [])}
    out, skipped = [], []
    for s in reg.get("assets", {}).get("sprites", []):
        sid = s.get("id")
        if not sid or sid in have or not isinstance(s.get("base"), int):
            continue
        tex = github_pr.get_file(token, fork, "sh_src/%s" % s.get("tex", ""),
                                 config.GITHUB_BRANCH)
        if not tex:
            skipped.append(sid)
            continue
        try:
            import tempfile
            with tempfile.NamedTemporaryFile("w", suffix=".h", delete=False) as fh:
                fh.write(tex); tmp = fh.name
            t = export_assets.parse_tex(tmp)
            os.unlink(tmp)
        except Exception:
            skipped.append(sid)
            continue
        # parse_tex returns the flat array in file order; the lo-res community
        # textures are row-major [H][W], which is what the client expects.
        kd = kinds.get(sid, {})
        out.append({
            "id": sid, "kind": s.get("kind"), "z": kd.get("z", 0.5),
            "mount": s.get("mount", "billboard"), "base": s["base"],
            "pal8": [[c * 255 // 31 for c in e] for e in (s.get("pal") or [])],
            "w": t["w"], "h": t["h"], "texels": t["data"],
            "world_h": s.get("world_h", 0.9), "world_hw": s.get("world_hw", 0.3),
        })
    return jsonify({"fork": fork, "sprites": out, "skipped": skipped})


@app.route("/fork/save_map", methods=["POST"])
def fork_save_map():
    """Commit the current map onto the contributor's own fork. Linted the same
    way as a submission (a broken map would just fail their own build), but it
    lands directly — their repo, their call."""
    token, login = session.get("gh_token"), session.get("gh_login")
    if not (token and login):
        return jsonify({"error": "not signed in"}), 401
    model = request.get_json(force=True, silent=True)
    model, text, errs, target, _is_update = _lint_submission(model)
    if errs:
        return jsonify({"ok": False, "errors": errs}), 400
    try:
        r = github_pr.commit_to_fork(
            token, login, config.GITHUB_REPO, config.GITHUB_BRANCH,
            [(target, text)],
            "Map: %s (from the hosted editor)" % model["name"])
    except github_pr.GitHubError as e:
        return jsonify({"ok": False, "errors": [str(e)]}), 502
    r["ok"] = True
    r["path"] = target
    return jsonify(r)


@app.route("/fork/save_sprite", methods=["POST"])
def fork_save_sprite():
    """Commit a baked sprite (texture + registry) onto the contributor's fork.
    In their own repo the community palette arena is theirs alone, so they can
    spend it on as many assets as they like."""
    token, login = session.get("gh_token"), session.get("gh_login")
    if not (token and login):
        return jsonify({"error": "not signed in"}), 401
    j = request.get_json(force=True, silent=True) or {}
    sprite_id = re.sub(r"[^a-z0-9_]", "", (j.get("id") or ""))[:16]
    texh, reg_text = j.get("tex_h") or "", j.get("registry") or ""
    texh_hi = j.get("tex_h_hi") or ""
    if not (sprite_id and texh and reg_text):
        return jsonify({"error": "incomplete bundle — re-bake and retry"}), 400
    files = [("sh_src/spr_%s_tex.h" % sprite_id, texh)]
    if texh_hi:
        files.append(("sh_src/spr_%s_tex_hi.h" % sprite_id, texh_hi))
    files.append(("registry.json", reg_text))
    try:
        r = github_pr.commit_to_fork(
            token, login, config.GITHUB_REPO, config.GITHUB_BRANCH, files,
            "Sprite: %s (from the hosted editor)" % sprite_id)
    except github_pr.GitHubError as e:
        return jsonify({"ok": False, "errors": [str(e)]}), 502
    r["ok"] = True
    return jsonify(r)


@app.route("/bake_sprite", methods=["POST"])
def bake_sprite_route():
    """Community sprite upload: bake the image against the shared COMM ramp
    and return everything a submission needs — a preview PNG (through the
    REAL palette), the generated tex.h, and the registry with the new
    entries appended. Stateless: nothing is written server-side; the bundle
    goes back out via download or the signed-in PR route below."""
    import base64, io
    from PIL import Image
    f = request.files.get("image")
    sprite_id = re.sub(r"[^a-z0-9_]", "", (request.form.get("id") or "").lower())[:16]
    try:
        world_h = float(request.form.get("height") or 1.0)
    except ValueError:
        world_h = 1.0
    author = (request.form.get("author") or "")[:24]
    mount = request.form.get("mount") or "billboard"
    if mount not in ("billboard", "wall"):
        mount = "billboard"
    want_hi = (request.form.get("hi") or "") == "1" and mount == "billboard"
    try:
        wall_z = float(request.form.get("z") or 0.5)
    except ValueError:
        wall_z = 0.5
    wall_z = min(0.95, max(0.05, wall_z))
    if not f:
        return jsonify({"error": "no image uploaded"}), 400
    if not re.match(r"^[a-z][a-z0-9_]{1,15}$", sprite_id):
        return jsonify({"error": "name must be 2-16 chars a-z 0-9 _ "
                                 "starting with a letter"}), 400
    if not (0.1 <= world_h <= 1.0):
        return jsonify({"error": "height must be 0.1-1.0 cells"}), 400
    reg = json.load(open(config.REGISTRY))
    ids = ([sp["id"] for sp in reg["assets"]["sprites"]] +
           [k["id"] for k in reg["decals"]["kinds"]])
    if sprite_id in ids:
        return jsonify({"error": "name %r is taken" % sprite_id}), 400
    try:
        rot = int(request.form.get("rotate") or 0)
    except ValueError:
        rot = 0
    mirror = (request.form.get("mirror") or "") == "1"
    # Read the upload into memory: PIL wants a seekable stream, and a failure's
    # REASON has to reach the author — "not a readable image" on a perfectly
    # good WebP is how an upload path loses someone.
    #
    # The retry is not paranoia. werkzeug 3.1's multipart parser strips a
    # trailing CR that belongs to the PAYLOAD: any file whose last byte is 0x0D
    # arrives one byte short (verified — a 9-byte file ending in \r is delivered
    # as 8). That is ~1 upload in 256, fatal for WebP ("could not create decoder
    # object") and silent corruption elsewhere. Double K's decal sheet ends in
    # \r, which is why his sprites never made it through the editor at all.
    import io as _io
    raw = f.read()
    img = err = None
    for candidate in (raw, raw + b"\r"):
        try:
            probe = Image.open(_io.BytesIO(candidate))
            probe.load()
            img = probe
            break
        except Exception as e:                     # noqa: BLE001 (reported below)
            err = e
    if img is None:
        return jsonify({"error": "could not read that image (%s: %s) — PNG, "
                                 "WebP and JPEG all work"
                                 % (type(err).__name__, err)}), 400
    img = bake_sprite.orient(img, rot if rot in (90, 180, 270) else 0, mirror)
    # A SHEET (several separate marks on one canvas) baked whole becomes one
    # decal of mostly empty space, each mark shrunk to a smudge — the ROM's
    # sprite stops resembling the upload. Report the marks, and bake just one
    # when the author picks it.
    marks = bake_sprite.find_marks(img)
    try:
        mark = int(request.form.get("mark") or 0)
    except ValueError:
        mark = 0
    if mark and 1 <= mark <= len(marks):
        img = img.crop(marks[mark - 1])
    # Width: the widest the per-mount texel budget allows. This was hardcoded at
    # 48 with a 32 fallback, so a wall decal baked at 48x33 — and a drawing's
    # fine grain (the tester's torn wallpaper is ~3px speckle in 654px of art)
    # averages to a smooth smudge long before the palette gets involved. We tell
    # contributors we bake their image DOWN; at that size we were mangling it.
    wall = (mount == "wall")
    max_w = bake_sprite.MAX_W_WALL if wall else bake_sprite.MAX_W
    max_h = bake_sprite.MAX_H_WALL if wall else bake_sprite.MAX_H
    budget = bake_sprite.MAX_TEXELS_WALL if wall else bake_sprite.MAX_TEXELS
    fit = bake_sprite.fit_width(img, max_w=max_w, max_h=max_h, budget=budget)
    rows, W, H, pal31, pal8 = bake_sprite.bake_image(img, fit, max_h=max_h)
    if W * H > budget:
        rows, W, H, pal31, pal8 = bake_sprite.bake_image(img, 32)
    sprite, kindent = bake_sprite.registry_entries(reg, sprite_id, W, H,
                                                  world_h, pal31, author,
                                                  mount, wall_z, want_hi)
    texh = bake_sprite.emit_header(sprite_id, rows, W, H, sprite["base"])
    texh_hi = None
    if want_hi:
        rows_hi, W_hi, H_hi, _p, _p8 = bake_sprite.bake_image(
            img, W * 2, max_h=bake_sprite.MAX_H * 2, pal8=pal8)
        texh_hi = bake_sprite.emit_header(sprite_id, rows_hi, W_hi, H_hi,
                                          sprite["base"], hi=True)
    reg["assets"]["sprites"].append(sprite)
    reg["decals"]["kinds"].append(kindent)
    reg_text = json.dumps(reg, indent=1, ensure_ascii=False) + "\n"
    # Preview: texels through the sprite's OWN quantized palette, 3x nearest.
    pv = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = pv.load()
    for y in range(H):
        for x in range(W):
            v = rows[y][x]
            if v:
                px[x, y] = tuple(pal8[v - 1]) + (255,)
    pv = pv.resize((W * 3, H * 3), Image.NEAREST)
    buf = io.BytesIO(); pv.save(buf, "PNG")
    # The shared game has a hard ceiling of 14 sprite palettes and it is the
    # first thing a growing community hits — say where they stand, and that
    # their own fork has the whole arena. This is the honest nudge toward a
    # fork: not "go away", but "there you own all of it".
    used, cap = lint_maps.arena_usage(reg["assets"]["sprites"])
    arena = {"used": used, "cap": cap, "left": max(0, cap - used)}
    sheet = None
    if len(marks) > 1 and not mark:
        sheet = ("Baked as ONE decal, which is your image as you drew it. It "
                 "holds %d separate marks — if you meant each to be its own "
                 "placeable decal, bake them one at a time here." % len(marks))
    return jsonify({
        "ok": True, "id": sprite_id, "w": W, "h": H, "kind": sprite["kind"],
        "marks": len(marks), "mark": mark, "sheet_warning": sheet,
        "arena": arena,
        "mount": mount, "z": kindent["z"],
        "base": sprite["base"], "pal8": [list(c) for c in pal8],
        "hi": bool(texh_hi),
        "world_h": world_h, "world_hw": sprite["world_hw"],
        "preview_png": base64.b64encode(buf.getvalue()).decode(),
        "texels": [v for row in rows for v in row],
        "tex_h": texh, "tex_path": "sh_src/spr_%s_tex.h" % sprite_id,
        "tex_h_hi": texh_hi,
        "registry": reg_text,
    })


@app.route("/submit_sprite_pr", methods=["POST"])
def submit_sprite_pr():
    """Signed-in sprite submit: commits the baked tex.h AND the updated
    registry.json on one branch, opens the PR as the user."""
    token, login = session.get("gh_token"), session.get("gh_login")
    if not (token and login):
        return jsonify({"error": "not signed in"}), 401
    j = request.get_json(force=True, silent=True) or {}
    sprite_id = re.sub(r"[^a-z0-9_]", "", (j.get("id") or ""))[:16]
    texh, reg_text = j.get("tex_h") or "", j.get("registry") or ""
    texh_hi = j.get("tex_h_hi") or ""
    if not (sprite_id and texh and reg_text):
        return jsonify({"error": "incomplete bundle — re-bake and retry"}), 400
    files = [("sh_src/spr_%s_tex.h" % sprite_id, texh)]
    if texh_hi:
        files.append(("sh_src/spr_%s_tex_hi.h" % sprite_id, texh_hi))
    files.append(("registry.json", reg_text))
    try:
        pr = github_pr.open_files_pr(
            token, login, config.GITHUB_REPO, config.GITHUB_BRANCH,
            files,
            title="Community sprite: %s" % sprite_id,
            body=("New community standee **%s** by @%s, baked and submitted "
                  "from the [hosted editor](https://backrooms-32x-project.fly.dev/). "
                  "Two files: the palette-indexed texture and its registry "
                  "entries. CI regenerates sprite_defs.h; once merged the "
                  "standee is placeable in maps." % (sprite_id, login)),
            branch="sprite-" + sprite_id)
    except github_pr.GitHubError as e:
        return jsonify({"ok": False, "errors": [str(e)]}), 502
    return jsonify({"ok": True, "pr_url": pr["url"], "pr_number": pr["number"],
                    "existing": pr["existing"]})


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


def _lint_submission(model):
    """Run the CI lint gate on a submission model. A brand-new name becomes a
    community map; a name matching an EXISTING community or CURATED map is an
    UPDATE of that map — it keeps the existing file's path and role, so the
    merged tree never grows a second copy (Smokemonster's promoted ALOHA
    COMPLEX hit exactly that: the old community-only exemption made his own
    curated file read as a duplicate). Curated files are CODEOWNERS-gated on
    the GitHub side, so promotion review still holds; core canon still
    collides — you can't shadow it. Returns (model, text, errors, target,
    is_update) where target is the repo-relative path to write."""
    if not isinstance(model, dict):
        return None, None, ["expected a JSON model"], None, False
    model = dict(model)
    model["name"] = (model.get("name") or "untitled").upper()[:16]
    this_name = model["name"]

    with open(config.REGISTRY) as fh:
        reg = json.load(fh)
    roles = reg.get("roles", {})

    # One scan of the repo's maps: seed the duplicate-name check AND find the
    # file this submission updates, if any. Only community/curated tiers are
    # updatable through the editor; any same-named core-tier map stays in
    # `seen` and fails lint as before.
    seen, errs, existing = {}, [], None
    for folder, name, path in _iter_map_files():
        role = "community"
        try:
            m = mapfmt.parse(open(path).read())
            nm = (m.get("name") or name).upper()
            role = m.get("role", "community")
        except Exception:
            nm = name.upper()
        if (nm == this_name and existing is None
                and roles.get(role, {}).get("tier") in ("community", "curated")):
            existing = (folder or "community", name, role)
            continue                      # updating this map, not duplicating it
        seen[nm] = "maps/%s/%s.map" % (folder or ".", name)

    if existing:
        folder, stem, role = existing
        model["role"] = role              # keep curated curated: role and
        lint_folder = folder              # folder must agree or CI lint fails
        target = "maps/%s/%s.map" % (folder, stem)
    else:
        model["role"] = "community"
        lint_folder = "community"
        target = "maps/community/%s.map" % _safe(this_name).lower()

    text, err = _serialize_or_400(model)
    if err:
        return model, None, [err], None, False
    lint_maps.lint_model(mapfmt.parse(text), model["name"], lint_folder,
                         reg, seen, errs)
    nxt = (model.get("next") or "").strip().upper()
    if nxt and nxt != this_name and nxt not in seen:
        errs.append("%s: next: %r is not a map in the game yet — submit the "
                    "later chapters of a story first (chains are built "
                    "tail-first)" % (model["name"], nxt))
    return model, text, errs, target, existing is not None


@app.route("/lint", methods=["POST"])
def map_lint():
    """The same gate CI runs, before the contributor leaves the editor."""
    model = request.get_json(force=True, silent=True)
    _model, _text, errs, _target, _is_update = _lint_submission(model)
    return jsonify({"ok": not errs, "errors": errs})


@app.route("/submit_url", methods=["POST"])
def submit_url():
    """Phase 1 of the community-PR pipeline: lint the model, then hand back a
    pre-filled github.com new-file URL. GitHub natively walks a signed-in
    contributor through fork -> commit -> pull request, so the PR is authored
    by THEIR identity with no tokens or secrets on this server. The client
    falls back to clipboard + a bare new-file URL if the encoded URL is too
    long for the browser/GitHub (~8KB)."""
    from urllib.parse import quote
    model = request.get_json(force=True, silent=True)
    model, text, errs, target, is_update = _lint_submission(model)
    if errs:
        return jsonify({"ok": False, "errors": errs}), 400
    # An update must land on the existing file. GitHub's /new page refuses to
    # overwrite, so route updates through the EDIT page for that path instead
    # (value can't be pre-filled there; the client's download/clipboard path
    # covers the content either way).
    filename = target
    if is_update:
        edit_url = "https://github.com/%s/edit/%s/%s" % (
            config.GITHUB_REPO, config.GITHUB_BRANCH, filename)
        return jsonify({"ok": True, "url": edit_url, "bare_url": edit_url,
                        "upload_url": edit_url, "filename": filename,
                        "text": text, "url_len": len(edit_url),
                        "update": True})
    base = "https://github.com/%s/new/%s" % (config.GITHUB_REPO, config.GITHUB_BRANCH)
    url = "%s?filename=%s&value=%s" % (base, quote(filename, safe=""), quote(text, safe=""))
    # For maps too big for a pre-filled URL, the client downloads the real .map
    # and hands the user GitHub's drag-drop UPLOAD page (a real file, no
    # clipboard) rather than an empty new-file editor — the old fallback silently
    # dropped the content and produced empty PRs.
    updir = filename.rsplit("/", 1)[0]
    upload_url = "https://github.com/%s/upload/%s/%s" % (
        config.GITHUB_REPO, config.GITHUB_BRANCH, updir)
    return jsonify({"ok": True, "url": url, "bare_url": "%s?filename=%s" % (base, quote(filename, safe="")),
                    "upload_url": upload_url, "filename": filename, "text": text,
                    "url_len": len(url)})


@app.route("/new")
def map_new():
    try:
        with open(config.REGISTRY) as fh:
            md = json.load(fh)["limits"]["max_dim"]
        w = max(4, min(md, int(request.args.get("w", 16))))
        h = max(4, min(md, int(request.args.get("h", 16))))
    except ValueError:
        return jsonify({"error": "bad w/h"}), 400
    name = _safe(request.args.get("name", "untitled")).upper()[:16]
    return jsonify({"model": mapfmt.new_model(name, w, h)})


if __name__ == "__main__":
    print("map-editor: repo=%s  maps=%s" % (config.REPO_ROOT, config.MAPS_DIR))
    app.run(host=config.HOST, port=config.PORT, debug=config.DEBUG)
