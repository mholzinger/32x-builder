#!/usr/bin/env python3
"""Bake a box-model FACE texture from a flat render (tools/render_glb.py):
crop to content, resize, quantize to the sprite's registry ramp, emit a
row-major header the engine's textured-face path (raycast.c tex_quad_lut)
and the dir-billboard baker (bake_dir_sprites.py --front-tex) both consume.

    tools/bake_face_tex.py --src render_front.png --sprite pvm \
        --out sh_src/pvm_front_tex.h --prefix PVM_FRONT --width 48

Values: 1..vmax index the sprite's CRAM ramp (registry pal, darkest first),
decoded in-game as ramp_base + (v-1) - fog. There is NO transparent 0: a box
face is opaque by definition, and emitting 0 would punch see-through holes
in the model. --vmax (default 4) matches the engine's 4-deep box ramp; render
pixels nearest a brighter pal entry clamp to it.
"""
import argparse, json, os, re, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    from PIL import Image
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True, help="RGBA render of the face (front view)")
    ap.add_argument("--sprite", required=True, help="registry sprite id — ramp source")
    ap.add_argument("--out", required=True)
    ap.add_argument("--prefix", required=True, help="macro prefix, e.g. PVM_FRONT")
    ap.add_argument("--width", type=int, default=48)
    ap.add_argument("--height", type=int, default=0,
                    help="0 = keep the cropped aspect")
    ap.add_argument("--vmax", type=int, default=4,
                    help="highest emitted value (ramp depth)")
    ap.add_argument("--auto-level", action="store_true",
                    help="stretch the render's 5th..95th luminance percentiles "
                         "across the full ramp — render_glb.py lights front "
                         "faces dimly (0.22 ambient, high key), so a raw "
                         "quantize wastes the ramp's bright half")
    ap.add_argument("--screen-ellipse", default="",
                    help="cx,cy,rx,ry as FRACTIONS of the face — the GLASS "
                         "CORE, marked vmax+1: static when powered, uniform "
                         "dark when off. The PVM's tube face: "
                         "0.51,0.42,0.40,0.30")
    ap.add_argument("--screen-rect", default="",
                    help="x0,y0,x1,y1 fractions — the full bezel opening. "
                         "Texels inside (outside the ellipse core) keep their "
                         "baked albedo, offset by vmax+1 (values 6..9): static "
                         "floods the whole 4:3 opening when powered, but off "
                         "they render their albedo — the corner shading that "
                         "reads as CRT glass curvature. The PVM's opening: "
                         "0.125,0.109,0.906,0.717")
    args = ap.parse_args()

    reg = json.load(open(os.path.join(REPO, "registry.json")))
    hits = [s for s in reg["assets"]["sprites"] if s["id"] == args.sprite]
    if not hits:
        sys.exit("--sprite %r not in registry.json assets.sprites" % args.sprite)
    pal31 = hits[0]["pal"]
    pal8 = [tuple(c * 255 // 31 for c in e) for e in pal31]

    img = Image.open(args.src).convert("RGBA")
    bb = img.split()[3].getbbox()
    if bb:
        img = img.crop(bb)
    W = args.width
    H = args.height or max(1, round(W * img.height / img.width))
    img = img.resize((W, H), Image.LANCZOS)
    px = img.load()

    def lum(r, g, b):
        return (r * 299 + g * 587 + b * 114) // 1000

    if args.auto_level:
        ls = sorted(lum(*px[x, y][:3]) for y in range(H) for x in range(W)
                    if px[x, y][3] >= 128)
        lo = ls[len(ls) * 5 // 100] if ls else 0
        hi = ls[len(ls) * 95 // 100] if ls else 255
        if hi <= lo:
            hi = lo + 1

        def value(r, g, b):
            t = (lum(r, g, b) - lo) / (hi - lo)
            return max(1, min(args.vmax, 1 + int(t * args.vmax)))
    else:
        def value(r, g, b):
            best, bd = 1, 1 << 30
            for i, (pr, pg, pb) in enumerate(pal8):
                d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
                if d < bd:
                    bd, best = d, i + 1
            return min(best, args.vmax)

    rows = []
    for y in range(H):
        row = []
        for x in range(W):
            r, g, b, a = px[x, y]
            # A transparent source pixel inside the face crop (antialiased
            # corner) still needs paint — collapse it to the darkest entry.
            row.append(value(r, g, b) if a >= 128 else 1)
        rows.append(row)

    if args.screen_rect:
        x0, y0, x1, y1 = (float(v) for v in args.screen_rect.split(","))
        for y in range(H):
            for x in range(W):
                fx, fy = (x + 0.5) / W, (y + 0.5) / H
                if x0 <= fx <= x1 and y0 <= fy <= y1:
                    rows[y][x] += args.vmax + 1          # albedo-carrying rim
    if args.screen_ellipse:
        cx, cy, rx, ry = (float(v) for v in args.screen_ellipse.split(","))
        for y in range(H):
            for x in range(W):
                dx = (x + 0.5) / W - cx
                dy = (y + 0.5) / H - cy
                if (dx / rx) ** 2 + (dy / ry) ** 2 <= 1.0:
                    rows[y][x] = args.vmax + 1           # glass core

    P = args.prefix.upper()
    guard = "%s_TEX_H_INCLUDED" % P
    with open(os.path.join(REPO, args.out), "w") as f:
        f.write("/* Auto-generated by tools/bake_face_tex.py from %s.\n"
                % os.path.basename(args.src))
        f.write(" * Box-face texture for sprite '%s': OPAQUE, values 1..%d ->\n"
                % (args.sprite, args.vmax))
        f.write(" * ramp_base + (v-1), fog-clamped by the caller's LUT. "
                "ROW-MAJOR tex[y][x]. Do not edit. */\n")
        f.write("#ifndef %s\n#define %s\n#include <stdint.h>\n\n" % (guard, guard))
        f.write("#define %s_TEX_W %d\n#define %s_TEX_H %d\n\n" % (P, W, P, H))
        f.write("static const uint8_t %s_tex[%s_TEX_H][%s_TEX_W] = {\n"
                % (P.lower(), P, P))
        for row in rows:
            f.write("    {" + ",".join(str(v) for v in row) + "},\n")
        f.write("};\n\n#endif /* %s */\n" % guard)
    print("wrote %s (%dx%d, ramp 1..%d)" % (args.out, W, H, args.vmax))


if __name__ == "__main__":
    main()
