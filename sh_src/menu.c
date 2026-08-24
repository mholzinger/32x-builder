#include "mars.h"
#include "font.h"
#include "menu.h"
#include "raycast.h"
#include "shared.h"
#include "version.h"
#include "custom_maps.h"
#include "sound.h"

/* Owned by m_main.c — the metrics-overlay gate. Exposed so the LIGHTING tab can
 * toggle it: the MODE-button shortcut is 6-button-only, so this is the way to
 * reach the overlay on a 3-button pad. */
extern uint8_t g_metrics_on;
/* Owned by m_main.c — wipes every debug overlay (metrics/automap/padtest).
 * Called on menu OPEN so a player stuck with phantom-toggled overlays can
 * always clear them with START, the one button every pad has. */
extern void debug_overlays_clear(void);
/* Owned by m_main.c — the current level's name + author, for the CREDITS tab
 * "now playing" lines (shared with the automap credits). */
extern void        cur_map_name(char *out);
extern const char *cur_map_author(void);
/* Owned by m_main.c — wipes the metrics overlay's Genesis tile rows. The
 * VISUALS toggle must blank on OFF or the last-drawn tiles hover forever
 * (they only redraw while the flag is on). */
extern void hud_genesis_blank(void);
/* Owned by m_main.c — the MAPS tab writes the chosen custom-map index here and
 * the main loop drains it into the warp. -1 = no request. */
extern volatile int g_warp_request;
extern volatile int g_ym_tl_dirty;   /* m_main.c: bed-level update request */
/* GAME tab plumbing (m_main.c): the automap lives there, and the viewer /
 * exit-to-lobby are whole-screen flows the main loop owns. The menu just
 * pokes state and posts requests -- Michael's "no memorizing" note: every
 * utility reachable from START on any pad, MODE combos stay optional. */
extern volatile int g_viewer_request, g_lobby_request;
extern volatile int g_sms_request;
extern volatile int g_smsgame_request;
uint8_t m_main_automap_get(void);
void    m_main_automap_cycle(int dir);
void    m_main_automap_zoom(int dir);

/* Two-tab pause menu. START opens/closes; tabs (AUDIO / LIGHTING) sit
 * on row 0 and LEFT/RIGHT switches between them when that row is
 * focused. UP/DOWN cycles between the tab row and the per-tab content
 * rows; LEFT/RIGHT on a content row adjusts the value (sliders) or
 * inverts the bool (toggles). Settings live in shared SDRAM so the
 * secondary's audio pump and the raycaster's effect gates see edits
 * immediately via cache-through. */

#define TAB_GAME     0
#define TAB_AUDIO    1
#define TAB_LIGHTING 2
#define TAB_VISUALS  3
#define TAB_COLOR    4
#define TAB_TESTING  5
#define TAB_CREDITS  6
#define TAB_MAPS     7
#define NUM_TABS     8

#define GAME_CONTENT_ROWS     4   /* MAP, ZOOM, 3D VIEWER, EXIT TO LOBBY */
#define AUDIO_CONTENT_ROWS    6   /* AMBIENCE, FOOTSTEPS, BUFFER, VOICE, HELLO, HUM */
#define LIGHTING_CONTENT_ROWS 3   /* FLICKER, STROBES, SHIMMER */
#define VISUALS_CONTENT_ROWS  6   /* WALLS, ADAPTIVE, METRICS, SHADOWS, SEAMS, DITHER */
#define TESTING_CONTENT_ROWS 15   /* SERIAL, VERT, BULKHEAD, CARPETLOD, HOLEJAMB, AUTOQTR, UNLITF, ULTRA, SPIN, IDLE, 68K, SMS32X, SMSBOOT, SMSGAME, EPOCH */
#define COLOR_CONTENT_ROWS    6   /* SURFACE, R, G, B, WARMTH, SAT */
#define CREDITS_CONTENT_ROWS  0   /* read-only display, no selection cursor */
#define CREDITS_DRAWN_ROWS    4   /* MAP / BY / BUILD / DATE lines it paints */
#define MAPS_WINDOW           6   /* map-list rows shown at once (fills the box) */

static int      menu_active = 0;
static int      menu_dirty  = 1;   /* menu content changed -> rewrite tiles */
static int      menu_redraw = 0;   /* this frame's gate (set from menu_dirty) */
static int      menu_tab    = TAB_GAME;
static int      menu_row    = 0;   /* 0 = tab row, 1..N = content row */
static uint16_t menu_prev_pad = 0;
static int      pal_sel     = 2;   /* COLOR tab: selected surface (0=WALL..3=LIGHT); default CEIL */
static const char *const pal_surf_names[4] = { " WALL", "FLOOR", " CEIL", "LIGHT" };
static int      auto_lod    = 1;   /* AUTO adaptive style: 0 = SCALE, 1 = LOD (default) */

