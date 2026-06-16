#!/usr/bin/env python3
"""Bake the Blender cardboard-box cinematic into sh_src/box_anim.h.

The box title screen is a pre-rendered flipbook: Blender does the
expensive photoreal shading offline (see scripts/genbox.py), and the
32X just RLE-decodes one 8bpp paletted frame per displayed vblank into
the framebuffer. The hardware runs in MARS_VDP_MODE_256 (1 byte/pixel,
each byte a CRAM index), so every frame shares ONE 256-colour palette
loaded into CRAM once at the start of playback — the cardboard tans +
black void quantise to 256 colours with room to spare.

Pipeline:
  1. Sample N frames from the rendered PNG sequence.
  2. Centre-crop each 320x240 render to the 320x224 framebuffer.
  3. Quantise ALL frames against one shared median-cut palette (no
     dither — dithering destroys RLE run length and the cardboard
     banding reads fine flat).
  4. PackBits-style RLE each frame (long black/flat-tan runs collapse).
  5. Emit box_anim.h: palette[256][3] (5-bit), concatenated RLE byte
     stream + per-frame offset table.

RLE token format (decoder in box_intro.c):
  ctrl & 0x80 : run  — (ctrl & 0x7F) copies of the next 1 byte
  else        : copy — `ctrl` literal bytes follow

Usage:
  tools/bake_box.py --src /tmp/boxrender/seq --frames 45 \
      --out sh_src/box_anim.h
"""
import argparse
import glob
import os
from PIL import Image

FB_W, FB_H = 320, 224


def sample_paths(src, want):
    paths = sorted(glob.glob(os.path.join(src, "f_*.png")))
    if not paths:
        raise SystemExit(f"no f_*.png frames in {src}")
    if want >= len(paths):
        return paths
    # Even stride across the whole sequence so we keep the full arc.
    step = len(paths) / want
    return [paths[min(len(paths) - 1, int(i * step))] for i in range(want)]


