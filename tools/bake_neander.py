#!/usr/bin/env python3
"""Bake the neanderthal sprite from images/neanderthal.png into a C
header containing a brightness-quantized index array for the 32X
raycaster.

Background rejection: source PNG has light-grey / white corners with
near-zero saturation. Anything matching that (high luminance + low
chroma) becomes palette index 0 (transparent). Everything else is
quantized to indices 1..7 matching NEANDER_BASE+1..7 in raycast.c
(1 = darkest figure shade, 7 = brightest).

Usage:
    tools/bake_neander.py <out_w> <out_h> <out_path> \
        --symbol <name> --prefix <PREFIX>

Run twice — once for the standard 32x64 and once for the close-range
64x128 or 128x256 hi-res variant — and they will be palette-compatible
because both go through the same quantizer.
"""
import argparse
from PIL import Image


def is_bg(r, g, b):
    lum = 0.299 * r + 0.587 * g + 0.114 * b
    chroma = abs(r - g) + abs(g - b) + abs(r - b)
    return lum >= 180 and chroma < 18


def quantize(r, g, b):
    lum = 0.299 * r + 0.587 * g + 0.114 * b
    idx = int(lum * 7 / 256) + 1
    return max(1, min(7, idx))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_w", type=int)
    ap.add_argument("out_h", type=int)
    ap.add_argument("out_path")
    ap.add_argument("--symbol", required=True)
    ap.add_argument("--prefix", required=True)
    ap.add_argument("--src", default="images/neanderthal.png")
    args = ap.parse_args()

    src = Image.open(args.src).convert("RGBA")
    sw, sh = src.size

    sx_step = sw / args.out_w
    sy_step = sh / args.out_h

    rows = []
    for oy in range(args.out_h):
        y0 = int(oy * sy_step)
        y1 = max(y0 + 1, int((oy + 1) * sy_step))
        row = []
        for ox in range(args.out_w):
            x0 = int(ox * sx_step)
            x1 = max(x0 + 1, int((ox + 1) * sx_step))
            bg_count = 0
            fg_sum_r = fg_sum_g = fg_sum_b = 0
            fg_count = 0
            for sy in range(y0, y1):
                for sx in range(x0, x1):
                    r, g, b, _ = src.getpixel((sx, sy))
                    if is_bg(r, g, b):
                        bg_count += 1
                    else:
                        fg_sum_r += r
                        fg_sum_g += g
                        fg_sum_b += b
                        fg_count += 1
            total = (y1 - y0) * (x1 - x0)
            if fg_count * 2 < total:
                row.append(0)
            else:
                ar = fg_sum_r // fg_count
                ag = fg_sum_g // fg_count
                ab = fg_sum_b // fg_count
                row.append(quantize(ar, ag, ab))
        rows.append(row)

    guard = f"{args.symbol.upper()}_H_INCLUDED"
    with open(args.out_path, "w") as f:
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define {args.prefix}_WIDTH  {args.out_w}\n")
        f.write(f"#define {args.prefix}_HEIGHT {args.out_h}\n\n")
        f.write(
            f"static const uint8_t {args.symbol}"
            f"[{args.prefix}_HEIGHT][{args.prefix}_WIDTH] = {{\n"
        )
        for row in rows:
            f.write("    {" + ",".join(str(v) for v in row) + "},\n")
        f.write("};\n\n")
        f.write(f"#endif\n")

    print(f"wrote {args.out_path}: {args.out_w}x{args.out_h} = "
          f"{args.out_w * args.out_h} bytes")


if __name__ == "__main__":
    main()