/* Friendly WALLS resolution category: 0=FULL 1=HALF 2=LOW 3=AUTO. wall_res_mode
 * stays the render driver (0 FULL, 1 HALF, 4 LOW/quarter, 2 AUTO-scale, 6 AUTO-
 * LOD; 3 SERIAL and 5 VERT are the TESTING-tab diagnostics). */
static int res_cat_of(int m) {
    switch (m) { case 0: return 0; case 1: return 1; case 4: return 2;
                 case 2: case 6: return 3; default: return 3; }
}

/* Layout — 22-char × 10-row box (176 × 80 px) centered on the 320×224
 * screen, wide enough for the "LIGHTING |CREDITS|" tab row and tall
 * enough for the LIGHTING tab's three toggle rows. */
#define MENU_W_PX      176
#define MENU_H_PX      176   /* 15 content rows (TESTING grew again: EPOCH) +
                              * footer. MUST be a multiple of 16 so MENU_Y stays
                              * 8-aligned — else the tile-layer text and the
                              * pixel-drawn highlight bar diverge 4px. Was 112/6
                              * rows; SMSBOOT overflowed into the footer's row
                              * (Mike's screenshot, 2026-08-09), then 128/8,
                              * then 144/10, then 160/13 (EPOCH hit the hint
                              * row, same overflow). The hint row moves with
                              * it. 176 is the last comfortable bump — one
                              * more TESTING row after this and the tab needs
                              * real scrolling, not a taller box. */
#define MENU_X        ((SCREEN_W - MENU_W_PX) / 2)
#define MENU_Y        ((SCREEN_H - MENU_H_PX) / 2)

#define MENU_BG_COLOR  46   /* CEIL_BASE end = dark eggshell */
#define MENU_FG_COLOR  49   /* LIGHT_BASE[0] = bright white-ish */
/* Selection highlight bar: a muted shade off the same LIGHT_BASE white ramp the
 * text is drawn from (49=full text, 50/51/52 = 75/50/25%). Picking this existing
 * index only changes which color fills the bar — it never rewrites a palette
 * entry, so the live 3D view behind the menu is untouched, and the bright text
 * (49) reads cleanly on top. The bar blinks on/off at ~10 Hz (see hl_blink). */
#define MENU_HL_BAR    51   /* LIGHT_BASE+2 (50%) */
/* Genesis-tile HUD: menu text lives on Name Table B now. 0 = palette line 0
 * (CRAM entry 1 = light gray) — the white-ish menu look. */
#define MENU_TILE_COLOR 0x0000
/* px/py (framebuffer) -> Genesis tile x/y. MENU_X/Y are 8-aligned. */
#define TX(px) ((px) >> 3)
#define TY(py) ((py) >> 3)
/* Blank the menu box's tile rows so navigation/value changes leave no ghosts.
 * The menu box spans tile cols 9..30, rows 9..18. */
static void menu_genesis_blank(void) {
    static char blank[24] = "                       ";
    for (int r = TY(MENU_Y); r <= TY(MENU_Y + MENU_H_PX); r++)
        HwMdPuts(blank, 0, TX(MENU_X), r);
}

/* Write s at tile (col, MENU_Y+y_off), space-padded to `width` so it overwrites
 * whatever was on that row in ONE pass — no separate blank, so navigating never
 * shows a blanked row mid-scan on the single-buffered nametable (the per-move
 * flash). Gated by menu_redraw so tiles are touched only when content changes. */
static void menu_puts_pad(int col, int y_off, const char *s, int width) {
    if (!menu_redraw) return;
    char buf[24];
    if (width > 23) width = 23;
    int n = 0;
    while (s[n] && n < width) { buf[n] = s[n]; n++; }
    while (n < width) buf[n++] = ' ';
    buf[n] = 0;
    HwMdPuts(buf, MENU_TILE_COLOR, col, TY(MENU_Y + y_off));
}

#define VOL_STEP 16

int menu_is_active(void) {
    return menu_active;
}

/* Rows whose value is a NUMBER worth auto-repeating while a direction is held
 * (vs tabs/surfaces/toggles, which should only step on a fresh press). */
static int menu_row_numeric(int tab, int row) {
    if (tab == TAB_COLOR) return row >= 2;                      /* R,G,B,WARMTH,SAT */
    if (tab == TAB_AUDIO) return row == 1 || row == 2 || row == 4;  /* volumes, voice */
    if (tab == TAB_GAME)  return row == 2;                      /* ZOOM ramps while held */
    return 0;
}

static int content_rows_for(int tab) {
    switch (tab) {
    case TAB_GAME:     return GAME_CONTENT_ROWS;
    case TAB_AUDIO:    return AUDIO_CONTENT_ROWS;
    case TAB_LIGHTING: return LIGHTING_CONTENT_ROWS;
    case TAB_VISUALS:  return VISUALS_CONTENT_ROWS;
    case TAB_COLOR:    return COLOR_CONTENT_ROWS;
    case TAB_TESTING:  return TESTING_CONTENT_ROWS;
    case TAB_MAPS:     return custom_pick_count;   /* one row per compiled-in map */
    default:           return CREDITS_CONTENT_ROWS;
    }
}

void menu_update(uint16_t pad) {
    uint16_t pressed = (uint16_t)(pad & ~menu_prev_pad);
    menu_prev_pad = pad;

    if (pressed & SEGA_CTRL_START) {
        menu_active = !menu_active;
        if (menu_active) { menu_row = 0; menu_dirty = 1; debug_overlays_clear(); }
        else menu_genesis_blank();   /* wipe the menu tiles on close */
        return;
    }
    if (!menu_active) return;
    if (pressed) menu_dirty = 1;     /* any input can change the tile content */

    int total_rows = 1 + content_rows_for(menu_tab);

    if (pressed & SEGA_CTRL_UP) {
        menu_row = (menu_row + total_rows - 1) % total_rows;
    }
    if (pressed & SEGA_CTRL_DOWN) {
        menu_row = (menu_row + 1) % total_rows;
    }

    /* GAME tab: A commits the action rows. VIEWER and EXIT close the menu
     * first (warp-close, like MAPS) -- both hand the screen to main-loop
     * flows that draw over the whole frame. */
    if (menu_tab == TAB_GAME && (pressed & SEGA_CTRL_A)) {
        if (menu_row == 1) m_main_automap_cycle(+1);
        else if (menu_row == 3) {
            g_viewer_request = 1;
            menu_active = 0;
            menu_genesis_blank();
        } else if (menu_row == 4) {
            g_lobby_request = 1;
            menu_active = 0;
            menu_genesis_blank();
        }
        return;
    }

    /* MAPS tab: A on a map row warps there and closes the menu. */
    if (menu_tab == TAB_MAPS && menu_row >= 1 && (pressed & SEGA_CTRL_A)) {
        if (menu_row - 1 < custom_pick_count) {
            g_warp_request = menu_row - 1;
            menu_active = 0;
            menu_genesis_blank();    /* warp-close: wipe tiles like a START close */
        }
        return;
    }

    /* COLOR tab: A resets the whole palette to the shipped defaults. */
    if (menu_tab == TAB_TESTING && (menu_row == 13 || menu_row == 14)
        && (pressed & SEGA_CTRL_A)) {
        /* SMSBOOT/SMSGAME are ACTIONS, not toggles — they must fire on A
         * like the GAME/MAPS rows do. (They also fire on LEFT/RIGHT via the
         * dispatch below, but nobody's thumb believes that for a GO row.) */
        if (menu_row == 13) g_sms_request = 1;
        else                g_smsgame_request = 1;
        menu_active = 0;
        menu_genesis_blank();
        return;
    }
    if (menu_tab == TAB_COLOR && (pressed & SEGA_CTRL_A)) {
        raycast_pal_reset();
        return;
    }

    /* LEFT/RIGHT step. A fresh press always steps once; HOLDING on a numeric
     * value row auto-repeats after a short delay so you can scroll the number
     * instead of tapping. Non-numeric rows (tabs/surface/toggles) stay edge-only. */
    #define REPEAT_DELAY 8    /* frames held before auto-repeat kicks in */
    #define REPEAT_RATE  1    /* frames between repeats once rolling */
    static int repeat_ctr = 0;
    int held = (pad & SEGA_CTRL_LEFT) ? -1 : (pad & SEGA_CTRL_RIGHT) ? +1 : 0;
    int dir  = (pressed & SEGA_CTRL_LEFT) ? -1 : (pressed & SEGA_CTRL_RIGHT) ? +1 : 0;
    if (dir) {
        repeat_ctr = REPEAT_DELAY;                 /* fresh press: step + arm repeat */
    } else if (held && menu_row_numeric(menu_tab, menu_row)) {
        if (--repeat_ctr <= 0) { dir = held; repeat_ctr = REPEAT_RATE; menu_dirty = 1; }
    }
    if (!held) repeat_ctr = 0;
    if (dir == 0) return;

    if (menu_row == 0) {
        /* Tab row: LEFT/RIGHT cycles through the tabs (wraps both ways). */
        menu_tab = (menu_tab + dir + NUM_TABS) % NUM_TABS;
        return;
    }

    if (menu_tab == TAB_GAME) {
        if (menu_row == 1) m_main_automap_cycle(dir);       /* OFF/FULL/LOCAL */
        else if (menu_row == 2) m_main_automap_zoom(dir);   /* held: ramps */
        return;
    }
    if (menu_tab == TAB_AUDIO) {
        if (menu_row == 3) {
            /* BUFFER: underrun-fix A/B — 64MS fixed vs 16MS chop-prone
             * arm. Either direction flips; pair with the HUD's AU:
             * counter to hear/see the difference. */
            amb_toggle_buf_len();
            return;
        }
        if (menu_row == 4) {
            /* VOICE: Voyager-hello playback speed (hardware pitch trim).
             * RIGHT faster/higher, LEFT slower; buzz/steps untouched. */
            amb_voice_speed_adjust(dir);
            return;
        }
        if (menu_row == 5) {
            /* HELLO: same-binary A/B for the Speex decode cost — the
             * ONLY honest way to measure it (toppling the neanderthal
             * also removes a screen-filling sprite, which is its own
             * multi-fps cost; see the world-quad 7fps floor). */
            SHARED_UC->voice_off ^= 1;
            return;
        }
        if (menu_row == 6) {
            /* HUM: the YM2612 bed, live. OFF keys the bed off and stops
             * every YM write; ON re-strikes the tube. The isolation
             * toggle the tick hunt was missing — FM never routes through
             * the PWM volumes above, so this row is its only mute. */
            SHARED_UC->hum_ym ^= 1;
            return;
        }
        volatile uint8_t *target =
            (menu_row == 1) ? &SHARED_UC->amb_volume
                            : &SHARED_UC->step_volume;
        int v = (int)*target + dir * VOL_STEP;
        if (v < 0)   v = 0;
        if (v > 255) v = 255;
        *target = (uint8_t)v;
        if (menu_row == 1) g_ym_tl_dirty = 1;   /* bed follows AMBIENCE */
    } else if (menu_tab == TAB_LIGHTING) {
        /* LIGHTING tab: toggle the corresponding effect bit. dir doesn't
         * matter — LEFT and RIGHT both flip. */
        uint8_t bit;
        switch (menu_row) {
        case 1: bit = LIGHTING_FLICKER; break;
        case 2: bit = LIGHTING_STROBE;  break;
        default: bit = LIGHTING_SHIMMER; break;
        }
        SHARED_UC->lighting_flags ^= bit;
    } else if (menu_tab == TAB_VISUALS) {
        if (menu_row == 1) {
            /* WALLS resolution: cycle FULL / HALF / LOW / AUTO. AUTO maps to the
             * ADAPTIVE style (SCALE=mode2 or LOD=mode6). */
            static const uint8_t cat_mode[4] = { 0, 1, 4, 2 };  /* AUTO base = SCALE */
            int cat = (res_cat_of(SHARED_UC->wall_res_mode) + dir + 4) % 4;
            SHARED_UC->wall_res_mode = (cat == 3) ? (auto_lod ? 6 : 2) : cat_mode[cat];
        } else if (menu_row == 2) {
            /* ADAPTIVE style for AUTO: LOD (dithered depth bands) vs SCALE (whole-
             * frame frame-time scaling). Toggles the pref; applies live if AUTO. */
            auto_lod ^= 1;
            uint8_t cur = SHARED_UC->wall_res_mode;
            if (cur == 2 || cur == 6) SHARED_UC->wall_res_mode = auto_lod ? 6 : 2;
        } else if (menu_row == 3) {
            g_metrics_on ^= 1;
            if (!g_metrics_on) hud_genesis_blank();   /* wipe, don't just stop drawing */
        }
        else if (menu_row == 4) SHARED_UC->shadows_off ^= 1;       /* A/B the shadow cost */
        else if (menu_row == 5) SHARED_UC->wall_seam_smooth ^= 1;  /* HARD/SMOOTH seams */
        else if (menu_row == 6) SHARED_UC->wall_qdither ^= 1;      /* quarter boundary dither */
    } else if (menu_tab == TAB_TESTING) {
        /* Diagnostic RENDER modes — each ON overrides the resolution, OFF returns
         * to AUTO. LEFT/RIGHT both flip. mode 3 = SERIAL (no CPU overlap), 5 = VERT. */
        uint8_t cur = SHARED_UC->wall_res_mode;
        if (menu_row == 1) SHARED_UC->wall_res_mode = (cur == 3) ? 2 : 3;   /* SERIAL */
        else if (menu_row == 2) SHARED_UC->wall_res_mode = (cur == 5) ? 2 : 5;  /* VERT */
        else if (menu_row == 3) SHARED_UC->bulk_kill ^= 1;   /* bulkhead A/B (L:) */
        else if (menu_row == 4) SHARED_UC->carpet_vlod ^= 1; /* carpet vertical LOD A/B (R:) */
        else if (menu_row == 5) SHARED_UC->hole_jamb ^= 1;   /* exit-hole jamb + cavity skin A/B */
        else if (menu_row == 6) SHARED_UC->auto_qtr ^= 1;    /* AUTO motion-gated quarter rung A/B */
        else if (menu_row == 7) SHARED_UC->unlit_kill ^= 1;  /* unlit-floor zone fills A/B (R:) */
        else if (menu_row == 8) SHARED_UC->ultra_enable ^= 1; /* rest-pair 60Hz flip A/B */
        else if (menu_row == 9)  SHARED_UC->bus_spin ^= 1;   /* -> SW */
        else if (menu_row == 10) SHARED_UC->bus_idle ^= 1;   /* -> H/S */
        else if (menu_row == 11) {                           /* -> HU */
            SHARED_UC->bus_68k ^= 1;
            /* The 68K arm has to be TOLD — it polls COMM0 out of its own RAM
             * and never sees shared memory. Sent on the toggle, not per frame:
             * it is a mode, and every send blocks on the COMM0 handshake. */
            HwMdSetBusThrottle((int)SHARED_UC->bus_68k);
        }
        else if (menu_row == 12) SHARED_UC->sms_on_32x ^= 1; /* SH-2 draws the SMS */
        else if (menu_row == 13) {                           /* the spike */
            g_sms_request = 1;
            menu_active = 0;
            menu_genesis_blank();
        }
        else if (menu_row == 14) {                           /* the mini-game */
            g_smsgame_request = 1;
            menu_active = 0;
            menu_genesis_blank();
        }
        else if (menu_row == 15) SHARED_UC->sms_epoch_on ^= 1;  /* delta bcast */
    } else if (menu_tab == TAB_COLOR) {
        /* Live palette lab. Row 1 picks a surface; 2-4 nudge its R/G/B anchor;
         * 5-6 are the global WARMTH/SAT masters. Every change flags the palette
         * dirty and repaints in the next vblank (see raycast_pal_flush). */
        switch (menu_row) {
        case 1: pal_sel = (pal_sel + dir + 4) % 4;        break;  /* SURFACE */
        case 2: raycast_pal_ch(pal_sel, 0, dir);          break;  /* R */
        case 3: raycast_pal_ch(pal_sel, 1, dir);          break;  /* G */
        case 4: raycast_pal_ch(pal_sel, 2, dir);          break;  /* B */
        case 5: raycast_pal_warmth(dir);                  break;  /* WARMTH */
        case 6: raycast_pal_sat(dir);                     break;  /* SAT */
        }
    }
}

static void fmt_pct(uint8_t v, char out[4]) {
    int pct = ((int)v * 100 + 127) / 255;
    out[0] = (pct >= 100) ? '1' : ' ';
    out[1] = (pct >=  10) ? ('0' + ((pct / 10) % 10)) : ' ';
    out[2] = ('0' + (pct % 10));
    out[3] = 0;
}

/* Right-justified signed int in a width-4 field (COLOR tab values). */
static void fmt_int(int v, char out[6]) {
    int neg = v < 0; if (neg) v = -v;
    char tmp[6]; int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    if (neg) tmp[n++] = '-';
    int p = 0; while (p < 4 - n) out[p++] = ' ';
    while (n) out[p++] = tmp[--n];
    out[p] = 0;
}

static void fill_bg(uint8_t *fb) {
    for (int yy = 0; yy < MENU_H_PX; yy++) {
        uint8_t *row = fb + (MENU_Y + yy) * SCREEN_W + MENU_X;
        for (int xx = 0; xx < MENU_W_PX; xx++) row[xx] = MENU_BG_COLOR;
    }
}

/* ~10 Hz blink: on for 3 frames, off for 3 (6-frame period at 60 fps). Tied to
 * frame_count, not wall-clock. The selection bar is drawn only while this is
 * true, so it flashes around the focused option. */
static int hl_blink(void) {
    return (SHARED_UC->frame_count % 6) < 3;
}

/* Concise highlight spanning `ncols` 8px character cells starting at character
 * column `col` (relative to the box left edge), 8px tall. Drawn before the text
 * so the glyphs (which only stamp set pixels) sit on top of it. Used to frame
 * just the selected option's word, padded by its leading/trailing space. */
static void draw_word_hl(uint8_t *fb, int y_off, int col, int ncols, uint8_t color) {
    int x0 = MENU_X + col * 8;
    for (int yy = 0; yy < 8; yy++) {
        uint8_t *row = fb + (MENU_Y + y_off + yy) * SCREEN_W + x0;
        for (int xx = 0; xx < ncols * 8; xx++) row[xx] = color;
    }
}

/* Render one content row with a "> LABEL  VALUE" layout. sel marks
 * which row is currently selected (shows the > prefix). */
static void draw_row(uint8_t *fb, int y_off, int sel,
                     const char *label, const char *value) {
    /* Blink a concise bar around just the selected row's label (the label sits
     * at column 3, after the "> " prefix; pad one space each side). FB side,
     * every frame — independent of the tile redraw. */
    if (sel && hl_blink()) {
        int ll = 0; while (label[ll]) ll++;
        draw_word_hl(fb, y_off, 2, ll + 2, MENU_HL_BAR);
    }
    /* One full-width line "> LABEL      VALUE" (label at col 2, value at col 12)
     * so the whole row is overwritten in place — no blank, no flash. */
    char line[22];
    for (int k = 0; k < 20; k++) line[k] = ' ';
    line[0] = sel ? '>' : ' ';
    for (int i = 0; label[i] && i < 9; i++)   line[2 + i]  = label[i];
    for (int j = 0; value[j] && 12 + j < 20; j++) line[12 + j] = value[j];
    line[20] = 0;
    menu_puts_pad(TX(MENU_X + 8), y_off, line, 20);
}

void menu_render(uint8_t *fb) {
    if (!menu_active) return;
    fill_bg(fb);              /* the dimming panel stays on the FB for now */
    menu_redraw = menu_dirty; menu_dirty = 0;   /* rewrite tiles only on change */

    const int X = MENU_X;

    /* Top + bottom rule. Every write below is full-width and overwrites its row
     * in place, so no blank pass is needed on a change — that blank-then-redraw
     * was the per-move flash. (The close path still blanks to clear the menu.) */
    menu_puts_pad(TX(X), 0,   "+--------------------+", 22);
    menu_puts_pad(TX(X), 104, "+--------------------+", 22);

    /* Tab row at y=16: the > cursor points at the active tab, with the next
     * tab in the cycle shown after a pipe separator — e.g. "> AUDIO |
     * LIGHTING |". Reads as "AUDIO is selected; LIGHTING is next". The >
     * shows only when the tab row is focused (menu_row == 0), so it doubles
     * as the focus marker; LEFT/RIGHT cycles tabs there. The active tab is
     * always leftmost, so it stays clear even with the cursor hidden. */
    static const char *const tab_names[NUM_TABS] = {
        "GAME", "AUDIO", "LIGHTING", "VISUALS", "COLOR", "TESTING", "CREDITS",
        "MAPS" };
    int tab_sel  = (menu_row == 0);
    int next_tab = (menu_tab + 1) % NUM_TABS;
    char tab_text[24];
    int t = 0;
    tab_text[t++] = tab_sel ? '>' : ' ';
    tab_text[t++] = ' ';
    for (const char *p = tab_names[menu_tab]; *p; p++) tab_text[t++] = *p;
    tab_text[t++] = ' ';
    tab_text[t++] = '|';
    tab_text[t++] = ' ';
    for (const char *p = tab_names[next_tab]; *p; p++) tab_text[t++] = *p;
    tab_text[t++] = ' ';
    tab_text[t++] = '|';
    tab_text[t]   = 0;
    /* Blink a concise bar around just the active tab name (leading/trailing
     * space included) while the tab row is focused — name starts at column 2,
     * after the "> " prefix. */
    if (tab_sel && hl_blink()) {
        int wl = 0; while (tab_names[menu_tab][wl]) wl++;
        draw_word_hl(fb, 16, 1, wl + 2, MENU_HL_BAR);
    }
    menu_puts_pad(TX(X), 16, tab_text, 22);

    /* Content rows at y = 32, 40, 48. */
    char num[4];
    if (menu_tab == TAB_GAME) {
        static const char *const am_names[3] = { "  OFF", " FULL", "LOCAL" };
        draw_row(fb, 32, menu_row == 1, "MAP", am_names[m_main_automap_get() % 3]);
        draw_row(fb, 40, menu_row == 2, "ZOOM", "< >");
        draw_row(fb, 48, menu_row == 3, "3D VIEWER", " OPEN");
        draw_row(fb, 56, menu_row == 4, "EXIT", "LOBBY");
    } else if (menu_tab == TAB_AUDIO) {
        fmt_pct(SHARED_UC->amb_volume, num);
        draw_row(fb, 32, menu_row == 1, "AMBIENCE",  num);
        fmt_pct(SHARED_UC->step_volume, num);
        draw_row(fb, 40, menu_row == 2, "FOOTSTEPS", num);
        draw_row(fb, 48, menu_row == 3, "BUFFER",
                 amb_buf_len_is_big() ? " 64MS" : " 16MS");
        {
            /* VOICE: hello playback speed, 100% = baseline. */
            char pc[6]; int p = amb_voice_speed_pct();
            if (p > 999) p = 999;
            pc[0] = (p >= 100) ? ('0' + (p / 100) % 10) : ' ';
            pc[1] = (p >= 10)  ? ('0' + (p / 10) % 10)  : ' ';
            pc[2] = '0' + p % 10;
            pc[3] = '%';
            pc[4] = 0;
            draw_row(fb, 56, menu_row == 4, "VOICE", pc);
        }
        draw_row(fb, 64, menu_row == 5, "HELLO",
                 SHARED_UC->voice_off ? "  OFF" : "   ON");
        draw_row(fb, 72, menu_row == 6, "HUM",
                 SHARED_UC->hum_ym ? "   ON" : "  OFF");
    } else if (menu_tab == TAB_LIGHTING) {
        uint8_t f = SHARED_UC->lighting_flags;
        draw_row(fb, 32, menu_row == 1, "FLICKER",
                 (f & LIGHTING_FLICKER) ? " ON" : "OFF");
        draw_row(fb, 40, menu_row == 2, "STROBES",
                 (f & LIGHTING_STROBE)  ? " ON" : "OFF");
        draw_row(fb, 48, menu_row == 3, "SHIMMER",
                 (f & LIGHTING_SHIMMER) ? " ON" : "OFF");
    } else if (menu_tab == TAB_VISUALS) {
        static const char *res_names[4] = { "FULL", "HALF", " LOW", "AUTO" };
        uint8_t m = SHARED_UC->wall_res_mode;
        draw_row(fb, 32, menu_row == 1, "WALLS", res_names[res_cat_of(m)]);
        const char *adaptive = (m == 6) ? "  LOD" : (m == 2) ? "SCALE"
                             : (auto_lod ? "  LOD" : "SCALE");
        draw_row(fb, 40, menu_row == 2, "ADAPTIVE", adaptive);
        draw_row(fb, 48, menu_row == 3, "METRICS",
                 g_metrics_on ? " ON" : "OFF");
        draw_row(fb, 56, menu_row == 4, "SHADOWS",
                 SHARED_UC->shadows_off ? "OFF" : " ON");
        draw_row(fb, 64, menu_row == 5, "SEAMS",
                 SHARED_UC->wall_seam_smooth ? "SMOOTH" : "  HARD");
        draw_row(fb, 72, menu_row == 6, "DITHER",
                 SHARED_UC->wall_qdither ? " ON" : "OFF");
    } else if (menu_tab == TAB_TESTING) {
        uint8_t m = SHARED_UC->wall_res_mode;
        draw_row(fb, 32, menu_row == 1, "SERIAL", (m == 3) ? " ON" : "OFF");
        draw_row(fb, 40, menu_row == 2, "VERT",   (m == 5) ? " ON" : "OFF");
        draw_row(fb, 48, menu_row == 3, "BULKHEAD", SHARED_UC->bulk_kill ? "OFF" : " ON");
        draw_row(fb, 56, menu_row == 4, "CARPETLOD", SHARED_UC->carpet_vlod ? " ON" : "OFF");
        draw_row(fb, 64, menu_row == 5, "HOLEJAMB", SHARED_UC->hole_jamb ? " ON" : "OFF");
        draw_row(fb, 72, menu_row == 6, "AUTOQTR", SHARED_UC->auto_qtr ? " ON" : "OFF");
        draw_row(fb, 80, menu_row == 7, "UNLITF", SHARED_UC->unlit_kill ? "OFF" : " ON");
        draw_row(fb, 88, menu_row == 8, "ULTRA", SHARED_UC->ultra_enable ? " ON" : "OFF");
        draw_row(fb, 96,  menu_row == 9,  "SPIN", SHARED_UC->bus_spin ? " ON" : "OFF");
        draw_row(fb, 104, menu_row == 10, "IDLE", SHARED_UC->bus_idle ? " ON" : "OFF");
        draw_row(fb, 112, menu_row == 11, "68K",  SHARED_UC->bus_68k  ? " ON" : "OFF");
        draw_row(fb, 120, menu_row == 12, "SMS32X", SHARED_UC->sms_on_32x ? " ON" : "OFF");
        draw_row(fb, 128, menu_row == 13, "SMSBOOT", "GO");
        draw_row(fb, 136, menu_row == 14, "SMSGAME", "GO");
        draw_row(fb, 144, menu_row == 15, "EPOCH",
                 SHARED_UC->sms_epoch_on ? " ON" : "OFF");
    } else if (menu_tab == TAB_COLOR) {
        char v[6];
        draw_row(fb, 32, menu_row == 1, "SURFACE", pal_surf_names[pal_sel]);
        fmt_int(raycast_pal_ch_get(pal_sel, 0), v); draw_row(fb, 40, menu_row == 2, "R", v);
        fmt_int(raycast_pal_ch_get(pal_sel, 1), v); draw_row(fb, 48, menu_row == 3, "G", v);
        fmt_int(raycast_pal_ch_get(pal_sel, 2), v); draw_row(fb, 56, menu_row == 4, "B", v);
        fmt_int(raycast_pal_warmth_get(),       v); draw_row(fb, 64, menu_row == 5, "WARMTH", v);
        fmt_int(raycast_pal_sat_get(),          v); draw_row(fb, 72, menu_row == 6, "SAT", v);
    } else if (menu_tab == TAB_CREDITS) {
        /* CREDITS — "now playing": current level + its author, then the build
         * stamp (read-only, no selection cursor). */
        char mn[18]; cur_map_name(mn);
        char line[22]; int q;
        q = 0; line[q++]='M'; line[q++]='A'; line[q++]='P'; line[q++]=':'; line[q++]=' ';
        for (int i = 0; mn[i] && q < 20; i++) line[q++] = mn[i];
        line[q] = 0;
        menu_puts_pad(TX(X + 8), 32, line, 20);
        q = 0; line[q++]='B'; line[q++]='Y'; line[q++]=' ';
        const char *au = cur_map_author();
        for (int i = 0; au[i] && q < 20; i++) line[q++] = au[i];
        line[q] = 0;
        menu_puts_pad(TX(X + 8), 40, line, 20);
        menu_puts_pad(TX(X + 8), 48, "BUILD " VERSION_BUILD_STR " " VERSION_SHA_STR, 20);
        menu_puts_pad(TX(X + 8), 56, "DATE  " VERSION_DATE_STR, 20);
    } else { /* TAB_MAPS — scrolling list of the compiled-in custom maps */
        if (custom_pick_count == 0) {
            menu_puts_pad(TX(X + 8), 32, "  (NO MAPS)", 20);
            for (int i = 1; i < MAPS_WINDOW; i++) menu_puts_pad(TX(X + 8), 32 + 8 * i, "", 20);
        } else {
            int sel = menu_row - 1;            /* selected map, or -1 on the tab row */
            int off = 0;                       /* window scrolls with selection */
            if (custom_pick_count > MAPS_WINDOW) {
                off = (sel > 0 ? sel : 0) - 1;
                if (off < 0) off = 0;
                if (off > custom_pick_count - MAPS_WINDOW) off = custom_pick_count - MAPS_WINDOW;
            }
            /* Always paint all window rows (blank when past the end) so the
             * list self-clears as it scrolls, no blank pass. */
            for (int i = 0; i < MAPS_WINDOW; i++) {
                int mi = off + i;
                if (mi < custom_pick_count) {
                    char line[22]; int p = 0;
                    line[p++] = (menu_row == mi + 1) ? '>' : ' ';
                    line[p++] = ' ';
                    for (const char *nm = custom_maps[mi].name; *nm && p < 19; ) line[p++] = *nm++;
                    line[p] = 0;
                    if (menu_row == mi + 1 && hl_blink())
                        draw_word_hl(fb, 32 + 8 * i, 2, p, MENU_HL_BAR);
                    menu_puts_pad(TX(X + 8), 32 + 8 * i, line, 20);
                } else {
                    menu_puts_pad(TX(X + 8), 32 + 8 * i, "", 20);
                }
            }
        }
    }

    /* Ghost-clear rows this tab doesn't PAINT (tile writes persist until
     * overwritten). Use DRAWN rows, not selectable rows: CREDITS has 0 selectable
     * but paints 4 display lines, and MAPS self-clears its whole window. */
    int used;
    if      (menu_tab == TAB_MAPS)    used = MAPS_WINDOW;         /* its loop blanks past-end rows */
    else if (menu_tab == TAB_CREDITS) used = CREDITS_DRAWN_ROWS;  /* keep MAP/BY/BUILD/DATE */
    else                              used = content_rows_for(menu_tab);
    for (int r = used + 1; r <= TESTING_CONTENT_ROWS; r++)
        menu_puts_pad(TX(X + 8), 32 + (r - 1) * 8, "", 20);

    /* Hint row below all content (box is 160px, 13 rows). */
    const char *hint = "START TO CLOSE";
    if      (menu_tab == TAB_GAME)  hint = "A=SELECT START=CLOSE";
    else if (menu_tab == TAB_MAPS)  hint = "A=GO  START=CLOSE";
    else if (menu_tab == TAB_COLOR) hint = "A=RESET  START=CLOSE";
    else if (menu_tab == TAB_TESTING) hint = "A=RUN  START=CLOSE";
    menu_puts_pad(TX(X + 8), 160, hint, 20);
}
