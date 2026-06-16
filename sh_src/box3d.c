#include "mars.h"
#include "raycast.h"     /* fx_t, FX/FX_MUL/FX_DIV/FX_INT, SCREEN_W/H */
#include "shared.h"      /* MARS_CMD_BOX / MARS_CMD_NONE                */
#include "font.h"        /* on-screen frame-time overlay               */
#include "box3d.h"
#include "box_model.h"
#include "box_label.h"

/* Per-frame render-time overlay (master total + slave band, in FRT
 * ticks at Phi/32 ~= 1.39us/tick; 60fps ~= 12000). Optimize against
 * numbers, not vibes. Set to 0 to hide. */
#define BOX_PROFILE 1

/* Buffer-swap state shared with m_main. */
extern uint32_t lastTick;
extern uint16_t currentFB;

/* --- Cardboard look ------------------------------------------------ */
#define N_SHADES   16
#define RAMP_BASE  64          /* cardboard CRAM 64..79                 */
#define N_TAPE      8
#define TAPE_BASE  80          /* packing-tape CRAM 80..87              */
#define BG_IDX      0          /* void background                       */
#define AMBIENT    FX(0.18)    /* darkest a lit face gets               */

/* Direction TO the key light (world space, unit). */
#define LIGHT_X    FX(-0.384)
#define LIGHT_Y    FX(-0.512)
#define LIGHT_Z    FX( 0.768)

/* Half-resolution render target. The box is rasterized into a 160x112
 * logical screen; each logical pixel is blitted as a 2x2 block into the
 * 320x224 framebuffer. Quarter the per-pixel detail work, no separate
 * upscale pass. It's an effect — chunky is fine. */
#define LOG_W (SCREEN_W / 2)   /* 160 */
#define LOG_H (SCREEN_H / 2)   /* 112 */

/* "Been around a while" detail knobs. */
#define EDGE_SOFT  FX(0.06)
#define EDGE_HARD  FX(0.02)
#define FLUTES     22
#define FLUTE_W    FX(0.16)
#define TAPE_HALF  FX(0.085)
#define GRAD_RANGE 4           /* vertical light falloff: +/-2 shades top->bottom */

#define TAPE_NONE   0
#define TAPE_USTRIP 1
#define TAPE_VEDGE  2
static const uint8_t face_tape[BOX_NFACES] = {
    TAPE_NONE, TAPE_USTRIP, TAPE_NONE, TAPE_NONE, TAPE_NONE,
    TAPE_VEDGE, TAPE_VEDGE, TAPE_NONE, TAPE_NONE
};

/* The South wall (face 1) is the labelled front: it samples the label
 * texture instead of the cardboard ramp, so the SEGA CORE label deforms
 * with the box. 128-texel texture -> 7-bit UV index. */
#define LABEL_FACE  1
#define LABEL_TSH   (FX_SHIFT - 7)
#define LABEL_TMASK 127


static const fx_t FACE_U[4] = { 0, FX_ONE, FX_ONE, 0 };
static const fx_t FACE_V[4] = { 0, 0, FX_ONE, FX_ONE };

/* Baked worn-cardboard detail (edges + corrugation), sampled per pixel. */
#define WEAR_BITS 6
#define WEAR_N    (1 << WEAR_BITS)
#define WEAR_SH   (FX_SHIFT - WEAR_BITS)
#define WEAR_MASK (WEAR_N - 1)
static int8_t wear_lut[WEAR_N][WEAR_N];

typedef struct { fx_t cx, cy, depth, u, v; } cvert_t;

/* --- Master-built shared draw-list --------------------------------- *
 * The MASTER does all the geometry (transform, near-clip, project,
 * shade, depth-sort) once and stores the screen-space polygons here.
 * Both SH-2s then rasterize disjoint framebuffer bands from it, so the
 * expensive fill is split in half. Accessed via the cache-through alias
 * on both CPUs (master writes, slave reads) — same coherency trick as
 * SHARED_UC. */
