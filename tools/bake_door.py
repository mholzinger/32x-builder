#!/usr/bin/env python3
"""Bake a rendered door PNG (tools/render_glb.py) into sh_src/door_tex.h — a
palette-indexed BILLBOARD texture (like the neanderthal) for the Backrooms
"door that never opens": a grey metal fire door with a green running-man EXIT
sign. Transparent background -> index 0 (not drawn). Green sign pixels map to a
green ramp; the metal slab is contrast-stretched across a grey ramp so the
frame / handle / hinges read instead of collapsing to a flat slab.

Stored value: 0 = transparent, 1..8 = offset+1 into the DOOR_BASE ramp
(1..5 grey dark->light, 6..7 green, 8 white). Also writes a preview PNG.

Usage: tools/bake_door.py [--w 64] [--h 128] [--src ...] [--flip] [--out ...]
"""
import argparse
import random
from PIL import Image


def _value_noise(seed, gw, gh):
    """Smooth value-noise sampler on [0,1]x[0,1] — MUST match the seeds/sizes in
    tools/door_grime_variants.py so --grime N reproduces door_grime_NN.png."""
    rnd = random.Random(seed)
    grid = [[rnd.random() for _ in range(gw + 1)] for _ in range(gh + 1)]
    def sample(fx, fy):
        gx, gy = fx * gw, fy * gh
        x0 = min(int(gx), gw - 1); y0 = min(int(gy), gh - 1)
        tx, ty = gx - x0, gy - y0
        sx = tx * tx * (3 - 2 * tx); sy = ty * ty * (3 - 2 * ty)
        v00, v10 = grid[y0][x0], grid[y0][x0 + 1]
        v01, v11 = grid[y0 + 1][x0], grid[y0 + 1][x0 + 1]
        a = v00 + (v10 - v00) * sx
        b = v01 + (v11 - v01) * sx
        return a + (b - a) * sy
    return sample

