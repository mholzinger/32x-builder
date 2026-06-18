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

/* On-screen debug metrics — off by default, toggled by the six-button
 * controller's MODE button (edge-detected once per frame from any loop). */
uint8_t g_metrics_on = 0;
static void metrics_mode_check(uint16_t pad) {
    static uint16_t prev = 0xFFFF;
    if ((pad & SEGA_CTRL_MODE) && !(prev & SEGA_CTRL_MODE)) g_metrics_on ^= 1;
    prev = pad;
}

/* Frame-time profiler. Reads the SH-2 free-running timer at Φ/32
 * (~720kHz, 1.39μs per tick) once per frame and displays the delta
 * since the previous frame in the top-right corner. 60fps ≈ 12000
 * ticks, 30fps ≈ 24000, 15fps ≈ 48000. Single-stage rolling EMA
 * smooths jitter; the display updates every frame so changes are
 * immediate without being visually noisy. Remove this block when
 * we're done with the optimization pass. */
static uint16_t prof_prev_frt = 0;
static uint16_t prof_smoothed = 0;
static uint16_t prof_secondary_smoothed = 0;
static uint16_t prof_half_smoothed = 0;

extern volatile uint16_t prof_primary_half_ticks;  /* written by raycast_render */

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
    uint16_t secondary = SHARED_UC->secondary_render_ticks;
    prof_secondary_smoothed = (uint16_t)((prof_secondary_smoothed - (prof_secondary_smoothed >> 3)) + (secondary >> 3));
    uint16_t half = prof_primary_half_ticks;
    prof_half_smoothed = (uint16_t)((prof_half_smoothed - (prof_half_smoothed >> 3)) + (half >> 3));

    /* "T:NNNNN H:NNNNN S:NNNNN" — frame total, primary half-render,
     * secondary half-render. Higher of H/S is the parallel bottleneck.
     * (Effective FPS rides the bottom line next to the per-pass breakdown.) */
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
    v = prof_secondary_smoothed;
    text[22] = '0' + (v % 10); v /= 10;
    text[21] = '0' + (v % 10); v /= 10;
    text[20] = '0' + (v % 10); v /= 10;
    text[19] = '0' + (v % 10); v /= 10;
    text[18] = '0' + v;
    text[23] = 0;
    /* Top-right corner. LIGHT_BASE[0] (palette idx 49) is the brightest
     * fixture-white, reads on every background. */
    font_draw_string(fb, SCREEN_W - 8 * 23 - 4, 4, text, 49);

    /* Second line: primary-half per-pass breakdown — Clear / ceiling-Grid /
     * caRpet / Walls (raw FRT ticks), then F = effective FPS. Per-pass tells
     * us which pass to optimize; F is the bottom-line score it rolls up to. */
    {
        extern volatile uint16_t prof_pass_clear, prof_pass_ceil,
                                 prof_pass_carpet, prof_pass_walls;
        static const char lbl[4] = {'C', 'G', 'R', 'W'};
        uint16_t pv[4] = { prof_pass_clear, prof_pass_ceil,
                           prof_pass_carpet, prof_pass_walls };
        char t2[40];
        int pos = 0;
        for (int i = 0; i < 4; i++) {
            t2[pos++] = lbl[i];
            t2[pos++] = ':';
            uint16_t x = pv[i];
            for (int d = 4; d >= 0; d--) { t2[pos + d] = '0' + (x % 10); x /= 10; }
            pos += 5;
            t2[pos++] = ' ';
        }
        /* Effective FPS = 720000 / frame_period (FRT is ~720kHz). The 16-bit
         * FRT wraps at 65536 (~91ms); a per-frame delta below one vblank
         * (12000 ticks) wrapped once, so add 65536 — honest down to ~10fps. */
        uint32_t ft = delta ? delta : 1;
        if (ft < 12000) ft += 65536;
        uint32_t fps = (720000u + ft / 2) / ft;
        if (fps > 99) fps = 99;
        t2[pos++] = 'F'; t2[pos++] = ':';
        t2[pos++] = '0' + (fps / 10);
        t2[pos++] = '0' + (fps % 10);
        t2[pos] = 0;
        font_draw_string(fb, 4, SCREEN_H - 12, t2, 49);
    }
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
    /* Release the secondary SH-2. The crt0 (mars_start.s:271-273) intends
     * to do this after the init JSR but uses a stale r0 — the write
     * to "clear secondary status" goes to ROM and is silently dropped.
     * Without this, the secondary loops forever in its S_OK wait at
     * 0x20004024 (= MARS_SYS_COMM4) and never reaches s_main().
     *
     * Writing 0 to COMM4 changes the upper half of the 32-bit word
     * the secondary is polling for "S_OK" (0x535F4F4B) → cmp/eq fails →
     * secondary exits the wait and jumps to s_main. */
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

    /* No button needed: the box intro flows straight into the trap-door
     * fall and we plummet into the void. (box3d_play can still be skipped
     * with START.) The map is chosen later, down in the lobby. */
    box3d_play_fall();

    /* Land in the lobby — the open carpeted room from the HobbyTown
     * reference. Build the lobby map BEFORE raycast_init so init_lights
     * lays the ceiling-fixture grid over it. */
    raycast_load_lobby();
    raycast_init();
    prof_init();

    /* Backrooms ambience comes in as we stand up in the lobby (secondary
     * starts pumping from the top of the loop now). */
    amb_set_active(1);

    /* ---- Landing reveal --------------------------------------------- *
     * You fell through the box into darkness; now you come to from the
     * floor. Fade up looking straight DOWN at the carpet, hold a beat so
     * the floor perspective reads, then STAND UP — ease the camera from
     * face-down to the level photo view, decelerating into standing. */
    SHARED_UC->pitch_y = 80;                 /* face-down at the carpet */
    for (int lvl = 0; lvl <= FADE_STEPS; lvl++) {     /* fade up from black */
        SHARED_UC->frame_count++;
        raycast_render();
        while (lastTick == MARS_SYS_COMM12);
        raycast_set_brightness(lvl);
        MARS_VDP_FBCTL = currentFB ^ 1;
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB);
        currentFB ^= 1;
        lastTick = MARS_SYS_COMM12;
    }
    for (int i = 0; i < 14; i++) {                    /* hold on the carpet */
        SHARED_UC->frame_count++;
        raycast_render();
        swapBuffers();
    }
    while (SHARED_UC->pitch_y > 0) {                  /* stand up */
        int p = SHARED_UC->pitch_y; p -= (p >> 3) + 1; if (p < 0) p = 0;
        SHARED_UC->pitch_y = (int8_t)p;
        SHARED_UC->frame_count++;
        raycast_render();
        swapBuffers();
    }

    /* --- Lobby: frozen menu, then walk in ---------------------------- *
     * Phase A: the player is FROZEN at the photo vantage; only the text
     * menu is live (UP/DOWN pick the level, any button confirms and
     * dismisses the menu). Phase B: the menu is gone and the choice is
     * locked — you wander the lobby and walk forward into the backrooms
     * to enter the level you picked. */
    int menu_selection = 0;       /* 0=FIXED 1=PROCEDURAL */
    uint32_t frame = 0;           /* time-in-lobby — entropy for procgen */
    const uint16_t LOBBY_COMMIT = SEGA_CTRL_START | SEGA_CTRL_A | SEGA_CTRL_B |
                                  SEGA_CTRL_C | SEGA_CTRL_X | SEGA_CTRL_Y | SEGA_CTRL_Z;

    /* Phase A — frozen menu over the still photo-perspective. */
    {
        const int opt_x = (SCREEN_W - 19 * 8) / 2;
        uint16_t prev_pad = 0xFFFF;
        for (;;) {
            HwMdReadPad(0);
            uint16_t pad = MARS_SYS_COMM8;
            uint16_t pressed = (uint16_t)(pad & ~prev_pad);
            prev_pad = pad;
            if ((pressed & SEGA_CTRL_UP)   && menu_selection > 0) menu_selection--;
            if ((pressed & SEGA_CTRL_DOWN) && menu_selection < 1) menu_selection++;
            if (pressed & LOBBY_COMMIT) break;   /* confirm, dismiss menu */
            metrics_mode_check(pad);
            frame++;

            SHARED_UC->frame_count++;
            raycast_render();                    /* stationary lobby view */
            uint8_t *fb_text = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
            font_draw_string(fb_text, (SCREEN_W - 13 * 8) / 2, 88,
                             "BACKROOMS 32X", 49);
            font_draw_string(fb_text, opt_x, 104,
                             (menu_selection == 0)
                               ? "> NOCLIP FIXED MAP  "
                               : "  NOCLIP FIXED MAP  ", 49);
            font_draw_string(fb_text, opt_x, 120,
                             (menu_selection == 1)
                               ? "> NOCLIP PROCEDURAL "
                               : "  NOCLIP PROCEDURAL ", 49);
            font_draw_string(fb_text, (SCREEN_W - 19 * 8) / 2, SCREEN_H - 20,
                             "ANY BUTTON: CONFIRM", 49);
            /* Controller-type readout — UNCONDITIONAL (the metrics overlay is
             * gated behind MODE, a 6-button-only button, so it can't report
             * this on a pad that fails the 6-button handshake). The high nibble
             * of the pad word is the type: 6 = six-button detected, 3 = three-
             * button, ? = none/unknown. Lets us see what each emulator presents. */
            uint16_t ptype = pad & SEGA_CTRL_TYPE;
            char padline[8] = { 'P','A','D',':',' ',
                (ptype == SEGA_CTRL_SIX) ? '6' : (ptype == SEGA_CTRL_THREE) ? '3' : '?',
                0, 0 };
            font_draw_string(fb_text, 8, 8, padline, 49);
            /* Crouch is A+B on every pad (X is avoided — emulators bind it to
             * Left). Metrics overlay lives in the pause menu's LIGHTING tab. */
            font_draw_string(fb_text, (SCREEN_W - 11 * 8) / 2, SCREEN_H - 32,
                             "CROUCH: A+B", 49);
            if (g_metrics_on) { prof_sample_and_draw(fb_text); pos_draw(fb_text); }
            swapBuffers();
        }
    }

    /* Phase B — menu dismissed, choice locked. Walk through the lobby and
     * into the black doorway (col 7) on the east wall. No prompt. */
    {
        for (;;) {
            HwMdReadPad(0);
            metrics_mode_check(MARS_SYS_COMM8);
            player_update();
            /* Stepped into the black exit doorway (col 7, rows 2-4). */
            if (player.x > FX(7) && player.y > FX(1.5) && player.y < FX(5)) break;
            SHARED_UC->frame_count++;
            raycast_render();
            uint8_t *fb_text = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
            if (g_metrics_on) { prof_sample_and_draw(fb_text); pos_draw(fb_text); }
            swapBuffers();
        }
    }

    /* Walk-through transition: fade the lobby to black, swap in the chosen
     * map behind the black, fade it up — reads as the lobby sealing off
     * and the backrooms opening ahead. (Own vblank flip so it bypasses
     * raycast_shimmer, which would reset the bright palette mid-fade.) */
    for (int lvl = FADE_STEPS; lvl >= 0; lvl -= 2) {
        SHARED_UC->frame_count++;
        raycast_render();
        while (lastTick == MARS_SYS_COMM12);
        raycast_set_brightness(lvl);
        MARS_VDP_FBCTL = currentFB ^ 1;
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB);
        currentFB ^= 1;
        lastTick = MARS_SYS_COMM12;
    }

    if (menu_selection == 1) {
        procgen_run((uint32_t)frame * 1000003u + (uint32_t)player.x);
        player.x = FX(16.5); player.y = FX(28.5); player.angle = 192;
    } else {
        raycast_load_fixed();
    }
    raycast_init();                 /* rebuilds full-bright palette... */
    raycast_set_brightness(0);      /* ...but hold black until the fade-in */

    for (int lvl = 0; lvl <= FADE_STEPS; lvl += 2) {
        SHARED_UC->frame_count++;
        raycast_render();
        while (lastTick == MARS_SYS_COMM12);
        raycast_set_brightness(lvl);
        MARS_VDP_FBCTL = currentFB ^ 1;
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB);
        currentFB ^= 1;
        lastTick = MARS_SYS_COMM12;
    }

    for (;;) {
        /* Read the joypad up-front so the menu can both react to
         * START and tell player_update to skip movement when open. */
        HwMdReadPad(0);
        uint16_t pad = MARS_SYS_COMM8;

        menu_update(pad);
        metrics_mode_check(pad);
        if (!menu_is_active()) {
            player_update();
        }
        /* Tick the shared frame counter before render so both CPUs
         * read the same value when computing the distant-wall strobe. */
        SHARED_UC->frame_count++;
        raycast_render();
        uint8_t *fb_text = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
        menu_render(fb_text);
        if (g_metrics_on) {
            prof_sample_and_draw(fb_text);
            pos_draw(fb_text);
        }
        swapBuffers();
    }
    return 0;
}
