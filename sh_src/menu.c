#include "mars.h"
#include "font.h"
#include "menu.h"
#include "raycast.h"
#include "shared.h"

/* Menu state — kept here as file-static. The settings themselves
 * live in shared SDRAM (shared.amb_volume / shared.step_volume) so
 * the slave's pump sees changes immediately via cache-through. */

#define MENU_ROWS 2

static int      menu_active = 0;
static int      menu_row    = 0;
static uint16_t menu_prev_pad = 0;

/* Layout: 2 rows of settings (ambient + footstep volume) framed by
 * a title and a "START to close" hint. We center an 18-char × 8-row
 * box of glyphs (144 × 64 px) in the 320 × 224 screen. */
#define MENU_W_PX      144
#define MENU_H_PX       64
#define MENU_X        ((SCREEN_W - MENU_W_PX) / 2)
#define MENU_Y        ((SCREEN_H - MENU_H_PX) / 2)

/* Palette indices for the menu — reusing existing palette slots so we
 * don't have to set up new colors. CEIL_BASE+13 is a dark eggshell;
 * LIGHT_BASE+0 is the brightest light-fixture color (basically white).
 * Together they read as "dark terminal box with bright text". */
#define MENU_BG_COLOR  46   /* near the end of the CEIL_BASE range = dark */
#define MENU_FG_COLOR  49   /* LIGHT_BASE[0] = bright white-ish */

/* Step amounts when LEFT/RIGHT is pressed — 16 per tap gives a quick
 * sweep across the 0..255 range in ~16 presses. */
#define VOL_STEP 16

int menu_is_active(void) {
    return menu_active;
}

void menu_update(uint16_t pad) {
    /* Edge detection — react only when a bit transitions from 0 to 1. */
    uint16_t pressed = (uint16_t)(pad & ~menu_prev_pad);
    menu_prev_pad = pad;

    if (pressed & SEGA_CTRL_START) {
        menu_active = !menu_active;
        if (menu_active) menu_row = 0;
        return;
    }

    if (!menu_active) return;

    if (pressed & SEGA_CTRL_UP) {
        menu_row = (menu_row + MENU_ROWS - 1) % MENU_ROWS;
    }
    if (pressed & SEGA_CTRL_DOWN) {
        menu_row = (menu_row + 1) % MENU_ROWS;
    }

    /* For LEFT/RIGHT we react on EDGE as well — repeating-press for
     * fast tuning. Holding to auto-repeat could be a follow-up if
     * users find it annoying. */
    int dir = 0;
    if (pressed & SEGA_CTRL_LEFT)  dir = -1;
    if (pressed & SEGA_CTRL_RIGHT) dir = +1;
    if (dir == 0) return;

    volatile uint8_t *target =
        (menu_row == 0) ? &SHARED_UC->amb_volume
                        : &SHARED_UC->step_volume;
    int v = (int)*target + dir * VOL_STEP;
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    *target = (uint8_t)v;
}

/* Format a 0..255 byte as a percentage (0..100%) in a 4-character
 * right-aligned string with the '%' suffix. Output buffer must be at
 * least 5 bytes. The +127 in the rounding is so e.g. 255 → 100 and
 * 0 → 0 without off-by-one. */
static void fmt_pct(uint8_t v, char out[5]) {
    int pct = ((int)v * 100 + 127) / 255;
    out[0] = (pct >= 100) ? '1' : ' ';
    out[1] = (pct >=  10) ? ('0' + ((pct / 10) % 10)) : ' ';
    out[2] = ('0' + (pct % 10));
    out[3] = '%';
    out[4] = 0;
}

/* Fill the menu rectangle with the background color. The framebuffer
 * is 320×224 8bpp; we use the existing fb_pixels()-style layout via
 * direct indexing. */
static void fill_bg(uint8_t *fb) {
    for (int yy = 0; yy < MENU_H_PX; yy++) {
        uint8_t *row = fb + (MENU_Y + yy) * SCREEN_W + MENU_X;
        for (int xx = 0; xx < MENU_W_PX; xx++) row[xx] = MENU_BG_COLOR;
    }
}

void menu_render(uint8_t *fb) {
    if (!menu_active) return;

    fill_bg(fb);

    /* Layout inside the box:
     *
     *   row 0 (y =  0):  +--------------+   (we draw '-' x 16 across)
     *   row 1 (y =  8):  |              |
     *   row 2 (y = 16):  |  SETTINGS    |
     *   row 3 (y = 24):  |              |
     *   row 4 (y = 32):  | > AMB    NNN |
     *   row 5 (y = 40):  |   STEP   NNN |
     *   row 6 (y = 48):  |              |
     *   row 7 (y = 56):  | START TO CLOSE|
     *   row 8 (y = 64):  +--------------+
     *
     * Box rule we just draw with '-' chars; the FG color makes the
     * whole thing read as a terminal panel.
     *
     * Coordinates below are absolute (px), relative to top-left of
     * the screen. */
    const int X = MENU_X;
    const int Y = MENU_Y;

    /* Top + bottom rule — solid dashes spanning the full 18-char box. */
    font_draw_string(fb, X, Y,      "+----------------+", MENU_FG_COLOR);
    font_draw_string(fb, X, Y + 56, "+----------------+", MENU_FG_COLOR);

    /* Title — SETTINGS is 8 chars; in an 18-char box that's 5 chars
     * of padding either side. */
    font_draw_string(fb, X + 8 * 5, Y + 16, "SETTINGS", MENU_FG_COLOR);

    char num[5];

    /* AMBIENCE row. */
    fmt_pct(SHARED_UC->amb_volume, num);
    font_draw_string(fb, X + 8, Y + 32,
                     (menu_row == 0) ? "> AMBIENCE " : "  AMBIENCE ",
                     MENU_FG_COLOR);
    font_draw_string(fb, X + 8 * 13, Y + 32, num, MENU_FG_COLOR);

    /* FOOTSTEPS row. */
    fmt_pct(SHARED_UC->step_volume, num);
    font_draw_string(fb, X + 8, Y + 40,
                     (menu_row == 1) ? "> FOOTSTEPS" : "  FOOTSTEPS",
                     MENU_FG_COLOR);
    font_draw_string(fb, X + 8 * 13, Y + 40, num, MENU_FG_COLOR);
}