#define BOX_MAXV 6
typedef struct {
    int16_t sx[BOX_MAXV], sy[BOX_MAXV];
    int32_t u[BOX_MAXV],  v[BOX_MAXV];
    uint8_t nv, level, tape, label;
} box_poly_t;
typedef struct {
    box_poly_t poly[BOX_NFACES];
    uint8_t    order[BOX_NFACES];
} box_drawlist_t;

static box_drawlist_t box_dl;
#define BOX_DL ((volatile box_drawlist_t *)((uintptr_t)&box_dl | 0x20000000))

/* Master-only geometry scratch. */
static fx_t    wverts[BOX_NVERTS][3];
static cvert_t cverts[BOX_NVERTS];

#if BOX_PROFILE
static inline uint16_t frt_read(void) {
    uint8_t hi = SH2_FRT_FRCH, lo = SH2_FRT_FRCL;
    return ((uint16_t)hi << 8) | lo;
}
/* Master-band breakdown: time spent clearing vs filling. */
static volatile uint16_t g_clear_ticks, g_fill_ticks;
#endif

/* --- Fixed-point sqrt ---------------------------------------------- */
static uint32_t isqrt32(uint32_t x) {
    uint32_t res = 0, bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= res + bit) { x -= res + bit; res = (res >> 1) + bit; }
        else res >>= 1;
        bit >>= 2;
    }
    return res;
}
static fx_t fx_sqrt(fx_t v) {
    if (v <= 0) return 0;
    return (fx_t)(isqrt32((uint32_t)v) * 256);
}

/* --- Wear LUT (built once on the master) --------------------------- */
static int wear_delta(fx_t u, fx_t v) {
    int d = 0;
    fx_t e = u;
    if (FX_ONE - u < e) e = FX_ONE - u;
    if (v < e)          e = v;
    if (FX_ONE - v < e) e = FX_ONE - v;
    if (e < EDGE_HARD)      d -= 3;
    else if (e < EDGE_SOFT) d -= 2;
    if ((FX_MUL(v, FX(FLUTES)) & (FX_ONE - 1)) < FLUTE_W) d -= 1;
    /* Smooth vertical gradient: top of the face lighter, bottom darker
     * (overhead light + ambient occlusion) — turns flat faces into
     * shaded ones. */
    d += FX_INT(FX_MUL(v - (FX_ONE >> 1), FX(GRAD_RANGE)));
    return d;
}
static void build_wear_lut(void) {
    fx_t half = FX_ONE >> (WEAR_BITS + 1);
    for (int iy = 0; iy < WEAR_N; iy++) {
        fx_t v = ((fx_t)iy << WEAR_SH) + half;
        for (int ix = 0; ix < WEAR_N; ix++) {
            fx_t u = ((fx_t)ix << WEAR_SH) + half;
            wear_lut[iy][ix] = (int8_t)wear_delta(u, v);
        }
    }
}

static inline int detail_index(int lvl, fx_t u, fx_t v, int tape_mode) {
    if (tape_mode == TAPE_USTRIP) {
        fx_t d = u - (FX_ONE >> 1);
        if (d < 0) d = -d;
        if (d < TAPE_HALF) { int tl = lvl >> 1; if (tl >= N_TAPE) tl = N_TAPE - 1; return TAPE_BASE + tl; }
    } else if (tape_mode == TAPE_VEDGE) {
        if (v > FX_ONE - 2 * TAPE_HALF) { int tl = lvl >> 1; if (tl >= N_TAPE) tl = N_TAPE - 1; return TAPE_BASE + tl; }
    }
    int L = lvl + wear_lut[(v >> WEAR_SH) & WEAR_MASK][(u >> WEAR_SH) & WEAR_MASK];
    if (L < 0) L = 0;
    else if (L >= N_SHADES) L = N_SHADES - 1;
    return RAMP_BASE + L;
}

