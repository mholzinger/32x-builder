#!/usr/bin/env python3
"""Sim-render draw_exit_hole() at a replayable camera, so the LOOK can be
judged (and A/B'd) without a hardware round trip.

Replicates raycast.c:draw_exit_hole exactly — same fixed point, same divides,
same dither — plus a stand-in wall pass for the surrounding face so the hole is
judged in context rather than floating on black. The wall surround is
approximate (flat plane, no DDA); the HOLE itself is the real code path.

    python3 tools/sim_exit_hole.py                 # baseline, 3 distances
    python3 tools/sim_exit_hole.py --dist 0.8      # one camera
    python3 tools/sim_exit_hole.py --off 0.35      # step sideways off-axis

Output: /private/tmp/.../sim_exit_hole_<tag>.png (path printed).
"""
import argparse, math, os, sys, importlib.util

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SH = os.path.join(REPO, "sh_src")

spec = importlib.util.spec_from_file_location(
    "export_assets", os.path.join(REPO, "tools/export_assets.py"))
ea = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ea)

# ---------------------------------------------------------------- fixed point
FX_SHIFT = 16
FX_ONE = 1 << FX_SHIFT


def FX(d):
    return int(d * FX_ONE)


def FX_MUL(a, b):
    return (a * b) >> FX_SHIFT          # C: (int64 a*b) >> 16, arithmetic


def trunc_div(n, d):
    """SH-2 DIVU/DIV1 truncates toward zero; Python // floors."""
    q = abs(n) // abs(d)
    return q if (n < 0) == (d < 0) else -q


def fx_div_hw(a, b):
    return trunc_div(a << FX_SHIFT, b)


def divu(a, b):
    return a // b                        # both operands unsigned at the C site


# ------------------------------------------------------------ engine constants
SCREEN_W, SCREEN_H = 320, 224
WALL_BASE, SHADE_LEVELS = 1, 16
DOOR_DARK_BASE = 104                 # 4 warm near-black greys, bottoms #201810
# The hole's own shade ramp: the wall ramp (luma 223..73) continued into
# DOOR_DARK's bottom three (60, 43, 25). Monotonic, so a fade can run the whole
# way from lit wallpaper to near-black without a palette seam.
B4 = (0,128,32,160, 192,64,224,96, 48,176,16,144, 240,112,208,80)
DITHER = "bayer2"
DITHER_AMP = 256
RAMP_STRIDE = 1
BN = [5, 80, 239, 202, 30, 222, 83, 243, 121, 6, 94, 142, 21, 111, 162, 42, 223, 149, 19, 138, 66, 108, 156, 199, 63, 187, 224, 51, 182, 252, 70, 205, 61, 109, 186, 93, 246, 178, 38, 18, 146, 79, 32, 116, 155, 10, 100, 130, 171, 229, 46, 213, 7, 123, 208, 96, 253, 166, 201, 233, 85, 216, 190, 29, 89, 12, 159, 78, 168, 58, 226, 135, 48, 106, 9, 132, 59, 36, 141, 241, 209, 126, 245, 112, 34, 189, 86, 15, 176, 212, 74, 160, 247, 175, 115, 52, 23, 152, 65, 198, 139, 249, 153, 67, 240, 128, 27, 194, 101, 4, 77, 192, 97, 45, 220, 3, 92, 25, 117, 195, 41, 99, 228, 50, 151, 211, 236, 163, 227, 185, 105, 173, 234, 49, 219, 169, 2, 144, 180, 87, 125, 33, 60, 120, 76, 31, 143, 69, 127, 157, 81, 110, 237, 72, 215, 13, 250, 184, 140, 11, 161, 203, 251, 14, 210, 188, 17, 56, 204, 131, 44, 164, 68, 104, 207, 238, 129, 91, 57, 113, 40, 90, 244, 147, 177, 26, 114, 230, 148, 24, 84, 43, 8, 179, 231, 136, 170, 217, 124, 35, 95, 255, 82, 196, 53, 221, 118, 193, 73, 150, 22, 197, 75, 1, 64, 225, 158, 16, 183, 133, 0, 174, 154, 248, 102, 218, 37, 98, 254, 145, 191, 103, 206, 71, 39, 107, 242, 88, 28, 55, 181, 122, 165, 54, 119, 172, 20, 47, 137, 235, 167, 214, 62, 200, 134, 232]
HOLE_DEEP_BASE = 140                 # 4-entry cool run: glow-bright, glow-dim, deep-mid, deepest
HOLE_RAMP = [1 + v for v in range(16)] + [HOLE_DEEP_BASE + 2, HOLE_DEEP_BASE + 3]
HOLE_DARKEST = len(HOLE_RAMP) - 1
DARK_ROOM_SHADE = 6
FOG_RAMP_DIST = FX(6)
HOLE_HW = FX(0.30)
HOLE_Z0, HOLE_Z1 = 100, 212
HOLE_REVEAL_D = FX(0.12)             # proposed: the wall's cut thickness
HOLE_FADE_START = FX(0.5)            # cavity holds its lip shade this far in
SIDE_SPAN = 6                        # max ramp steps any interior panel travels
AO_TOP, AO_BOT = 1, -1               # side-panel vertical AO (1: 2 stacked into a black seam)
WALL_TILE_HI_X, WALL_TILE_HI_Y = 4, 4


