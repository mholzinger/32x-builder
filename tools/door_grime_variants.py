#!/usr/bin/env python3
"""Generate door grime/wear PREVIEW variants (levels 0..19) into images/ for
spot-checking. Each renders the door in the in-game muted-brown palette with an
organic baked-in grime/wear pass (smooth value-noise blotches + vertical
streaking + edge wear), scaled by the level. Pick one, then we bake that level
into door_tex.h.

    python3 tools/door_grime_variants.py
"""
import argparse, random
from PIL import Image

# Door grey ramp, dark->light, matching DOOR_DARK_BASE(4) + DOOR_BASE(5) in
# raycast.c. 0-31 channel values; *8 for 0-255 preview.
GREY = [(4,3,2),(7,5,3),(9,7,5),(12,10,7),
        (15,12,9),(18,15,11),(22,18,14),(25,21,16),(29,25,20)]
GREEN_D=(3,12,6); GREEN_L=(8,19,10); WHITE=(29,30,28); WALL=(15,14,9)


def value_noise(seed, gw, gh):
    """Smooth (smoothstep-interpolated) value noise sampler on [0,1]x[0,1]."""
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="models/render/door_front.png")
    ap.add_argument("--w", type=int, default=64)
    ap.add_argument("--h", type=int, default=128)
    ap.add_argument("--scale", type=int, default=5)
    ap.add_argument("--outdir", default="images")
    args = ap.parse_args()
    W, H = args.w, args.h

    src = Image.open(args.src).convert("RGBA")
    bbox = src.split()[3].getbbox()
    if bbox:
        src = src.crop(bbox)
    src = src.resize((W, H), Image.LANCZOS)
    px = src.load()

    def is_green(r, g, b): return g > r + 18 and g > b + 18
    def is_white(r, g, b): return r > 200 and g > 200 and b > 200

    greys = []
    for y in range(H):
        for x in range(W):
            r, g, b, a = px[x, y]
            if a >= 128 and not is_green(r, g, b) and not is_white(r, g, b):
                greys.append((r + g + b) // 3)
    greys.sort()
    lo = greys[int(len(greys) * 0.04)] if greys else 0
    hi = greys[int(len(greys) * 0.96)] if greys else 255
    if hi <= lo:
        hi = lo + 1

    # Classify each texel: ('t'ransparent | 'm'etal+greyidx | 'g'reen | 'w'hite).
    kind = [[None] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            r, g, b, a = px[x, y]
            if a < 128:
                kind[y][x] = ('t', 0)
            elif is_white(r, g, b):
                kind[y][x] = ('w', 0)
            elif is_green(r, g, b):
                lum = (r + g + b) / 3
                kind[y][x] = ('g', 0 if lum < 110 else 1)
            else:
                lum = (r + g + b) / 3
                t = max(0.0, min(1.0, (lum - lo) / (hi - lo)))
                kind[y][x] = ('m', 4 + min(4, int(t * 5)))   # lit door = GREY[4..8]

    # Clean the worn/faded bottom (cap to body) so grime is the only wear.
    for y in range(int(H * 0.80), H):
        for x in range(W):
            k = kind[y][x]
            if k[0] == 'm' and k[1] > 6:
                kind[y][x] = ('m', 6)

    coarse = value_noise(11, 5, 11)    # big dirt blotches
    fine   = value_noise(22, 13, 26)   # finer grime
    streak = value_noise(33, 7, 2)     # vertical run-down streaking
    scuff  = value_noise(44, 20, 40)   # sparse light scuffs

    for level in range(20):
        amt = level / 19.0
        img = Image.new("RGB", (W, H), (12, 12, 14))
        out = img.load()
        for y in range(H):
            fy = y / (H - 1)
            for x in range(W):
                k = kind[y][x]
                if k[0] == 't':
                    out[x, y] = tuple(c * 8 for c in WALL); continue
                if k[0] == 'g':
                    out[x, y] = tuple(c * 8 for c in (GREEN_D if k[1] == 0 else GREEN_L)); continue
                if k[0] == 'w':
                    out[x, y] = tuple(c * 8 for c in WHITE); continue
                gi = k[1]
                fx = x / (W - 1)
                dirt = 0.55 * coarse(fx, fy) + 0.45 * fine(fx, fy)
                dirt = 0.7 * dirt + 0.3 * streak(fx, fy)        # bias toward streaks
                darken = amt * (dirt ** 1.5) * 5.5              # gamma -> patchy, not uniform
                light = amt * max(0.0, scuff(fx, fy) - 0.78) * 6.0   # rare bright scuffs
                g2 = int(round(gi - darken + light))
                out[x, y] = tuple(c * 8 for c in GREY[max(0, min(8, g2))])
        big = img.resize((W * args.scale, H * args.scale), Image.NEAREST)
        big.save("%s/door_grime_%02d.png" % (args.outdir, level))
    print("wrote %s/door_grime_00.png .. door_grime_19.png" % args.outdir)


if __name__ == "__main__":
    main()