/* ================================================================== *
 *  MASTER: geometry — transform, shade, clip, project, sort          *
 * ================================================================== */
static void build_frame(int f) {
    fx_t t = box_morph[f];
    const int32_t *cr = box_cam_right[f];
    const int32_t *cu = box_cam_up[f];
    const int32_t *cb = box_cam_back[f];
    const int32_t *cp = box_cam_pos[f];
    for (int i = 0; i < BOX_NVERTS; i++) {
        fx_t wx = box_pose_open[i][0] + FX_MUL(t, box_pose_closed[i][0] - box_pose_open[i][0]);
        fx_t wy = box_pose_open[i][1] + FX_MUL(t, box_pose_closed[i][1] - box_pose_open[i][1]);
        fx_t wz = box_pose_open[i][2] + FX_MUL(t, box_pose_closed[i][2] - box_pose_open[i][2]);
        wverts[i][0] = wx; wverts[i][1] = wy; wverts[i][2] = wz;
        fx_t rx = wx - cp[0], ry = wy - cp[1], rz = wz - cp[2];
        cverts[i].cx    =  FX_MUL(rx, cr[0]) + FX_MUL(ry, cr[1]) + FX_MUL(rz, cr[2]);
        cverts[i].cy    =  FX_MUL(rx, cu[0]) + FX_MUL(ry, cu[1]) + FX_MUL(rz, cu[2]);
        cverts[i].depth = -(FX_MUL(rx, cb[0]) + FX_MUL(ry, cb[1]) + FX_MUL(rz, cb[2]));
    }
}

static int face_level(int fi, int f) {
    const uint8_t *vi = box_faces[fi];
    const fx_t *a = wverts[vi[0]], *b = wverts[vi[1]], *c = wverts[vi[2]];
    fx_t ux = b[0]-a[0], uy = b[1]-a[1], uz = b[2]-a[2];
    fx_t vx = c[0]-a[0], vy = c[1]-a[1], vz = c[2]-a[2];
    fx_t nx = FX_MUL(uy,vz) - FX_MUL(uz,vy);
    fx_t ny = FX_MUL(uz,vx) - FX_MUL(ux,vz);
    fx_t nz = FX_MUL(ux,vy) - FX_MUL(uy,vx);
    const int32_t *cp = box_cam_pos[f];
    fx_t facing = FX_MUL(nx, cp[0]-a[0]) + FX_MUL(ny, cp[1]-a[1]) + FX_MUL(nz, cp[2]-a[2]);
    if (facing < 0) { nx = -nx; ny = -ny; nz = -nz; }
    fx_t len = fx_sqrt(FX_MUL(nx,nx) + FX_MUL(ny,ny) + FX_MUL(nz,nz));
    if (len <= 0) return N_SHADES - 1;
    fx_t bri = FX_DIV(FX_MUL(nx,LIGHT_X) + FX_MUL(ny,LIGHT_Y) + FX_MUL(nz,LIGHT_Z), len);
    if (bri < AMBIENT) bri = AMBIENT;
    if (bri > FX_ONE)  bri = FX_ONE;
    int lvl = FX_INT(FX_MUL(bri, FX(N_SHADES - 1)));
    if (lvl < 0) lvl = 0;
    if (lvl >= N_SHADES) lvl = N_SHADES - 1;
    return lvl;
}

/* Near-clip + project one face, store the screen polygon into the
 * shared draw-list slot for `fi`. */
