#!/usr/bin/env python3
"""Sprite-sheet rips from the reference SMS ROMs -> srcref/sms/rips/.

Everything this emits stays under srcref/ (gitignored): the rips are
Sega's art, kept LOCAL as reference/tracing stock — the public releases
never ship them. Two output forms per hero:

  *_sheet.png            the full extracted sheet (all directions/frames,
                         grayscale, 4x) — the "sprite output as tilemaps"
  *.tileset.json         an editor-loadable tileset: the down-facing walk
                         cycle centered in our 32x32 player frame boxes,
                         grayscale palette (retint slots in the editor),
                         map kinds left blank. Load it in
                         tools/tile-editor.html and the WALK SHEET panel
                         shows the cycle ready to study, trace, or recolor.

Sources:
- Predator 2: raw 4bpp planar tiles at ROM tiles 5632.. (offset 0x2C000),
  frame = 8 consecutive tiles laid out 2 cols x 4 rows ROW-MAJOR, 16x32 px,
  32 hero frames = 8 directions x 4 walk frames (row order S, SE?, E, NE?,
  N, NW?, W, SW? — see the sheet labels). Fully re-derivable from ROM.
- Alien Syndrome: sprite art is COMPRESSED in ROM; frames were recovered
  at runtime via MAME VRAM capture (agent run, 2026-08-16) and preserved
  in srcref/sms/rips/hero_frames.json (S-facing 2-frame cycle, 16x24) +
  a_final_frames.pkl (all 8 directions). This script converts from that
  capture; it cannot re-derive them from the ROM alone.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
RIPS = ROOT / "srcref" / "sms" / "rips"
PRED_ROM = ROOT / "srcref" / "sms" / "Predator 2 (USA, Europe).sms"

PRED_TILE0 = 5632
PRED_FRAMES = 32          # 8 directions x 4 walk frames
GRAY_PALETTE = ["#%02X%02X%02X" % ((17 * i,) * 3) for i in range(16)]


def decode_tile(rom, t):
    out = []
    for r in range(8):
        b = rom[t * 32 + r * 4: t * 32 + r * 4 + 4]
        row = []
        for x in range(8):
            bit = 7 - x
            row.append(((b[0] >> bit) & 1) | (((b[1] >> bit) & 1) << 1)
                       | (((b[2] >> bit) & 1) << 2)
                       | (((b[3] >> bit) & 1) << 3))
        out.append(row)
    return out


def pred_frame(rom, f):
    """One 16x32 hero frame: 8 tiles, 2x4 row-major."""
    grid = [[0] * 16 for _ in range(32)]
    for i in range(8):
        t = decode_tile(rom, PRED_TILE0 + f * 8 + i)
        ox, oy = (i % 2) * 8, (i // 2) * 8
        for r in range(8):
            for x in range(8):
                grid[oy + r][ox + x] = t[r][x]
    return grid


def center_32(grid):
    """Center an arbitrary-size frame in a 32x32 box (1:1, never scaled)."""
    h, w = len(grid), len(grid[0])
    ox, oy = (32 - w) // 2, (32 - h) // 2
    box = [[0] * 32 for _ in range(32)]
    for r in range(h):
        for x in range(w):
            box[oy + r][ox + x] = grid[r][x]
    return box


def slice_16(box):
    return [[box[ty * 8 + r][tx * 8 + c] for r in range(8) for c in range(8)]
            for ty in range(4) for tx in range(4)]


def make_tileset(frames4):
    """4 32x32 frames -> a full editor-loadable tileset (map kinds blank)."""
    tiles = [[0] * 64]                       # tile 0 = blank
    canon = {tuple(tiles[0]): 0}
    metatiles = {k: [0] * 16 for k in
                 ["floor", "wall", "exit", "slab_h", "slab_v"]}
    for kind, box in zip(["player", "player_b", "player_c", "player_d"],
                         frames4):
        ids = []
        for t in slice_16(box):
            key = tuple(t)
            if key not in canon:
                canon[key] = len(tiles)
                tiles.append(t)
            ids.append(canon[key])
        metatiles[kind] = ids
    if len(tiles) > 128:
        sys.exit(f"{len(tiles)} tiles — over the 128 bank")
    return {"version": 2, "palette": GRAY_PALETTE,
            "tiles": tiles, "metatiles": metatiles}


def save_sheet_png(frames, path, scale=4):
    from PIL import Image
    h, w = len(frames[0]), len(frames[0][0])
    img = Image.new("RGB", ((w + 2) * len(frames), h))
    px = img.load()
    for f, grid in enumerate(frames):
        for r in range(h):
            for x in range(w):
                v = grid[r][x] * 17
                px[f * (w + 2) + x, r] = (v, v, v)
    img = img.resize((img.width * scale, img.height * scale), 0)
    img.save(path)


def main():
    RIPS.mkdir(parents=True, exist_ok=True)

    # ---- Predator 2: everything straight from ROM ---------------------
    rom = PRED_ROM.read_bytes()
    all_frames = [pred_frame(rom, f) for f in range(PRED_FRAMES)]
    save_sheet_png(all_frames, RIPS / "predator2_hero_sheet.png")
    south = [center_32(g) for g in all_frames[0:4]]     # row 0 = S-facing
    (RIPS / "predator2_hero.tileset.json").write_text(
        json.dumps(make_tileset(south)))

    # ---- Alien Syndrome: from the preserved MAME capture --------------
    cap = json.loads((RIPS / "hero_frames.json").read_text())
    a = [center_32(g) for g in cap["alien"]]            # 2-frame cycle
    (RIPS / "alien_syndrome_hero.tileset.json").write_text(
        json.dumps(make_tileset([a[0], a[1], a[0], a[1]])))

    (RIPS / "README.md").write_text(
        "Reference sprite rips (LOCAL ONLY — srcref/ is gitignored; none of\n"
        "this ships in any release).\n\n"
        "- predator2_hero_sheet.png — all 32 frames (8 dirs x 4) from ROM\n"
        "- hero_frames_pred.png / hero_frames_alien.png — labeled direction\n"
        "  sheets from the extraction run\n"
        "- p_all32.png / a_hero_merged.png — raw extraction grids\n"
        "- *.tileset.json — down-facing walk cycles as editor-loadable\n"
        "  tilesets (grayscale palette; retint slots in the editor).\n"
        "  Load in tools/tile-editor.html -> the WALK SHEET panel.\n"
        "- hero_frames.json / a_final_frames.pkl — capture data (Alien\n"
        "  Syndrome's art is compressed in ROM; recovered via MAME VRAM\n"
        "  capture, so keep these — they cannot be re-derived)\n\n"
        "Regenerate: python3 tools/rip_sms_sprites.py\n")
    print(f"wrote {RIPS.relative_to(ROOT)}: predator sheet ({PRED_FRAMES} "
          f"frames) + 2 editor-loadable tilesets + README")


if __name__ == "__main__":
    main()