def cos_fx(a):
    return int(round(math.cos(2 * math.pi * (a % 256) / 256.0) * FX_ONE))


def sin_fx(a):
    return int(round(math.sin(2 * math.pi * (a % 256) / 256.0) * FX_ONE))


PAL = ea.build_palette(open(os.path.join(SH, "raycast.c")).read(),
                       os.path.join(SH, "raycast.c"))
_t = ea.parse_tex(os.path.join(SH, "wall_tex_hi.h"))
TEX_W, TEX_H, TEX = _t["w"], _t["h"], _t["data"]      # x-major: [x*h + y]


def base_shade(t):
    """The face's fog shade — identical expression to the C."""
    if t < FX(2.5):
        return (t * 2) // FX(2.5)
    past = t - FX(2.5)
    span = FOG_RAMP_DIST - FX(2.5)
    return 2 + (past * 13) // span


# --------------------------------------------------------------- the sim frame
class Scene:
    """Hole on a y-plane (axis=1) at y=8, centred on x=8.5, cavity toward +y."""
    plane = FX(8.0)
    c0 = FX(8.5)
    axis = 1
    dir = 1


def render(dist, off_x, pitch=0, eye=128, fix=0):
    px = Scene.c0 + FX(off_x)
    py = Scene.plane - FX(dist)
    # ENCLOSURE: within 0.15 cells of the face the tunnel wraps dark
    # (+5 steps max) with the light left at the exit. Mirrors the game.
    cam_d = FX(dist)
    enc8 = 0
    if 0 < cam_d < FX(0.15):
        enc8 = min(5 * 256, (FX(0.15) - cam_d) * (5 * 256) // FX(0.13))
    angle = 64                                   # facing +y
    dirX, dirY = cos_fx(angle), sin_fx(angle)
    planeX, planeY = FX_MUL(-dirY, FX(0.66)), FX_MUL(dirX, FX(0.66))
    horizon_y = SCREEN_H // 2 - pitch
    PAR = FX(0.01)
    fb = [0] * (SCREEN_W * SCREEN_H)

    for col in range(SCREEN_W):
        camX = ((2 * col - SCREEN_W) << FX_SHIFT) // SCREEN_W
        rdx = dirX + FX_MUL(planeX, camX)
        rdy = dirY + FX_MUL(planeY, camX)
        rdp = rdy if Scene.axis else rdx
        rda = rdx if Scene.axis else rdy
        if -PAR < rdp < PAR:
            continue
        pp = py if Scene.axis else px
        pa = px if Scene.axis else py
        t = fx_div_hw(Scene.plane - pp, rdp)
        if t <= PAR:
            continue
        off = pa + FX_MUL(t, rda) - Scene.c0

        # --- stand-in wall pass: the face this hole is cut into -------------
        bsh = base_shade(t)
        lh = divu(SCREEN_H << FX_SHIFT, t)
        wb = horizon_y + ((lh * eye) >> 8)
        wtop = wb - lh
        wx = (pa + FX_MUL(t, rda)) & (FX_ONE - 1)
        tx = ((wx * (TEX_W * WALL_TILE_HI_X)) >> FX_SHIFT) & (TEX_W - 1)
        vstep = divu((TEX_H * WALL_TILE_HI_Y) << FX_SHIFT, lh) if lh else 0
        for y in range(max(0, wtop), min(SCREEN_H, wb + 1)):
            v = (((y - wtop) * vstep) >> FX_SHIFT) & (TEX_H - 1)
            ws = bsh + (TEX[tx * TEX_H + v] >> 1)
            fb[y * SCREEN_W + col] = WALL_BASE + min(ws, SHADE_LEVELS - 1)

        # --- draw_exit_hole, verbatim ---------------------------------------
        if off < -HOLE_HW or off > HOLE_HW:
            continue

        side_hit = 0
        t2_back = fx_div_hw(Scene.plane + Scene.dir * FX_ONE - pp, rdp)
        t2 = t2_back
        if rda > 64 or rda < -64:
            adrift = -rda if rda < 0 else rda
            edge = (HOLE_HW - off) if rda > 0 else (off + HOLE_HW)
            edge = max(edge, 0)
            ts = t + fx_div_hw(edge, adrift)
            if ts < t2:
                t2, side_hit = ts, 1

        lh_n = divu(SCREEN_H << FX_SHIFT, t)
        lh_f = divu(SCREEN_H << FX_SHIFT, t2)
        wb_n = horizon_y + ((lh_n * eye) >> 8)
        wb_f = horizon_y + ((lh_f * eye) >> 8)
        hn, hf = wb_n - ((lh_n * HOLE_Z1) >> 8), wb_f - ((lh_f * HOLE_Z1) >> 8)
        sn, sf = wb_n - ((lh_n * HOLE_Z0) >> 8), wb_f - ((lh_f * HOLE_Z0) >> 8)
        head_lo, head_hi = min(hn, hf), max(hn, hf)
        sill_lo, sill_hi = min(sn, sf), max(sn, sf)

        # THE IN-BETWEEN: crawlspace endpoints (the world's fog law at each
        # surface's ray distance), carried in 8.8 so the value between two
        # fog bands survives, and the 2x2 resolves it. Mixing starts at the
        # OUTER EDGE where the hole begins and spreads evenly with depth --
        # no floating front (the eased ramp's failure), no hard contour
        # bands (the pure fog-step version's).
        murk = HOLE_DARKEST - (bsh >> 2)
        if murk < HOLE_DARKEST - 2:
            murk = HOLE_DARKEST - 2
        depth_in = max(t2 - t, 0)

        def fog8(d):
            if d < FX(2.5):
                return (d * 512) // FX(2.5)
            d = min(d, FOG_RAMP_DIST)
            return 512 + (d - FX(2.5)) * (13 * 256) // (FOG_RAMP_DIST - FX(2.5))

        f0_8, f2_8 = fog8(t), fog8(t2)
        s_off = 1 if (side_hit and rda < 0) else 5 if side_hit else 2
        # Nothing but the BACK panel may reach murk: corners stay corners.
        # Enclosure raises the fall's ceiling along with the values.
        cap8 = min(((murk - 3) << 8) + enc8, 15 << 8)
        sv8 = min(f2_8 + (s_off << 8) + enc8, cap8)
        # Head/sill fall toward fog at THIS COLUMN's far distance (t2), which
        # is continuous across the side/back column boundary -- one global
        # back-plane target with clamps drew a vertical cliff down the head
        # at the back panel's corner column. The seam rule is structural now.

        def dith(acc8, y):
            if DITHER == "checker":   th = ((y ^ col) & 1) << 7
            elif DITHER == "bayer2":  th = (0,128,192,64)[((y & 1) << 1) | (col & 1)]
            elif DITHER == "bayer4":  th = B4[((y & 3) << 2) | (col & 3)]
            elif DITHER == "none":    th = 128
            elif DITHER == "blue":    th = BN[((y & 15) << 4) | (col & 15)]
            else:                     th = (0,128,192,64)[((y & 1) << 1) | (col & 1)]
            v = (acc8 + th * DITHER_AMP // 256) >> 8
            v = max(0, min(v, HOLE_DARKEST))
            return HOLE_RAMP[(v // RAMP_STRIDE) * RAMP_STRIDE]

        def put(y, c):
            if 0 <= y < SCREEN_H:
                fb[y * SCREEN_W + col] = c

        # head underside: fog lerp lip -> back, +3-step reveal shadow at the
        # lip decaying to the +2 run-in by the back
        h = max(head_hi - head_lo, 1)
        y0, y1 = max(head_lo, 0), min(head_hi, SCREEN_H - 1)
        for y in range(y0, y1 + 1):
            k = ((y - head_lo) << 8) // h            # 0..256 lip -> back
            s8 = f0_8 + ((f2_8 - f0_8) * k >> 8) + (2 << 8) + 3 * (256 - k) + enc8
            put(y, dith(min(s8, cap8), y))
        # core. (The jamb branch is gone: its flat strip seamed against the
        # fog-continuum side panels; fog8 of a shallow ts covers the jamb.)
        y0, y1 = max(head_hi + 1, 0), min(sill_lo - 1, SCREEN_H - 1)
        if not side_hit and y0 <= y1:
            if fix:
                # Not a flat oblong: the far wall is deepest in shadow at the
                # top and lifted where the sill bounces into its foot. One
                # step of range, but it is the step that makes it a SURFACE.
                span = max(sill_lo - head_hi, 1)
                # Horizontal term too: the room's light enters from one side,
                # so the far wall cannot be evenly dark across its width or it
                # reads as a sticker rather than the back of a lit box. Signed
                # by the ray's own offset across the aperture, the same
                # quantity the side panels take their lit/shadow sense from.
                hx = (off << 8) // HOLE_HW                # -256..+256
                # Only the hole's own cool run, never the wall ramp. LIGHTER:
                # darkest is deep-mid, a shadowed room rather than a void; a
                # faint interior light still rises at the foot, side-biased.
                lift = (1 << 8) + (((hx + 256) * 160) >> 9)   # 256..416
                # Fuzzy edge: panel border dissolves into the tunnel over a
                # few pixels, chunky 2px checker (the quarter-res soft edge).
                # FOG HONOR: crossfade into plain fogged tunnel tone past
                # ~3 cells -- fixed CRAM ignores fog and glowed blue at range.
                fogmix = max(0, (f0_8 - (4 << 8)) >> 7)
                fogmix = min(fogmix, 4)
                for y in range(y0, y1 + 1):
                    if fogmix and ((y + (col << 1)) & 3) < fogmix:
                        put(y, dith(min(f2_8 + (2 << 8), cap8), y))
                        continue
                    k = ((y - head_hi) << 8) // span
                    kk = (k * k) >> 8
                    v = ((2 << 8) - ((kk * lift) >> 8) + 128) >> 8
                    put(y, HOLE_DEEP_BASE + max(0, min(v, 3)))
            else:
                ca, cb = HOLE_RAMP[murk], HOLE_RAMP[max(murk - 1, 0)]
                for y in range(y0, y1 + 1):
                    put(y, cb if ((y ^ col) & 1) else ca)
        else:
            # Cavity side wall. Depth is constant down a column, so the panel
            # was one flat value -- which is what segmented it into bands. A
            # side wall in a real cavity is not evenly lit: the head above it
            # shadows the top, the sill below bounces into the bottom. That
            # vertical AO is both the truth and the thing that breaks the band.
            acc = sv8 + ((1 << 8) if fix else 0)
            span = max(sill_lo - head_hi, 1)
            if fix:
                a0 = acc + (AO_TOP << 8)
                astep = ((AO_BOT - AO_TOP) << 8) // span
                for y in range(y0, y1 + 1):
                    put(y, dith(a0 + astep * (y - head_hi), y))
            else:
                for y in range(y0, y1 + 1):
                    put(y, dith(acc, y))
        # sill: same fog lerp, no reveal shadow (the ledge catches the light)
        h = max(sill_hi - sill_lo, 1)
        y0, y1 = max(sill_lo, 0), min(sill_hi, SCREEN_H - 1)
        for y in range(y0, y1 + 1):
            k = ((sill_hi - y) << 8) // h            # 0 at the near lip
            s8 = f0_8 + ((f2_8 - f0_8) * k >> 8) + (2 << 8) + enc8
            put(y, dith(min(s8, cap8), y))
    return fb


def save(fb, path, scale=2):
    rows = []
    for y in range(SCREEN_H):
        row = bytearray()
        for x in range(SCREEN_W):
            r, g, b = PAL[fb[y * SCREEN_W + x]]
            row += bytes((r, g, b)) * scale
        for _ in range(scale):
            rows.append(bytes(row))
    try:
        from PIL import Image
        im = Image.frombytes("RGB", (SCREEN_W * scale, SCREEN_H * scale),
                             b"".join(rows))
        im.save(path)
    except ImportError:
        path = path.rsplit(".", 1)[0] + ".ppm"
        with open(path, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (SCREEN_W * scale, SCREEN_H * scale))
            f.write(b"".join(rows))
    return path


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--dist", type=float, action="append",
                    help="camera distance from the face, in cells (repeatable)")
    ap.add_argument("--off", type=float, default=0.0,
                    help="lateral offset from the hole centre, in cells")
    ap.add_argument("--pitch", type=int, default=0)
    ap.add_argument("--fix", action="store_true")
    ap.add_argument("--dither", default="bayer2",  # the GAME: 8.8 fog + 2x2 on gradients
                    choices=["none", "checker", "bayer2", "bayer4", "blue"])
    ap.add_argument("--amp", type=int, default=256,
                    help="threshold range as /256 of one ramp step")
    ap.add_argument("--stride", type=int, default=1,
                    help="use every Nth ramp entry — coarser ramp, stronger stipple")
    ap.add_argument("--tag", default="base")
    ap.add_argument("--out", default=os.environ.get("SCRATCH", "/tmp"))
    a = ap.parse_args()
    globals().update(DITHER=a.dither, DITHER_AMP=a.amp, RAMP_STRIDE=a.stride)
    for d in (a.dist or [0.7, 1.2, 2.5]):
        p = os.path.join(a.out, "sim_exit_hole_%s_d%.1f_o%.2f.png"
                         % (a.tag, d, a.off))
        print(save(render(d, a.off, a.pitch, fix=a.fix), p))