static void clip_project_store(int fi, int level, int show_label) {
    const uint8_t *vi = box_faces[fi];
    cvert_t in[4], out[BOX_MAXV];
    for (int k = 0; k < 4; k++) {
        in[k] = cverts[vi[k]];
        in[k].u = FACE_U[k];
        in[k].v = FACE_V[k];
    }
    int nout = 0;
    for (int k = 0; k < 4; k++) {
        cvert_t cur = in[k], nxt = in[(k + 1) & 3];
        int curIn = cur.depth >= BOX_NEAR;
        int nxtIn = nxt.depth >= BOX_NEAR;
        if (curIn) out[nout++] = cur;
        if (curIn != nxtIn) {
            fx_t tt = FX_DIV(BOX_NEAR - cur.depth, nxt.depth - cur.depth);
            out[nout].cx    = cur.cx + FX_MUL(tt, nxt.cx - cur.cx);
            out[nout].cy    = cur.cy + FX_MUL(tt, nxt.cy - cur.cy);
            out[nout].depth = BOX_NEAR;
            out[nout].u     = cur.u + FX_MUL(tt, nxt.u - cur.u);
            out[nout].v     = cur.v + FX_MUL(tt, nxt.v - cur.v);
            nout++;
        }
    }
    volatile box_poly_t *p = &BOX_DL->poly[fi];
    if (nout < 3) { p->nv = 0; return; }
    for (int k = 0; k < nout; k++) {
        fx_t ndcx = FX_MUL(BOX_PROJ_X, FX_DIV(out[k].cx, out[k].depth));
        fx_t ndcy = FX_MUL(BOX_PROJ_Y, FX_DIV(out[k].cy, out[k].depth));
        if (ndcx < FX(-8)) ndcx = FX(-8); else if (ndcx > FX(8)) ndcx = FX(8);
        if (ndcy < FX(-8)) ndcy = FX(-8); else if (ndcy > FX(8)) ndcy = FX(8);
        p->sx[k] = (int16_t)((LOG_W >> 1) + FX_INT(FX_MUL(ndcx, FX(LOG_W >> 1))));
        p->sy[k] = (int16_t)((LOG_H >> 1) - FX_INT(FX_MUL(ndcy, FX(LOG_H >> 1))));
        p->u[k]  = out[k].u;
        p->v[k]  = out[k].v;
    }
    p->nv    = (uint8_t)nout;
    p->level = (uint8_t)level;
    p->tape  = face_tape[fi];
    p->label = (uint8_t)(fi == LABEL_FACE && show_label);
}

static void render_geometry(int f) {
    build_frame(f);
    /* Only show the label while the camera is in FRONT of the labelled
     * wall (y=-1, facing -Y). Once we arc over / dive inside, that wall's
     * interior is plain cardboard. */
    int show_label = (box_cam_pos[f][1] < FX(-1));
    fx_t key[BOX_NFACES];
    uint8_t order[BOX_NFACES];
    for (int i = 0; i < BOX_NFACES; i++) {
        const uint8_t *vi = box_faces[i];
        key[i] = cverts[vi[0]].depth + cverts[vi[1]].depth
               + cverts[vi[2]].depth + cverts[vi[3]].depth;
        clip_project_store(i, face_level(i, f), show_label);
        order[i] = (uint8_t)i;
    }
    for (int i = 1; i < BOX_NFACES; i++) {
        uint8_t o = order[i]; fx_t kk = key[o]; int j = i - 1;
        while (j >= 0 && key[order[j]] < kk) { order[j + 1] = order[j]; j--; }
        order[j + 1] = o;
    }
    for (int i = 0; i < BOX_NFACES; i++) BOX_DL->order[i] = order[i];
}

/* ================================================================== *
 *  BOTH CPUs: rasterize a framebuffer band from the draw-list        *
 * ================================================================== */
/* Fill one face in 160x112 logical space, clamped to logical rows
 * [yc0, yc1). Each logical pixel is written as a 2x2 block in the
 * 320x224 framebuffer via two paired 16-bit stores (B). Edges are
 * walked incrementally: each edge's x/u/v slope-per-scanline is divided
 * out ONCE up front, so the per-scanline loop has no divide (A). */
