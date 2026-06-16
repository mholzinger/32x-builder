#include "mars.h"
#include "menu.h"
#include "raycast.h"
#include "font.h"
#include "shared.h"
#include "procgen.h"
#include "box3d.h"
#include "box_hero.h"
#include "sound.h"

/* Non-static so box3d.c can drive the same swap state during the
 * title screen — keeps the front/back buffer bookkeeping in one
 * place. */
uint32_t lastTick = 0;
uint16_t currentFB = 0;

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

/* Top-left position + angle overlay for debugging map locations.
 * Line 1: "X:NN.N Y:NN.N" — integer cell + one decimal.
 * Line 2: "A:NNN"          — raw uint8 angle.
 * Two lines so A: doesn't collide with the top-right T/H/S timer. */
static void pos_draw(uint8_t *fb) {
    char line1[14];
    char line2[6];
    int32_t px = player.x;
    int32_t py = player.y;
    int px_i = (int)(px >> 16);
    int px_f = (int)(((uint32_t)(px & 0xFFFF) * 10) >> 16);
    int py_i = (int)(py >> 16);
    int py_f = (int)(((uint32_t)(py & 0xFFFF) * 10) >> 16);
    int angle = (int)player.angle;
    if (px_i < 0)  px_i = 0;
    if (px_i > 99) px_i = 99;
    if (py_i < 0)  py_i = 0;
    if (py_i > 99) py_i = 99;

    line1[0] = 'X'; line1[1] = ':';
    line1[2] = '0' + (px_i / 10);
    line1[3] = '0' + (px_i % 10);
    line1[4] = '.';
    line1[5] = '0' + px_f;
    line1[6] = ' '; line1[7] = 'Y'; line1[8] = ':';
    line1[9]  = '0' + (py_i / 10);
    line1[10] = '0' + (py_i % 10);
    line1[11] = '.';
    line1[12] = '0' + py_f;
    line1[13] = 0;

    line2[0] = 'A'; line2[1] = ':';
    line2[4] = '0' + (angle % 10); angle /= 10;
    line2[3] = '0' + (angle % 10); angle /= 10;
    line2[2] = '0' + (angle % 10);
    line2[5] = 0;

    font_draw_string(fb, 4,  4, line1, 49);
    font_draw_string(fb, 4, 16, line2, 49);
}

void swapBuffers(void) {
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

    /* High-res "attic box" splash: the SEGA CORE label on the closed
     * carton, held until START. Then we hand off to the live low-res 3D
     * box for the open + dive. */
    box_hero_show();

    /* Cardboard box title screen — the box mesh + camera dive are
     * imported from box_model.h and rendered live by box3d (see
     * tools/export_box.py). It owns its own CRAM palette and a
     * shimmer-free flip, and runs BEFORE raycast_init so the gameplay
     * palette build reclaims CRAM after a map is chosen. */
    box3d_play();   /* loads the box palette in vblank on its first frame */

    /* Start menu over the final "inside the box" cinematic frame.
     * UP/DOWN toggle the two options, START commits. The box is
     * already open, so START is live immediately. */
    int menu_selection = 0;  /* 0 = FIXED MAP, 1 = PROCEDURAL */
    uint32_t frame = 0;
    /* Any face button or Start commits the highlighted choice; UP/DOWN
     * just move the cursor. */
    const uint16_t MENU_COMMIT = SEGA_CTRL_START | SEGA_CTRL_A | SEGA_CTRL_B |
                                 SEGA_CTRL_C | SEGA_CTRL_X | SEGA_CTRL_Y | SEGA_CTRL_Z;
    {
        const int opt_x = (SCREEN_W - 19 * 8) / 2;
        uint16_t prev_pad = 0xFFFF;
        for (;;) {
            HwMdReadPad(0);
            uint16_t pad = MARS_SYS_COMM8;
            uint16_t pressed = (uint16_t)(pad & ~prev_pad);
            prev_pad = pad;
            if (pressed & SEGA_CTRL_UP)    menu_selection = 0;
            if (pressed & SEGA_CTRL_DOWN)  menu_selection = 1;
            if (pressed & MENU_COMMIT) break;

            box3d_show_final();
            uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
            font_draw_string(fb, (SCREEN_W - 13 * 8) / 2, 36,
                             "BACKROOMS 32X", BOX_TEXT_IDX);
            font_draw_string(fb, opt_x, 120,
                             (menu_selection == 0)
                               ? "> NOCLIP FIXED MAP "
                               : "  NOCLIP FIXED MAP ", BOX_TEXT_IDX);
            font_draw_string(fb, opt_x, 136,
                             (menu_selection == 1)
                               ? "> NOCLIP PROCEDURAL"
                               : "  NOCLIP PROCEDURAL", BOX_TEXT_IDX);
            box3d_flip();
            frame++;
        }
    }

    /* Selection made — build the gameplay palette + world (reclaims
     * CRAM from the cardboard palette). */
    raycast_init();
    prof_init();

    /* NOCLIP PROCEDURAL overwrites world_map via xorshift32 seeded from
     * `frame`; FIXED MAP leaves the hand-tuned default map alone. */
    if (menu_selection == 1) {
        procgen_run(frame);
    }
    /* Wait until the commit buttons are released so the in-game pause
     * menu's edge-detect doesn't see the same press and pop open. */
    for (;;) {
        HwMdReadPad(0);
        if (!(MARS_SYS_COMM8 & MENU_COMMIT)) break;
        swapBuffers();
    }

    /* Game world is up — bring the backrooms ambience in (slave starts
     * pumping from the top of the loop now). */
    amb_set_active(1);

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
        pos_draw(fb_text);
        swapBuffers();
    }
    return 0;
}
