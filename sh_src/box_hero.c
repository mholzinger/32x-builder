#include "mars.h"
#include "box3d.h"        /* box3d_flip (shimmer-free page flip) */
#include "box_hero.h"
#include "box_hero_data.h"

/* Decode scratch for the full 320x224 frame (line-major, matches the
 * contiguous framebuffer pixel area). */
static uint8_t hero_scratch[HERO_W * HERO_H];

void box_hero_show(void) {
    /* Hero's own 256-colour palette into CRAM. */
    for (int i = 0; i < HERO_PAL_N; i++)
        Hw32xSetBGColor(i, hero_palette[i][0], hero_palette[i][1], hero_palette[i][2]);

    /* RLE-decode once into scratch. */
    const uint8_t *p = hero_data, *end = hero_data + HERO_DATA_LEN;
    uint8_t *d = hero_scratch;
    while (p < end) {
        uint8_t c = *p++;
        if (c & 0x80) { int n = c & 0x7F; uint8_t v = *p++; do { *d++ = v; } while (--n); }
        else          { int n = c;        do { *d++ = *p++; } while (--n); }
    }

    /* Blit to both buffer pages (long copies) so either displayed page
     * shows the hero. */
    for (int pass = 0; pass < 2; pass++) {
        uint32_t *dst = (uint32_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
        const uint32_t *src = (const uint32_t *)hero_scratch;
        for (int i = 0; i < HERO_W * HERO_H / 4; i++) dst[i] = src[i];
        box3d_flip();
    }

    /* Hold the splash for up to ~4 seconds (240 vblanks at 60fps), or
     * until any button skips it. box3d_flip paces the poll to vblank. */
    const uint16_t SKIP = SEGA_CTRL_START | SEGA_CTRL_A | SEGA_CTRL_B |
                          SEGA_CTRL_C | SEGA_CTRL_X | SEGA_CTRL_Y | SEGA_CTRL_Z;
    uint16_t prev = 0xFFFF;
    for (int held = 0; held < 240; held++) {
        HwMdReadPad(0);
        uint16_t pad = MARS_SYS_COMM8;
        uint16_t pressed = (uint16_t)(pad & ~prev);
        prev = pad;
        if (pressed & SKIP) break;
        box3d_flip();
    }
}