static void fill_face(const int16_t *xs, const int16_t *ys,
                      const int32_t *us, const int32_t *vs,
                      int n, int base_lvl, int tape_mode, int label,
                      int yc0, int yc1) {
    uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);

    /* Per-edge setup: top vertex + slope of x,u,v per scanline. */
    int  e_yt[BOX_MAXV], e_yb[BOX_MAXV], e_xt[BOX_MAXV];
    fx_t e_ut[BOX_MAXV], e_vt[BOX_MAXV];
    fx_t e_sx[BOX_MAXV], e_su[BOX_MAXV], e_sv[BOX_MAXV];
    int ne = 0, ymin = LOG_H, ymax = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        int ya = ys[i], yb = ys[j];
        if (ya == yb) continue;
        int yt, yb2, xt, xb; fx_t ut, ub, vt, vb;
        if (ya < yb) { yt=ya; yb2=yb; xt=xs[i]; xb=xs[j]; ut=us[i]; ub=us[j]; vt=vs[i]; vb=vs[j]; }
        else         { yt=yb; yb2=ya; xt=xs[j]; xb=xs[i]; ut=us[j]; ub=us[i]; vt=vs[j]; vb=vs[i]; }
        int dy = yb2 - yt;
        e_yt[ne]=yt; e_yb[ne]=yb2; e_xt[ne]=xt; e_ut[ne]=ut; e_vt[ne]=vt;
        e_sx[ne] = ((fx_t)(xb - xt) << FX_SHIFT) / dy;   /* x per scanline (fx) */
        e_su[ne] = (ub - ut) / dy;                        /* u per scanline      */
        e_sv[ne] = (vb - vt) / dy;
        ne++;
        if (yt  < ymin) ymin = yt;
        if (yb2 > ymax) ymax = yb2;
    }
    if (ymin < yc0) ymin = yc0;
    if (ymax > yc1) ymax = yc1;

    for (int yy = ymin; yy < ymax; yy++) {
        int xL = LOG_W, xR = -1;
        fx_t uL = 0, vL = 0, uR = 0, vR = 0;
        for (int e = 0; e < ne; e++) {
            if (yy < e_yt[e] || yy >= e_yb[e]) continue;
            int dy = yy - e_yt[e];
            int  xx = e_xt[e] + (int)(((int64_t)e_sx[e] * dy) >> FX_SHIFT);
            fx_t uu = e_ut[e] + e_su[e] * dy;
            fx_t vv = e_vt[e] + e_sv[e] * dy;
            if (xx < xL) { xL = xx; uL = uu; vL = vv; }
            if (xx > xR) { xR = xx; uR = uu; vR = vv; }
        }
        if (xR < xL) continue;
        int span = xR - xL; if (span < 1) span = 1;
        fx_t du = (uR - uL) / span;
        fx_t dv = (vR - vL) / span;
        int sxL = xL, sxR = xR;
        fx_t u = uL, v = vL;
        if (sxL < 0) { u += du * (-sxL); v += dv * (-sxL); sxL = 0; }
        if (sxR > LOG_W - 1) sxR = LOG_W - 1;

        /* Each logical pixel is a 2x2 block: two framebuffer rows, one
         * 16-bit store each (both screen pixels share the colour). The
         * front face samples the label texture (its texels ARE CRAM
         * indices); every other face runs the cardboard wear/tape path. */
        uint16_t *r0 = (uint16_t *)(fb + (yy << 1) * SCREEN_W);
        uint16_t *r1 = (uint16_t *)(fb + ((yy << 1) + 1) * SCREEN_W);
        if (label) {
            for (int x = sxL; x <= sxR; x++) {
                uint8_t c = label_tex[(v >> LABEL_TSH) & LABEL_TMASK]
                                     [(u >> LABEL_TSH) & LABEL_TMASK];
                uint16_t cc = (uint16_t)((c << 8) | c);
                r0[x] = cc;
                r1[x] = cc;
                u += du; v += dv;
            }
        } else {
            for (int x = sxL; x <= sxR; x++) {
                uint8_t c = (uint8_t)detail_index(base_lvl, u, v, tape_mode);
                uint16_t cc = (uint16_t)((c << 8) | c);
                r0[x] = cc;
                r1[x] = cc;
                u += du; v += dv;
            }
        }
    }
}

