#!/usr/bin/env python3
"""Bake the directional chair billboard set from the IN-GAME box model.

Renders the 7-box chair (parsed live from sh_src/chair3d.h, so model edits
are picked up automatically) at N viewer poses with the engine's exact
transform math, quantizes to the door-brown ramp, and emits
sh_src/chair_dir_tex.h plus a validation strip PNG to eyeball BEFORE any
ROM build.

The workflow for an import (repeatable, nothing eyeballed):

    tools/bake_dir_sprites.py --sprite <registry-id> \
        --header sh_src/<model>3d.h --symbol <model>_boxes --prefix <MODEL>

Pitch derives from the sprite's registry world_h (the player's look-down
angle at the 4-cell LOD swap); pass --pitch <n> only to reproduce a
legacy eyeballed bake (the chair's 246). Example legacy form:

    tools/bake_dir_sprites.py --pitch 246 --yaws 0,238,218,194,180,156,128

Yaws should cover the half circle 0..128 (front..back, uneven spacing is
fine); the engine's 12-sector picker mirrors them for the other half.
Values: 0 transparent, 1..5 -> DOOR_BASE+(v-1) via the sprite vmap (the
same fog-aware decode the in-game paint path uses).
"""
import argparse, math, os, re, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FACES = [((1,5,7,3),4),((0,2,6,4),4),((2,3,7,6),6),((0,4,5,1),2),
         ((4,6,7,5),3),((0,1,3,2),3)]   # corner-bit quads + chair_face_shade axsh


def load_boxes(header="sh_src/chair3d.h", symbol="chair_boxes"):
    """Parse a cbox_t list out of a model header. Returns 11-tuples in struct
    order x0,y0,z0,x1,y1,z1,tx0,tz0,tx1,tz1,taper.

    Two spellings exist and both are model space with height 1.0:
      - hand-authored (chair3d.h) writes CM(0.26) — already model units
      - bake_boxes.py output (desk3d.h) writes raw 8.8 ints — /256

    A plain box is written with only its six base values and zero-fills the
    rest; a WEDGE writes all eleven. The billboard MUST honour taper or the
    far LOD keeps the shape the near 3D no longer has, which pops at the swap.
    """
    src = open(os.path.join(REPO, header)).read()
    body = src.split(symbol + "[")[1]
    body = body[:body.index("};")] if "};" in body else body
    boxes = []
    for m in re.finditer(r"\b(BOX6|WEDGE)\s*\(", body):
        # Paren-match rather than regex the interior: BOX6(CM(...), ...) nests.
        i = m.end()
        depth, start = 1, i
        while depth and i < len(body):
            depth += (body[i] == '(') - (body[i] == ')')
            i += 1
        inner = body[start:i - 1]
        vals = re.findall(r"CM\(\s*(-?[0-9.]+)\s*\)", inner)
        if vals:
            b = [float(v) for v in vals]
        else:
            b = [int(v) / 256.0 for v in re.findall(r"-?\d+", inner)]
        want = 10 if m.group(1) == "WEDGE" else 6
        if len(b) != want:
            continue
        b += [0.0] * (10 - len(b))
        b.append(1.0 if m.group(1) == "WEDGE" else 0.0)
        boxes.append(tuple(b))
    if not boxes:
        sys.exit("no %s parsed from %s" % (symbol, header))
    return boxes


def build_mesh(boxes):
    """Verts plus QUAD faces. The engine rasterizes triangles, but this baker
    fills whole quads: splitting a face into two triangles left a hairline along
    their shared edge where the box's back face showed through, and on the
    desk's large tabletop that baked in as a diagonal scar. One polygon, no
    internal edge. Depth sorting is per-face either way."""
    verts, quads = [], []
    for (x0, y0, z0, x1, y1, z1, tx0, tz0, tx1, tz1, taper) in boxes:
        base = len(verts)
        for v in range(8):
            top = v & 2
            if top and taper:           # wedge: inset top rect (cbox_corner)
                verts.append((tx1 if v & 1 else tx0, y1, tz1 if v & 4 else tz0))
                continue
            verts.append((x1 if v & 1 else x0,
                          y1 if v & 2 else y0,
                          z1 if v & 4 else z0))
        for (c, s) in FACES:
            quads.append((base+c[0], base+c[1], base+c[2], base+c[3], s))
    return verts, quads


