#include "smsvdp.h"

/* See smsvdp.h. Layout follows the real chip:
 *   control port: byte pairs. 2nd byte bits 7-6 = code:
 *     0/1 = set VRAM address (0 also primes a read on hardware; write-only
 *           here), 2 = register write (reg in 2nd byte bits 3-0, data in
 *           1st byte), 3 = set CRAM address.
 *   data port: write byte at the latched address, autoincrement (VRAM
 *   wraps 16KB, CRAM wraps 32).
 * The two-byte latch state is part of the contract: real code interleaves
 * control pairs and data runs, and a stray single control byte before a
 * data write is resolved exactly like the silicon resolves it. */

static uint8_t  vram[0x4000];
static uint8_t  cram[32];
static uint8_t  regs[16];
static uint16_t addr;        /* current VRAM/CRAM address */
static uint8_t  code;        /* 0..3, from the last control pair */
static uint8_t  ctrl_pending;/* 1 = first control byte latched */
static uint8_t  ctrl_first;

void smsvdp_reset(void) {
    for (int i = 0; i < 0x4000; i++) vram[i] = 0;
    for (int i = 0; i < 32; i++) cram[i] = 0;
    for (int i = 0; i < 16; i++) regs[i] = 0;
    addr = 0; code = 0; ctrl_pending = 0; ctrl_first = 0;
}

void smsvdp_write_ctrl(uint8_t b) {
    if (!ctrl_pending) { ctrl_first = b; ctrl_pending = 1; return; }
    ctrl_pending = 0;
    code = (uint8_t)(b >> 6);
    if (code == 2) {
        regs[b & 0x0F] = ctrl_first;
    } else {
        addr = (uint16_t)(((b & 0x3F) << 8) | ctrl_first);
    }
}

void smsvdp_write_data(uint8_t b) {
    ctrl_pending = 0;            /* a data write clears a half-latched pair */
    if (code == 3) {
        cram[addr & 31] = b;
        addr = (uint16_t)((addr + 1) & 0x3FFF);
    } else {
        vram[addr & 0x3FFF] = b;
        addr = (uint16_t)((addr + 1) & 0x3FFF);
    }
}

uint8_t smsvdp_cram(int i) { return cram[i & 31]; }
uint8_t smsvdp_reg(int r)  { return regs[r & 15]; }

/* One 8-pixel pattern row, 4 planar bytes -> 8 4-bit pixels. */
static inline uint8_t pat_pixel(const uint8_t *row, int px) {
    int bit = 7 - px;
    return (uint8_t)((((row[0] >> bit) & 1))
                   | (((row[1] >> bit) & 1) << 1)
                   | (((row[2] >> bit) & 1) << 2)
                   | (((row[3] >> bit) & 1) << 3));
}

void smsvdp_render(uint8_t *dst, int pitch) {
    if (!(regs[1] & 0x40)) {                     /* display off: border fill */
        uint8_t bd = (uint8_t)(16 + (regs[7] & 0x0F));
        for (int y = 0; y < 192; y++)
            for (int x = 0; x < 256; x++) dst[y * pitch + x] = bd;
        return;
    }
    const uint8_t *nt  = &vram[(regs[2] & 0x0E) << 10];   /* name table   */
    const uint8_t *sat = &vram[(regs[5] & 0x7E) << 7];    /* sprite attrs */
    int spr_base = (regs[6] & 0x04) ? 0x2000 : 0x0000;    /* sprite tiles */
    int spr_h    = (regs[1] & 0x02) ? 16 : 8;             /* 8x16 mode    */
    int sx_scroll = regs[8];
    int sy_scroll = regs[9] % 224;                        /* 28-row space */

    /* Background, priority recorded per pixel (bit 7 of a scratch byte —
     * the dst byte itself carries it so no second buffer is needed; it is
     * stripped after sprites composite). */
    for (int y = 0; y < 192; y++) {
        int vy = (y + sy_scroll) % 224;
        int trow = vy >> 3, py = vy & 7;
        uint8_t *out = dst + y * pitch;
        for (int x = 0; x < 256; x++) {
            /* X scroll ADDS screen-left shift on hardware: column moves
             * right as the value grows, i.e. source x = screen x - scroll. */
            int vx = (x - sx_scroll) & 255;
            const uint8_t *e = &nt[(trow << 6) | ((vx >> 3) << 1)];
            uint16_t ent = (uint16_t)(e[0] | (e[1] << 8));
            int tile = ent & 0x1FF;
            int px = vx & 7, ry = py;
            if (ent & 0x200) px = 7 - px;                 /* hflip */
            if (ent & 0x400) ry = 7 - ry;                 /* vflip */
            const uint8_t *prow = &vram[(tile << 5) | (ry << 2)];
            uint8_t c = pat_pixel(prow, px);
            uint8_t pal = (ent & 0x800) ? 16 : 0;         /* palette select */
            uint8_t v = (uint8_t)(pal + c);
            if ((ent & 0x1000) && c) v |= 0x80;           /* bg priority    */
            out[x] = v;
        }
    }

    /* Sprites, first-match-wins per pixel (hardware gives the LOWEST sprite
     * number priority), colour 0 transparent, always the sprite palette. */
    for (int y = 0; y < 192; y++) {
        uint8_t *out = dst + y * pitch;
        for (int s = 0; s < 64; s++) {
            int sy = sat[s];
            if (sy == 0xD0) break;                        /* terminator */
            sy = (sy + 1) & 255;                          /* y is stored -1 */
            if (y < sy || y >= sy + spr_h) continue;
            int sx  = sat[0x80 + s * 2];
            int idx = sat[0x80 + s * 2 + 1];
            if (spr_h == 16) idx &= 0xFE;
            int ry = y - sy;
            const uint8_t *prow =
                &vram[spr_base + ((idx + (ry >> 3)) << 5) + ((ry & 7) << 2)];
            for (int px = 0; px < 8; px++) {
                int x = sx + px;
                if (x >= 256) break;
                uint8_t c = pat_pixel(prow, px);
                if (!c) continue;
                if (out[x] & 0x80) continue;              /* bg priority wins */
                if (out[x] & 0x40) continue;              /* lower sprite won
                                                           * (bit 6 = sprite
                                                           * scratch — bg in
                                                           * the sprite pal
                                                           * must not block) */
                out[x] = (uint8_t)(0x40 | (16 + c));
            }
        }
    }

    /* Strip the priority scratch bit; apply the left-column blank. */
    uint8_t bd = (uint8_t)(16 + (regs[7] & 0x0F));
    for (int y = 0; y < 192; y++) {
        uint8_t *out = dst + y * pitch;
        for (int x = 0; x < 256; x++) out[x] &= 0x1F;
        if (regs[0] & 0x20)
            for (int x = 0; x < 8; x++) out[x] = bd;
    }
}