void box3d_render_band(int band) {
    int ly0 = band ? LOG_H / 2 : 0;        /* logical row range          */
    int ly1 = band ? LOG_H : LOG_H / 2;

#if BOX_PROFILE
    uint16_t tprof = (band == 0) ? frt_read() : 0;
#endif
    /* Clear this half's full-res rows [2*ly0, 2*ly1) to the void. */
    uint32_t *p = (uint32_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200 + (ly0 << 1) * SCREEN_W);
    uint32_t bg = (BG_IDX << 24) | (BG_IDX << 16) | (BG_IDX << 8) | BG_IDX;
    int words = ((ly1 - ly0) << 1) * SCREEN_W / 4;
    for (int i = 0; i < words; i++) p[i] = bg;

#if BOX_PROFILE
    if (band == 0) { g_clear_ticks = (uint16_t)(frt_read() - tprof); tprof = frt_read(); }
#endif

    /* Faces far-to-near (painter's). Per-face polygon is copied out of
     * the cache-through draw-list into locals so per-scanline reads hit
     * cached memory. */
    for (int i = 0; i < BOX_NFACES; i++) {
        int fi = BOX_DL->order[i];
        volatile box_poly_t *vp = &BOX_DL->poly[fi];
        int nv = vp->nv;
        if (nv < 3) continue;
        int16_t lx[BOX_MAXV], ly[BOX_MAXV];
        int32_t lu[BOX_MAXV], lv[BOX_MAXV];
        for (int k = 0; k < nv; k++) {
            lx[k] = vp->sx[k]; ly[k] = vp->sy[k];
            lu[k] = vp->u[k];  lv[k] = vp->v[k];
        }
        fill_face(lx, ly, lu, lv, nv, vp->level, vp->tape, vp->label, ly0, ly1);
    }
#if BOX_PROFILE
    if (band == 0) g_fill_ticks = (uint16_t)(frt_read() - tprof);
#endif
}

/* Master geometry + dual-CPU fill for one frame. */
static void render_frame(int f) {
    render_geometry(f);

    /* Drain the cache-through SDRAM write stream before waking the slave
     * (same race as raycast_render's partition dispatch): the ~675 bytes
     * of BOX_DL we just wrote go through the 0x20000000 alias, the COMM4
     * wake goes through MARS MMIO. With no barrier the slave can read a
     * stale BOX_DL and rasterize the bottom band from garbage. A
     * read-back of the last-written field (order[]) through the same
     * alias serializes against all prior writes; the compiler barrier
     * keeps it from sinking past the COMM4 store. */
    (void)BOX_DL->order[BOX_NFACES - 1];
    __asm__ __volatile__("" ::: "memory");

    MARS_SYS_COMM4 = MARS_CMD_BOX;        /* slave: bottom half         */
    box3d_render_band(0);                 /* master: top half           */
    while (MARS_SYS_COMM4 != MARS_CMD_NONE) {
        __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop\n\t"
                             "nop\n\tnop\n\tnop\n\tnop\n\t");
    }
}

#if BOX_PROFILE
static void put5(char *s, uint16_t v) {
    s[4] = '0' + v % 10; v /= 10;
    s[3] = '0' + v % 10; v /= 10;
    s[2] = '0' + v % 10; v /= 10;
    s[1] = '0' + v % 10; v /= 10;
    s[0] = '0' + v % 10;
}
static void draw_perf(uint8_t *fb, uint16_t m, uint16_t s) {
    char buf[16];
    buf[0] = 'M'; buf[1] = ':'; put5(buf + 2, m);
    buf[7] = ' '; buf[8] = 'S'; buf[9] = ':'; put5(buf + 10, s); buf[15] = 0;
    font_draw_string(fb, 4, 4, buf, BOX_TEXT_IDX);
    /* Master-band breakdown: clear vs fill. */
    char b2[16];
    b2[0] = 'C'; b2[1] = ':'; put5(b2 + 2, g_clear_ticks);
    b2[7] = ' '; b2[8] = 'F'; b2[9] = ':'; put5(b2 + 10, g_fill_ticks); b2[15] = 0;
    font_draw_string(fb, 4, 13, b2, BOX_TEXT_IDX);
}
#endif