def project(verts, rotY, rotX, zoom, big):
    """Shared yaw/pitch/ortho transform — raycast_model_view's, exactly."""
    th = rotY*2*math.pi/256; ph = rotX*2*math.pi/256
    cy, sy = math.cos(th), math.sin(th); cx, sx = math.cos(ph), math.sin(ph)
    P, Z = [], []
    for (x, y, z) in verts:
        y -= 0.5
        x1 = x*cy + z*sy; z1 = -x*sy + z*cy
        y2 = y*cx - z1*sx; z2 = y*sx + z1*cx
        P.append((big/2 + x1*zoom, big/2 - y2*zoom)); Z.append(z2)
    return P, Z


def fit_zoom(verts, yaws, rotX, big, margin=0.94):
    """One zoom for the WHOLE set, sized so the widest pose still fits.

    The old hardcoded big*0.62 assumed a chair-sized footprint; a desk is 2.15
    model units wide and got clipped at the cardinal yaws. Sharing a single
    fitted zoom across views also keeps the scale stable as the engine's picker
    swaps sectors — a per-view fit would make the sprite breathe as you circle
    it."""
    legacy = big*0.62
    worst = 0.0
    for yw in yaws:
        P, _ = project(verts, yw, rotX, 1.0, 0.0)
        for (px, py) in P:
            worst = max(worst, abs(px), abs(py))
    if worst <= 0:
        return legacy
    # Only ever SHRINK. Enlarging a model that already fit would change the
    # intermediate render resolution and so re-quantize an asset that is
    # already shipped — the chair must re-bake bit-identical.
    return min(legacy, (big*margin*0.5)/worst)


def load_face_tex(header):
    """Parse a bake_face_tex.py header: (rows, W, H), values 1..4."""
    src = open(os.path.join(REPO, header)).read()
    W = int(re.search(r"_TEX_W (\d+)", src).group(1))
    H = int(re.search(r"_TEX_H (\d+)", src).group(1))
    # Tolerate whitespace inside the braces: bake_face_tex.py writes {3,3,3}
    # but pvm_bezel_edit.py writes { 3,3,3 }, so a tight regex silently parsed
    # ZERO rows from any hand-edited bezel and died on the assert below.
    rows = [[int(v) for v in m.group(1).replace(" ", "").split(",")]
            for m in re.finditer(r"\{\s*([\d,\s]+?)\s*\}", src)]
    rows = [r for r in rows if len(r) == W]
    assert len(rows) == H, "%s: parsed %d rows of width %d, expected %d" % (
        header, len(rows), W, H)
    return rows, W, H


