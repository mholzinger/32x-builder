#!/usr/bin/env python3
"""Bake a wallpaper PNG/WebP into wall_tex.h.

The raycaster's inner column loop computes a shade offset per texel
as `(wall_tex[ty][texX] * detail_factor) >> 4`, where detail_factor
goes from 0 (far walls) to WALL_PATTERN_MAX=16 (close walls). So
wall_tex values directly index a darkening offset 0..levels-1 on top
of the distance-derived base shade — higher = darker.

The baker inverts luminance (so darker source pixels get larger offsets),
auto-fits to the source's min..max range, and quantizes to --levels
buckets. Output is column-major by default for SH-2 cache friendliness:
column samples walk contiguous bytes during the shade_lut build.

Usage:
    tools/bake_wall.py <out_w> <out_h> <out_path> --src images/foo.png
"""
import argparse
from PIL import Image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_w", type=int)
    ap.add_argument("out_h", type=int)
    ap.add_argument("out_path")
    ap.add_argument("--src", required=True)
    ap.add_argument("--symbol", default="wall_tex")
    ap.add_argument("--prefix", default="WALL_TEX")
    ap.add_argument("--levels", type=int, default=5,
                    help="Number of pattern darkness levels (0..levels-1). "
                         "Existing 16x16 wall_tex uses 5.")
    ap.add_argument("--column-major", action="store_true", default=True,
                    help="Emit [W][H] layout (default; cache-friendly).")
    ap.add_argument("--row-major", dest="column_major", action="store_false",
                    help="Emit [H][W] instead.")
    args = ap.parse_args()

    src = Image.open(args.src).convert("RGB")
    small = src.resize((args.out_w, args.out_h), Image.LANCZOS)
    px = small.load()

    lums = [
        0.299 * px[x, y][0] + 0.587 * px[x, y][1] + 0.114 * px[x, y][2]
        for y in range(args.out_h)
        for x in range(args.out_w)
    ]
    lmin = min(lums)
    lmax = max(lums)
    span = max(1.0, lmax - lmin)
    levels = args.levels

    rows = []
    for y in range(args.out_h):
        row = []
        for x in range(args.out_w):
            r, g, b = px[x, y]
            lum = 0.299 * r + 0.587 * g + 0.114 * b
            inv = (lmax - lum) / span
            p = int(inv * (levels - 1) + 0.5)
            row.append(max(0, min(levels - 1, p)))
        rows.append(row)

    guard = f"{args.symbol.upper()}_H_INCLUDED"
    with open(args.out_path, "w") as f:
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define {args.prefix}_WIDTH  {args.out_w}\n")
        f.write(f"#define {args.prefix}_HEIGHT {args.out_h}\n\n")
        if args.column_major:
            f.write(
                "/* Column-major: tex[texX][texY]. Each screen column samples\n"
                " * a contiguous run of TEX_H bytes during the per-column\n"
                " * shade_lut build, so the SH-2 cache loads one strip and\n"
                " * the loop hits every line. */\n"
            )
            f.write(
                f"static const uint8_t {args.symbol}"
                f"[{args.prefix}_WIDTH][{args.prefix}_HEIGHT] = {{\n"
            )
            for x in range(args.out_w):
                col = [rows[y][x] for y in range(args.out_h)]
                f.write("    {" + ",".join(str(v) for v in col) + "},\n")
        else:
            f.write(
                f"static const uint8_t {args.symbol}"
                f"[{args.prefix}_HEIGHT][{args.prefix}_WIDTH] = {{\n"
            )
            for row in rows:
                f.write("    {" + ",".join(str(v) for v in row) + "},\n")
        f.write("};\n\n")
        f.write(f"#endif\n")

    print(f"wrote {args.out_path}: {args.out_w}x{args.out_h} = "
          f"{args.out_w * args.out_h} bytes, "
          f"layout={'column-major' if args.column_major else 'row-major'}, "
          f"levels={levels}")


if __name__ == "__main__":
    main()