/* --- Public API ---------------------------------------------------- */
void box3d_flip(void) {
    while (lastTick == MARS_SYS_COMM12);
    MARS_VDP_FBCTL = currentFB ^ 1;
    while ((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB);
    currentFB ^= 1;
    lastTick = MARS_SYS_COMM12;
}

/* Frame-0 flip: swap the hero palette out for the box palette IN vblank,
 * immediately before the buffer flip. The box's first frame is already
 * drawn to the hidden page, so the hero is never displayed through the
 * box palette — no transition corruption, no black blink. The ~57 CRAM
 * writes fit comfortably in the vblank window. */
static void box3d_flip_load_palette(void) {
    while (lastTick == MARS_SYS_COMM12);
    box3d_load_palette();
    MARS_VDP_FBCTL = currentFB ^ 1;
    while ((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB);
    currentFB ^= 1;
    lastTick = MARS_SYS_COMM12;
}

void box3d_load_palette(void) {
    Hw32xSetBGColor(BG_IDX, 0, 0, 0);
    for (int i = 0; i < N_SHADES; i++) {
        int r = 10 + ((29 - 10) * i) / (N_SHADES - 1);
        int g =  7 + ((24 -  7) * i) / (N_SHADES - 1);
        int b =  3 + ((14 -  3) * i) / (N_SHADES - 1);
        Hw32xSetBGColor(RAMP_BASE + i, r, g, b);
    }
    for (int i = 0; i < N_TAPE; i++) {
        int r = 20 + ((31 - 20) * i) / (N_TAPE - 1);
        int g = 19 + ((30 - 19) * i) / (N_TAPE - 1);
        int b = 15 + ((27 - 15) * i) / (N_TAPE - 1);
        Hw32xSetBGColor(TAPE_BASE + i, r, g, b);
    }
    /* Label texture palette (front face). */
    for (int i = 0; i < LABEL_N; i++)
        Hw32xSetBGColor(LABEL_BASE + i, label_palette[i][0],
                        label_palette[i][1], label_palette[i][2]);
    Hw32xSetBGColor(BOX_TEXT_IDX, 31, 31, 31);
}

void box3d_show_final(void) {
    render_frame(BOX_NFRAMES - 1);
}

void box3d_play(void) {
#if BOX_PROFILE
    SH2_FRT_TIER = 0x01; SH2_FRT_TCR = 0x01; SH2_FRT_FTCSR = 0;  /* Phi/32 */
#endif
    build_wear_lut();        /* CPU precompute (was in load_palette) */
    uint16_t prev_pad = 0xFFFF;
    for (int f = 0; f < BOX_NFRAMES; f++) {
#if BOX_PROFILE
        uint16_t t0 = frt_read();
        render_frame(f);
        uint16_t mdelta = (uint16_t)(frt_read() - t0);   /* pure render time */
        draw_perf((uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200),
                  mdelta, SHARED_UC->slave_render_ticks);
#else
        render_frame(f);
#endif
        /* Frame 0 swaps in the box palette during this flip's vblank,
         * after the box is already on the hidden page — clean hand-off. */
        if (f == 0) box3d_flip_load_palette();
        else        box3d_flip();
        HwMdReadPad(0);
        uint16_t pad = MARS_SYS_COMM8;
        uint16_t pressed = (uint16_t)(pad & ~prev_pad);
        prev_pad = pad;
        if (pressed & SEGA_CTRL_START) return;
    }
}
