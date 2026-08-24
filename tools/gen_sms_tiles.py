#!/usr/bin/env python3
"""SMS art-tile pipeline: sms/tileset.json -> every consumer.

The maze mini-game's map view is built from 4x4-tile METATILE cells
(32x32 px per map cell). The art lives in sms/tileset.json — authored in
tools/tile-editor.html — and this script compiles it for all sides:

  sh_src/sms_tiles.h            4bpp tiles + BGR555 palette for the SH-2
                                fullscreen/zoom painters (SMS32X path)
  sms/games/maze/tiles.inc      the COMPOSED metatile table the Z80 frame
                                builder stamps cells from, pinned at $1700
  sms/games/maze/tiles_sms.inc  mode-4 planar tiles + 6-bit palette + a
                                demo level pack for the STANDALONE .sms
                                build (make maze-ares — fast Ares loop,
                                no 32X boot)

AUTHORED kinds (the editor's list): floor, wall, exit, player, slab_h,
slab_v. The slab metatiles carry partition art on their edge rows/cols
(slab_h: row 0 = N-edge slab, row 3 = S-edge slab; slab_v: col 0 = W,
col 3 = E; everything else floor). The COMPOSED table the Z80 sees is 19
rows: floor, wall, exit, player, then all 15 edge combinations
(N=1 E=2 S=4 W=8 -> kind 3+combo), spliced from floor + the slab rows.

Tile-id space: TILEBUF bytes 0..44 stay the boot font. Ids 128+ index
this tileset (id = 128 + tiles[] index). The MD plane-B arm clamps
ids >= 128 to '%'.

Palette contract: 16 entries; entry 0 transparent (CRT-black backing;
the 32X framebuffer drops zero byte writes anyway); 1..15 land on CRAM
SMS_PIC_BASE+1..15 on the 32X, and BG CRAM 1..15 on a real SMS. Colors
quantize to the SMS master palette (2 bits per channel).

The WALL metatile is baked from images/walltile.jpg (the game's chevron
wallpaper) when Pillow is available, so the SMS map shows OUR yellow
walls. --placeholder rebuilds sms/tileset.json from scratch (procedural
floor/exit/player/slabs + the baked wall); --bake-wall re-bakes only the
wall metatile into an existing tileset.
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TILESET = ROOT / "sms" / "tileset.json"
WALL_JPG = ROOT / "images" / "walltile.jpg"
FONT_S = ROOT / "md_src" / "font.s"
OUT_H = ROOT / "sh_src" / "sms_tiles.h"
OUT_INC = ROOT / "sms" / "games" / "maze" / "tiles.inc"
OUT_SMS = ROOT / "sms" / "games" / "maze" / "tiles_sms.inc"

ART_BASE = 128
MAX_TILES = 128
# player, player_b/c/d = the 4-frame walk cycle (player = idle/frame 0)
AUTHORED_KINDS = ["floor", "wall", "exit", "player", "slab_h", "slab_v",
                  "player_b", "player_c", "player_d"]
PLAYER_FRAMES = ["player", "player_b", "player_c", "player_d"]
# v4 geometry: MAP cells are 32x32 px = 4x4 tiles (16 metatile indices),
# the same size as a PLAYER frame — one cell is one character, so a
# corridor reads at human scale. v3's 16px cells made a corridor half a
# character wide, which is why the walk never looked like a walk.
MAP_MT = 16
PLAYER_MT = 16
FONT_INK = 12          # palette slot the boot font renders with (white)


def sms_quant(c):
    return min(3, (c + 42) // 85) * 85


def hex_to_rgb(s):
    s = s.lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def rgb_to_bgr555(r, g, b):
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def rgb_to_sms6(r, g, b):
    lvl = lambda c: min(3, (c + 42) // 85)
    return (lvl(b) << 4) | (lvl(g) << 2) | lvl(r)


# ---------------------------------------------------------------------------
# Placeholder art (procedural, backrooms-flavored). The wall is baked from
# the real chevron texture when possible; these cover everything else.
# ---------------------------------------------------------------------------
PLACEHOLDER_PALETTE = [
    "#000000",  # 0 transparent (CRT black)
    "#555500",  # 1 carpet dark olive
    "#AAAA55",  # 2 carpet speckle light
    "#AAAA00",  # 3 dirty yellow (stripes / shadows / slabs)
    "#FFFF55",  # 4 wallpaper yellow
    "#FFFFAA",  # 5 wallpaper highlight
    "#55FF55",  # 6 exit-sign green
    "#AA5500",  # 7 door brown
    "#550000",  # 8 deep brown (frames, hair)
    "#AA5555",  # 9 skin (warm brown — the hero reference)
    "#AAAAFF",  # 10 gray-lavender button-down (the hero reference)
    "#AAAAAA",  # 11 gray hardware
    "#FFFFFF",  # 12 white (also the boot-font ink)
    "#000000",  # 13 ink black (paintable, unlike 0)
    "#005500",  # 14 dark green
    "#AA0000",  # 15 red accent
]


def draw_floor(px):
    # Blank. The carpet speckle read as noise at map scale (Mike,
    # 2026-08-14) — floor is the void: transparent 0 = CRT black.
    for y in range(len(px)):
        for x in range(len(px)):
            px[y][x] = 0


def draw_wall_procedural(px):
    for y in range(16):
        for x in range(16):
            c = 4
            if x % 8 in (3, 4):
                c = 3
            if x < 1 or y < 1:
                c = 5
            if x >= 15 or y >= 15:
                c = 3
            px[y][x] = c


def bake_wall(px, palette):
    """The game's chevron wallpaper, 32x32, nearest-SMS-color. Returns
    False (caller falls back to procedural) if Pillow or the jpg is out."""
    try:
        from PIL import Image, ImageOps
    except ImportError:
        return False
    if not WALL_JPG.exists():
        return False
    # The chevron's contrast is subtler than the SMS palette's 85-step
    # channels — a plain nearest-color match collapses it to one flat
    # yellow. Stretch the contrast, then Floyd-Steinberg dither against
    # the palette so the weave survives as texture.
    img = Image.open(WALL_JPG).convert("RGB").resize((16, 16),
                                                     Image.LANCZOS)
    # One linear stretch from the LUMA percentiles, applied to all three
    # channels alike — per-channel autocontrast amplified blue-channel
    # noise to full range and turned the yellow weave gray.
    hist = sorted(img.convert("L").getdata())
    lo, hi = hist[len(hist) // 50], hist[-1 - len(hist) // 50]
    if hi <= lo:
        hi = lo + 1
    img = img.point(lambda c: max(0, min(255, (c - lo) * 255 // (hi - lo))))
    palimg = Image.new("P", (1, 1))
    flat = []
    for h in palette[1:]:                   # slots 1..15; never transparent
        flat += list(hex_to_rgb(h))
    palimg.putpalette(flat + [0, 0, 0] * (256 - 15))
    q = img.quantize(palette=palimg, dither=Image.FLOYDSTEINBERG)
    for y in range(16):
        for x in range(16):
            px[y][x] = min(q.getpixel((x, y)), 14) + 1
    return True


def draw_exit(px):
    draw_wall_procedural(px)
    for y in range(1, 4):                    # sign plate
        for x in range(4, 12):
            edge = y == 1 or x in (4, 11)
            px[y][x] = 14 if edge else 6
    for y in range(5, 16):                   # doorway
        for x in range(4, 12):
            px[y][x] = 8 if (x < 5 or x >= 11) else 13
    for x in range(5, 11):
        px[5][x] = 8


# The hero as authored pixel grids — a standing 3/4 figure like the
# commercial SMS top-down games draw (the Alien Syndrome / Predator 2
# study): bobblehead proportions (head ~40% of the figure), full 1px
# dark outline (the right strategy for mid-value interiors like the
# lavender shirt), boots as the high-contrast step accent, offset-stride
# legs. Frames: 0/2 = stand (contact), 1 = step, 3 = mirror of 1 (the
# 1px recenter drift reads as gait sway). Chars: . transparent,
# # outline/hair/beard, S skin, P shirt, D slacks, B boots.
HERO_INK = {".": 0, "#": 13, "S": 9, "P": 10, "D": 1, "B": 8}
HERO_STAND = [
    "................................",
    "............#######.............",
    "..........###########...........",
    ".........#############..........",
    ".........#############..........",
    ".........##SSSSSSSSS##..........",
    ".........#SSSSSSSSSSS#..........",
    ".........#SS#SSSSS#SS#..........",
    ".........#SSSSSSSSSSS#..........",
    ".........##SSS###SSS##..........",
    ".........###SSSSSSS###..........",
    "..........####SSS####...........",
    "...........#########............",
    "..........###PPPPP###...........",
    "........##PPPPPPPPPPP##.........",
    ".......#PPPPPPPPPPPPPPP#........",
    ".......#PPPPPPPPPPPPPPP#........",
    ".......#PP#PPPPPPPPP#PP#........",
    ".......#PP#PPPPPPPPP#PP#........",
    ".......#PP#PPPPPPPPP#PP#........",
    ".......#SS#PPPPPPPPP#SS#........",
    ".......#SS#PPPPPPPPP#SS#........",
    "........###DDDDDDDDD###.........",
    "..........#DDDDDDDDD#...........",
    "..........#DDDD#DDDD#...........",
    "..........#DDD###DDD#...........",
    "..........#DDD#.#DDD#...........",
    "..........#BBB#.#BBB#...........",
    "..........#BBB#.#BBB#...........",
    "..........#####.#####...........",
    "................................",
    "................................",
]
HERO_STEP = [
    "............#######.............",
    "..........###########...........",
    ".........#############..........",
    ".........#############..........",
    ".........##SSSSSSSSS##..........",
    ".........#SSSSSSSSSSS#..........",
    ".........#SS#SSSSS#SS#..........",
    ".........#SSSSSSSSSSS#..........",
    ".........##SSS###SSS##..........",
    ".........###SSSSSSS###..........",
    "..........####SSS####...........",
    "...........#########............",
    "..........###PPPPP###...........",
    "........##PPPPPPPPPPP##.........",
    ".......#PPPPPPPPPPPPPPP#........",
    ".......#PPPPPPPPPPPPPPP#........",
    ".......#PP#PPPPPPPPP#PP#........",
    ".......#PP#PPPPPPPPP#PP#........",
    ".......#SS#PPPPPPPPP#PP#........",
    ".......#SS#PPPPPPPPP#PP#........",
    "........###PPPPPPPPP#SS#........",
    "..........#DDDDDDDDD#SS#........",
    "..........#DDDD#DDDD####........",
    "..........#DDD###DDD#...........",
    "..........#DDD#.#DDD#...........",
    "..........#DDD#.#DDD#...........",
    "..........#BBB#.#DDD#...........",
    "..........#BBB#.#BBB#...........",
    "..........#####.#BBB#...........",
    "................#####...........",
    "................................",
    "................................",
]


def draw_player(px, frame=0):
    grid = HERO_STAND if frame in (0, 2) else HERO_STEP
    for y, row in enumerate(grid):
        assert len(row) == 32, f"hero grid row {y} is {len(row)} chars"
        line = row if frame != 3 else row[::-1]
        for x, ch in enumerate(line):
            px[y][x] = HERO_INK[ch]


def draw_slab_h(px):
    """Partition slabs on the N (rows 0-2) and S (rows 13-15) edges over
    floor — the spotted-olive office divider read."""
    draw_floor(px)
    for y0 in (0, 13):
        for y in range(y0, y0 + 3):
            for x in range(16):
                c = 3
                if y in (y0, y0 + 2):
                    c = 8
                elif (x * 5 + y * 3) % 11 == 0:
                    c = 2
                px[y][x] = c


def draw_slab_v(px):
    draw_floor(px)
    for x0 in (0, 13):
        for x in range(x0, x0 + 3):
            for y in range(16):
                c = 3
                if x in (x0, x0 + 2):
                    c = 8
                elif (x * 3 + y * 5) % 11 == 0:
                    c = 2
                px[y][x] = c


def slice_tiles(px):
    n = len(px) // 8                    # 2 for 16px map cells, 4 for the hero
    return [[px[ty * 8 + r][tx * 8 + c] for r in range(8) for c in range(8)]
            for ty in range(n) for tx in range(n)]


def downsample_16(px32):
    """32x32 -> 16x16 by point-sampling — the harness TILEBUF path stamps
    the player as a single map cell, so frame 0 needs a half-size stand-in."""
    return [[px32[y * 2][x * 2] for x in range(16)] for y in range(16)]


def make_map_kinds():
    """The five map-cell kinds as 32x32 pixel grids (--placeholder only).

    The painters below are written against a 16x16 canvas; pixel-doubling
    them up to the v4 cell size keeps the placeholder art honest without
    reworking every painter's hardcoded coordinates."""
    return {k: [[px[y // 2][x // 2] for x in range(32)] for y in range(32)]
            for k, px in _make_map_kinds_16().items()}


def _make_map_kinds_16():
    out = {}
    px = [[0] * 16 for _ in range(16)]
    draw_floor(px)
    out["floor"] = [r[:] for r in px]
    px = [[0] * 16 for _ in range(16)]
    if not bake_wall(px, PLACEHOLDER_PALETTE):
        draw_wall_procedural(px)
    out["wall"] = [r[:] for r in px]
    for kind, painter in (("exit", draw_exit), ("slab_h", draw_slab_h),
                          ("slab_v", draw_slab_v)):
        px = [[0] * 16 for _ in range(16)]
        painter(px)
        out[kind] = [r[:] for r in px]
    return out


def placeholder_tileset():
    tiles, metatiles = [], {}

    def add_kind(kind, cells):
        ids = []
        for t in cells:
            ids.append(len(tiles))
            tiles.append(t)
        metatiles[kind] = ids

    for kind, grid in make_map_kinds().items():
        add_kind(kind, slice_tiles(grid))
    for f, kind in enumerate(PLAYER_FRAMES):    # the 4-frame walk cycle
        px = [[0] * 32 for _ in range(32)]
        draw_player(px, f)
        add_kind(kind, slice_tiles(px))
    # dedup identical tiles (the blank floor alone is 16 copies)
    canon, remap, kept = {}, {}, []
    for i, t in enumerate(tiles):
        key = tuple(t)
        if key not in canon:
            canon[key] = len(kept)
            kept.append(t)
        remap[i] = canon[key]
    for k in metatiles:
        metatiles[k] = [remap[i] for i in metatiles[k]]
    return {"version": 3, "palette": PLACEHOLDER_PALETTE,
            "tiles": kept, "metatiles": metatiles}


# ---------------------------------------------------------------------------
# Composition: authored kinds -> the 19-row table the Z80 stamps from.
# ---------------------------------------------------------------------------
def compose_table(metatiles, player_row):
    """19 rows of 16 tile indices (4x4 = one 32px map cell). Composition
    only ever REUSES authored tiles, so the 15 edge combos cost no tile
    slots at all."""
    floor, sh, sv = (metatiles[k] for k in ("floor", "slab_h", "slab_v"))
    rows = [metatiles["floor"], metatiles["wall"],
            metatiles["exit"], player_row]
    for combo in range(1, 16):              # N=1 E=2 S=4 W=8
        ids = list(floor)
        if combo & 1:
            ids[0:4] = sh[0:4]              # N edge: slab_h top tile row
        if combo & 4:
            ids[12:16] = sh[12:16]          # S edge: bottom tile row
        if combo & 8:
            for r in range(4):              # W edge: left tile col
                ids[r * 4] = sv[r * 4]
        if combo & 2:
            for r in range(4):              # E edge: right tile col
                ids[r * 4 + 3] = sv[r * 4 + 3]
        rows.append(ids)
    return rows


# ---------------------------------------------------------------------------
# Standalone .sms data: planar tiles, 6-bit palette, demo level pack.
# ---------------------------------------------------------------------------
def planar_tile(pixvals):
    """64 pixel values -> 32 bytes of mode-4 planar (4 bitplanes/row)."""
    out = []
    for r in range(8):
        row = pixvals[r * 8:r * 8 + 8]
        for p in range(4):
            b = 0
            for x in range(8):
                if (row[x] >> p) & 1:
                    b |= 0x80 >> x
            out.append(b)
    return out


def font_tiles():
    """Boot font from md_src/font.s as pixel values (0 or FONT_INK)."""
    longs = [int(x, 16) for x in
             re.findall(r"\.long\s+0x([0-9A-Fa-f]{8})", FONT_S.read_text())]
    tiles = []
    for t in range(len(longs) // 8):
        vals = []
        for r in range(8):
            v = longs[t * 8 + r]
            for px in range(8):
                vals.append(FONT_INK if (v >> (28 - 4 * px)) & 0xF else 0)
        tiles.append(vals)
    return tiles


def demo_pack():
    """A 440-byte level pack (identical layout to sms_pack_level +
    the 68K's split copy): the sim maze plus a few edge partitions."""
    pack = bytearray(440)
    maze = [[0] * 32 for _ in range(32)]
    for i in range(32):
        maze[0][i] = maze[31][i] = maze[i][0] = maze[i][31] = 1
    for y in range(1, 31):
        if y != 16:
            maze[y][5] = 1
    for y in range(32):
        for x in range(32):
            if maze[y][x]:
                pack[y * 4 + (x >> 3)] |= 0x80 >> (x & 7)
    pack[128:132] = bytes([2, 2, 31, 16])           # spawn, exit
    name = [17, 20, 16, 23, 15, 0, 31, 16, 30, 31]  # FIELD TEST
    off = 132 + (16 - len(name)) // 2
    pack[off:off + len(name)] = bytes(name)
    # partitions: a cubicle pod east of the bar (N/S runs + a W return)
    pn = lambda y, x: 148 + y * 4 + (x >> 3)
    pw = lambda y, x: 280 + y * 5 + (x >> 3)
    for x in range(10, 14):
        pack[pn(8, x)] |= 0x80 >> (x & 7)
        pack[pn(12, x)] |= 0x80 >> (x & 7)
    for y in range(8, 12):
        pack[pw(y, 14)] |= 0x80 >> (14 & 7)
    return pack


# ---------------------------------------------------------------------------
def main():
    if not TILESET.exists() or "--placeholder" in sys.argv:
        TILESET.write_text(json.dumps(placeholder_tileset()))
        print(f"wrote {TILESET.relative_to(ROOT)} (placeholder art)")

    ts = json.loads(TILESET.read_text())
    palette, tiles, metatiles = ts["palette"], ts["tiles"], ts["metatiles"]

    if "--bake-wall" in sys.argv:
        px = [[0] * 32 for _ in range(32)]
        if not bake_wall(px, palette):
            sys.exit("--bake-wall needs Pillow + images/walltile.jpg")
        cells = slice_tiles(px)
        for i, ti in enumerate(metatiles["wall"]):
            tiles[ti] = cells[i]
        TILESET.write_text(json.dumps(ts))
        print("re-baked the wall metatile from walltile.jpg")

    if len(palette) != 16:
        sys.exit(f"palette has {len(palette)} entries, need 16")
    if len(tiles) > MAX_TILES:
        sys.exit(f"{len(tiles)} tiles, max {MAX_TILES} (ids 128..255)")
    # (The v2 -> v3 downsample to 16px cells lived here. v4 went back to
    # 32px cells, so a tileset authored at 4x4 now passes straight
    # through — see MAP_MT.)
    for k in AUTHORED_KINDS:
        want = PLAYER_MT if k in PLAYER_FRAMES else MAP_MT
        mt = metatiles.get(k)
        if not mt or len(mt) != want:
            sys.exit(f"metatile '{k}' missing or not {want} tiles — author "
                     "it in tools/tile-editor.html (or --placeholder)")
        for i in mt:
            if not 0 <= i < len(tiles):
                sys.exit(f"metatile '{k}' references tile {i}, "
                         f"only {len(tiles)} exist")

    # The harness TILEBUF path stamps the player as one 16x16 map cell:
    # A map cell and a player frame are both 4x4 now, so the harness
    # TILEBUF path stamps frame 0 itself — no half-size stand-in, and no
    # extra tiles (v3 needed a downsampled copy to fit a 2x2 cell).
    player_row = list(metatiles["player"])

    # Reorder the bank: tiles the BACKGROUND references (the composed
    # table, incl. the player stand-in) come first; sprite-only tiles
    # (the 32x32 walk frames) go last. Only the background span needs
    # VRAM art slots 128..191 — the frames live in the sprite region at
    # 192+. Compile-time only; tileset.json is never reordered.
    bg_used = set()
    for row in compose_table(metatiles, player_row):
        bg_used.update(row)
    order = [i for i in range(len(tiles)) if i in bg_used] + \
            [i for i in range(len(tiles)) if i not in bg_used]
    remap = {old: new for new, old in enumerate(order)}
    tiles = [tiles[o] for o in order]
    metatiles = {k: [remap[i] for i in mt] for k, mt in metatiles.items()}
    player_row = [remap[i] for i in player_row]
    n_bg = len(bg_used)

    pal555 = []
    for h in palette:
        r, g, b = (sms_quant(c) for c in hex_to_rgb(h))
        pal555.append(rgb_to_bgr555(r, g, b))
    table = compose_table(metatiles, player_row)

    # ---- sh_src/sms_tiles.h -------------------------------------------
    lines = [
        "/* AUTO-GENERATED by tools/gen_sms_tiles.py from sms/tileset.json",
        " * — do not edit. Author art in tools/tile-editor.html. */",
        "#ifndef SMS_TILES_H",
        "#define SMS_TILES_H",
        f"#define SMS_ART_BASE  {ART_BASE}   "
        "/* TILEBUF ids >= this index sms_art[] */",
        f"#define SMS_ART_TILES {len(tiles)}",
        "/* Picture palette, BGR555, SMS-quantized. [0] is transparent",
        " * (never painted); 1..15 go to CRAM at SMS_PIC_BASE+1..15. */",
        "static const uint16_t sms_art_pal[16] = {",
        "    " + ", ".join(f"0x{v:04X}" for v in pal555),
        "};",
        "/* 4bpp, 2 px per byte, high nibble left: 32 bytes per 8x8 tile. */",
        f"static const uint8_t sms_art[{len(tiles)}][32] = {{",
    ]
    for t in tiles:
        packed = [(t[i] << 4) | t[i + 1] for i in range(0, 64, 2)]
        lines.append("    {" + ",".join(f"0x{b:02X}" for b in packed) + "},")
    lines += ["};"]
    lines.append("/* Per-tile draw hints for the smooth renderer: bit 0 =")
    lines.append(" * all-transparent (skip the blit), bit 1 = fully opaque")
    lines.append(" * (straight row blit, no per-pixel transparency tests). */")
    flags = []
    for t in tiles:
        f = 0
        if not any(t):
            f |= 1
        if all(t):
            f |= 2
        flags.append(f)
    lines.append(f"static const uint8_t sms_art_flags[{len(tiles)}] = {{")
    lines.append("    " + ",".join(str(f) for f in flags))
    lines.append("};")
    # the composed metatile table + player sprite art, for the SH-2's
    # SMOOTH MAZE renderer (it classifies cells itself from world_map/
    # pedge and draws the scrolled view — same rows the Z80 stamps)
    lines.append("/* Composed metatile table (2x2 tiles per 16px map cell):")
    lines.append(" * floor wall exit player-stand-in + 15 edge combos")
    lines.append(" * (kind = 3+combo, N1 E2 S4 W8). Art INDICES (no +128). */")
    lines.append("static const uint8_t sms_metatiles[19][4] = {")
    for row in table:
        lines.append("    {" + ",".join(str(i) for i in row) + "},")
    lines += ["};"]
    lines.append("/* Player sprite art: the 4-frame walk cycle (0 = idle),")
    lines.append(" * each frame the metatile with the floor diffed to")
    lines.append(" * transparent — 4x4 tiles, row-major. */")
    lines.append("static const uint8_t sms_player_sprite[4][16][32] = {")
    for kind in PLAYER_FRAMES:
        lines.append("  {")
        for pi in metatiles[kind]:
            t = tiles[pi]                    # frames are authored on
            packed = [(t[i] << 4) | t[i + 1]  # transparency already
                      for i in range(0, 64, 2)]
            lines.append("    {" + ",".join(f"0x{b:02X}" for b in packed)
                         + "},")
        lines.append("  },")
    lines += ["};", "#endif", ""]
    OUT_H.write_text("\n".join(lines))

    # ---- sms/games/maze/tiles.inc (composed metatile table) -----------
    inc = [
        "; AUTO-GENERATED by tools/gen_sms_tiles.py from sms/tileset.json",
        "; — do not edit. Author art in tools/tile-editor.html.",
        "; COMPOSED metatile table, pinned at METATILES ($1700): 16 tile",
        "; ids per row (4x4 = one 32px map cell). Rows: FLOOR WALL EXIT",
        "; PLAYER (walk frame 0), then the 15 edge-",
        "; partition combos (N=1 E=2 S=4 W=8 -> row 3+combo), spliced",
        "; from floor + the authored slab_h/slab_v metatiles.",
        ".ORG $1700",
        "metatiles:",
    ]
    names = ["floor", "wall", "exit", "player"] + \
            [f"combo {c:04b} (NESW... N=1 E=2 S=4 W=8)" for c in range(1, 16)]
    for row, nm in zip(table, names):
        inc.append(".DB " + ", ".join(str(ART_BASE + i) for i in row)
                   + f"   ; {nm}")
    inc.append("")
    OUT_INC.write_text("\n".join(inc))

    # ---- sms/games/maze/tiles_sms.inc (standalone .sms data) ----------
    def db_block(label, data, per=16):
        out = [label + ":"]
        for i in range(0, len(data), per):
            out.append(".DB " + ", ".join(f"${b:02X}"
                                          for b in data[i:i + per]))
        return out

    fonts = font_tiles()
    # Player SPRITE art: the player metatile minus the floor it was
    # authored over — pixels matching the floor metatile go transparent
    # (sprite color 0), so the figure rides the scrolling background
    # cleanly. 16 tiles, uploaded at a fixed VRAM slot above the bank.
    SPRITE_TILE_BASE = 192      # 4 frames x 16 tiles fill 192..255
    if n_bg > SPRITE_TILE_BASE - ART_BASE:
        sys.exit(f"{n_bg} background tiles overflow VRAM art slots "
                 f"128..191 (max 64; sprite-only tiles don't count)")
    sprite_tiles = []
    for kind in PLAYER_FRAMES:
        for pi in metatiles[kind]:
            sprite_tiles.append(list(tiles[pi]))
    sms = [
        "; AUTO-GENERATED by tools/gen_sms_tiles.py — data for the",
        "; STANDALONE maze build (make maze-ares): mode-4 planar tiles,",
        "; 6-bit BG palette, the player-sprite tiles (floor diffed out),",
        "; and a demo level pack in the exact layout the 68K patches",
        "; into Z80 RAM (440B: 148 core + 132 pedge_n + 160 pedge_w).",
        f".DEFINE SMS_FONT_COUNT {len(fonts)}",
        f".DEFINE SMS_ART_COUNT {n_bg}   ; background span only — the",
        ";   walk-frame-only tiles ride as sprites at SMS_SPRITE_TILE_BASE",
        f".DEFINE SMS_SPRITE_TILE_BASE {SPRITE_TILE_BASE}",
    ]
    sms += db_block("sms_bg_palette",
                    [rgb_to_sms6(*hex_to_rgb(h)) for h in palette])
    font_planar = [b for t in fonts for b in planar_tile(t)]
    sms += db_block("sms_font_planar", font_planar)
    art_planar = [b for t in tiles[:n_bg] for b in planar_tile(t)]
    sms += db_block("sms_art_planar", art_planar)
    sms += db_block("sms_player_sprite_planar",
                    [b for t in sprite_tiles for b in planar_tile(t)])
    sms += db_block("sms_demo_pack", demo_pack())
    sms.append("")
    OUT_SMS.write_text("\n".join(sms))

    print(f"wrote {OUT_H.relative_to(ROOT)} ({len(tiles)} tiles), "
          f"{OUT_INC.relative_to(ROOT)} (19 metatiles) and "
          f"{OUT_SMS.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
