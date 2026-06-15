#include "mars.h"
#include "font.h"
#include "menu.h"
#include "raycast.h"
#include "shared.h"

/* Two-tab pause menu. START opens/closes; tabs (AUDIO / LIGHTING) sit
 * on row 0 and LEFT/RIGHT switches between them when that row is
 * focused. UP/DOWN cycles between the tab row and the per-tab content
 * rows; LEFT/RIGHT on a content row adjusts the value (sliders) or
 * inverts the bool (toggles). Settings live in shared SDRAM so the
 * slave's audio pump and the raycaster's effect gates see edits
 * immediately via cache-through. */

#define TAB_AUDIO    0
#define TAB_LIGHTING 1

#define AUDIO_CONTENT_ROWS    2   /* AMBIENCE, FOOTSTEPS */
#define LIGHTING_CONTENT_ROWS 3   /* FLICKER, STROBES, SHIMMER */

static int      menu_active = 0;
static int      menu_tab    = TAB_AUDIO;
static int      menu_row    = 0;   /* 0 = tab row, 1..N = content row */
static uint16_t menu_prev_pad = 0;

/* Layout — 18-char × 10-row box (144 × 80 px) centered on the 320×224
 * screen, tall enough for the LIGHTING tab's three toggle rows. */
#define MENU_W_PX      144
#define MENU_H_PX       80
#define MENU_X        ((SCREEN_W - MENU_W_PX) / 2)
#define MENU_Y        ((SCREEN_H - MENU_H_PX) / 2)

#define MENU_BG_COLOR  46   /* CEIL_BASE end = dark eggshell */
#define MENU_FG_COLOR  49   /* LIGHT_BASE[0] = bright white-ish */

#define VOL_STEP 16

int menu_is_active(void) {
    return menu_active;
}

static int content_rows_for(int tab) {
    return (tab == TAB_AUDIO) ? AUDIO_CONTENT_ROWS : LIGHTING_CONTENT_ROWS;
}

void menu_update(uint16_t pad) {
    uint16_t pressed = (uint16_t)(pad & ~menu_prev_pad);
    menu_prev_pad = pad;

    if (pressed & SEGA_CTRL_START) {
        menu_active = !menu_active;
        if (menu_active) menu_row = 0;
        return;
    }
    if (!menu_active) return;

    int total_rows = 1 + content_rows_for(menu_tab);

    if (pressed & SEGA_CTRL_UP) {
        menu_row = (menu_row + total_rows - 1) % total_rows;
    }
    if (pressed & SEGA_CTRL_DOWN) {
        menu_row = (menu_row + 1) % total_rows;
    }

    int dir = 0;
    if (pressed & SEGA_CTRL_LEFT)  dir = -1;
    if (pressed & SEGA_CTRL_RIGHT) dir = +1;
    if (dir == 0) return;

    if (menu_row == 0) {
        /* Tab row: LEFT/RIGHT switches tabs. Reset row so we land on
         * the new tab's first content row visually. */
        menu_tab = (menu_tab == TAB_AUDIO) ? TAB_LIGHTING : TAB_AUDIO;
        return;
    }

    if (menu_tab == TAB_AUDIO) {
        volatile uint8_t *target =
            (menu_row == 1) ? &SHARED_UC->amb_volume
                            : &SHARED_UC->step_volume;
        int v = (int)*target + dir * VOL_STEP;
        if (v < 0)   v = 0;
        if (v > 255) v = 255;
        *target = (uint8_t)v;
    } else {
        /* LIGHTING tab: toggle the corresponding bit. dir doesn't
         * matter — LEFT and RIGHT both flip. */
        uint8_t bit;
        switch (menu_row) {
        case 1: bit = LIGHTING_FLICKER; break;
        case 2: bit = LIGHTING_STROBE;  break;
        default: bit = LIGHTING_SHIMMER; break;
        }
        SHARED_UC->lighting_flags ^= bit;
    }
}

static void fmt_pct(uint8_t v, char out[4]) {
    int pct = ((int)v * 100 + 127) / 255;
    out[0] = (pct >= 100) ? '1' : ' ';
    out[1] = (pct >=  10) ? ('0' + ((pct / 10) % 10)) : ' ';
    out[2] = ('0' + (pct % 10));
    out[3] = 0;
}

static void fill_bg(uint8_t *fb) {
    for (int yy = 0; yy < MENU_H_PX; yy++) {
        uint8_t *row = fb + (MENU_Y + yy) * SCREEN_W + MENU_X;
        for (int xx = 0; xx < MENU_W_PX; xx++) row[xx] = MENU_BG_COLOR;
    }
}

/* Render one content row with a "> LABEL  VALUE" layout. sel marks
 * which row is currently selected (shows the > prefix). */
static void draw_row(uint8_t *fb, int y_off, int sel,
                     const char *label, const char *value) {
    const int X = MENU_X;
    const int Y = MENU_Y;
    char left[12];
    left[0]  = sel ? '>' : ' ';
    left[1]  = ' ';
    int i = 0;
    while (label[i] && i < 9) { left[2 + i] = label[i]; i++; }
    while (i < 9) { left[2 + i] = ' '; i++; }
    left[11] = 0;
    font_draw_string(fb, X + 8,        Y + y_off, left,  MENU_FG_COLOR);
    font_draw_string(fb, X + 8 * 13,   Y + y_off, value, MENU_FG_COLOR);
}

void menu_render(uint8_t *fb) {
    if (!menu_active) return;
    fill_bg(fb);

    const int X = MENU_X;
    const int Y = MENU_Y;

    /* Top + bottom rule. */
    font_draw_string(fb, X, Y,      "+----------------+", MENU_FG_COLOR);
    font_draw_string(fb, X, Y + 72, "+----------------+", MENU_FG_COLOR);

    /* Tab row at y=16: "|AUDIO| LIGHTING" or "AUDIO |LIGHTING|" —
     * the bar-wrapped one is the active tab. > prefix marks the tab
     * row as selected; LEFT/RIGHT switches when on this row. */
    const char *tab_text;
    if (menu_tab == TAB_AUDIO) {
        tab_text = (menu_row == 0) ? "> |AUDIO| LIGHTING"
                                   : "  |AUDIO| LIGHTING";
    } else {
        tab_text = (menu_row == 0) ? "> AUDIO |LIGHTING|"
                                   : "  AUDIO |LIGHTING|";
    }
    font_draw_string(fb, X, Y + 16, tab_text, MENU_FG_COLOR);

    /* Content rows at y = 32, 40, 48. */
    char num[4];
    if (menu_tab == TAB_AUDIO) {
        fmt_pct(SHARED_UC->amb_volume, num);
        draw_row(fb, 32, menu_row == 1, "AMBIENCE",  num);
        fmt_pct(SHARED_UC->step_volume, num);
        draw_row(fb, 40, menu_row == 2, "FOOTSTEPS", num);
    } else {
        uint8_t f = SHARED_UC->lighting_flags;
        draw_row(fb, 32, menu_row == 1, "FLICKER",
                 (f & LIGHTING_FLICKER) ? " ON" : "OFF");
        draw_row(fb, 40, menu_row == 2, "STROBES",
                 (f & LIGHTING_STROBE)  ? " ON" : "OFF");
        draw_row(fb, 48, menu_row == 3, "SHIMMER",
                 (f & LIGHTING_SHIMMER) ? " ON" : "OFF");
    }

    /* Hint row at y=64. */
    font_draw_string(fb, X + 8, Y + 64, "START TO CLOSE", MENU_FG_COLOR);
}
