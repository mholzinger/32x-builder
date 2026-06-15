#include "mars.h"
#include "menu.h"
#include "raycast.h"
#include "font.h"

static uint32_t lastTick = 0;
static uint16_t currentFB = 0;

/* Frame-time profiler. Reads the SH-2 free-running timer at Φ/32
 * (~720kHz, 1.39μs per tick) once per frame and displays the delta
 * since the previous frame in the top-right corner. 60fps ≈ 12000
 * ticks, 30fps ≈ 24000, 15fps ≈ 48000. Single-stage rolling EMA
 * smooths jitter; the display updates every frame so changes are
 * immediate without being visually noisy. Remove this block when
 * we're done with the optimization pass. */
static uint16_t prof_prev_frt = 0;
static uint16_t prof_smoothed = 0;
static uint16_t prof_idle_smoothed = 0;

extern volatile uint16_t prof_master_idle_ticks;  /* written by raycast_render */

static inline uint16_t prof_read_frt(void) {
    /* Hitachi SH-2 FRT quirk: reading FRCH latches FRCL into a
     * temporary register so the 16-bit value stays atomic. */
    uint8_t hi = SH2_FRT_FRCH;
    uint8_t lo = SH2_FRT_FRCL;
    return ((uint16_t)hi << 8) | lo;
}

static inline void prof_init(void) {
    SH2_FRT_TIER  = 0x01;  /* default — no interrupts enabled */
    SH2_FRT_TCR   = 0x01;  /* Φ/32 prescaler = ~720kHz */
    SH2_FRT_FTCSR = 0;     /* clear OVF/OCF; free-running */
    prof_prev_frt = prof_read_frt();
}

static void prof_sample_and_draw(uint8_t *fb) {
    uint16_t now = prof_read_frt();
    uint16_t delta = (uint16_t)(now - prof_prev_frt);  /* mod-16 wrap is fine for <91ms frames */
    prof_prev_frt = now;
    /* EMA: 7/8 old + 1/8 new — ~8-frame time constant. */
    prof_smoothed = (uint16_t)((prof_smoothed - (prof_smoothed >> 3)) + (delta >> 3));
    uint16_t idle = prof_master_idle_ticks;
    prof_idle_smoothed = (uint16_t)((prof_idle_smoothed - (prof_idle_smoothed >> 3)) + (idle >> 3));

    /* "T:NNNNN I:NNNNN" — total frame ticks and master idle ticks. */
    char text[16];
    text[0] = 'T'; text[1] = ':';
    uint16_t v = prof_smoothed;
    text[6] = '0' + (v % 10); v /= 10;
    text[5] = '0' + (v % 10); v /= 10;
    text[4] = '0' + (v % 10); v /= 10;
    text[3] = '0' + (v % 10); v /= 10;
    text[2] = '0' + v;
    text[7] = ' '; text[8] = 'I'; text[9] = ':';
    v = prof_idle_smoothed;
    text[14] = '0' + (v % 10); v /= 10;
    text[13] = '0' + (v % 10); v /= 10;
    text[12] = '0' + (v % 10); v /= 10;
    text[11] = '0' + (v % 10); v /= 10;
    text[10] = '0' + v;
    text[15] = 0;
    /* Top-right corner. LIGHT_BASE[0] (palette idx 49) is the brightest
     * fixture-white, reads on every background. */
    font_draw_string(fb, SCREEN_W - 8 * 15 - 4, 4, text, 49);
}

static void swapBuffers(void) {
    while (lastTick == MARS_SYS_COMM12);
    /* In vblank now — safe palette-write window. */
    raycast_shimmer();
    MARS_VDP_FBCTL = currentFB ^ 1;
    while ((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB);
    currentFB ^= 1;
    lastTick = MARS_SYS_COMM12;
}

int m_main(void) {
    /* Release the slave SH-2. The crt0 (mars_start.s:271-273) intends
     * to do this after the init JSR but uses a stale r0 — the write
     * to "clear slave status" goes to ROM and is silently dropped.
     * Without this, the slave loops forever in its S_OK wait at
     * 0x20004024 (= MARS_SYS_COMM4) and never reaches s_main().
     *
     * Writing 0 to COMM4 changes the upper half of the 32-bit word
     * the slave is polling for "S_OK" (0x535F4F4B) → cmp/eq fails →
     * slave exits the wait and jumps to s_main. */
    MARS_SYS_COMM4 = 0;

    Hw32xInit(MARS_VDP_MODE_256, 0);
    Hw32xDelay(1);    /* wait for first vblank — palette is writable now */
    raycast_init();
    prof_init();

    for (;;) {
        /* Read the joypad up-front so the menu can both react to
         * START and tell player_update to skip movement when open. */
        HwMdReadPad(0);
        uint16_t pad = MARS_SYS_COMM8;

        menu_update(pad);

        if (!menu_is_active()) {
            player_update();
        }
        raycast_render();
        uint8_t *fb_text = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
        menu_render(fb_text);
        prof_sample_and_draw(fb_text);
        swapBuffers();
    }
    return 0;
}
