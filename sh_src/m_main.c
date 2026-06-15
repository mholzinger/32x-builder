#include "mars.h"
#include "menu.h"
#include "raycast.h"
#include "font.h"
#include "shared.h"
#include "procgen.h"

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
static uint16_t prof_slave_smoothed = 0;
static uint16_t prof_half_smoothed = 0;

extern volatile uint16_t prof_master_half_ticks;  /* written by raycast_render */

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
    uint16_t slave = SHARED_UC->slave_render_ticks;
    prof_slave_smoothed = (uint16_t)((prof_slave_smoothed - (prof_slave_smoothed >> 3)) + (slave >> 3));
    uint16_t half = prof_master_half_ticks;
    prof_half_smoothed = (uint16_t)((prof_half_smoothed - (prof_half_smoothed >> 3)) + (half >> 3));

    /* "T:NNNNN H:NNNNN S:NNNNN" — frame total, master half-render,
     * slave half-render. Higher of H/S is the parallel bottleneck. */
    char text[24];
    text[0] = 'T'; text[1] = ':';
    uint16_t v = prof_smoothed;
    text[6] = '0' + (v % 10); v /= 10;
    text[5] = '0' + (v % 10); v /= 10;
    text[4] = '0' + (v % 10); v /= 10;
    text[3] = '0' + (v % 10); v /= 10;
    text[2] = '0' + v;
    text[7] = ' '; text[8] = 'H'; text[9] = ':';
    v = prof_half_smoothed;
    text[14] = '0' + (v % 10); v /= 10;
    text[13] = '0' + (v % 10); v /= 10;
    text[12] = '0' + (v % 10); v /= 10;
    text[11] = '0' + (v % 10); v /= 10;
    text[10] = '0' + v;
    text[15] = ' '; text[16] = 'S'; text[17] = ':';
    v = prof_slave_smoothed;
    text[22] = '0' + (v % 10); v /= 10;
    text[21] = '0' + (v % 10); v /= 10;
    text[20] = '0' + (v % 10); v /= 10;
    text[19] = '0' + (v % 10); v /= 10;
    text[18] = '0' + v;
    text[23] = 0;
    /* Top-right corner. LIGHT_BASE[0] (palette idx 49) is the brightest
     * fixture-white, reads on every background. */
    font_draw_string(fb, SCREEN_W - 8 * 23 - 4, 4, text, 49);
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

    /* Cardboard box title screen. Box body fills the center, two top
     * flaps animate from closed (covering the top) to open (folded
     * up and away) revealing the title text inside. The "fold" is
     * stylized — flap height shrinks linearly from full to zero — but
     * reads as opening once corrugation lines and a tape strip suggest
     * the cardboard surface. */
    {
        const int box_x1 = 60,  box_x2 = 260;
        const int box_y1 = 60,  box_y2 = 180;
        const int box_mid_x  = (box_x1 + box_x2) / 2;
        const int flap_full_h = 40;
        const int OPEN_FRAMES = 60;

        /* Cardboard palette indices — reuse FLOOR_BASE brown shades.
         * +1 = light kraft, +3 = mid, +6 = dark edge/seam. */
        const uint8_t CB_BODY     = 17 + 2;
        const uint8_t CB_FLAP_L   = 17 + 1;
        const uint8_t CB_FLAP_R   = 17 + 3;
        const uint8_t CB_EDGE     = 17 + 6;
        const uint8_t CB_CORRUGAT = 17 + 4;
        const uint8_t BG_DARK     = 46;

        uint16_t prev_pad = 0xFFFF;
        uint32_t frame = 0;
        int menu_selection = 0;  /* 0 = JUMP IN, 1 = NOCLIP PROCEDURAL */
        for (;;) {
            HwMdReadPad(0);
            uint16_t pad = MARS_SYS_COMM8;
            uint16_t pressed = (uint16_t)(pad & ~prev_pad);
            prev_pad = pad;

            /* After the flaps have opened, run the start menu: UP/DOWN
             * toggles between the two options, START commits. Pressing
             * START before the box is open is ignored so the user
             * actually sees the animation. */
            if (frame > OPEN_FRAMES) {
                if (pressed & SEGA_CTRL_UP)    menu_selection = 0;
                if (pressed & SEGA_CTRL_DOWN)  menu_selection = 1;
                if (pressed & SEGA_CTRL_START) break;
            }

            uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);

            /* Dark room background. */
            for (int y = 0; y < SCREEN_H; y++) {
                uint8_t *row = fb + y * SCREEN_W;
                for (int x = 0; x < SCREEN_W; x++) row[x] = BG_DARK;
            }

            /* Box body. */
            for (int y = box_y1; y < box_y2; y++) {
                uint8_t *row = fb + y * SCREEN_W;
                for (int x = box_x1; x < box_x2; x++) row[x] = CB_BODY;
            }
            /* Subtle horizontal corrugation lines — every 6 rows. */
            for (int y = box_y1 + 6; y < box_y2 - 2; y += 6) {
                uint8_t *row = fb + y * SCREEN_W;
                for (int x = box_x1 + 1; x < box_x2 - 1; x++) row[x] = CB_CORRUGAT;
            }
            /* Vertical tape strip down the centre — gives the box its
             * "this thing was sealed and just opened" feel. */
            for (int y = box_y1; y < box_y2; y++) {
                fb[y * SCREEN_W + box_mid_x - 1] = CB_EDGE;
                fb[y * SCREEN_W + box_mid_x    ] = CB_EDGE;
            }
            /* Box outer edge. */
            for (int x = box_x1; x < box_x2; x++) {
                fb[box_y1       * SCREEN_W + x] = CB_EDGE;
                fb[(box_y2 - 1) * SCREEN_W + x] = CB_EDGE;
            }
            for (int y = box_y1; y < box_y2; y++) {
                fb[y * SCREEN_W + box_x1    ] = CB_EDGE;
                fb[y * SCREEN_W + box_x2 - 1] = CB_EDGE;
            }

            /* Flaps shrinking from full height to zero over OPEN_FRAMES. */
            int flap_h = (frame < OPEN_FRAMES)
                ? flap_full_h - (int)((flap_full_h * frame) / OPEN_FRAMES)
                : 0;
            for (int y = box_y1 + 1; y < box_y1 + flap_h; y++) {
                uint8_t *row = fb + y * SCREEN_W;
                for (int x = box_x1 + 1; x < box_mid_x; x++) row[x] = CB_FLAP_L;
                for (int x = box_mid_x + 1; x < box_x2 - 1; x++) row[x] = CB_FLAP_R;
            }

            /* Once the flaps are out of the way, draw the title +
             * two-option menu inside the box. */
            if (frame >= OPEN_FRAMES) {
                const int mid_y = (box_y1 + box_y2) / 2;
                font_draw_string(fb, (SCREEN_W - 13 * 8) / 2,
                                 mid_y - 28, "BACKROOMS 32X", 49);
                font_draw_string(fb, box_x1 + 14, mid_y - 4,
                                 (menu_selection == 0)
                                   ? "> NOCLIP FIXED MAP"
                                   : "  NOCLIP FIXED MAP", 49);
                font_draw_string(fb, box_x1 + 14, mid_y + 8,
                                 (menu_selection == 1)
                                   ? "> NOCLIP PROCEDURAL"
                                   : "  NOCLIP PROCEDURAL", 49);
            }

            swapBuffers();
            frame++;
        }
        /* Branch on selection. NOCLIP PROCEDURAL overwrites world_map
         * via xorshift32 seeded from `frame` — every press time gives
         * a different layout. JUMP IN leaves the hand-tuned default
         * map alone. */
        if (menu_selection == 1) {
            procgen_run(frame);
        }
        /* Wait until START is released so the in-game pause menu's
         * edge-detect doesn't see the same press and pop open. */
        for (;;) {
            HwMdReadPad(0);
            if (!(MARS_SYS_COMM8 & SEGA_CTRL_START)) break;
            swapBuffers();
        }
    }

    for (;;) {
        /* Read the joypad up-front so the menu can both react to
         * START and tell player_update to skip movement when open. */
        HwMdReadPad(0);
        uint16_t pad = MARS_SYS_COMM8;

        menu_update(pad);

        if (!menu_is_active()) {
            player_update();
        }
        /* Tick the shared frame counter before render so both CPUs
         * read the same value when computing the distant-wall strobe. */
        SHARED_UC->frame_count++;
        raycast_render();
        uint8_t *fb_text = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
        menu_render(fb_text);
        prof_sample_and_draw(fb_text);
        swapBuffers();
    }
    return 0;
}