def paint_face_tex(img, quad_pts, tex):
    """Affine-texture one projected quad (bl, tl, tr, br screen points) with
    corner-identity UVs — the same assignment the engine's tex_tri_lut uses,
    so the baked panel and the near 3D face agree pixel-for-pixel in spirit.
    Two barycentric triangles; covered pixels overwrite whatever the painter
    has laid down so far, exactly like the flat polygon fill it replaces."""
    rows, TW, TH = tex
    px = img.load()
    W, H = img.size
    uv = [(0.0, 1.0), (0.0, 0.0), (1.0, 0.0), (1.0, 1.0)]   # bl tl tr br
    for tri in ((0, 1, 2), (0, 2, 3)):
        (x0, y0), (x1, y1), (x2, y2) = (quad_pts[k] for k in tri)
        (u0, v0), (u1, v1), (u2, v2) = (uv[k] for k in tri)
        det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(det) < 1e-9:
            continue
        xmin = max(0, int(min(x0, x1, x2))); xmax = min(W - 1, int(max(x0, x1, x2)) + 1)
        ymin = max(0, int(min(y0, y1, y2))); ymax = min(H - 1, int(max(y0, y1, y2)) + 1)
        for y in range(ymin, ymax + 1):
            for x in range(xmin, xmax + 1):
                w1 = ((x - x0) * (y2 - y0) - (x2 - x0) * (y - y0)) / det
                w2 = ((x1 - x0) * (y - y0) - (x - x0) * (y1 - y0)) / det
                if w1 < 0 or w2 < 0 or w1 + w2 > 1:
                    continue
                u = u0 + w1 * (u1 - u0) + w2 * (u2 - u0)
                v = v0 + w1 * (v1 - v0) + w2 * (v2 - v0)
                tx = min(TW - 1, max(0, int(u * TW)))
                ty = min(TH - 1, max(0, int(v * TH)))
                px[x, y] = rows[ty][tx]


# The -z face of box 0: FACES order puts it 6th (index 5), and build_mesh
# emits box 0's quads first. Must track FACES/chair_face_v.
FRONT_QUAD = 5


# Texel value layout, matching build_standup_vmap in raycast.c:
#   0        transparent
#   1..4     the set's OWN ramp (dirset base), shade 0..3   <- the shipped one
#   5..9     the screen/ftex range, when a set has one
#   10..     EXTRA ramp slots, four values each, for COMPOSITE models
# Slot 0 is the model's own ramp so every set baked before this keeps its
# values byte-for-byte; extra slots are appended past the screen range so
# nothing already decoded has to move.
SLOT_EXTRA0 = 10