def crop_to_fb(im):
    """320x240 render -> centre 320x224 (drop 8px top/bottom)."""
    im = im.convert("RGB")
    w, h = im.size
    if (w, h) != (FB_W, FB_H):
        if w != FB_W:
            im = im.resize((FB_W, int(h * FB_W / w)), Image.LANCZOS)
            w, h = im.size
        top = max(0, (h - FB_H) // 2)
        im = im.crop((0, top, FB_W, top + FB_H))
    return im


def rle_encode(buf):
    """PackBits-ish RLE over a bytes object. Runs of >=3 become a run
    token; everything else accumulates into literal tokens. Counts are
    capped at 127 so the control byte stays 7-bit + run flag."""
    out = bytearray()
    n = len(buf)
    i = 0
    lit = bytearray()

    def flush_lit():
        nonlocal lit
        k = 0
        while k < len(lit):
            chunk = lit[k:k + 127]
            out.append(len(chunk))          # high bit clear => literal
            out.extend(chunk)
            k += 127
        lit = bytearray()

    while i < n:
        v = buf[i]
        run = 1
        while i + run < n and buf[i + run] == v and run < 127:
            run += 1
        if run >= 3:
            flush_lit()
            out.append(0x80 | run)          # high bit set => run
            out.append(v)
            i += run
        else:
            lit.append(v)
            i += 1
    flush_lit()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="/tmp/boxrender/seq")
    ap.add_argument("--frames", type=int, default=45)
    ap.add_argument("--out", default="sh_src/box_anim.h")
    ap.add_argument("--colors", type=int, default=256)
    ap.add_argument("--scale", type=int, default=1,
                    help="integer downscale; player upscales by this on blit")
    ap.add_argument("--dry", action="store_true",
                    help="measure RLE size only; don't write the header")
    ap.add_argument("--preview", default=None,
                    help="dir to dump quantized+upscaled preview PNGs")
    args = ap.parse_args()

    sw, sh = FB_W // args.scale, FB_H // args.scale
    paths = sample_paths(args.src, args.frames)
    nframes = len(paths)
    frames = [crop_to_fb(Image.open(p)) for p in paths]
    if args.scale != 1:
        frames = [im.resize((sw, sh), Image.LANCZOS) for im in frames]

    # Shared palette: stack every frame vertically and median-cut once,
    # so the CRAM palette is stable for the whole cinematic (no flicker).
    stack = Image.new("RGB", (sw, sh * nframes))
    for idx, im in enumerate(frames):
        stack.paste(im, (0, idx * sh))
    pal_img = stack.quantize(colors=args.colors, method=Image.MEDIANCUT,
                             dither=Image.Dither.NONE)

    pal_raw = pal_img.getpalette()[:args.colors * 3]
    palette5 = []
    for c in range(args.colors):
        r, g, b = pal_raw[c * 3:c * 3 + 3]
        palette5.append((r >> 3, g >> 3, b >> 3))

    # Map each frame through the shared palette and RLE it.
    streams = []
    quantized = []
    for im in frames:
        q = im.quantize(palette=pal_img, dither=Image.Dither.NONE)
        quantized.append(q)
        streams.append(rle_encode(q.tobytes()))

    # Optional: dump quantized+nearest-upscaled previews of the exact
    # bytes that will ship, so the on-console look can be eyeballed.
    if args.preview:
        os.makedirs(args.preview, exist_ok=True)
        for fi in (0, nframes // 3, 2 * nframes // 3, nframes - 1):
            up = quantized[fi].convert("RGB").resize(
                (sw * args.scale, sh * args.scale), Image.NEAREST)
            up.save(os.path.join(args.preview, f"qprev_{fi:02d}.png"))

    blob = bytearray()
    offsets = [0]
    for s in streams:
        blob.extend(s)
        offsets.append(len(blob))

    raw = sw * sh * nframes
    print(f"colors={args.colors:3d} scale={args.scale} {nframes} frames "
          f"{sw}x{sh}  raw={raw} bytes  rle={len(blob)} bytes  "
          f"ratio={raw/len(blob):.1f}x  ({len(blob)/1024:.0f} KB)")
    if args.dry:
        return

    with open(args.out, "w") as f:
        f.write("/* Auto-generated by tools/bake_box.py — do not edit. */\n")
        f.write(f"/* Source: {args.src}  ({nframes} frames) */\n")
        f.write("#ifndef BOX_ANIM_H\n#define BOX_ANIM_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define BOX_W {sw}\n")
        f.write(f"#define BOX_H {sh}\n")
        f.write(f"#define BOX_SCALE {args.scale}\n")
        f.write(f"#define BOX_FRAMES {nframes}\n")
        f.write(f"#define BOX_PAL_N {args.colors}\n\n")

        f.write("/* 5-bit-per-channel CRAM palette, shared by all frames. */\n")
        f.write(f"static const uint8_t box_palette[BOX_PAL_N][3] = {{\n")
        for r, g, b in palette5:
            f.write(f"    {{{r},{g},{b}}},\n")
        f.write("};\n\n")

        f.write("/* Per-frame [start,end) byte offsets into box_frame_data. */\n")
        f.write(f"static const uint32_t box_frame_offsets[BOX_FRAMES + 1] = {{\n    ")
        f.write(", ".join(str(o) for o in offsets))
        f.write("\n};\n\n")

        f.write(f"/* PackBits-style RLE stream, {len(blob)} bytes total. */\n")
        f.write(f"static const uint8_t box_frame_data[{len(blob)}] = {{\n")
        for k in range(0, len(blob), 32):
            f.write("    " + ",".join(str(b) for b in blob[k:k + 32]) + ",\n")
        f.write("};\n\n")
        f.write("#endif\n")

    print(f"wrote {args.out}: {os.path.getsize(args.out)/1024:.0f} KB source")


if __name__ == "__main__":
    main()