# DOOR_BASE ramp as 32X 0..31 colors (must match raycast.c). 0-based; texture
# value v>0 maps to DOOR_BASE + (v-1).
RAMP = [
    (6, 6, 7),       # 0 metal shadow / recess (darkest)
    (10, 10, 12),    # 1 metal dark (frame)
    (15, 15, 17),    # 2 metal mid (leaf)
    (19, 19, 21),    # 3 metal light
    (24, 24, 26),    # 4 metal highlight / handle glint
    (3, 12, 6),      # 5 sign green dark
    (8, 19, 10),     # 6 sign green light
    (29, 30, 28),    # 7 white (figure/text/bright edge)
]
NGREY = 5  # values 1..5 are grey (RAMP 0..4)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--w", type=int, default=64)
    ap.add_argument("--h", type=int, default=128)
    ap.add_argument("--src", default="models/render/door_front.png")
    ap.add_argument("--flip", action="store_true")
    ap.add_argument("--out", default="sh_src/door_tex.h")
    ap.add_argument("--preview", default="models/render/door_baked_preview.png")
    ap.add_argument("--grime", type=int, default=0,
                    help="0-19 baked grime/wear level (matches door_grime_NN.png)")
    ap.add_argument("--stipple", type=int, default=0,
                    help="0-11 baked stipple/dither level (matches door_stipple_NN.png)")
    args = ap.parse_args()
    W, H = args.w, args.h

    src = Image.open(args.src).convert("RGBA")
    bbox = src.split()[3].getbbox()
    if bbox:
        src = src.crop(bbox)
    if args.flip:
        src = src.transpose(Image.FLIP_LEFT_RIGHT)
    src = src.resize((W, H), Image.LANCZOS)
    px = src.load()

    def is_green(r, g, b):
        return g > r + 18 and g > b + 18
    def is_white(r, g, b):
        return r > 200 and g > 200 and b > 200

    # Contrast-stretch the metal: gather grey-pixel luminances, use 4th/96th
    # percentiles so the narrow slab range spans the 5 grey steps.
    greys = []
    for y in range(H):
        for x in range(W):
            r, g, b, a = px[x, y]
            if a >= 128 and not is_green(r, g, b) and not is_white(r, g, b):
                greys.append((r + g + b) // 3)
    greys.sort()
    if greys:
        lo = greys[int(len(greys) * 0.04)]
        hi = greys[int(len(greys) * 0.96)]
    else:
        lo, hi = 0, 255
    if hi <= lo:
        hi = lo + 1

    def classify(r, g, b, a):
        if a < 128:
            return 0   # transparent marker -> the door fill paints the wall colour here
        if is_white(r, g, b):
            return 8
        if is_green(r, g, b):
            lum = (r + g + b) / 3
            return 6 if lum < 110 else 7
        lum = (r + g + b) / 3
        t = max(0.0, min(1.0, (lum - lo) / (hi - lo)))
        return 1 + min(NGREY - 1, int(t * NGREY))

    rows = [[classify(*px[x, y]) for x in range(W)] for y in range(H)]

    # Clean the worn/faded bottom: the source door render lightens toward the
    # floor, leaving a pale scuffed band. Cap the bottom fifth's grey/white to
    # the door-body level so the slab reads as clean, uniform metal. (Colour is
    # set by the palette, not here, so the muted brown is unaffected.)
    BODY_V = 2
    for y in range(int(H * 0.80), H):
        for x in range(W):
            v = rows[y][x]
            if v == 0 or v in (6, 7):     # leave transparent + green sign alone
                continue
            if v > BODY_V:                # pale grey + white scuff -> body grey
                rows[y][x] = BODY_V

    # Baked grime/wear (matches tools/door_grime_variants.py at this --grime
    # level). Organic value-noise blotches + vertical streaks darken the metal
    # greys; rare scuffs lighten. Texel grey 1..5 == GREY ramp index 4..8, so
    # work in that space then clamp back to 1..5 (dark dips pin at DOOR_BASE+0).
    if args.grime > 0:
        amt = max(0, min(19, args.grime)) / 19.0
        coarse = _value_noise(11, 5, 11)
        fine   = _value_noise(22, 13, 26)
        streak = _value_noise(33, 7, 2)
        scuff  = _value_noise(44, 20, 40)
        for y in range(H):
            fy = y / (H - 1)
            for x in range(W):
                v = rows[y][x]
                if not (1 <= v <= 5):     # only the metal greys
                    continue
                fx = x / (W - 1)
                dirt = 0.55 * coarse(fx, fy) + 0.45 * fine(fx, fy)
                dirt = 0.7 * dirt + 0.3 * streak(fx, fy)
                darken = amt * (dirt ** 1.5) * 5.5
                light  = amt * max(0.0, scuff(fx, fy) - 0.78) * 6.0
                gi = (v + 3) - darken + light            # GREY-ramp space (4..8)
                rows[y][x] = max(1, min(5, int(round(gi)) - 3))

    # Baked STIPPLE/dither (matches tools/door_stipple_variants.py). One jittered
    # dot per 2x2 cell, kept by a wear weight (denser at edges + bottom), so the
    # door reads engraved — but baked into the texel grid = ZERO runtime cost
    # (vs the per-pixel version, which was too hot). Darkens the metal greys 1-2.
    if args.stipple > 0:
        lvl = max(1, min(11, args.stipple))
        rnd = random.Random(1234 + lvl)
        keep_scale = lvl / 6.0
        cell = 2
        for cy in range(0, H, cell):
            for cx in range(0, W, cell):
                dx = cx + rnd.randint(0, cell - 1)
                dy = cy + rnd.randint(0, cell - 1)
                if dx >= W or dy >= H:
                    continue
                v = rows[dy][dx]
                if not (1 <= v <= 5):
                    continue
                fx = dx / (W - 1); fy = dy / (H - 1)
                edge = min(fx, 1 - fx) * 2.0
                wear = (0.45 + 0.55 * fy) * (1.0 - 0.5 * edge) * keep_scale
                if rnd.random() > wear:
                    continue
                rows[dy][dx] = v + 8     # grey 1..5 -> soft stipple shade 9..13

    # Leaf bounding box = the door slab. The EXIT sign lives in the top band;
    # below it, every non-transparent pixel is the swinging door. Compute the
    # tight bbox so raycast.c animates EXACTLY the slab (no eyeballed inset).
    SIGN_BAND = max(1, H // 8)        # top ~1/8 is the sign + mounting gap
    xs, ys = [], []
    for y in range(SIGN_BAND, H):
        for x in range(W):
            if rows[y][x] != 0:
                xs.append(x); ys.append(y)
    if xs:
        lx0, lx1, ly0, ly1 = min(xs), max(xs), min(ys), max(ys)
    else:
        lx0, lx1, ly0, ly1 = 0, W - 1, SIGN_BAND, H - 1

    # --- Clean up the slab: contiguous L/R frame jambs + a crisp lever handle ---
    # The GLB render's narrow grey range contrast-stretches into broken edges and
    # a mushy knob; redraw them cleanly over the baked texels.
    def setpx(x, y, v):
        if 0 <= x < W and 0 <= y < H and rows[y][x] != 0:
            rows[y][x] = v

    FRAME_V = 18                                 # dedicated muted-brown jamb shade
    JAMB_W = 3                                   # frame thickness each side
    BODY_V = 2                                   # door-body grey (for wipes)
    # Per-row: paint a CONTIGUOUS jamb in from each slab edge — covers the dark
    # leaf/frame gap line that was breaking the strip.
    for y in range(ly0, ly1 + 1):
        row_xs = [x for x in range(lx0, lx1 + 1) if rows[y][x] != 0]
        if not row_xs:
            continue
        L, R = min(row_xs), max(row_xs)
        for k in range(JAMB_W):
            setpx(L + k, y, FRAME_V)
            setpx(R - k, y, FRAME_V)
    # Top AND bottom jambs — horizontal strips matching the sides, so the casing
    # reads continuous on all four sides (the bottom seam was missing).
    for y in range(ly0, ly0 + JAMB_W):
        for x in range(lx0, lx1 + 1):
            setpx(x, y, FRAME_V)
    for y in range(ly1 - JAMB_W + 1, ly1 + 1):
        for x in range(lx0, lx1 + 1):
            setpx(x, y, FRAME_V)

    # The door opens INWARD, so the hinges (texture-RIGHT edge) wouldn't be seen.
    # Wipe them to body so the hinge side is a flush jamb line top-to-bottom.
    for y in range(ly0 + JAMB_W, ly1 - JAMB_W + 1):
        for x in range(lx1 - JAMB_W - 2, lx1 - JAMB_W + 1):
            setpx(x, y, BODY_V)

    # Handle: small escutcheon + lever, sat FLUSH against the latch jamb (the
    # texture-LEFT side; mirrors to screen-right). Wipe knob leftovers first
    # (clamped so it never eats the jamb).
    hy = ly0 + int((ly1 - ly0) * 0.55)
    hx = lx0 + JAMB_W + 2                         # plate flush just inside the latch jamb
    for yy in range(hy - 6, hy + 7):
        for xx in range(max(lx0 + JAMB_W, hx - 6), hx + 9):
            setpx(xx, yy, BODY_V)
    H_DARK, H_MID, H_HI = 14, 15, 17             # bronze handle texel values
    for yy in range(hy - 4, hy + 5):             # small backplate
        for xx in range(hx - 1, hx + 3):
            setpx(xx, yy, H_MID)
        setpx(hx - 2, yy, H_DARK)                # plate shadow edge (flush to jamb)
    for i in range(0, 7):                        # lever bar, pointing inward
        setpx(hx + 3 + i, hy, H_HI)
        setpx(hx + 3 + i, hy + 1, H_DARK)        # under-shadow
    setpx(hx + 1, hy, H_HI)                      # highlight on the plate

    with open(args.out, "w") as f:
        f.write("#ifndef DOOR_TEX_H_INCLUDED\n#define DOOR_TEX_H_INCLUDED\n")
        f.write("#include <stdint.h>\n\n")
        f.write("/* AUTO-GENERATED by tools/bake_door.py from a rendered GLB door.\n")
        f.write(" * Billboard texture: 0=transparent, v>0 -> DOOR_BASE+(v-1)\n")
        f.write(" * (1..5 grey metal, 6..7 green EXIT sign, 8 white). */\n")
        f.write("#define DOOR_TEX_WIDTH  %d\n" % W)
        f.write("#define DOOR_TEX_HEIGHT %d\n" % H)
        f.write("/* INNER PANEL bounds (the swinging leaf) — the slab bbox inset by the\n")
        f.write(" * jamb width on top/left/right (bottom meets the floor). Only this\n")
        f.write(" * region animates; the frame/jamb + sign stay static (a real door\n")
        f.write(" * swinging inside its fixed casing). */\n")
        f.write("#define DOOR_LEAF_X0 %d\n" % (lx0 + JAMB_W))
        f.write("#define DOOR_LEAF_X1 %d\n" % (lx1 - JAMB_W))
        f.write("#define DOOR_LEAF_Y0 %d\n" % (ly0 + JAMB_W))
        f.write("#define DOOR_LEAF_Y1 %d\n\n" % (ly1 - JAMB_W))
        f.write("/* COLUMN-MAJOR: door_tex[x][y] — walking down a screen column reads\n")
        f.write(" * sequential bytes (cache-friendly), like the neanderthal hi-res. */\n")
        f.write("static const uint8_t door_tex[DOOR_TEX_WIDTH][DOOR_TEX_HEIGHT] = {\n")
        for x in range(W):
            col = [rows[y][x] for y in range(H)]
            f.write("  {" + ",".join(str(v) for v in col) + "},\n")
        f.write("};\n\n#endif\n")

    prev = Image.new("RGB", (W, H), (28, 28, 30))
    pp = prev.load()
    for y in range(H):
        for x in range(W):
            v = rows[y][x]
            if v:
                if v == 18:                       # muted-brown jamb
                    r, g, b = (21, 18, 15)
                elif v >= 14:                     # muted bronze handle hardware
                    r, g, b = [(10,8,5),(15,12,8),(21,17,12),(26,22,16)][v - 14]
                else:
                    rv = v - 8 if v >= 9 else v   # stipple previews as base grey
                    r, g, b = RAMP[rv - 1]
                pp[x, y] = (r * 8, g * 8, b * 8)
    prev.resize((W * 4, H * 4), Image.NEAREST).save(args.preview)
    print("wrote %s (%dx%d) lo=%d hi=%d + preview %s" % (args.out, W, H, lo, hi, args.preview))


if __name__ == "__main__":
    main()