def render_idx(verts, tris, rotY, rotX, big=448, zoom=None, front_tex=None,
               stand_bias=0, box_ramp=None):
    """Ramp-index image: 0 transparent, values per the layout above. Same
    yaw/pitch/ortho/painter math as raycast_model_view.

    box_ramp is one slot per BOX, so a composite bakes each part into its own
    ramp — the desk set is gray monitor, brown desk and charcoal console, and
    a single-ramp bake has to paint two of those three wrong."""
    from PIL import Image, ImageDraw
    img = Image.new('L', (big, big), 0); d = ImageDraw.Draw(img)
    ZOOM = zoom if zoom else big*0.62
    P, Z = project(verts, rotY, rotX, ZOOM, big)
    quads = tris
    for t in sorted(range(len(quads)),
                    key=lambda t: -(Z[quads[t][0]] + Z[quads[t][1]]
                                  + Z[quads[t][2]] + Z[quads[t][3]])):
        a, b, c, e, s = quads[t]
        if front_tex is not None and t == FRONT_QUAD:
            # Face-5 vertex order (0,1,3,2) = bl, br, tr, tl in model space;
            # reorder to paint_face_tex's bl, tl, tr, br.
            paint_face_tex(img, [P[a], P[e], P[c], P[b]], front_tex)
            continue
        bi = t // 6                    # 6 quads per box
        r = max(0, min(3, (s-1)*5//7))
        slot = box_ramp[bi] if box_ramp else 0
        if slot:
            v = SLOT_EXTRA0 + (slot - 1) * 4 + r
        else:
            if bi > 0:                 # box 0 keeps its tone
                r = max(0, r - stand_bias)
            v = r + 1
        d.polygon([P[a], P[b], P[c], P[e]], fill=v)
    return img


def main():
    from PIL import Image, ImageDraw
    ap = argparse.ArgumentParser()
    ap.add_argument("--yaws", default="0,238,218,194,180,156,128",
                    help="comma list of model yaws (engine 0..255), front..back half circle")
    ap.add_argument("--pitch", default="auto",
                    help="camera pitch (engine 0..255), or 'auto' (default): "
                         "derived from the sprite's world_h so the bake camera "
                         "matches the player's real look-down angle at the "
                         "4-cell LOD swap — needs --sprite or --world-h")
    ap.add_argument("--sprite", default="",
                    help="registry.json assets.sprites id — source of world_h for auto pitch")
    ap.add_argument("--world-h", type=float, default=0.0,
                    help="model world height in cells (overrides --sprite lookup)")
    ap.add_argument("--height", type=int, default=56, help="sprite height in pixels")
    ap.add_argument("--out", default="")
    ap.add_argument("--strip", default="", help="optional validation strip PNG path")
    ap.add_argument("--png", default="", help="write each pose as RGBA PNG (%y = yaw); feeds bake_sprite.py")
    ap.add_argument("--header", default="sh_src/chair3d.h",
                    help="model header holding the cbox_t list")
    ap.add_argument("--symbol", default="chair_boxes", help="box array symbol in --header")
    ap.add_argument("--prefix", default="CHAIR", help="emitted macro/symbol prefix")
    ap.add_argument("--front-tex", default="",
                    help="bake_face_tex.py header to composite onto box 0's -z "
                         "(front) face — keeps the far billboard's panel in "
                         "step with the engine's textured near face. NOTE: the "
                         "engine x-mirrors half the circle, so an asymmetric "
                         "panel bakes mirrored on mirrored sectors.")
    ap.add_argument("--box-ramp", default="",
                    help="comma list, one RAMP SLOT per box, for COMPOSITE "
                         "models that mix materials: 0 = the set's own ramp "
                         "(values 1..4), 1.. = an extra slot (values 10+, four "
                         "each) that dirset_t.vbase maps to a CRAM base in "
                         "raycast.c. Must match the model's boxmodels[] "
                         "box_base grouping. Desk set: 0,1,1,1,1,1,2,2 — gray "
                         "monitor, brown desk, charcoal console.")
    ap.add_argument("--stand-bias", type=int, default=0,
                    help="shade steps subtracted from every box except box 0 "
                         "— must match the model's boxmodels[] stand_bias in "
                         "raycast.c or the LOD swap pops tone (PVM: 2)")
    ap.add_argument("--sprite-pose", default="",
                    help="also emit sh_src/spr_<sprite>_tex.h from this yaw's "
                         "baked view, verbatim — the flat standee (editor, "
                         "asset preview) then shows the exact engine pose on "
                         "the exact ramp, instead of a re-render re-quantized "
                         "through bake_sprite. Values carry over unchanged: "
                         "standee decode base+v lands on the same CRAM rows "
                         "as the dir decode ramp+(v-1). Needs --sprite.")
    args = ap.parse_args()
    box_ramp = [int(v) for v in args.box_ramp.split(",")] if args.box_ramp else None
    if box_ramp is not None:
        nb = len(load_boxes(args.header, args.symbol))
        if len(box_ramp) != nb:
            sys.exit("--box-ramp has %d entries, %s has %d boxes"
                     % (len(box_ramp), args.symbol, nb))
    # Pose provenance: pitch is DERIVED, not eyeballed, so every import bakes
    # the same way. The player's eye (STAND_EYE 128 = 0.5 cell) looks down at
    # the model's top by atan((eye - world_h) / 4) at the 4-cell LOD swap
    # (CHAIR_CULL_D2) — bake from that angle and the billboard sits on the
    # walking plane like the 3D pop-in (a taller-than-eye model gets a
    # slight look-UP, which is equally correct). Desk: world_h 0.31 -> 254.
    if args.pitch == "auto":
        wh = args.world_h
        if not wh and args.sprite:
            import json
            reg = json.load(open(os.path.join(REPO, "registry.json")))
            hits = [s for s in reg["assets"]["sprites"] if s["id"] == args.sprite]
            if not hits:
                sys.exit("--sprite %r not in registry.json assets.sprites" % args.sprite)
            wh = hits[0]["world_h"]
        if not wh:
            sys.exit("--pitch auto needs --sprite <id> or --world-h <cells>")
        EYE, SWAP = 0.5, 4.0
        pitch = (256 - round(math.atan2(EYE - wh, SWAP) * 256 / (2 * math.pi))) % 256
        print("auto pitch: world_h %.3f -> pitch %d" % (wh, pitch))
    else:
        pitch = int(args.pitch) & 255
    yaws = [int(y) & 255 for y in args.yaws.split(",")]
    H = args.height
    PRE = args.prefix.upper()
    pre = PRE.lower()
    if not args.out:
        args.out = "sh_src/%s_dir_tex.h" % pre

    verts, tris = build_mesh(load_boxes(args.header, args.symbol))
    BIG = 448
    # RENDER at the NEGATED yaw. draw_chair_3d's facing rotation is the exact
    # x-mirror of this baker's raw rotation (rx = wx*fc + wz*fs with
    # fs = -sin), so the in-game 3D model at facing yaw t looks like the raw
    # bake at -t. Baking each labeled yaw from its negation makes the far
    # billboard agree with the near 3D render; sector tables are unchanged.
    # The chair never showed the difference (x-symmetric); the desk's LOD
    # swap visibly x-flipped.
    rend = [(256 - yw) & 255 for yw in yaws]
    zoom = fit_zoom(verts, rend, pitch, BIG)
    # All views share ONE vertical band (the union of every pose's content
    # rows). Cropping each view to its own content made H rows mean a
    # different world height per view — and always MORE than the model's
    # front elevation, because the pitched camera adds top-face rows. The
    # engine sizes the blit as world_h == H rows, so every view drew small,
    # by a view-dependent factor (desk: 9%..21%). With a shared band, H rows
    # = one world span, emitted below as VSPAN so the engine can inflate the
    # blit and land the model's BODY at exactly world_h on screen.
    ftex = load_face_tex(args.front_tex) if args.front_tex else None
    renders = []
    y0g, y1g = BIG, 0
    for yw, rw in zip(yaws, rend):
        im = render_idx(verts, tris, rw, pitch, BIG, zoom, front_tex=ftex,
                        stand_bias=args.stand_bias, box_ramp=box_ramp)
        bb = im.getbbox()
        if bb is None:
            sys.exit("pose %d rendered empty" % yw)
        renders.append((yw, im, bb))
        y0g, y1g = min(y0g, bb[1]), max(y1g, bb[3])
    band_h = y1g - y0g
    # Projected front-elevation height: model y-span is 1.0 by convention,
    # foreshortened only by the camera pitch (see project()).
    front_px = math.cos(pitch * 2 * math.pi / 256) * zoom
    vspan = max(1, round(band_h / front_px * 256))
    views = []
    for yw, im, bb in renders:
        im = im.crop((bb[0], y0g, bb[2], y1g))
        w = max(1, round(H * im.width / im.height))
        views.append((yw, w, im.resize((w, H), Image.NEAREST)))

    out = os.path.join(REPO, args.out)
    with open(out, "w") as f:
        f.write("/* Auto-generated by tools/bake_dir_sprites.py from the in-game box model\n")
        f.write(" * (%s). Poses: pitch %d, yaws %s. Do not edit.\n" % (args.header, pitch, yaws))
        f.write(" * Values: 0 transparent, 1..5 -> DOOR_BASE+(v-1) via the sprite vmap\n")
        f.write(" * (distance fog applies). ROW-MAJOR tex[y][x]; X-mirror covers the\n")
        f.write(" * other half circle in the engine's 12-sector picker. */\n")
        f.write("#ifndef %s_DIR_TEX_H_INCLUDED\n#define %s_DIR_TEX_H_INCLUDED\n#include <stdint.h>\n\n" % (PRE, PRE))
        wmax = max(w for _, w, _ in views)
        f.write("#define %s_DIR_VIEWS  %d\n#define %s_DIR_H      %d\n" % (PRE, len(views), PRE, H))
        f.write("/* Widest view — the engine sizes its decode scratch from this so a\n")
        f.write(" * re-bake at any --height stays in bounds (no fixed-40 overflow). */\n")
        f.write("#define %s_DIR_WMAX   %d\n" % (PRE, wmax))
        f.write("/* 8.8: shared image band height / model front-elevation height. The\n")
        f.write(" * engine multiplies the blit height by this so the model BODY spans\n")
        f.write(" * exactly world_h on screen (the band's extra rows are the pitched\n")
        f.write(" * camera's view of the top face, drawn above). */\n")
        f.write("#define %s_DIR_VSPAN  %d\n\n" % (PRE, vspan))
        for i, (yw, w, im) in enumerate(views):
            f.write("/* view %d: model yaw %d */\n" % (i, yw))
            f.write("#define %s_DIR_W%d %d\n" % (PRE, i, w))
            f.write("static const uint8_t %s_dir_tex%d[%s_DIR_H][%d] = {\n" % (pre, i, PRE, w))
            for y in range(H):
                f.write("    {" + ",".join(str(im.getpixel((x, y))) for x in range(w)) + "},\n")
            f.write("};\n\n")
        f.write("typedef struct { const uint8_t *tex; uint8_t w; } %s_dir_view_t;\n" % pre)
        f.write("static const %s_dir_view_t %s_dir_views[%s_DIR_VIEWS] = {\n" % (pre, pre, PRE))
        for i, (yw, w, im) in enumerate(views):
            f.write("    { (const uint8_t*)%s_dir_tex%d, %s_DIR_W%d },\n" % (pre, i, PRE, i))
        f.write("};\n\n")

        # Sector tables: every baked yaw is a direct sector; every non-self-mirror
        # yaw also serves its reflection (256-yaw) with texX reversed. The engine
        # picker just argmaxes dot products over these — view count is data.
        sectors = [(yw % 256, i, 0) for i, (yw, w, im) in enumerate(views)]
        for i, (yw, w, im) in enumerate(views):
            m = (256 - yw) % 256
            if m != yw % 256:
                sectors.append((m, i, 1))
        sectors.sort()
        f.write("/* Bearing sectors: normalized view yaw, sprite index, mirror flag.\n")
        f.write(" * The picker maximizes dot(chair->player, dir(facing+128+v)). */\n")
        f.write("#define %s_DIR_SECTORS %d\n" % (PRE, len(sectors)))
        f.write("static const uint8_t %s_dir_sect_v[%s_DIR_SECTORS] = {\n    %s };\n"
                % (pre, PRE, ",".join(str(v) for v, s, m in sectors)))
        f.write("static const uint8_t %s_dir_sect_view[%s_DIR_SECTORS] = {\n    %s };\n"
                % (pre, PRE, ",".join(str(s) for v, s, m in sectors)))
        f.write("static const uint8_t %s_dir_sect_mirror[%s_DIR_SECTORS] = {\n    %s };\n"
                % (pre, PRE, ",".join(str(m) for v, s, m in sectors)))
        f.write("\n#endif\n")
    print("wrote %s  views: %s" % (args.out, [(yw, w) for yw, w, _ in views]))

    if args.sprite_pose:
        if not args.sprite:
            sys.exit("--sprite-pose needs --sprite <id>")
        want = int(args.sprite_pose) & 255
        hit = [(yw, w, im) for yw, w, im in views if yw == want]
        if not hit:
            sys.exit("--sprite-pose %d is not one of the baked yaws %s"
                     % (want, [yw for yw, _, _ in views]))
        yw, w, im = hit[0]
        sid = args.sprite
        SID = sid.upper()
        sp = os.path.join(REPO, "sh_src", "spr_%s_tex.h" % sid)
        with open(sp, "w") as f:
            f.write("#ifndef SPR_%s_TEX_H_INCLUDED\n#define SPR_%s_TEX_H_INCLUDED\n\n"
                    % (SID, SID))
            f.write("#include <stdint.h>\n\n")
            f.write("/* AUTO-GENERATED by tools/bake_dir_sprites.py --sprite-pose %d —\n"
                    " * the yaw-%d directional view emitted as the flat standee, so the\n"
                    " * editor/asset-preview sprite is the engine pose on the engine ramp.\n"
                    " * 0 = transparent, v -> CRAM base + v. Row-major. Do not edit. */\n"
                    % (yw, yw))
            f.write("#define SPR_%s_TEX_WIDTH  %d\n" % (SID, w))
            f.write("#define SPR_%s_TEX_HEIGHT %d\n\n" % (SID, H))
            f.write("static const uint8_t spr_%s_tex[SPR_%s_TEX_HEIGHT][SPR_%s_TEX_WIDTH] = {\n"
                    % (sid, SID, SID))
            # The standee is a still, so it ships the POWERED-OFF look: glass
            # core (5) stays the dark-glass marker, rim texels (6..9) collapse
            # back to their albedo — the corner shading that reads as curved
            # glass. Also keeps every value <= 5 for the editor's decode.
            for y in range(H):
                f.write("    {" + ",".join(
                    str(v - 5 if v > 5 else v)
                    for v in (im.getpixel((x, y)) for x in range(w))) + "},\n")
            f.write("};\n\n#endif\n")
        # world_hw derives from the standee texture's aspect, same formula as
        # bake_sprite's registry_entries.
        import json as _json
        regp = os.path.join(REPO, "registry.json")
        reg2 = _json.load(open(regp))
        ent = [s for s in reg2["assets"]["sprites"] if s["id"] == sid][0]
        ent["world_hw"] = round(ent["world_h"] * (w / float(H)) / 2.0, 4)
        _json.dump(reg2, open(regp, "w"), indent=1, ensure_ascii=False)
        open(regp, "a").write("\n")
        print("wrote %s (%dx%d, yaw %d) + world_hw %.4f"
              % (sp, w, H, yw, ent["world_hw"]))

    if args.png:
        # Emit each pose as an RGBA PNG in the door ramp, so the SAME box model
        # and the SAME engine pose can feed bake_sprite.py's standee path. A
        # standee rendered from some other camera won't sit on the floor the way
        # the 3D model does, and the LOD swap pops.
        DOOR = [(15*8,12*8,9*8),(18*8,15*8,11*8),(22*8,18*8,14*8),
                (25*8,21*8,16*8),(29*8,25*8,20*8)]
        for yw, w, im in views:
            rgba = Image.new('RGBA', im.size, (0, 0, 0, 0))
            for y in range(im.height):
                for x in range(im.width):
                    v = im.getpixel((x, y))
                    if v:
                        rgba.putpixel((x, y), DOOR[v-1] + (255,))
            path = args.png.replace("%y", str(yw))
            rgba.save(path)
            print("wrote pose png:", path)

    if args.strip:
        DOOR = [(15*8,12*8,9*8),(18*8,15*8,11*8),(22*8,18*8,14*8),(25*8,21*8,16*8),(29*8,25*8,20*8)]
        sc = 4
        tot = sum(w for _, w, _ in views)
        sheet = Image.new('RGB', ((tot+len(views)*3)*sc, H*sc+16), (40, 40, 44))
        dd = ImageDraw.Draw(sheet)
        x0 = 0
        for yw, w, im in views:
            for y in range(H):
                for x in range(w):
                    v = im.getpixel((x, y))
                    if v:
                        for dy in range(sc):
                            for dx in range(sc):
                                sheet.putpixel((x0+x*sc+dx, y*sc+dy), DOOR[v-1])
            dd.text((x0, H*sc+2), str(yw), fill=(255, 230, 0))
            x0 += (w+3)*sc
        sheet.save(args.strip)
        print("wrote strip:", args.strip)


if __name__ == "__main__":
    main()
