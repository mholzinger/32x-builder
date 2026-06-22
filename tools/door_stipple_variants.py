#!/usr/bin/env python3
"""Generate door STIPPLE preview variants (levels 0..11) into images/ for spot-
checking. Stippling = tone from dot density. Dots are placed by a jittered-grid
(blue-noise-ish, evenly spread, never gridded/clumped), denser toward the worn
edges/bottom so the door reads engraved rather than flat. Rendered in the in-game
muted-brown palette and upscaled, so the dots show at a sub-texel (per-pixel)
fineness — i.e. what a per-pixel engine stipple would look like, not a chunky
baked 64x128 one.

    python3 tools/door_stipple_variants.py
"""
import argparse, random
from PIL import Image

GREY = [(4,3,2),(7,5,3),(9,7,5),(12,10,7),
        (15,12,9),(18,15,11),(22,18,14),(25,21,16),(29,25,20)]
GREEN_D=(3,12,6); GREEN_L=(8,19,10); WHITE=(29,30,28); WALL=(15,14,9)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="models/render/door_front.png")
    ap.add_argument("--w", type=int, default=64)
    ap.add_argument("--h", type=int, default=128)
    ap.add_argument("--scale", type=int, default=5)   # render/stipple resolution
    ap.add_argument("--outdir", default="images")
    args = ap.parse_args()
    W, H = args.w, args.h
    SW, SH = W * args.scale, H * args.scale

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
    for y in range(int(H * 0.80), H):                        # clean faded bottom
        for x in range(W):
            k = kind[y][x]
            if k[0] == 'm' and k[1] > 6:
                kind[y][x] = ('m', 6)

    def metal_at(sx, sy):
        """Sample the door base at preview resolution (nearest texel)."""
        return kind[min(H - 1, sy * H // SH)][min(W - 1, sx * W // SW)]

    for level in range(12):
        # Dot density: cell size shrinks with level (sparser -> denser).
        if level == 0:
            cell = 0
        else:
            cell = max(2, 11 - level)        # ~10px cells at lvl1 down to ~2px at lvl11
        img = Image.new("RGB", (SW, SH), tuple(c * 8 for c in WALL))
        out = img.load()
        # Base shading first.
        for sy in range(SH):
            for sx in range(SW):
                k = metal_at(sx, sy)
                if k[0] == 't':
                    out[sx, sy] = tuple(c * 8 for c in WALL)
                elif k[0] == 'g':
                    out[sx, sy] = tuple(c * 8 for c in (GREEN_D if k[1] == 0 else GREEN_L))
                elif k[0] == 'w':
                    out[sx, sy] = tuple(c * 8 for c in WHITE)
                else:
                    out[sx, sy] = tuple(c * 8 for c in GREY[k[1]])
        # Stipple: one jittered dot per cell, kept only on metal; density boosted
        # toward edges/bottom (wear) so the door reads engraved, not flat.
        if cell:
            rnd = random.Random(1234 + level)
            for cy in range(0, SH, cell):
                for cx in range(0, SW, cell):
                    dx = cx + rnd.randint(0, cell - 1)
                    dy = cy + rnd.randint(0, cell - 1)
                    if dx >= SW or dy >= SH:
                        continue
                    k = metal_at(dx, dy)
                    if k[0] != 'm':
                        continue
                    # wear weighting: more dots low + near L/R edges
                    fx = dx / (SW - 1); fy = dy / (SH - 1)
                    edge = min(fx, 1 - fx) * 2.0          # 0 at edge .. 1 centre
                    wear = (0.45 + 0.55 * fy) * (1.0 - 0.5 * edge)
                    if rnd.random() > wear:
                        continue
                    drop = 2 if rnd.random() < 0.15 else 1   # a few deeper dots
                    gi = max(0, k[1] - drop)
                    out[dx, dy] = tuple(c * 8 for c in GREY[gi])
        img.save("%s/door_stipple_%02d.png" % (args.outdir, level))
    print("wrote %s/door_stipple_00.png .. door_stipple_11.png" % args.outdir)


if __name__ == "__main__":
    main()
