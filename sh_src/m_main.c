#include "mars.h"
#include "menu.h"
#include "raycast.h"
#include "sin_table.h"   /* COS_FX/SIN_FX for the automap player arrow */
#include "font.h"
#include "sms_font.h"    /* generated from md_src/font.s — index IS the tile id */
#include "sms_tiles.h"   /* generated from sms/tileset.json — 4bpp art tiles
                          * (TILEBUF ids >= SMS_ART_BASE) + picture palette */
#include "version.h"
#include "shared.h"
#include "procgen.h"
#include "custom_maps.h"
#include "box3d.h"
#include "box_hero.h"
#include "sound.h"

/* Non-static so box3d.c can drive the same swap state during the
 * title screen — keeps the front/back buffer bookkeeping in one
 * place. */
uint32_t lastTick = 0;
uint16_t currentFB = 0;
extern char _end[];   /* linker: .bss end — the hero overlay parks just above
                       * (see mars.ld); the ULTRA merge borrows it as scratch */

/* On-screen debug metrics — off by default, toggled by the six-button
 * controller's MODE button (edge-detected once per frame from any loop). */
uint8_t g_metrics_on = 0;
uint8_t g_padtest_on = 0;   /* MODE+Z: raw controller register overlay */

/* HUD text now lives on the GENESIS tile layer (Name Table B via HwMdPuts),
 * composited over the 3D — every glyph is a framebuffer store the SH-2 no
 * longer makes (the measured win: offload to the idle 68K, not FB tricks).
 * 0x4000 = palette line 2 = red (CRAM entry 33). */
#define HUD_TILE_COLOR 0x4000

/* Automap overlay (MODE+B cycles): 0 = off, 1 = FULL (whole map, north-up,
 * arrow rotates), 2 = ROTATE (player fixed at center pointing screen-up, the
 * world rotating around the reticle in real time). d32xr-style vectors, but
 * composited in red OVER the live yellow view instead of a dedicated screen. */
static uint8_t g_automap_on = 0;
/* Continuous zoom, px-per-cell in 16.16: HOLDING MODE+UP/DOWN ramps the
 * target exponentially (~2s across the full range), and the drawn scale
 * eases a quarter of the gap per frame — phone-style pinch feel on a d-pad.
 * Range: 2 px/cell (tiny) .. 64 px/cell (one cell ~30% of the screen). */
static fx_t am_s_tgt = 4 << 16;
static fx_t am_s_cur = 4 << 16;

/* Which custom_maps[] entry is currently loaded; -1 = procgen/fixed/lobby.
 * The exit-door portal reads its next_map to walk story chains. */
static int g_custom_current = -1;
/* Seed of the procgen level currently loaded — its entire identity. The
 * automap footer derives the level's "name" from it (stable per level). */
static uint32_t g_procgen_seed = 0;


/* Pause-menu MAPS tab -> warp request. -1 = none; else a custom_maps[] index.
 * The menu (menu.c) sets it; the main loop drains it into portal_to_custom. */
volatile int g_warp_request = -1;
/* GAME tab requests: whole-screen flows the main loop owns. VIEWER opens the
 * asset viewer over the paused game; LOBBY breaks the game loop back to the
 * start list (the "exit to main menu" that used to need a console reset). */
volatile int g_viewer_request = 0, g_lobby_request = 0;
/* TESTING>SMSBOOT: hand the screen to the Master System spike. */
volatile int g_sms_request = 0;

/* YM hum patch — MASTER COPY (drip-fed one register/frame by the game
 * loop; see the hum service). Two algorithm-7 additive voices at
 * 60/120/180/240Hz: ch1 (+0 regs) = neon sting, ch2 (+1) = buzz bed
 * (max feedback, deeper AM, ~6dB under). fnum 1181 @ block 1 = 59.99Hz.
 * The 68K's case-15 op-1 burst mirror is RETIRED: bursts landed silent
 * (B00246) where this frame-spaced stream sounds. */
const uint8_t ym_hum_patch[][2] = {
    {0x22, 0x08}, {0x27, 0x00}, {0x2B, 0x00},
    {0x30, 0x01}, {0x34, 0x03}, {0x38, 0x02}, {0x3C, 0x14},
    {0x40, 0x28}, {0x44, 0x34}, {0x48, 0x20}, {0x4C, 0x3A},
    {0x50, 0x1F}, {0x54, 0x1F}, {0x58, 0x1F}, {0x5C, 0x1F},
    {0x60, 0x80}, {0x64, 0x80}, {0x68, 0x80}, {0x6C, 0x80},
    {0x70, 0x00}, {0x74, 0x00}, {0x78, 0x00}, {0x7C, 0x00},
    {0x80, 0x06}, {0x84, 0x06}, {0x88, 0x06}, {0x8C, 0x06},
    {0x90, 0x00}, {0x94, 0x00}, {0x98, 0x00}, {0x9C, 0x00},
    {0xB0, 0x2F}, {0xB4, 0xD1}, {0xA4, 0x0C}, {0xA0, 0x9D},
    {0x31, 0x01}, {0x35, 0x03}, {0x39, 0x02}, {0x3D, 0x34},
    {0x41, 0x20}, {0x45, 0x2C}, {0x49, 0x18}, {0x4D, 0x32},
    {0x51, 0x1F}, {0x55, 0x1F}, {0x59, 0x1F}, {0x5D, 0x1F},
    {0x61, 0x80}, {0x65, 0x80}, {0x69, 0x80}, {0x6D, 0x80},
    {0x71, 0x00}, {0x75, 0x00}, {0x79, 0x00}, {0x7D, 0x00},
    {0x81, 0x08}, {0x85, 0x08}, {0x89, 0x08}, {0x8D, 0x08},
    {0x91, 0x00}, {0x95, 0x00}, {0x99, 0x00}, {0x9D, 0x00},
    {0xB1, 0x3F}, {0xB5, 0xE1}, {0xA5, 0x0C}, {0xA1, 0x9D},
};
#define YM_HUM_PATCH_N (sizeof(ym_hum_patch) / 2)
/* The BED's four operator TLs at full AMBIENCE (regs 0x41/45/49/4D).
 * The slider adds attenuation on top: (255 - amb_volume) >> 2 steps of
 * 0.75dB, so slider 0 is ~48dB down = gone, 255 = these values. */
static const uint8_t ym_bed_tl_reg[4]  = { 0x41, 0x45, 0x49, 0x4D };
static const uint8_t ym_bed_tl_base[4] = { 0x20, 0x2C, 0x18, 0x32 };  /* +6dB 2026-08-10 */
volatile int g_ym_tl_dirty = 0;   /* menu sets on AMBIENCE change */
/* ULTRA diagnosis (HUD "U:XXXX/NN" next to A:). XXXX = which arm gate said
 * no THIS frame (0000 = armed), NN = parks entered since boot. A frozen
 * 0000 with NN stuck at 00 means the gates pass but the park never runs;
 * any set bit names the refusing gate directly. */
volatile uint16_t g_ultra_gate  = 0xFFFF;
volatile uint16_t g_ultra_parks = 0;
/* Words the merge actually rewrote (capped 9999). Zero after a park means
 * pass B rendered identical to pass A — the jitter did nothing. */
volatile uint16_t g_ultra_diff  = 0;
/* 0 = idle; 1..N = next patch index+1. STARTS AT 1: the YM hum is the
 * shipping ambience (the 437KB PWM buzz bake is deleted) — the game
 * loop drips the patch in over its first ~70 frames, the fluorescent
 * tubes striking as the world fades in. SMS exits re-arm it below
 * (their Z80 re-park resets the YM too — same silicon line). */
volatile int g_ym_upload = 1;
/* TESTING>SMSGAME: the SMS mini-game on the CURRENT level's map. */
volatile int g_smsgame_request = 0;
/* Desk-console boot transition: frames of in-world CRT birth remaining
 * before the full-screen Master System takes the glass. */
static int g_smsboot_frames = 0;

/* GAME-tab automap hooks (menu.c): the same state the MODE+B combo and the
 * MODE+UP/DOWN zoom drive, reachable without MODE -- full parity for
 * 3-button pads and MODE+ABC hybrid layouts (gameplay itself never needed
 * X/Y/Z: run/crawl/activate/look all live on A/B/C). */
uint8_t m_main_automap_get(void) { return g_automap_on; }
void m_main_automap_cycle(int dir) {
    g_automap_on = (uint8_t)((g_automap_on + (dir < 0 ? 2 : 1)) % 3);
}
void m_main_automap_zoom(int dir) {
    if (dir > 0) { am_s_tgt += am_s_tgt >> 3; if (am_s_tgt > (64 << 16)) am_s_tgt = 64 << 16; }
    else         { am_s_tgt -= am_s_tgt >> 3; if (am_s_tgt < (2 << 16))  am_s_tgt = 2 << 16; }
}
/* ── Controller input tester (MODE+Z) ────────────────────────────────────
 * Shows the exact word the 68K bridge delivers: RAW = MARS_SYS_COMM8 read
 * right now, SNP = the frame's snapshot the game logic is using (a diff
 * between them means the 68K rewrote COMM8 mid-frame). The lamp row is in
 * BIT ORDER — bit11..bit0 = M X Y Z S A C B R L D U — so the register can
 * be read straight off the screen. HIST logs the last 4 distinct words so
 * one-frame glitches leave evidence. */
static void pad_hex4(char *p, uint16_t v) {
    for (int i = 0; i < 4; i++) {
        int n = (v >> (12 - 4 * i)) & 15;
        p[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
    }
}
static void pad_lamps(char *p, uint16_t v) {
    static const char L[12] = { 'M','X','Y','Z','S','A','C','B','R','L','D','U' };
    for (int i = 0; i < 12; i++)
        p[i] = (v & (1u << (11 - i))) ? L[i] : '.';
}
static void pad_test_draw(uint8_t *fb, uint16_t snap) {
    static uint16_t hist[4] = {0,0,0,0};
    static uint16_t last = 0xFFFF;
    uint16_t raw = MARS_SYS_COMM8;
    if (raw != last) {
        hist[3] = hist[2]; hist[2] = hist[1]; hist[1] = hist[0]; hist[0] = raw;
        last = raw;
    }
    uint16_t ty = raw & SEGA_CTRL_TYPE;
    char l1[28], l2[20], l3[28], l4[28];
    /* RAW:XXXX 6BTN */
    l1[0]='R';l1[1]='A';l1[2]='W';l1[3]=':'; pad_hex4(l1+4, raw);
    l1[8]=' ';
    l1[9]  = (ty == SEGA_CTRL_SIX) ? '6' : (ty == SEGA_CTRL_THREE) ? '3' : '?';
    l1[10]='B';l1[11]='T';l1[12]='N';l1[13]=0;
    pad_lamps(l2, raw); l2[12]=0;
    l3[0]='S';l3[1]='N';l3[2]='P';l3[3]=':'; pad_hex4(l3+4, snap);
    l3[8]=' ';
    pad_lamps(l3+9, snap); l3[21]=0;
    l4[0]='H';l4[1]=':';
    for (int i = 0; i < 4; i++) { pad_hex4(l4+2+i*5, hist[i]); l4[6+i*5] = ' '; }
    l4[21]=0;
    (void)fb;                                 /* text is on the Genesis layer now */
    HwMdPuts(l1, HUD_TILE_COLOR, 0, 5);
    HwMdPuts(l2, HUD_TILE_COLOR, 0, 6);
    HwMdPuts(l3, HUD_TILE_COLOR, 0, 7);
    HwMdPuts(l4, HUD_TILE_COLOR, 0, 8);
}

/* The nametable is single-buffered, so HUD tiles persist after MODE+Y off —
 * blank the rows the HUD used. One-time on toggle-off; ~one frame of COMM. */
void hud_genesis_blank(void) {   /* non-static: the menu's METRICS toggle blanks too */
    static char blank[41] = "                                        ";
    HwMdPuts(blank, 0, 0, 0);    /* X/Y + T/H/S */
    HwMdPuts(blank, 0, 0, 1);    /* HU/SW/ID (the fixed per-frame overhead) */
    HwMdPuts(blank, 0, 0, 2);    /* A */
    HwMdPuts(blank, 0, 0, 23);   /* H/TX/P6 (chair A/B + pad type) — was MISSING:
                                  * turning metrics off left this row hovering */
    HwMdPuts(blank, 0, 0, 24);   /* D/Q/N/V/M (partition campaign) */
    HwMdPuts(blank, 0, 0, 25);   /* O/P/K/E */
    HwMdPuts(blank, 0, 0, 26);   /* C/G/R/W/F */
    HwMdPuts(blank, 0, 0, 27);   /* build stamp */
}

static void metrics_mode_check(uint16_t pad) {
    static uint16_t prev = 0xFFFF;
    /* Debug shortcuts live behind a HELD-MODE modifier: bare X used to cycle
     * the wall res while ALSO acting as a menu commit, and emulators with
     * loose six-button mappings fire phantom X/MODE singles. MODE alone now
     * does nothing (pure modifier); the combo edge-triggers on the second
     * button. The VISUALS pause-menu tab remains the discoverable path. */
    /* MODE must be held across TWO consecutive frames before any combo is
     * honored: a deliberate human MODE-hold always is, while a single-frame
     * phantom MODE (mis-read pad on real hardware) never toggles anything. */
    if ((pad & SEGA_CTRL_MODE) && (prev & SEGA_CTRL_MODE)) {
        if ((pad & SEGA_CTRL_X) && !(prev & SEGA_CTRL_X))
            SHARED_UC->wall_res_mode = (uint8_t)((SHARED_UC->wall_res_mode + 1) % 5);
        if ((pad & SEGA_CTRL_Y) && !(prev & SEGA_CTRL_Y)) {
            g_metrics_on ^= 1;
            if (!g_metrics_on) hud_genesis_blank();
        }
        if ((pad & SEGA_CTRL_B) && !(prev & SEGA_CTRL_B))
            g_automap_on = (uint8_t)((g_automap_on + 1) % 3);
        if ((pad & SEGA_CTRL_C) && !(prev & SEGA_CTRL_C))   /* partition diag (HUD J) */
            SHARED_UC->part_diag = (uint8_t)((SHARED_UC->part_diag + 1) % 3);
        if ((pad & SEGA_CTRL_A) && !(prev & SEGA_CTRL_A))   /* chair flat/textured A/B (HUD TX) */
            SHARED_UC->chair_tex ^= 1;
        if ((pad & SEGA_CTRL_Z) && !(prev & SEGA_CTRL_Z)) {
            g_padtest_on ^= 1;
            if (!g_padtest_on) {
                static char blank[41] = "                                        ";
                HwMdPuts(blank, 0, 0, 5); HwMdPuts(blank, 0, 0, 6);
                HwMdPuts(blank, 0, 0, 7); HwMdPuts(blank, 0, 0, 8);
            }
        }
        if (g_automap_on) {
            if (pad & SEGA_CTRL_UP) {                    /* held: ramp in */
                am_s_tgt += am_s_tgt >> 4;
                if (am_s_tgt > (64 << 16)) am_s_tgt = 64 << 16;
            }
            if (pad & SEGA_CTRL_DOWN) {                  /* held: ramp out */
                am_s_tgt -= am_s_tgt >> 4;
                if (am_s_tgt < (2 << 16)) am_s_tgt = 2 << 16;
            }
        }
    }
    prev = pad;
}

/* Escape hatch: opening the pause menu clears EVERY debug overlay. Field
 * report (smokemonster, real hardware): phantom MODE combos from a mis-read
 * pad turned overlays ON that a three-button pad could never turn off —
 * START is the one button every controller has. Called from menu_update. */
void debug_overlays_clear(void) {
    if (g_metrics_on) { g_metrics_on = 0; hud_genesis_blank(); }
    g_automap_on = 0;
    if (g_padtest_on) {
        static char blank[41] = "                                        ";
        g_padtest_on = 0;
        HwMdPuts(blank, 0, 0, 5); HwMdPuts(blank, 0, 0, 6);
        HwMdPuts(blank, 0, 0, 7); HwMdPuts(blank, 0, 0, 8);
    }
}

/* Frame-time profiler. Reads the SH-2 free-running timer at Φ/32
 * (~720kHz, 1.39μs per tick) once per frame and displays the delta
 * since the previous frame in the top-right corner. 60fps ≈ 12000
 * ticks, 30fps ≈ 24000, 15fps ≈ 48000. Single-stage rolling EMA
 * smooths jitter; the display updates every frame so changes are
 * immediate without being visually noisy. Remove this block when
 * we're done with the optimization pass. */
static uint16_t prof_prev_frt = 0;
static uint32_t prof_smoothed = 0;   /* 32-bit: a sub-15fps frame exceeds the 16-bit FRT range */
static uint16_t prof_secondary_smoothed = 0;
static uint16_t prof_half_smoothed = 0;

extern volatile uint16_t prof_primary_half_ticks;  /* written by raycast_render */

/* THE UNMEASURED QUARTER. Summing max(H,S) + L + I + P against T left a gap of
 * ~5,600 ticks that stayed FLAT across four captures while the work under it
 * swung 3,250 — so it is fixed per-frame overhead, not the vblank wait (that
 * would shrink as work grows). These three name it:
 *   HU = post-render draw: automap + menu_render + the HUD text itself
 *   SW = swapBuffers: the vblank wait and the framebuffer flip handshake
 *   ID = prof_primary_idle_ticks, the primary spinning on the secondary barrier
 * Sampled one frame BEHIND what's on screen (prof_sample_and_draw runs inside
 * the HU bracket), which is fine for a steady-state read. */
static uint16_t prof_post_hud = 0;
static uint16_t prof_post_swap = 0;
extern volatile uint16_t prof_primary_idle_ticks;  /* written by raycast_render */

static inline uint16_t prof_read_frt(void) {
    /* Hitachi SH-2 FRT quirk: reading FRCH latches FRCL into a
     * temporary register so the 16-bit value stays atomic. */
    uint8_t hi = SH2_FRT_FRCH;
    uint8_t lo = SH2_FRT_FRCL;
    return ((uint16_t)hi << 8) | lo;
}

static inline void prof_init(void) {
    SH2_FRT_TIER  = 0x01;  /* default — no interrupts enabled */
    SH2_FRT_TCR   = 0x02;  /* Φ/128 prescaler ≈ 180kHz — heavy diagnostic frames
                          * (SERL ~120ms) were wrapping the 16-bit window at Φ/32,
                          * poisoning every T/H/S/W read above 91ms. 364ms window
                          * now; ~5.6us/tick is ample for pass-level metrics. */
    SH2_FRT_FTCSR = 0;     /* clear OVF/OCF; free-running */
    prof_prev_frt = prof_read_frt();
}

static void prof_sample_and_draw(uint8_t *fb) {
    uint16_t now = prof_read_frt();
    uint16_t raw = (uint16_t)(now - prof_prev_frt);
    prof_prev_frt = now;
    /* The 16-bit FRT wraps once per ~91ms. A frame under ~15fps (>48000 ticks)
     * still fits, but a sub-11fps frame (>65536) wraps and reads tiny. Unwrap
     * the single overflow the same way the FPS calc does: a "frame" shorter
     * than 12000 ticks (>60fps) can't be real here, so it's a wrapped long one. */
    uint32_t delta = (raw < 3000) ? (uint32_t)raw + 65536u : raw;
    /* EMA: 7/8 old + 1/8 new — ~8-frame time constant. */
    prof_smoothed = (prof_smoothed - (prof_smoothed >> 3)) + (delta >> 3);
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
    HwMdPuts(text, HUD_TILE_COLOR, 16, 0);   /* T/H/S, top-right, GENESIS layer */
    /* Build stamp: every metrics screenshot self-identifies. GENESIS layer. */
    HwMdPuts((char *)("B" VERSION_BUILD_STR " " VERSION_SHA_STR),
             HUD_TILE_COLOR, 25, 27);
    /* Speex decode profiling, same row left of the stamp. DT = FRT
     * ticks of the last frame decode (secondary FRT prescaler, like
     * S:), DX = cumulative decoded frames (needs to climb ~50/s while
     * the hello is audible; slower = ring starving = snippets). */
    {
        char d[20];
        uint16_t dt = SHARED_UC->spx_dec_ticks;
        uint16_t dx = SHARED_UC->spx_dec_count;
        d[0] = 'D'; d[1] = 'T'; d[2] = ':';
        for (int i = 7; i >= 3; i--) { d[i] = '0' + (dt % 10); dt /= 10; }
        d[8] = ' ';
        d[9] = 'D'; d[10] = 'X'; d[11] = ':';
        for (int i = 16; i >= 12; i--) { d[i] = '0' + (dx % 10); dx /= 10; }
        d[17] = 0;
        HwMdPuts(d, HUD_TILE_COLOR, 0, 27);
    }

    /* Row 1 — THE FIXED OVERHEAD, previously invisible. HU = post-render draw
     * (automap + menu + this HUD), SW = swapBuffers (vblank wait + flip), ID =
     * primary spinning on the secondary barrier. HU+SW+ID should account for
     * most of the ~5,600-tick gap between max(H,S)+L+I+P and T. */
    {
        /* OD = % of this half's screen the wall strips cover. Ceiling and carpet
         * paint every pixel BEFORE the walls do, so OD is the share of G+R that
         * gets buried — the overdraw number, not a cost in ticks. */
        extern volatile uint16_t prof_wall_cover;
        static const char lbl[4][3] = { "HU", "SW", "ID", "OD" };
        uint16_t pv[4] = { prof_post_hud, prof_post_swap,
                           prof_primary_idle_ticks, prof_wall_cover };
        char t1[40];
        int pos = 0;
        for (int i = 0; i < 4; i++) {
            t1[pos++] = lbl[i][0];
            t1[pos++] = lbl[i][1];
            t1[pos++] = ':';
            uint16_t x = pv[i];
            for (int d = 4; d >= 0; d--) { t1[pos + d] = '0' + (x % 10); x /= 10; }
            pos += 5;
            t1[pos++] = ' ';
        }
        t1[pos] = 0;
        HwMdPuts(t1, HUD_TILE_COLOR, 0, 1);   /* HU/SW/ID, GENESIS layer */
    }

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
        if (ft < 3000) ft += 65536;
        uint32_t fps = (180000u + ft / 2) / ft;
        if (fps > 99) fps = 99;
        t2[pos++] = 'F'; t2[pos++] = ':';
        t2[pos++] = '0' + (fps / 10);
        t2[pos++] = '0' + (fps % 10);
        t2[pos] = 0;
        HwMdPuts(t2, HUD_TILE_COLOR, 0, 26);   /* C/G/R/W/F, GENESIS layer */
    }

    /* Third line: the SERIAL TAIL — primary-only post-sync work that the
     * C/G/R/W line does NOT cover. L = low-ceiling slab + bulkheads
     * (crawlspace, scene-dependent), P = lights + standups sprites. This is
     * the ~25%-of-frame block that was invisible until now. */
    {
        /* I = draw_lights, split out of P so the light loop's off-screen
         * rejection sweep (ALL NUM_LIGHTS visited, culled inside the body) is
         * readable on its own; P is now STANDUPS ONLY. Six fields at 8 chars
         * each overrun the 40-char line, so only the first FIVE render — I sits
         * in the last visible slot and K (split column, a position not a cost)
         * takes the clipped one. E/prof_dda_fat was already off-screen here. */
        extern volatile uint16_t prof_pass_ovl, prof_pass_sprite, prof_pass_slab;
        extern volatile uint16_t prof_split_col, prof_ovl_px, prof_pass_lights;
        static const char lbl[6] = {'L', 'O', 'U', 'P', 'I', 'K'};
        uint16_t pv[6] = { prof_pass_slab, prof_pass_ovl, prof_ovl_px,
                           prof_pass_sprite, prof_pass_lights, prof_split_col };
        char t3[52];
        int pos = 0;
        for (int i = 0; i < 6; i++) {
            t3[pos++] = lbl[i];
            t3[pos++] = ':';
            uint16_t x = pv[i];
            for (int d = 4; d >= 0; d--) { t3[pos + d] = '0' + (x % 10); x /= 10; }
            pos += 5;
            t3[pos++] = ' ';
        }
        t3[pos] = 0;
        HwMdPuts(t3, HUD_TILE_COLOR, 0, 25);   /* L/O/U/P/I visible, GENESIS layer */
    }

    /* Fourth line — the PARTITION CAMPAIGN counters (primary half, per frame):
     * D = DDA steps walked, Q = run-extent cells re-scanned (per contact per
     * column!), N = partial contacts kept, V = columns on the slow overlay
     * path, M = columns promoted to the fast main path. Together with O
     * (overlay ticks) these say whether the wall pass's time goes to the DDA
     * walk, the run re-scans, or the overlay draw. */
    {
        extern volatile uint16_t prof_dda_steps, prof_runwalk, prof_efg_kept,
                                 prof_ovl_cols, prof_promote_cols;
        static const char lbl[5] = {'D', 'Q', 'N', 'V', 'M'};
        static const uint8_t wid[5] = {5, 5, 5, 3, 3};   /* V/M are column counts <= 320 */
        uint16_t pv[5] = { prof_dda_steps, prof_runwalk, prof_efg_kept,
                           prof_ovl_cols, prof_promote_cols };
        char t4[42];
        int pos = 0;
        for (int i = 0; i < 5; i++) {
            t4[pos++] = lbl[i];
            t4[pos++] = ':';
            uint16_t x = pv[i];
            for (int d = wid[i] - 1; d >= 0; d--) { t4[pos + d] = '0' + (x % 10); x /= 10; }
            pos += wid[i];
            t4[pos++] = ' ';
        }
        t4[pos++] = 'J';                     /* partition diag mode (MODE+C) */
        t4[pos++] = ':';
        t4[pos++] = (char)('0' + SHARED_UC->part_diag);
        t4[pos] = 0;
        HwMdPuts(t4, HUD_TILE_COLOR, 0, 24);   /* D/Q/N/V/M + J, GENESIS layer */
    }

    /* Fifth line — chair fill A/B (MODE+A). H = primary-half chair fill ticks
     * this frame (the flat/textured cost we're measuring); TX = 0 flat /
     * 1 textured. Face a rendered chair and toggle: watch H jump. */
    {
        extern volatile uint16_t prof_pass_chair;
        char t5[48];   /* H/TX/P6/AU + E + FQ — 38 cols of the 40-col layer */
        int pos = 0;
        t5[pos++] = 'H';
        t5[pos++] = ':';
        uint16_t x = prof_pass_chair;
        for (int d = 4; d >= 0; d--) { t5[pos + d] = '0' + (x % 10); x /= 10; }
        pos += 5;
        t5[pos++] = ' ';
        t5[pos++] = 'T';
        t5[pos++] = 'X';
        t5[pos++] = ':';
        t5[pos++] = (char)('0' + (SHARED_UC->chair_tex & 1));
        /* P6: what the 68K thinks the pad is (1 = six-button handshake
         * validated / sticky-latched, 0 = three-button). Field diagnostic for
         * "my MODE combos don't work" — reachable via pause menu -> VISUALS ->
         * METRICS, which needs no MODE press. */
        t5[pos++] = ' ';
        t5[pos++] = 'P'; t5[pos++] = '6'; t5[pos++] = ':';
        t5[pos++] = (char)('0' + ((MARS_SYS_COMM8 >> 12) & 1));
        /* AU: audio underruns — DMA swaps into a buffer the pump never
         * refilled (each one = an audible stale-fragment replay). Frozen
         * = healthy; climbing = the ping-pong is starving. Pair with the
         * AUDIO tab's BUFFER A/B row: 16MS should climb in dense scenes,
         * 64MS should hold. */
        t5[pos++] = ' ';
        t5[pos++] = 'A'; t5[pos++] = 'U'; t5[pos++] = ':';
        {
            uint16_t u = amb_get_underruns();
            for (int d = 4; d >= 0; d--) { t5[pos + d] = '0' + (u % 10); u /= 10; }
            pos += 5;
        }
        /* E: eye height above the floor (SHARED_UC->eye_h, 128 = standing,
         * 40 = fully crouched, anything between = mid-ease).
         *
         * X/Y/A alone do not pin down a screenshot: eye height is exactly what
         * changes between a standing and a crouching shot, and without it a
         * reported render bug cannot be reproduced on the host — it has to be
         * guessed at from pixel positions in a photo, which is unreliable
         * enough to have sent this desk investigation down two dead ends.
         * X/Y/A + E is a replayable camera. */
        t5[pos++] = ' ';
        t5[pos++] = 'E'; t5[pos++] = ':';
        {
            uint16_t e = SHARED_UC->eye_h;
            for (int d = 2; d >= 0; d--) { t5[pos + d] = '0' + (e % 10); e /= 10; }
            pos += 3;
        }
        t5[pos] = 0;
        HwMdPuts(t5, HUD_TILE_COLOR, 0, 23);   /* H + TX, GENESIS layer */
    }
}

/* ---- Automap overlay ------------------------------------------------- *
 * Vector map over the live render: red boundary lines for every wall face
 * that touches open floor (voids stay open = exits read as gaps), partitions
 * as their true segments, the player in bright red.
 *   FULL:   whole 32x32 grid, north-up, fixed 4px/cell, arrow rotates.
 *   ROTATE: player pinned at screen center pointing screen-up; every segment
 *           is translated player-relative and rotated by (192 - angle) --
 *           the rotation that maps the facing vector onto -Y (up). */
#define AM_CX     (SCREEN_W / 2)
#define AM_CY     (SCREEN_H / 2)

static void am_line(uint8_t *fb, int x0, int y0, int x1, int y1, uint8_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        if ((unsigned)x0 < SCREEN_W && (unsigned)y0 < SCREEN_H)
            fb[y0 * SCREEN_W + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/* Per-frame transform state (set by automap_draw, read by am_pt). */
static fx_t am_rc, am_rs, am_px, am_py;
static int  am_rotate, am_ax, am_ay;

/* World FX * scale FX -> screen pixels ((2^16 x)(2^16 s) >> 32 = x*s). */
#define AM_PX(v) ((int)(((int64_t)(v) * am_s_cur) >> 32))

static void am_pt(fx_t wx, fx_t wy, int *ox, int *oy) {
    if (!am_rotate) {
        *ox = am_ax + AM_PX(wx);
        *oy = am_ay + AM_PX(wy);
        return;
    }
    fx_t dx = wx - am_px, dy = wy - am_py;
    fx_t rx = FX_MUL(dx, am_rc) - FX_MUL(dy, am_rs);
    fx_t ry = FX_MUL(dx, am_rs) + FX_MUL(dy, am_rc);
    *ox = AM_CX + AM_PX(rx);
    *oy = AM_CY + AM_PX(ry);
}

static void am_emit(uint8_t *fb, fx_t wx0, fx_t wy0, fx_t wx1, fx_t wy1, uint8_t c) {
    int x0, y0, x1, y1;
    am_pt(wx0, wy0, &x0, &y0);
    am_pt(wx1, wy1, &x1, &y1);
    am_line(fb, x0, y0, x1, y1, c);
}

/* Current level's display name into out[] (needs 18 bytes): the authored map's
 * real name, or a pronounceable 8-char name derived from the procgen SEED
 * (consonant-vowel syllables) — deterministic, so a level keeps its name for as
 * long as you wander it. Shared by the automap and the pause CREDITS tab. */
void cur_map_name(char *out) {
    int n = 0;
    if (g_custom_current >= 0) {
        for (const char *p = custom_maps[g_custom_current].name; *p && n < 16; p++)
            out[n++] = *p;
    } else {
        static const char CONS[] = "BDKLMNPRSTVZ";   /* 12 */
        static const char VOWS[] = "AEIOU";          /*  5 */
        uint32_t h = g_procgen_seed * 2654435761u;   /* Knuth mix */
        for (int i = 0; i < 4; i++) {
            out[n++] = CONS[h % 12]; h /= 12;
            out[n++] = VOWS[h % 5];  h /= 5;
            h ^= h >> 7;
        }
    }
    out[n] = 0;
}

/* Current level's author credit. Authored maps carry their own; a blank field
 * and every procgen level credit the project. */
const char *cur_map_author(void) {
    if (g_custom_current >= 0) {
        const char *a = custom_maps[g_custom_current].author;
        if (a && *a) return a;
    }
    return "-BACKROOMS-";
}

/* Automap credits: the map NAME bottom-center in bright red, the AUTHOR
 * top-center in dim red. Both slide clear of the metrics HUD when it's up. */
static void am_footer(uint8_t *fb) {
    char name[18];
    cur_map_name(name);
    int n = 0; while (name[n]) n++;
    int y = g_metrics_on ? (SCREEN_H - 36) : (SCREEN_H - 12);
    font_draw_string(fb, (SCREEN_W - n * 8) / 2, y, name, AMAP_RED_BRIGHT);

    const char *author = cur_map_author();
    int an = 0; while (author[an]) an++;
    int ay = g_metrics_on ? 40 : 8;
    font_draw_string(fb, (SCREEN_W - an * 8) / 2, ay, author, AMAP_RED);
}

static void automap_draw(uint8_t *fb) {
    am_rotate = (g_automap_on == 2);
    /* Glide the drawn scale toward the target (quarter-gap per frame). */
    fx_t d = am_s_tgt - am_s_cur;
    am_s_cur += (d > 255 || d < -255) ? (d >> 2) : d;
    if (am_rotate) {
        uint8_t th = (uint8_t)(192 - (uint8_t)player.angle);
        am_rc = COS_FX(th); am_rs = SIN_FX(th);
        am_px = player.x;   am_py = player.y;
    } else {
        /* North-up camera, per-axis: centered while that axis of the map fits
         * the screen; once it outgrows it, follow the player, clamped so the
         * map edge never leaves a gap. The clamp range collapses exactly at
         * the fits/doesn't boundary, so zoom glides through with no pop. */
        int map_w = AM_PX((fx_t)MAP_W << FX_SHIFT);
        int map_h = AM_PX((fx_t)MAP_H << FX_SHIFT);
        if (map_w <= SCREEN_W) {
            am_ax = (SCREEN_W - map_w) / 2;
        } else {
            am_ax = AM_CX - AM_PX(player.x);
            if (am_ax > 0) am_ax = 0;
            if (am_ax < SCREEN_W - map_w) am_ax = SCREEN_W - map_w;
        }
        if (map_h <= SCREEN_H) {
            am_ay = (SCREEN_H - map_h) / 2;
        } else {
            am_ay = AM_CY - AM_PX(player.y);
            if (am_ay > 0) am_ay = 0;
            if (am_ay < SCREEN_H - map_h) am_ay = SCREEN_H - map_h;
        }
    }
    /* Grid: each wall cell's faces that border walkable floor. A few hundred
     * short segments, only while the overlay is on. */
    for (int cy = 0; cy < MAP_H; cy++) {
        for (int cx = 0; cx < MAP_W; cx++) {
            if (world_map[cy][cx] != 1) continue;
            fx_t x0 = (fx_t)cx << FX_SHIFT,  y0 = (fx_t)cy << FX_SHIFT;
            fx_t x1 = x0 + FX_ONE,           y1 = y0 + FX_ONE;
            if (cy > 0         && world_map[cy - 1][cx] == 0) am_emit(fb, x0, y0, x1, y0, AMAP_RED);
            if (cy < MAP_H - 1 && world_map[cy + 1][cx] == 0) am_emit(fb, x0, y1, x1, y1, AMAP_RED);
            if (cx > 0         && world_map[cy][cx - 1] == 0) am_emit(fb, x0, y0, x0, y1, AMAP_RED);
            if (cx < MAP_W - 1 && world_map[cy][cx + 1] == 0) am_emit(fb, x1, y0, x1, y1, AMAP_RED);
        }
    }
    /* The outer shell. It's solid — the DDA treats out of bounds as wall and
     * collision always has — but it lives nowhere in world_map, so the map has
     * to draw it explicitly or it stays the last system claiming the boundary
     * is open. Emit only where it faces walkable floor: a border cell that's
     * already a wall draws its own face and hides the shell, same as in 3D. */
    {
        const fx_t xe = (fx_t)MAP_W << FX_SHIFT, ye = (fx_t)MAP_H << FX_SHIFT;
        for (int cx = 0; cx < MAP_W; cx++) {
            fx_t x0 = (fx_t)cx << FX_SHIFT, x1 = x0 + FX_ONE;
            if (world_map[0][cx] == 0)          am_emit(fb, x0,  0, x1,  0, AMAP_RED);
            if (world_map[MAP_H - 1][cx] == 0)  am_emit(fb, x0, ye, x1, ye, AMAP_RED);
        }
        for (int cy = 0; cy < MAP_H; cy++) {
            fx_t y0 = (fx_t)cy << FX_SHIFT, y1 = y0 + FX_ONE;
            if (world_map[cy][0] == 0)          am_emit(fb,  0, y0,  0, y1, AMAP_RED);
            if (world_map[cy][MAP_W - 1] == 0)  am_emit(fb, xe, y0, xe, y1, AMAP_RED);
        }
    }
    /* Partitions are cell-edge flags now — each flagged edge draws as its
     * one-cell segment; contiguous runs read as continuous lines. */
    if (g_pedge_any) {
        for (int ey = 0; ey < MAP_H; ey++)
            for (int ex = 0; ex <= MAP_W; ex++)
                if (pedge_w[ey][ex] & CM_PEDGE_PRESENT)
                    am_emit(fb, (fx_t)ex << FX_SHIFT, (fx_t)ey << FX_SHIFT,
                                (fx_t)ex << FX_SHIFT, (fx_t)(ey + 1) << FX_SHIFT,
                            AMAP_RED);
        for (int ey = 0; ey <= MAP_H; ey++)
            for (int ex = 0; ex < MAP_W; ex++)
                if (pedge_n[ey][ex] & CM_PEDGE_PRESENT)
                    am_emit(fb, (fx_t)ex << FX_SHIFT, (fx_t)ey << FX_SHIFT,
                                (fx_t)(ex + 1) << FX_SHIFT, (fx_t)ey << FX_SHIFT,
                            AMAP_RED);
    }

    if (am_rotate) {
        /* Fixed reticle: center arrow always pointing screen-up. */
        am_line(fb, AM_CX, AM_CY + 3, AM_CX, AM_CY - 5, AMAP_RED_BRIGHT);
        am_line(fb, AM_CX, AM_CY - 5, AM_CX - 3, AM_CY - 1, AMAP_RED_BRIGHT);
        am_line(fb, AM_CX, AM_CY - 5, AM_CX + 3, AM_CY - 1, AMAP_RED_BRIGHT);
    } else {
        /* FULL mode: the arrow lives on the map and rotates with the view. */
        int px = am_ax + AM_PX(player.x);
        int py = am_ay + AM_PX(player.y);
        int fx = (int)((COS_FX((uint8_t)player.angle) * 6) >> FX_SHIFT);
        int fy = (int)((SIN_FX((uint8_t)player.angle) * 6) >> FX_SHIFT);
        am_line(fb, px - fx / 2, py - fy / 2, px + fx, py + fy, AMAP_RED_BRIGHT);
        int bx = (-fx - fy) / 3, by = (fx - fy) / 3;
        am_line(fb, px + fx, py + fy, px + fx + bx, py + fy + by, AMAP_RED_BRIGHT);
        bx = (-fx + fy) / 3; by = (-fx - fy) / 3;
        am_line(fb, px + fx, py + fy, px + fx + bx, py + fy + by, AMAP_RED_BRIGHT);
    }
    am_footer(fb);
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

    HwMdPuts(line1, HUD_TILE_COLOR, 0, 0);   /* X/Y, top-left, GENESIS layer */
    HwMdPuts(line2, HUD_TILE_COLOR, 0, 2);   /* A, GENESIS layer */
    /* "U:XXXX P:NN D:NNNN" — ULTRA arm gate / parks entered / words the
     * merge rewrote. Same row, right of A:. */
    {
        char u[20];
        u[0]='U'; u[1]=':'; pad_hex4(u+2, g_ultra_gate);
        u[6]=' '; u[7]='P'; u[8]=':';
        uint16_t p = g_ultra_parks;
        u[10]=(char)('0'+p%10); p/=10; u[9]=(char)('0'+p%10);
        u[11]=' '; u[12]='D'; u[13]=':';
        uint16_t dv = g_ultra_diff;
        for (int i = 17; i >= 14; i--) { u[i]=(char)('0'+dv%10); dv/=10; }
        u[18]=0;
        HwMdPuts(u, HUD_TILE_COLOR, 7, 2);
    }
}

/* BUS A/B (TESTING>BUS, shared.h). raycast.c's CMD_HALF/CMD_TAIL barrier waits
 * have carried a 16-nop throttle since the 68K-bridge starvation fix — bare
 * polling at ~5M reads/sec was landing on the 68K's joypad window often enough
 * to drop COMM8 updates. The waits below never got the same treatment, and they
 * are the LONGEST in the frame: swapBuffers parks on MARS_VDP_FBCTL for up to a
 * full vblank at low fps, hammering a 32X VDP register the whole time, while
 * the secondary is next door doing its audio work out of the same silicon.
 *
 * The flag is hoisted to a register BEFORE the loop on purpose: reading
 * SHARED_UC (uncached) inside the spin would be the very traffic being removed. */
#define BUS_PAUSE16()                                        \
    __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop\n\t"      \
                         "nop\n\tnop\n\tnop\n\tnop\n\t"      \
                         "nop\n\tnop\n\tnop\n\tnop\n\t"      \
                         "nop\n\tnop\n\tnop\n\tnop")
#define BUS_WAIT(cond)                                       \
    do {                                                     \
        int _thr = (int)SHARED_UC->bus_spin;                 \
        while (cond) { if (_thr) BUS_PAUSE16(); }            \
    } while (0)

void swapBuffers(void) {
    /* Advertise both bus-free waits to the secondary's Speex decoder
     * (amb_audio_idle): each poll below reads only a sysreg, so the
     * ROM/SDRAM bus is genuinely idle inside them. The FS flip
     * handshake is the one that matters — the VDP flips only at
     * vblank, so at 8 fps it parks here for up to a full vblank while
     * the COMM12 tick-wait usually exits instantly (the frame overran
     * the tick long ago). Flag dropped around shimmer/pal work in
     * between: those touch the framebuffer and CRAM. */
    SHARED_UC->primary_vwait = 1;
    BUS_WAIT(lastTick == MARS_SYS_COMM12);
    SHARED_UC->primary_vwait = 0;
    /* In vblank now — safe palette-write window. */
    raycast_shimmer();
    raycast_pal_flush();     /* live COLOR-tab palette edits, repaint when dirty */
    MARS_VDP_FBCTL = currentFB ^ 1;
    SHARED_UC->primary_vwait = 1;
    BUS_WAIT((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB);
    SHARED_UC->primary_vwait = 0;
    currentFB ^= 1;
    lastTick = MARS_SYS_COMM12;
}

/* One brightness-fade step with its own vblank flip (bypasses raycast_shimmer,
 * which would reset the bright palette mid-fade). Shared by the lobby walk-
 * through and the door portal. */
static void fade_step(int lvl) {
    SHARED_UC->frame_count++;
    raycast_render();
    BUS_WAIT(lastTick == MARS_SYS_COMM12);
    raycast_set_brightness(lvl);
    /* The MD layer fades WITH the 32X. raycast_set_brightness only reaches
     * 32X CRAM; the HUD red, the boot green and the text grey live in MD
     * CRAM and used to burn at full brightness through the whole fade on a
     * CRT (Ares crops the overscan, so it never showed there). Genesis
     * color word is 0000BBB0 GGG0RRR0 — scale each boot entry's nibbles. */
    {
        int r = ((0xA * lvl) / FADE_STEPS) & 0xE;   /* HUD red, entry 33 */
        int g = ((0xA * lvl) / FADE_STEPS) & 0xE;   /* green,   entry 17 */
        int c = ((0xC * lvl) / FADE_STEPS) & 0xE;   /* lt grey, entry 1  */
        HwMdSetColor(33, (unsigned short)r);
        HwMdSetColor(17, (unsigned short)(g << 4));
        HwMdSetColor(1,  (unsigned short)((c << 8) | (c << 4) | c));
    }
    MARS_VDP_FBCTL = currentFB ^ 1;
    BUS_WAIT((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB);
    currentFB ^= 1;
    lastTick = MARS_SYS_COMM12;
}

/* Fill an 8px-tall selection bar into the text framebuffer — the same muted
 * LIGHT_BASE+2 (idx 51) shade as the pause-menu highlight, so the lobby start
 * picker reads consistently. Caller gates the ~10 Hz blink. */
static void lobby_hl_bar(uint8_t *fb, int x, int y, int w) {
    for (int yy = 0; yy < 8; yy++) {
        uint8_t *row = fb + (y + yy) * SCREEN_W + x;
        for (int xx = 0; xx < w; xx++) row[xx] = 51;
    }
}

/* Start-menu picker row: "> [ NAME ]" — brackets always (so the current pick
 * reads), cursor + blinking bar when selected. Drawn at box-pixel (x,y). */
/* Start-menu action row: "> LABEL" — cursor + blinking bar when selected. */
/* Translucent dark panel behind the start-menu text: a 50% checkerboard of
 * the pause menu's dark eggshell (index 46) over the live lobby render — on a
 * CRT/scaler the dither reads as a smoked-glass box, and the halved contrast
 * behind the glyphs makes the white text pop. Solid would hide the lobby;
 * this keeps it breathing through. */
static void lobby_menu_panel(uint8_t *fb, int x0, int y0, int x1, int y1) {
    for (int yy = y0; yy < y1; yy++) {
        uint8_t *row = fb + yy * SCREEN_W;
        for (int xx = x0 + (yy & 1); xx < x1; xx += 2) row[xx] = 46;
    }
}

static void lobby_action_row(uint8_t *fb, int x, int y, int sel, const char *label) {
    char line[20]; int p = 0;
    line[p++] = sel ? '>' : ' ';
    line[p++] = ' ';
    for (const char *nm = label; *nm; nm++) line[p++] = *nm;
    line[p] = '\0';
    if (sel && (SHARED_UC->frame_count % 6) < 3) {
        int nl = 0; while (label[nl]) nl++;
        lobby_hl_bar(fb, x + 2 * 8, y, nl * 8);
    }
    font_draw_string(fb, x, y, line, 49);   /* menu text — stays white */
}

/* ---- Start-menu flip-out ------------------------------------------------
 * On a final selection the whole menu pane is grabbed off the framebuffer and
 * replayed as a rotating textured quad: it hinges up in perspective and recedes
 * to nothing over the live lobby, so the menu physically lifts off and you're
 * standing in the level. This is the payoff for keeping the start menu on the
 * 32X — a per-pixel warp the framebuffer does for free and tiles never could. */
#define PANE_X 48
#define PANE_Y 24
#define PANE_W 224
#define PANE_H 176   /* = MENU_H_PX: the menu grew to 15 TESTING rows (EPOCH)
                      * and MENU_Y now equals PANE_Y, so the capture covers
                      * the box exactly — the old 156 clipped the footer off
                      * the flip animation. Overlay cost: +4.4KB of the
                      * ~75KB stack headroom in .hero_overlay_lo. */
/* MEMORY OVERLAY (.hero_overlay_lo — see mars.ld): the captured pane is live
 * only between capture_menu_pane() and the end of menu_flip_out(), a window with
 * no raycast_render call in it, so the deep gameplay stack can never reach it
 * while it holds anything. It is refilled by capture_menu_pane every time, so a
 * prior render's spill through this address range is overwritten before use —
 * which is why losing the .bss zero-init is safe. */
static uint8_t pane_buf[PANE_W * PANE_H]
    __attribute__((section(".hero_overlay_lo")));   /* SDRAM: captured menu pane */

static void capture_menu_pane(const uint8_t *fb) {
    for (int v = 0; v < PANE_H; v++) {
        const uint8_t *s = fb + (PANE_Y + v) * SCREEN_W + PANE_X;
        uint8_t *d = pane_buf + v * PANE_W;
        for (int u = 0; u < PANE_W; u++) d[u] = s[u];
    }
}

/* Which exit transform the commit plays. Cycled at the start menu with
 * MODE+A, which also fires an instant slowed preview — the exploration loop
 * needs no rebuilds. 0 hinge-up, 3 fall-forward, 4 fly-through. */
static uint8_t g_flip_style = 3;

static void menu_flip_out(int style, int NF) {
    const int D  = 220;            /* viewer distance, px */
    const int CX = SCREEN_W / 2;   /* pane centre x */
    const int CY0 = PANE_Y + PANE_H / 2;
    const int HY  = PANE_Y + PANE_H;             /* bottom hinge (fall-forward) */
    for (int f = 1; f <= NF; f++) {
        SHARED_UC->frame_count++;
        raycast_render();                            /* live lobby behind */
        uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);

        if (style == 4) {
            /* FLY-THROUGH: zoom about the pane centre (scale 1..8) with a
             * progressive checker dissolve — the menu blows past the camera
             * and shreds as you punch through into the level. Per-pixel work
             * is one add + mask via the incremental u walk. */
            fx_t k   = FX_ONE + (fx_t)(((int64_t)f * FX(7)) / NF);
            fx_t inv = FX_DIV(FX_ONE, k);
            int diss = (f * 17) / NF;                /* dissolve 0..16 */
            for (int sy = 0; sy < SCREEN_H; sy++) {
                int v = PANE_H / 2 + (int)(((int64_t)(sy - CY0) * inv) >> FX_SHIFT);
                if (v < 0 || v >= PANE_H) continue;
                const uint8_t *srow = pane_buf + v * PANE_W;
                uint8_t *drow = fb + sy * SCREEN_W;
                fx_t u_fx = ((fx_t)(PANE_W / 2) << FX_SHIFT) - (fx_t)CX * inv;
                int hash = (sy * 13) & 15;
                for (int sx = 0; sx < SCREEN_W; sx++, u_fx += inv) {
                    hash = (hash + 7) & 15;
                    if (hash < diss) continue;       /* dissolved away */
                    int u = (int)(u_fx >> FX_SHIFT);
                    if ((unsigned)u < (unsigned)PANE_W) drow[sx] = srow[u];
                }
            }
        } else if (style == 3) {
            /* FALL-FORWARD: hinged at the pane's bottom edge, the top falls
             * away from the camera until it lies flat — the menu topples like
             * the neanderthal. v measured up from the hinge:
             * v = dy*D/(D*cos - dy*sin), width scale 1/s = (D + v*sin)/D. */
            uint8_t ang = (uint8_t)((f * 60) / NF);  /* 0..~84 deg */
            int32_t cs = COS_FX(ang), sn = SIN_FX(ang);
            for (int sy = 0; sy < SCREEN_H; sy++) {
                int dyp = HY - sy;                   /* px above the hinge */
                if (dyp < 0) continue;
                int32_t denom = (int32_t)D * cs - dyp * sn;
                if (denom <= 0) continue;
                int32_t vpx = (int32_t)((((int64_t)dyp * D) << 16) / denom);
                int v = PANE_H - 1 - vpx;            /* source row (top falls) */
                if (v < 0 || v >= PANE_H) continue;
                int32_t denom_s = ((int32_t)D << 16) + vpx * sn;  /* D + v*sin */
                int half_w = (int)((((int64_t)(PANE_W / 2) * D) << 16) / denom_s);
                int32_t inv_s = (int32_t)((int64_t)denom_s / D);
                const uint8_t *srow = pane_buf + v * PANE_W;
                uint8_t *drow = fb + sy * SCREEN_W;
                int x0 = CX - half_w; if (x0 < 0) x0 = 0;
                int x1 = CX + half_w; if (x1 > SCREEN_W) x1 = SCREEN_W;
                for (int sx = x0; sx < x1; sx++) {
                    int u = PANE_W / 2 + (int)(((int32_t)(sx - CX) * inv_s) >> 16);
                    if (u >= 0 && u < PANE_W) drow[sx] = srow[u];
                }
            }
        } else {
            /* HINGE-UP (the original): tilts back at the top and lifts away. */
            uint8_t ang = (uint8_t)((f * 56) / NF);      /* tilt: 0..~78 deg */
            int CY = CY0 - (int)((int32_t)f * 46 / NF);  /* drift up as it lifts */
            int32_t cs = COS_FX(ang);                    /* 16.16 */
            int32_t sn = SIN_FX(ang);                    /* 16.16 */
            for (int sy = 0; sy < SCREEN_H; sy++) {
                int dy = sy - CY;
                int32_t denom = (int32_t)D * cs + dy * sn;
                if (denom <= 0) continue;
                int32_t Y = (int32_t)((((int64_t)dy * D) << 16) / denom);
                int v = Y + PANE_H / 2;
                if (v < 0 || v >= PANE_H) continue;
                int32_t denom_s = ((int32_t)D << 16) - Y * sn;   /* (D - Y*sin) */
                if (denom_s <= 0) continue;
                int half_w = (int)((((int64_t)(PANE_W / 2) * D) << 16) / denom_s);
                int32_t inv_s = (int32_t)((int64_t)denom_s / D);  /* 16.16 = 1/s */
                const uint8_t *srow = pane_buf + v * PANE_W;
                uint8_t *drow = fb + sy * SCREEN_W;
                int x0 = CX - half_w; if (x0 < 0) x0 = 0;
                int x1 = CX + half_w; if (x1 > SCREEN_W) x1 = SCREEN_W;
                for (int sx = x0; sx < x1; sx++) {
                    int u = PANE_W / 2 + (int)(((int32_t)(sx - CX) * inv_s) >> 16);
                    if (u >= 0 && u < PANE_W) drow[sx] = srow[u];
                }
            }
        }
        swapBuffers();
    }
}

/* SHOW CONTROLS sub-screen: the title and the controls legend over the frozen
 * lobby, until any face/START button sends you back to the start menu. */
static void show_controls_screen(void) {
    const uint16_t BTNS = SEGA_CTRL_START | SEGA_CTRL_A | SEGA_CTRL_B |
                          SEGA_CTRL_C | SEGA_CTRL_X | SEGA_CTRL_Y | SEGA_CTRL_Z;
    HwMdReadPad(0);
    uint16_t prev = MARS_SYS_COMM8;          /* seed: ignore the button still held */
    for (;;) {
        HwMdReadPad(0);
        uint16_t pad = MARS_SYS_COMM8;
        uint16_t pressed = (uint16_t)(pad & ~prev);
        prev = pad;
        if ((pressed & BTNS) && !(pad & SEGA_CTRL_MODE)) break;
        SHARED_UC->frame_count++;
        raycast_render();
        uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
        font_draw_string(fb, (SCREEN_W - 13 * 8) / 2, 32, "BACKROOMS 32X", 49);
        const int LEG_X = 92;
        font_draw_string(fb, LEG_X, 78,  "RUN / INTERACT: A", 49);
        font_draw_string(fb, LEG_X, 92,  "LOOK: C",           49);
        font_draw_string(fb, LEG_X, 106, "CROUCH: A+B",       49);
        font_draw_string(fb, LEG_X, 120, "DEBUG STATS: MODE+Y", 49);
        font_draw_string(fb, LEG_X, 134, "RESOLUTION: MODE+X",  49);
        font_draw_string(fb, LEG_X, 148, "AUTOMAP: MODE+B U/D ZOOM", 49);
        font_draw_string(fb, LEG_X, 162, "PAD TEST: MODE+Z",     49);
        font_draw_string(fb, (SCREEN_W - 16 * 8) / 2, 170, "ANY BUTTON: BACK", 49);
        swapBuffers();
    }
}

/* ---- SMS audio duck ----------------------------------------------------
 * While the Master System has the stage, it should be the only thing you
 * hear: ramp the 32X mix to silence before boot and back after teardown.
 * Two knobs cover the whole mixer — amb_volume gains buzz + neon + the
 * Voyager hello, step_volume gains footsteps + slide + CRT one-shots
 * (sound.c line ~663). ~0.5 s ramp, frame-stepped; the menu's own values
 * come back exactly as the player left them. */
static uint8_t duck_amb, duck_step;
static void sms_audio_duck(void) {
    duck_amb  = SHARED_UC->amb_volume;
    duck_step = SHARED_UC->step_volume;
    for (int t = 15; t >= 0; t--) {
        SHARED_UC->amb_volume  = (uint8_t)((duck_amb  * t) / 16);
        SHARED_UC->step_volume = (uint8_t)((duck_step * t) / 16);
        Hw32xDelay(2);
    }
}
static void sms_audio_restore(void) {
    for (int t = 1; t <= 16; t++) {
        SHARED_UC->amb_volume  = (uint8_t)((duck_amb  * t) / 16);
        SHARED_UC->step_volume = (uint8_t)((duck_step * t) / 16);
        Hw32xDelay(2);
    }
}

/* ---- SMS boot screen ---------------------------------------------------
 * The Master System spike, v1: blank the 32X layer so the Genesis VDP owns
 * the glass, ask the 68K to upload the Z80 hello and drop the VDP into SMS
 * mode 4, then wait. What you should see: the whole screen pulsing through
 * the SMS palette — a real Z80 executing SMS-family code against a VDP in
 * SMS mode, inside a running 32X game. START hands the world back. */
static void sms_boot_screen(void) {
    uint16_t saved_mode = MARS_VDP_DISPMODE;
    sms_audio_duck();
    /* PROVEN COMPOSITING ONLY (v5b): the one arm that ever showed mode-4
     * tiles kept the 32X layer ON with MD pixels overlaying it — exactly
     * how the HUD text rides the game. So no MODE_OFF gamble: paint the
     * 32X frame BLACK (word stores — the FB drops zero BYTE writes) and
     * let the Master System's white glyphs overlay a black room. */
    {
        uint32_t *fb32 = (uint32_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
        for (int i = 0; i < (SCREEN_W * SCREEN_H) / 4; i++) fb32[i] = 0;
    }
    swapBuffers();     /* fill lands on the DRAW side — flip it to the glass */
    HwMdSmsBoot();
    /* SELF-INSTRUMENTING WAIT (v5d): the SH-2 redraws its layer each flip —
     * black field, COMM8's 16 bits as blocks top-left (live pad telemetry:
     * if they never change while buttons are held, the 68K stopped
     * publishing — the screenshot itself names the broken link), and a
     * walking heartbeat pixel row proving THIS loop is alive. A 15-second
     * timeout exits unconditionally: no CPU state can trap the player. */
    uint16_t prev = MARS_SYS_COMM8;
    uint32_t beats = 0;
    for (;;) {
        uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
        {
            uint32_t *fb32 = (uint32_t *)fb;
            for (int i = 0; i < (SCREEN_W * SCREEN_H) / 4; i++) fb32[i] = 0;
        }
        uint16_t pad = MARS_SYS_COMM8;
        for (int b = 0; b < 16; b++) {          /* bit blocks, MSB first */
            uint8_t c = (pad & (0x8000 >> b)) ? 49 : 46;
            for (int y = 4; y < 10; y++)
                for (int x = 0; x < 6; x++)
                    fb[(y) * SCREEN_W + 8 + b * 8 + x] = c;
        }
        fb[12 * SCREEN_W + 8 + (beats & 127)] = 49;   /* walking heartbeat */
        MARS_VDP_FBCTL = currentFB ^ 1;
        BUS_WAIT((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB);
        currentFB ^= 1;
        uint16_t pressed = (uint16_t)(pad & ~prev);
        prev = pad;
        if (pressed & SEGA_CTRL_START) break;
        if (++beats > 60u * 15u) break;         /* the escape no bug can eat */
    }
    /* STAGE BEACONS: a short colored bar appears at the top-left as each
     * teardown stage completes. If the screen freezes, the bar count in
     * the screenshot names the stage that hung: 1 bar = left the wait
     * loop, 2 = HwMdSmsStop returned, 3 = START drained. */
    {
        uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
        for (int x = 0; x < 12; x++) fb[2 * SCREEN_W + 8 + x] = 49;
    }
    {
        HwMdSmsStop();
        g_ym_upload = 1;   /* SMS teardown parked the Z80 = YM wiped; re-strike */
    }
    {
        uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
        for (int x = 0; x < 12; x++) fb[2 * SCREEN_W + 24 + x] = 49;
    }
    {   /* bounded START drain — a stuck bit must not hold the exit */
        uint32_t guard = 1000000;
        while ((MARS_SYS_COMM8 & SEGA_CTRL_START) && --guard) ;
    }
    {
        uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
        for (int x = 0; x < 12; x++) fb[2 * SCREEN_W + 40 + x] = 49;
    }
    MARS_VDP_DISPMODE = saved_mode;
    raycast_set_brightness(FADE_STEPS);      /* 32X CRAM untouched, but belt */
    sms_audio_restore();
}

/* ---- SMS mini-game screen ----------------------------------------------
 * ESCAPE THE BACKROOMS, running on the Master System's CPU, on THIS level's
 * map. The "ROM patching system" in one breath: the Z80 game blob compiled
 * into the 32X ROM has a 132-byte hole at a fixed address; we pack the live
 * world_map to 1bpp (a bit per cell — by now procgen vs curated is just
 * bytes), append the player's current cell as spawn and the level's exit
 * cell as goal, stream it over COMM, and the 68K writes it into the hole
 * right after the code copy. The Master System wakes up inside the level
 * you were just standing in. Overhead view, boot-font tiles, D-pad to move,
 * step into the door to escape. START exits any time (and is the prompt on
 * the YOU ESCAPED screen — the SH-2 owns that exit path, so no Z80 state
 * can trap the player). */
/* Pack the live level for the Z80: 1bpp map, spawn = the player's cell,
 * exit = the level's real exit (fallback: farthest open cell), the
 * level name as 16 centered boot-font tile ids (TEST PATTERN <name> —
 * procgen syllable hashes become specimen ids; mapping mirrors mars.c
 * NextChr), and the edge-partition bitmaps so thin slabs read as thin
 * slabs on the map instead of vanishing (pedge_n rows are 4B wide,
 * pedge_w rows 5B — 33 edge columns). Shared by the fullscreen game and
 * the glass session; the 68K splits the three ranges back apart. */
#define SMS_PACK_LEN 440
static int sms_exit_x, sms_exit_y;   /* stashed for the smooth renderer */
static void sms_pack_level(unsigned char *pack) {
    for (int i = 0; i < SMS_PACK_LEN; i++) pack[i] = 0;
    for (int y = 0; y <= MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            if (pedge_n[y][x])
                pack[148 + y * 4 + (x >> 3)] |=
                    (unsigned char)(0x80 >> (x & 7));
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x <= MAP_W; x++)
            if (pedge_w[y][x])
                pack[280 + y * 5 + (x >> 3)] |=
                    (unsigned char)(0x80 >> (x & 7));
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            if (world_map[y][x] != 0)
                pack[y * 4 + (x >> 3)] |= (unsigned char)(0x80 >> (x & 7));
    int sx = FX_INT(player.x), sy = FX_INT(player.y);
    if (sx < 0) sx = 0; if (sx > MAP_W - 1) sx = MAP_W - 1;
    if (sy < 0) sy = 0; if (sy > MAP_H - 1) sy = MAP_H - 1;
    int ex, ey;
    if (!raycast_exit_cell(&ex, &ey)) {
        /* No exit on this map (the lobby): farthest open cell wins. Not a
         * pathfind — the fun here is proving the pipe, not the puzzle. */
        int best = -1; ex = sx; ey = sy;
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) {
                if (world_map[y][x] != 0) continue;
                int dx = x > sx ? x - sx : sx - x;
                int dy = y > sy ? y - sy : sy - y;
                if (dx + dy > best) { best = dx + dy; ex = x; ey = y; }
            }
    }
    pack[128] = (unsigned char)sx;
    pack[129] = (unsigned char)sy;
    pack[130] = (unsigned char)ex;
    pack[131] = (unsigned char)ey;
    sms_exit_x = ex;
    sms_exit_y = ey;
    char mn[18];
    cur_map_name(mn);
    int len = 0;
    while (mn[len] && len < 16) len++;
    int off = 132 + (16 - len) / 2;
    for (int i = 0; i < len; i++) {
        char ch = mn[i];
        unsigned char t = 1;
        if (ch >= '0' && ch <= '9')      t = (unsigned char)(ch - '0' + 2);
        else if (ch >= 'A' && ch <= 'Z') t = (unsigned char)(ch - 'A' + 12);
        else if (ch >= 'a' && ch <= 'z') t = (unsigned char)(ch - 'a' + 12);
        else if (ch == ' ')              t = 0;
        else if (ch == '.')              t = 39;
        else if (ch == '-')              t = 40;
        pack[off + i] = t;
    }
}

/* Game-on-glass session toggle (DESIGN.md 4c): boot the mini-game
 * HEADLESS and let every powered PVM show its picture. The Z80 reset
 * pulse wipes the YM both ways — re-arm the hum patch like every other
 * SMS transition does. */
static void sms_glass_toggle(void) {
    if (raycast_glass_active()) {
        HwMdSmsGlassStop();
        raycast_glass_set_active(0);
    } else {
        unsigned char pack[SMS_PACK_LEN];
        sms_pack_level(pack);
        HwMdSmsGameMap(pack);
        HwMdSmsGlassBoot();
        raycast_glass_set_active(1);
    }
    g_ym_upload = 1;       /* the glass boot/park reset the YM (same line) */
}

/* Console boot beat: rendered frames the picture plays ON THE MONITOR
 * before the screen takes the room. Short on purpose — the monitor is the
 * establishing shot, not the show. The picture now converges in a single
 * frame (raycast_glass_sample gathers rather than samples), so this is
 * pure pacing, not a wait for data. */
#define GLASS_BEAT_FRAMES 8

/* from_glass: the console path — a glass session is already running the
 * Z80, so hand it over instead of rebooting (the chime and the music play
 * straight through the cut). Otherwise (TESTING>SMSGAME) boot it cold. */
/* ---- FULLSCREEN SMS ON THE 32X FRAMEBUFFER ---------------------------
 * The mini-game's picture has always arrived as MD plane-B tiles composited
 * over a black 32X frame. Here the SH-2 draws it instead, from raw TILEBUF ids
 * broadcast by the 68K (md_main.c cmd 21), through the SAME 8x8 font those ids
 * index (sms/DESIGN.md). 32 cols x 8px = 256, 24 rows x 8px = 192, centered in
 * 320x224.
 *
 * The point is not parity with the tile layer. It is that the zoom-into-the-
 * glass transition needs its start (a quad on the PVM face) and its end
 * (this) to be the SAME renderer, or the arrival is a cut. Measured: the glass
 * area is 34x28 texels for a 32x24 grid -- 1.06 texels per cell, so a glyph
 * does not fit there and does fit here. That gap IS the resampling ladder. */
#define SMS_FS_COLS  32
#define SMS_FS_ROWS  24
#define SMS_FS_CELLS (SMS_FS_COLS * SMS_FS_ROWS)
#define SMS_FS_X0    ((SCREEN_W - SMS_FS_COLS * 8) / 2)   /* 32 */
#define SMS_FS_Y0    ((SCREEN_H - SMS_FS_ROWS * 8) / 2)   /* 16 */
#define SMS_FS_WORDS (SMS_FS_CELLS / 2)                   /* 384 slots */
#define SMS_FS_INK   49    /* LIGHT_BASE[0], the bright menu white */
/* The session's FRAME: everything outside the 256x192 picture is the tuned
 * wallpaper chevron yellow, not black — the room's colour holds the screen
 * (Mike, 2026-08-13). Dedicated CRAM entry, painted at session start from
 * the live COLOR-lab wall value; the picture's interior stays index 0 so
 * the CRT face keeps its black. */
#define SMS_FS_FRAME    254
#define SMS_FS_FRAME_W  ((uint16_t)0xFEFE)     /* index pair, word fills */
#define SMS_FS_FRAME_32 ((uint32_t)0xFEFEFEFE)
/* The picture palette: a dedicated 15-entry CRAM run just under the frame
 * entry (239..253 — the community arena's top tail; ~2 potential sprite
 * blocks spent, see raycast.c COMM_BASE). Art pixel value v (1..15) paints
 * index SMS_PIC_BASE+v; value 0 is transparent — the CRT-black backing
 * stays, and the FB would drop a zero byte write anyway. Painted at session
 * entry; nothing else renders these indices, so there is no restore. */
#define SMS_PIC_BASE    238
static void sms_fs_paint_palette(void) {
    volatile uint16_t *pal = &MARS_CRAM;
    for (int i = 1; i < 16; i++)
        pal[SMS_PIC_BASE + i] = sms_art_pal[i];
}
/* Paint one 4bpp art tile at (x,y): 2 px per byte, high nibble left. */
static void sms_art_draw(uint8_t *fb, int x, int y, const uint8_t *tile) {
    for (int r = 0; r < 8; r++) {
        uint8_t *p = fb + (y + r) * SCREEN_W + x;
        for (int b = 0; b < 4; b++) {
            uint8_t two = *tile++;
            uint8_t v = (uint8_t)(two >> 4);
            if (v) p[b * 2] = (uint8_t)(SMS_PIC_BASE + v);
            v = (uint8_t)(two & 0x0F);
            if (v) p[b * 2 + 1] = (uint8_t)(SMS_PIC_BASE + v);
        }
    }
}
#define SMS_FS_FRAME_DIM 96    /* /256: the settled session frame — bright
                                * yellow SELLS the zoom, then the room recedes
                                * the moment the screen owns the frame */

static uint8_t sms_fs_tiles[SMS_FS_CELLS];   /* the picture being displayed */
static uint8_t sms_fs_stage[SMS_FS_CELLS];   /* the one being assembled */
static uint8_t sms_fs_seen[SMS_FS_WORDS];
static int     sms_fs_need;
static uint16_t sms_fs_churn;      /* cells changed on the last new picture */
static uint8_t  sms_fs_first;      /* force the first paint (churn may be 0) */

/* Drink one whole picture off the COMM rotation. Same gather-don't-gamble
 * shape as the glass sampler, sized for 384 slots instead of 48: spin until
 * every slot has been seen once, BOUNDED so a stopped broadcast leaves with a
 * partial frame rather than wedging. Affordable here precisely because the
 * raycaster is not running -- the cost budget and the data appetite move in
 * opposite directions along the zoom, which is the whole reason this works. */
/* Returns 1 when a WHOLE picture has landed and been promoted.
 *
 * The set accumulates ACROSS frames. One gather cannot collect all 384 slots:
 * the spin runs ~8.7ms and the 68K only broadcasts a few dozen slots in that
 * time, so a per-frame "collect everything or give up" gather always gave up
 * ~80% short -- and painting what it had left the missing cells showing their
 * previous contents. That is the title text bleeding through the maze.
 *
 * So: stage the slots, keep `seen` and `need` between calls, and promote to the
 * live buffer only when need hits 0. Slower to update, never wrong. A partial
 * picture is now unrepresentable rather than merely unlikely. */
/* DIRTY-EPOCH reader (TESTING>EPOCH; md_main.c has the word formats).
 * Collect the current epoch's slots into a pending list; a MARKER whose
 * count we hold applies the whole epoch to the live picture at once —
 * atomic presentation, same promise as the full gather, tiny payload.
 * Repair slots (epoch tag 63) apply directly: absolute content healing
 * whatever a lost epoch left stale. Returns 1 when anything changed the
 * live picture. */
#define SMS_EPOCH_REPAIR 63
static uint16_t ep_pend_idx[SMS_FS_WORDS];
static uint16_t ep_pend_dat[SMS_FS_WORDS];
static uint8_t  ep_seen[SMS_FS_WORDS];
static uint16_t ep_n   = 0;
static uint8_t  ep_cur = 0xFF;          /* epoch being collected; FF = none */
/* Protocol ground truth for the metrics overlay (the diag-page starvation
 * hunt): EP = epochs applied whole, SV = cells applied via supersede-
 * salvage, RP = cells applied via repair slots, MK = markers seen. */
static uint16_t epd_applied, epd_salvage, epd_repair, epd_markers;
static void sms_fs_epoch_reset(void) {
    for (int i = 0; i < SMS_FS_WORDS; i++) ep_seen[i] = 0;
    ep_n = 0;
    ep_cur = 0xFF;
}
static int sms_fs_apply_word(uint16_t slot, uint16_t w) {
    int ch = 0;
    uint16_t c = (uint16_t)(slot * 2u);
    uint8_t lo = (uint8_t)(w & 0xFF), hi = (uint8_t)(w >> 8);
    if (sms_fs_tiles[c] != lo)     { sms_fs_tiles[c] = lo;     ch++; }
    if (sms_fs_tiles[c + 1] != hi) { sms_fs_tiles[c + 1] = hi; ch++; }
    sms_fs_churn = (uint16_t)(sms_fs_churn + ch);
    return ch;
}
/* guard = the per-call spin budget, SUPPLIED BY CONTEXT (Mike's split):
 * the zoom ladder passes SMS_EPOCH_SPIN_ZOOM (small — this reader has no
 * steady-state completion to exit on, so the bound IS the cost, and a fat
 * bound stalled every transition frame ~50ms); the fullscreen session
 * passes SMS_EPOCH_SPIN_GAME (huge — the session owns the CPU, and the
 * only price of spinning is START latency measured in milliseconds).
 * Either way an applied epoch returns immediately. */
#define SMS_EPOCH_SPIN_ZOOM   3000
#define SMS_EPOCH_SPIN_GAME 100000
/* SIP, DON'T CHUG. This reader shares its bus with the 68K, whose loop is
 * the Z80's clock (FRAME_MBX) — a 100%-duty uncached spin here starves the
 * 68K's vblank poll and the whole Master System slows down. That was "the
 * stall": every reader-logic fix changed nothing because the reader's BUS
 * PRESSURE was the defect. So the spin runs in short bursts with off-bus
 * rests on the on-chip FRT (zero bus traffic — the TESTING>IDLE lesson).
 * Markers repeat every pass of the delta set, so resting through a few
 * costs sub-millisecond latency; the 68K getting its bus back is worth
 * orders of magnitude more. Rest length is FRT ticks at Φ/128 (~5.5μs
 * each): 32 ticks ≈ 180μs off-bus per 256-read burst ≈ ~60% duty. */
#define SMS_EPOCH_SIP_MASK  0xFF
#define SMS_EPOCH_SIP_TICKS 32
static int sms_fs_gather_epoch(uint32_t guard) {
    int changed = 0;
    while (--guard) {
        if ((guard & SMS_EPOCH_SIP_MASK) == 0) {
            uint16_t t0 = prof_read_frt();        /* off-bus rest (see above) */
            while ((uint16_t)(prof_read_frt() - t0) < SMS_EPOCH_SIP_TICKS) {}
        }
        uint16_t i1 = MARS_SYS_COMM10;
        uint16_t w  = MARS_SYS_COMM6;
        if (MARS_SYS_COMM10 != i1) continue;      /* torn pair: skip */
        if (i1 == 0xFFFF) continue;               /* seqlock invalidate */
        if (i1 & 0x8000) {                        /* MARKER: epoch, count */
            uint8_t  e = (uint8_t)((i1 >> 9) & 63);
            uint16_t n = (uint16_t)(i1 & 0x1FF);
            epd_markers++;
            if (e == ep_cur && n && ep_n >= n) {
                for (uint16_t k = 0; k < ep_n; k++)
                    changed += sms_fs_apply_word(ep_pend_idx[k], ep_pend_dat[k]);
                sms_fs_epoch_reset();
                epd_applied++;
                if (changed) return 1;            /* paint this epoch */
            }
            continue;
        }
        uint8_t  e = (uint8_t)((i1 >> 9) & 63);
        uint16_t s = (uint16_t)(i1 & 0x1FF);
        if (s >= SMS_FS_WORDS) continue;          /* stale/garbage: skip */
        if (e == SMS_EPOCH_REPAIR) {              /* absolute repair slot */
            int ch = sms_fs_apply_word(s, w);
            changed += ch;
            epd_repair = (uint16_t)(epd_repair + ch);
            continue;
        }
        if (e != ep_cur) {                        /* new epoch supersedes */
            /* SALVAGE, don't discard: payloads are ABSOLUTE, so a
             * superseded epoch's partial set is safe to apply late —
             * atomicity only protected a frame that is already stale.
             * Discarding here starved one-shot paints under continuous
             * churn (the diagnostics page: counters epoch every frame,
             * the static text rode one lost epoch and then crawled in
             * at repair speed — Mike's "not using our fast method"). */
            for (uint16_t k = 0; k < ep_n; k++) {
                int ch = sms_fs_apply_word(ep_pend_idx[k], ep_pend_dat[k]);
                changed += ch;
                epd_salvage = (uint16_t)(epd_salvage + ch);
            }
            sms_fs_epoch_reset();
            ep_cur = e;
        }
        if (!ep_seen[s]) {
            ep_seen[s] = 1;
            ep_pend_idx[ep_n] = s;
            ep_pend_dat[ep_n] = w;
            ep_n++;
        }
    }
    return changed ? 1 : 0;   /* repair-only progress still repaints */
}

static int sms_fs_gather(void) {
    int *needp = &sms_fs_need;
    /* The spin reads three sysregs an iteration at full rate, ~8.7ms per
     * call -- sized to drink a WHOLE broadcast rotation (and change) in one
     * visit, so a complete picture promotes nearly every frame and the
     * mini-game's screen runs at frame rate.
     *
     * HISTORY: this was cut to 3000 during the build-302 "clipping" hunt to
     * relieve bus pressure on the Z80's pauses -- the clipping turned out to
     * be macOS Low Power Mode throttling the emulator (see the audio-symptom
     * memory), and the cut's real effect was a 12-20Hz picture ("slow video
     * frames", Mike). The pressure theory was never validated on anything.
     * If hardware someday shows real contention here, fix it with pacing on
     * the 68K side, not by starving this reader.
     *
     * CRANKED (Mike): the SMS session owns this CPU — nothing else runs
     * while the modal screen is up, so the spin may as well go until the
     * picture is whole. The loop exits the moment need hits 0 (normal case:
     * one rotation, ~6-10ms); the bound is pure wedge protection for a
     * stopped broadcast, and its only cost is START latency in that
     * already-broken state. */
    uint32_t guard = 100000;
    while (*needp && --guard) {
        uint16_t i1 = MARS_SYS_COMM10;
        uint16_t w  = MARS_SYS_COMM6;
        if (MARS_SYS_COMM10 != i1) continue;    /* torn pair: skip */
        if (i1 >= SMS_FS_WORDS) continue;       /* stale pad word: skip */
        if (sms_fs_seen[i1]) continue;
        sms_fs_seen[i1] = 1;
        uint16_t c = (uint16_t)(i1 * 2u);
        sms_fs_stage[c]     = (uint8_t)(w & 0xFF);
        sms_fs_stage[c + 1] = (uint8_t)(w >> 8);
        (*needp)--;
    }
    if (*needp) return 0;                  /* still assembling — show nothing new */
    for (int i = 0; i < SMS_FS_CELLS; i++)
        if (sms_fs_tiles[i] != sms_fs_stage[i]) {
            sms_fs_tiles[i] = sms_fs_stage[i];
            sms_fs_churn++;
        }
    for (int i = 0; i < SMS_FS_WORDS; i++) sms_fs_seen[i] = 0;
    *needp = SMS_FS_WORDS;                 /* start the next picture */
    return 1;
}

/* Paint the whole 32x24 grid into the back buffer. Full redraw, not a dirty-
 * cell patch: the framebuffer is double-buffered, so a per-cell update would
 * need its own shadow per page. 768 glyphs is cheap next to a raycast frame,
 * and nothing else is competing for this CPU here. */
static void sms_fs_draw(void) {
    /* Reset the line table. raycast_render rewrites it EVERY frame (head bob,
     * and WALLS=VERT folds odd rows onto even), and it is not running here --
     * so this screen would inherit whatever offset gameplay stopped on, and
     * under VERT would show each glyph's even rows doubled. The old path never
     * noticed because it never put pixels in the 32X framebuffer at all. */
    volatile uint16_t *line_table = &MARS_FRAMEBUFFER;
    for (int i = 0; i < SCREEN_H; i++)
        line_table[i] = (uint16_t)(i * 160 + 0x100);
    uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
    uint32_t *fb32 = (uint32_t *)fb;
    for (int i = 0; i < (SCREEN_W * SCREEN_H) / 4; i++) fb32[i] = SMS_FS_FRAME_32;
    /* CRT face: the picture rect is black; the frame around it is the room. */
    for (int yy = 0; yy < SMS_FS_ROWS * 8; yy++) {
        uint32_t *pw = (uint32_t *)(fb + (SMS_FS_Y0 + yy) * SCREEN_W + SMS_FS_X0);
        for (int i = 0; i < (SMS_FS_COLS * 8) / 4; i++) pw[i] = 0;
    }
    const uint8_t *t = sms_fs_tiles;
    for (int r = 0; r < SMS_FS_ROWS; r++)
        for (int c = 0; c < SMS_FS_COLS; c++) {
            uint8_t id = *t++;
            if (id >= SMS_ART_BASE) {           /* 4bpp art tile (maze cells) */
                if (id < SMS_ART_BASE + SMS_ART_TILES)
                    sms_art_draw(fb, SMS_FS_X0 + c * 8, SMS_FS_Y0 + r * 8,
                                 sms_art[id - SMS_ART_BASE]);
                continue;
            }
            if (id >= SMS_FONT_TILES) id = 0;   /* torn/garbage slot: blank */
            font_draw_rows(fb, SMS_FS_X0 + c * 8, SMS_FS_Y0 + r * 8,
                           sms_font[id], SMS_FS_INK);
        }
}

/* THE ZOOM, screen-space half. The world renderer can only carry the push
 * until the glass is a big quad of blurry texels — from there the PICTURE
 * ITSELF takes over: drawn as a growing rect from tile ids, re-rendered
 * crisp at every size (k px per cell, 2..8), while the last world frame
 * melts away underneath it (the fly-through's checker dissolve). k=8
 * centered is sms_fs_draw's exact output, so the session loop takes over
 * with zero seam. Run backwards this is Mike's pull-out: fullscreen
 * shrinks onto the tube while the room melts back in. */
#define SMS_ZOOM_WORLD  5      /* frames of the WORLD riding the zoom, checker-
                                * dithering away around the bezel as it goes —
                                * the dither is the mask for the magnifier's
                                * cost (the chop reads as the effect). Tuned
                                * down twice: any slower reads as a CPU hang. */
#define SMS_ZOOM_LADDER 9      /* total — leaves 4 black-surround frames for
                                * the final rush: fast and subtle. */
/* Magnify the back buffer IN PLACE by one incremental window step (prev ->
 * cur). The 32X maps only the undisplayed buffer, so the world frame we zoom
 * IS the buffer we compose into: rows are processed centre-out (top half
 * top-down, bottom half bottom-up — the source row always lies centre-ward,
 * still unwritten) with a row scratch for the horizontal resample. Word
 * stores throughout: the FB drops zero BYTE writes, and a zoomed room is
 * full of index-0 black. */
static uint8_t sms_zoom_rowbuf[SCREEN_W];
static void sms_zoom_world_step(uint8_t *fb,
                                int ww0, int wh0, int cxw0, int cyw0,
                                int ww1, int wh1, int cxw1, int cyw1) {
    int wx_d = (cxw1 - ww1 / 2) - (cxw0 - ww0 / 2);
    int wy_d = (cyw1 - wh1 / 2) - (cyw0 - wh0 / 2);
    int dux = (ww1 << 8) / ww0;                  /* src step per dest px, 24.8 */
    int duy = (wh1 << 8) / wh0;
    int sx0 = ((wx_d * SCREEN_W) << 8) / ww0;
    int sy0 = ((wy_d * SCREEN_H) << 8) / wh0;
    /* Split the two passes at the mapping's FIXED POINT (src row == dest
     * row), NOT the screen middle: the window translates toward the glass
     * as it shrinks, so the point rows contract toward moves with it.
     * Splitting mid-screen let one side read rows already overwritten,
     * and the corruption arced across frames — the "rotating square"
     * smear in Mike's 21-26 captures. Above yF sources sit below (toward
     * yF, unwritten top-down); below yF they sit above (unwritten
     * bottom-up). */
    int yF = (256 - duy > 0) ? sy0 / (256 - duy) : SCREEN_H / 2;
    if (yF < 0) yF = 0;
    if (yF > SCREEN_H) yF = SCREEN_H;
    for (int half = 0; half < 2; half++) {
        int y    = half ? SCREEN_H - 1 : 0;
        int yend = half ? yF - 1 : yF;
        int ys   = half ? -1 : 1;
        for (; y != yend; y += ys) {
            int sy = (sy0 + y * duy) >> 8;
            if (sy < 0) sy = 0;
            if (sy >= SCREEN_H) sy = SCREEN_H - 1;
            for (int i = 0; i < SCREEN_W; i++)
                sms_zoom_rowbuf[i] = fb[sy * SCREEN_W + i];
            uint16_t *dst = (uint16_t *)(fb + y * SCREEN_W);
            int ux = sx0;
            for (int xw = 0; xw < SCREEN_W / 2; xw++) {
                int sa = ux >> 8; ux += dux;
                int sb = ux >> 8; ux += dux;
                if (sa < 0) sa = 0; if (sa >= SCREEN_W) sa = SCREEN_W - 1;
                if (sb < 0) sb = 0; if (sb >= SCREEN_W) sb = SCREEN_W - 1;
                dst[xw] = (uint16_t)((sms_zoom_rowbuf[sa] << 8)
                                     | sms_zoom_rowbuf[sb]);
            }
        }
    }
}
/* Draw the picture into an ARBITRARY rect, re-rendered from tile ids at that
 * size (nearest sampling of the 1bpp glyphs — crisp at every step, never
 * scaled pixels). At w=256,h=192 centred the mapping is the identity, i.e.
 * sms_fs_draw's exact output — the ladder's last frame IS the session frame. */
static void sms_fs_draw_scaled(int w, int h, int cx, int cy) {
    uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
    if (w < 16) w = 16;
    if (h < 12) h = 12;
    w &= ~1;
    int x0 = (cx - w / 2) & ~1;            /* even: the black fill is words */
    int y0 = cy - h / 2;
    if (x0 < 0) x0 = 0;
    if (x0 + w > SCREEN_W) x0 = (SCREEN_W - w) & ~1;
    if (y0 < 0) y0 = 0;
    if (y0 + h > SCREEN_H) y0 = SCREEN_H - h;
    /* Opaque black backing first (WORD stores — the FB drops zero bytes),
     * then ink on top: the growing rect is the CRT face, not a stencil. */
    for (int yy = 0; yy < h; yy++) {
        uint16_t *pw = (uint16_t *)(fb + (y0 + yy) * SCREEN_W + x0);
        for (int i = 0; i < w / 2; i++) pw[i] = 0;
    }
    int du = (256 << 8) / w, dv = (192 << 8) / h;   /* src step, 8.8 */
    int v8 = 0;
    for (int yy = 0; yy < h; yy++, v8 += dv) {
        int srcY = v8 >> 8;
        const uint8_t *rowt = &sms_fs_tiles[(srcY >> 3) * SMS_FS_COLS];
        int gy = srcY & 7;
        uint8_t *p = fb + (y0 + yy) * SCREEN_W + x0;
        int u8 = 0;
        for (int xx = 0; xx < w; xx++, u8 += du) {
            int srcX = u8 >> 8;
            uint8_t id = rowt[srcX >> 3];
            if (id == 0) continue;
            if (id >= SMS_ART_BASE) {           /* art: nearest-sample 4bpp */
                if (id < SMS_ART_BASE + SMS_ART_TILES) {
                    uint8_t two = sms_art[id - SMS_ART_BASE]
                                         [gy * 4 + ((srcX & 7) >> 1)];
                    uint8_t v = (srcX & 1) ? (uint8_t)(two & 0x0F)
                                           : (uint8_t)(two >> 4);
                    if (v) p[xx] = (uint8_t)(SMS_PIC_BASE + v);
                }
                continue;
            }
            if (id >= SMS_FONT_TILES) continue;
            if (sms_font[id][gy] & (uint8_t)(0x80u >> (srcX & 7)))
                p[xx] = SMS_FS_INK;
        }
    }
}
/* Checker dissolve toward black, sparing the [px0,px1)x[py0,py1) rect — the
 * bezel-and-glass safe zone. Runs over the zooming world frames: by the last
 * one the surround is fully dissolved, so the hand-off to the black-surround
 * phase is invisible. Word stores — the FB drops zero BYTE writes. */
static void sms_zoom_melt(uint8_t *fb, int diss,
                          int px0, int px1, int py0, int py1) {
    for (int y = 0; y < SCREEN_H; y++) {
        uint16_t *row = (uint16_t *)(fb + y * SCREEN_W);
        int inY = (y >= py0 && y < py1);
        int hash = (y * 13) & 15;
        for (int xw = 0; xw < SCREEN_W / 2; xw++) {
            hash = (hash + 7) & 15;
            if (hash >= diss) continue;
            if (inY) { int x = xw * 2; if (x >= px0 && x < px1) continue; }
            row[xw] = SMS_FS_FRAME_W;   /* dissolves to the room's yellow */
        }
    }
}

/* ---- SMOOTH MAZE (SMS32X arm) ----------------------------------------
 * Tile-by-tile TILEBUF updates read as cell hops next to the standalone
 * .sms's hardware scrolling (Mike, 2026-08-14). So while the maze is
 * being PLAYED, this side stops shipping pictures through the epoch pipe
 * and renders the maze itself: the SH-2 already owns world_map, the
 * edge-partition bitmaps and the 4bpp art — the 68K just publishes the
 * player's CELL on COMM14 every frame, and this renderer runs the same
 * pixel camera + 4px/frame glide the standalone's scroll engine does
 * (RPT_DELAY 9 > the 8-frame glide, so the picture always catches the
 * game). Full redraw per frame — nothing else wants this CPU here.
 * TILEBUF stays authoritative for every text screen and the glass; the
 * epoch reader just sits maze frames out and its repair rotation heals
 * whatever it missed on return. */
static int mz_on;
static int mz_pxw, mz_pyw, mz_txw, mz_tyw;   /* world px, eased/target */

static int mz_cell_kind(int cx, int cy) {    /* mirror of cell_kind_bg */
    if (cx == sms_exit_x && cy == sms_exit_y) return 2;
    if (world_map[cy][cx]) return 1;
    int combo = 0;
    if (pedge_n[cy][cx])     combo |= 1;
    if (pedge_w[cy][cx + 1]) combo |= 2;
    if (pedge_n[cy + 1][cx]) combo |= 4;
    if (pedge_w[cy][cx])     combo |= 8;
    return combo ? 3 + combo : 0;
}

/* 4bpp art tile at screen (x,y), clipped to the 256x192 picture rect.
 * Pixel 0 = transparent (CRT black stays; FB drops zero bytes anyway). */
static void sms_art_draw_clip(uint8_t *fb, int x, int y,
                              const uint8_t *tile) {
    for (int r = 0; r < 8; r++) {
        int py = y + r;
        if (py < SMS_FS_Y0 || py >= SMS_FS_Y0 + 192) continue;
        uint8_t *p = fb + py * SCREEN_W;
        for (int b = 0; b < 4; b++) {
            uint8_t two = tile[r * 4 + b];
            int px = x + b * 2;
            uint8_t v = (uint8_t)(two >> 4);
            if (v && px >= SMS_FS_X0 && px < SMS_FS_X0 + 256)
                p[px] = (uint8_t)(SMS_PIC_BASE + v);
            v = (uint8_t)(two & 0x0F);
            px++;
            if (v && px >= SMS_FS_X0 && px < SMS_FS_X0 + 256)
                p[px] = (uint8_t)(SMS_PIC_BASE + v);
        }
    }
}

/* The draw must FIT ONE VBLANK or the swap drops frames and the glide
 * (px per RENDER) slows with it — measured as ~80% walking speed vs the
 * standalone (Mike, 2026-08-16). Three costs cut: the yellow frame
 * paints once per buffer instead of every frame; blank tiles (most of
 * the floor) skip entirely; fully-opaque tiles blit rows with no
 * per-pixel transparency or clip tests. The glide itself now steps by
 * ELAPSED 68K vblank ticks, so even a dropped frame can't change the
 * walking SPEED — only its smoothness. */
static uint8_t mz_border_done[2];
static uint8_t mz_parity;
static void sms_maze_draw(void) {
    volatile uint16_t *line_table = &MARS_FRAMEBUFFER;
    for (int i = 0; i < SCREEN_H; i++)
        line_table[i] = (uint16_t)(i * 160 + 0x100);
    uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
    mz_parity ^= 1;
    if (!mz_border_done[mz_parity]) {        /* the room-yellow frame */
        mz_border_done[mz_parity] = 1;
        uint32_t *fb32 = (uint32_t *)fb;
        for (int i = 0; i < (SCREEN_W * SCREEN_H) / 4; i++)
            fb32[i] = SMS_FS_FRAME_32;
    }
    for (int yy = 0; yy < 192; yy++) {       /* the CRT face */
        uint32_t *pw = (uint32_t *)(fb + (SMS_FS_Y0 + yy) * SCREEN_W
                                    + SMS_FS_X0);
        for (int i = 0; i < 256 / 4; i++) pw[i] = 0;
    }
    int camx = mz_pxw - 120;                 /* v3: 16px cells, 512px world */
    if (camx < 0) camx = 0; if (camx > 256) camx = 256;
    int camy = mz_pyw - 88;
    if (camy < 0) camy = 0; if (camy > 320) camy = 320;
    int tx0 = camx >> 3, ty0 = camy >> 3;
    int ox = camx & 7, oy = camy & 7;
    int ck_cx = -1, ck_cy = -1, ck = 0;      /* 1-cell classifier cache */
    for (int ty = 0; ty <= 24; ty++) {
        int mr = ty0 + ty;
        if (mr >= 64) break;
        int y = SMS_FS_Y0 + ty * 8 - oy;
        for (int tx = 0; tx <= 32; tx++) {
            int mc = tx0 + tx;
            if (mc >= 64) break;
            int cx = mc >> 1, cy = mr >> 1;
            if (cx != ck_cx || cy != ck_cy) {
                ck_cx = cx; ck_cy = cy;
                ck = mz_cell_kind(cx, cy);
            }
            uint8_t ai = sms_metatiles[ck][(mr & 1) * 2 + (mc & 1)];
            uint8_t fl = sms_art_flags[ai];
            if (fl & 1) continue;            /* all-transparent: backing */
            int x = SMS_FS_X0 + tx * 8 - ox;
            if ((fl & 2) && x >= SMS_FS_X0 && x + 8 <= SMS_FS_X0 + 256
                         && y >= SMS_FS_Y0 && y + 8 <= SMS_FS_Y0 + 192) {
                const uint8_t *t = sms_art[ai];  /* opaque + fully inside */
                for (int r = 0; r < 8; r++) {
                    uint8_t *p = fb + (y + r) * SCREEN_W + x;
                    for (int b = 0; b < 4; b++) {
                        uint8_t two = *t++;
                        p[b * 2]     = (uint8_t)(SMS_PIC_BASE + (two >> 4));
                        p[b * 2 + 1] = (uint8_t)(SMS_PIC_BASE + (two & 0x0F));
                    }
                }
                continue;
            }
            sms_art_draw_clip(fb, x, y, sms_art[ai]);
        }
    }
    /* the 32x32 hero straddles 16px cells: centered over the cell,
     * feet on its bottom edge */
    int sx = mz_pxw - camx - 8, sy = mz_pyw - camy - 16;
    /* walk cycle: a new frame every 8px of travel; idle = frame 0 */
    int frame = 0;
    if (mz_pxw != mz_txw || mz_pyw != mz_tyw)
        frame = ((mz_pxw + mz_pyw) >> 3) & 3;
    for (int i = 0; i < 16; i++)
        sms_art_draw_clip(fb, SMS_FS_X0 + sx + (i & 3) * 8,
                          SMS_FS_Y0 + sy + (i >> 2) * 8,
                          sms_player_sprite[frame][i]);
}

static int mz_step_axis(int cur, int tgt) {
    if (cur < tgt) return cur + 4;
    if (cur > tgt) return cur - 4;
    return cur;
}

static uint16_t mz_ltick;
static void sms_maze_frame(uint16_t st) {
    int px = ((st >> 5) & 31) * 16, py = (st & 31) * 16;
    /* the 68K's frame tick lives in COMM12's high word (single writer:
     * the C loop, vblank-status paced — the _vblank ISR never runs, SR
     * stays $2700). Stepping the glide by ELAPSED ticks makes walking
     * speed independent of our own render rate. */
    uint16_t tick = (uint16_t)(MARS_SYS_COMM12 >> 16);
    if (!mz_on) {                            /* entry: snap, no glide */
        mz_on = 1;
        mz_pxw = mz_txw = px;
        mz_pyw = mz_tyw = py;
        mz_ltick = tick;
        mz_border_done[0] = mz_border_done[1] = 0;
    } else {
        int d = (uint16_t)(tick - mz_ltick);
        mz_ltick = tick;
        if (d > 4) d = 4;                    /* pause/glitch: no teleport */
        mz_txw = px;
        mz_tyw = py;
        while (d--) {
            mz_pxw = mz_step_axis(mz_pxw, mz_txw);
            mz_pyw = mz_step_axis(mz_pyw, mz_tyw);
        }
    }
    sms_maze_draw();
}

static void sms_game_screen(int from_glass) {
    uint16_t saved_mode = MARS_VDP_DISPMODE;
    sms_audio_duck();
    sms_fs_paint_palette();     /* before the zoom: art cells resolve in
                                 * colour from the first ladder rung */
    unsigned char pack[SMS_PACK_LEN];
    if (!from_glass) {
        raycast_glass_set_active(0);   /* fullscreen takes the one Z80 */
        sms_pack_level(pack);
    }
    /* TESTING>SMS32X: draw the picture ourselves out of broadcast tile ids
     * instead of letting the 68K blit it to plane B. The MD path stays intact
     * as the other arm — it is the one that works today, and the zoom is not
     * worth breaking a shipping screen for. */
    int on_32x = (int)SHARED_UC->sms_on_32x;
    if (on_32x) {
        for (int i = 0; i < SMS_FS_CELLS; i++) sms_fs_tiles[i] = 0;
        for (int i = 0; i < SMS_FS_CELLS; i++) sms_fs_stage[i] = 0;
        for (int i = 0; i < SMS_FS_WORDS; i++) sms_fs_seen[i] = 0;
        sms_fs_need  = SMS_FS_WORDS;
        sms_fs_churn = 0;
        sms_fs_first = 1;
        /* The frame colour follows the COLOR lab's live wall tuning. Full
         * brightness through the zoom — it is the room still holding the
         * screen; a direct (menu) entry has no zoom and starts settled. */
        raycast_paint_wallpaper_index(SMS_FS_FRAME,
                                      from_glass ? 256 : SMS_FS_FRAME_DIM);
        /* ARM FIRST, before any command can touch plane B. The title's
         * blinking prompt sets DIRTY continuously, so any gap where the
         * blit still runs repaints plane B with tiles nobody will clear —
         * the frozen white ghost over the framebuffer picture. Armed here,
         * the blit is dead before the boot/handoff below, cmd 18 skips its
         * bridge paint, and the clear at the bottom is the last writer. */
        sms_fs_epoch_reset();
        HwMdSetSmsTileBcast(1 | (SHARED_UC->sms_epoch_on ? 2 : 0));
    }
    /* Black BOTH 32X buffers (word stores — the FB drops zero BYTE writes)
     * so the Master System's tiles overlay a black room, the compositing
     * the diag spike proved. EXCEPT on the zoom path: the dolly just ended
     * with the tube filling the frame, and that world frame IS the bridge —
     * it stays up while the first whole picture gathers, so the arrival is
     * a lock-on instead of a black flash. */
    if (!(from_glass && on_32x)) {
        uint32_t fill = on_32x ? SMS_FS_FRAME_32 : 0;   /* MD path stays black */
        for (int b = 0; b < 2; b++) {
            uint32_t *fb32 = (uint32_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
            for (int i = 0; i < (SCREEN_W * SCREEN_H) / 4; i++) fb32[i] = fill;
            swapBuffers();
        }
    }
    if (from_glass) {
        HwMdSmsGlassHandoff();         /* same Z80, no second chime */
        raycast_glass_set_active(0);   /* the monitors let go of the signal */
    } else {
        HwMdSmsGameMap(pack);
        HwMdSmsGameBoot();
    }
    if (on_32x) {
        /* WIPE THE MD TILE LAYER. In the shipping path the game IS those tiles,
         * so whatever the HUD left there was overwritten by the picture. Here
         * the 68K stops blitting entirely, so the gameplay HUD -- and the whole
         * metrics overlay, if it is on -- would sit on top of our framebuffer
         * picture. That is the garbage. */
        hud_genesis_blank();      /* metrics rows + their internal state */
        HwMdClearScreen();        /* and the whole of Name Table B under it */
    }
    if (from_glass && on_32x) {
        /* THE ZOOM: picture grows out of the tube while the room melts.
         * Both buffers still hold late dolly frames (the skip-black above);
         * each pass dissolves that backdrop a step further and draws the
         * picture bigger and closer to centre. Accelerating curve (q²): the
         * approach feel of constant speed toward a plane. The gather runs
         * every pass, so the picture resolves IN during the early rungs —
         * a blank rect first is the signal still locking on, which is the
         * aesthetic. Final pass forces k=8 dead centre = the session
         * renderer's exact output: the swap to the wait loop is invisible. */
        /* CAMERA-ZOOM-TO-RECT. The picture is PLANTED on the glass rect the
         * rasterizer published — it never travels. What moves is the virtual
         * camera: a window over the frame that starts as the whole screen
         * and eases down onto the tube (final window = glass rect padded by
         * the session's black border, so the arrival is the session frame).
         * Every on-screen position/size below falls out of pushing the
         * fixed glass rect through that shrinking window — pure zoom, no
         * object motion, the player never senses the machinery. */
        int gw  = SHARED_UC->glass_sr_x1 - SHARED_UC->glass_sr_x0;
        int gh  = SHARED_UC->glass_sr_y1 - SHARED_UC->glass_sr_y0;
        int gcx = (SHARED_UC->glass_sr_x0 + SHARED_UC->glass_sr_x1) / 2;
        int gcy = (SHARED_UC->glass_sr_y0 + SHARED_UC->glass_sr_y1) / 2;
        if (gw < 16 || gh < 12) { gw = 64; gh = 48; gcx = 160; gcy = 112; }
        int wwF = gw * 320 / 256;      /* window that shows 1:1 + border */
        int whF = gh * 224 / 192;
        /* Per-BUFFER previous window: the two framebuffers alternate, so the
         * world content a compose starts from is two ladder steps old, not
         * one — stepping it by a single-frame delta leaves the zoom lagging
         * on every other buffer. Indexed by frame parity. */
        int wwp[2] = {320, 320}, whp[2] = {224, 224};
        int cxp[2] = {160, 160}, cyp[2] = {112, 112};
        for (int f = 1; f <= SMS_ZOOM_LADDER; f++) {
            if (SHARED_UC->sms_epoch_on) sms_fs_gather_epoch(SMS_EPOCH_SPIN_ZOOM);
            else                         sms_fs_gather();
            volatile uint16_t *line_table = &MARS_FRAMEBUFFER;
            for (int i = 0; i < SCREEN_H; i++)
                line_table[i] = (uint16_t)(i * 160 + 0x100);
            uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
            int q  = (f << 8) / SMS_ZOOM_LADDER;
            int q2 = (q * q) >> 8;
            int ww  = 320 + (((wwF - 320) * q2) >> 8);   /* the window */
            int wh  = 224 + (((whF - 224) * q2) >> 8);
            int cxw = 160 + (((gcx - 160) * q2) >> 8);
            int cyw = 112 + (((gcy - 112) * q2) >> 8);
            int w  = gw * 320 / ww;    /* the fixed glass, seen through it */
            int h  = gh * 224 / wh;
            int cx = 160 + (gcx - cxw) * 320 / ww;
            int cy = 112 + (gcy - cyw) * 224 / wh;
            if (f == SMS_ZOOM_LADDER) { w = 256; h = 192; cx = 160; cy = 112; }
            int bp = f & 1;
            if (f <= SMS_ZOOM_WORLD) {
                /* The camera visibly leaves the room: the world frame in
                 * this buffer rides the same window motion the picture does,
                 * dissolving as it goes — everything but a bezel-sized zone
                 * around the glass. Fully dissolved by the segment's end. */
                sms_zoom_world_step(fb, wwp[bp], whp[bp], cxp[bp], cyp[bp],
                                        ww,      wh,      cxw,     cyw);
                int diss = (f * 17) / SMS_ZOOM_WORLD;
                sms_zoom_melt(fb, diss,
                              cx - (w * 5) / 8, cx + (w * 5) / 8,
                              cy - (h * 5) / 8, cy + (h * 5) / 8);
            } else {
                /* Then the room's geometry is gone but its COLOUR holds:
                 * the surround is the wallpaper yellow to the session frame. */
                uint32_t *fb32 = (uint32_t *)fb;
                for (int i = 0; i < (SCREEN_W * SCREEN_H) / 4; i++)
                    fb32[i] = SMS_FS_FRAME_32;
            }
            wwp[bp] = ww; whp[bp] = wh; cxp[bp] = cxw; cyp[bp] = cyw;
            sms_fs_draw_scaled(w, h, cx, cy);
            swapBuffers();
        }
        /* Fullscreen reached: the frame drops to dim — one palette write
         * recolours the surround already on screen, no redraw. */
        raycast_paint_wallpaper_index(SMS_FS_FRAME, SMS_FS_FRAME_DIM);
        sms_fs_first = 1;         /* repaint immediately in the loop below */
    }
    /* Wait for START. No timeout: this is a game, not a diagnostic — and
     * the exit runs entirely on this CPU, so no Z80 wedge can eat it. */
    uint16_t prev = MARS_SYS_COMM8;
    for (;;) {
        uint16_t pad = MARS_SYS_COMM8;
        uint16_t pressed = (uint16_t)(pad & ~prev);
        prev = pad;
        if ((pressed & SEGA_CTRL_START) && !(pad & 0x4000)) break;
        /* bit 14: the Z80 owns START right now (its debrief or its
         * diagnostics page — both return to its terminal). The flag drops
         * with STATE_MBX, so the next START, seen at the terminal, exits
         * the session. One ROM, layered exits. */
        if (on_32x) {
            /* SMOOTH MAZE takes the frame whenever the 68K says the maze
             * is live; the epoch pipe resumes (and self-heals via its
             * repair rotation) the moment a text screen returns. */
            uint16_t mzst = MARS_SYS_COMM14;
            if ((mzst & 0x8000) && (mzst & 0x0400)) {
                sms_maze_frame(mzst);
                sms_fs_first = 1;    /* text repaints whole on return */
                swapBuffers();       /* vblank-paced: one glide step */
                continue;
            }
            mz_on = 0;
            sms_fs_churn = 0;
            int ep = (int)SHARED_UC->sms_epoch_on;
            int whole = ep ? sms_fs_gather_epoch(SMS_EPOCH_SPIN_GAME)
                           : sms_fs_gather();
            /* Only repaint when the picture actually moved. The Z80 sets DIRTY
             * only on change and most frames are not dirty, so redrawing 768
             * glyphs every frame was work nobody asked for -- and the swap that
             * followed it forced a vblank wait each time regardless. */
            if (whole && (sms_fs_churn || sms_fs_first)) {
                sms_fs_first = 0;
                sms_fs_draw();
                if (g_metrics_on) {      /* CH: cells changed this picture */
                    char m[10];
                    uint16_t v = sms_fs_churn;
                    m[0]='C'; m[1]='H'; m[2]=':';
                    for (int i = 7; i >= 3; i--) { m[i] = '0'+(v%10); v/=10; }
                    m[8] = 0;
                    font_draw_string(
                        (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200),
                        0, 0, m, SMS_FS_INK);
                    /* Protocol ground truth (the diag-page hunt): epochs
                     * applied whole / salvaged cells / repair cells /
                     * markers seen. Raw counters — two captures a few
                     * seconds apart give the rates. */
                    char pm[40];
                    int pi = 0;
                    const char *tags[4] = { "EP:", "SV:", "RP:", "MK:" };
                    uint16_t vals[4];
                    vals[0] = epd_applied;  vals[1] = epd_salvage;
                    vals[2] = epd_repair;   vals[3] = epd_markers;
                    for (int t = 0; t < 4; t++) {
                        pm[pi++] = tags[t][0]; pm[pi++] = tags[t][1];
                        pm[pi++] = tags[t][2];
                        uint16_t x = vals[t];
                        for (int d = 3; d >= 0; d--) {
                            pm[pi + d] = (char)('0' + x % 10); x /= 10;
                        }
                        pi += 4;
                        pm[pi++] = ' ';
                    }
                    pm[pi] = 0;
                    font_draw_string(
                        (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200),
                        0, 8, pm, SMS_FS_INK);
                }
                swapBuffers();
            } else if (!ep) {
                /* Legacy arm only. In epoch mode the loop goes straight
                 * back to drinking the stream — a vblank nap here added
                 * up to 16ms to every delta's arrival, and the session
                 * has nothing better to spend the CPU on (the split:
                 * no levers unbound at full screen). */
                Hw32xDelay(1);
            }
        } else {
            Hw32xDelay(1);
        }
    }
    if (on_32x) HwMdSetSmsTileBcast(0);
    HwMdSmsGameStop();
    g_ym_upload = 1;       /* same: the minigame teardown reset the YM */
    {   /* bounded START drain — a stuck bit must not hold the exit */
        uint32_t guard = 1000000;
        while ((MARS_SYS_COMM8 & SEGA_CTRL_START) && --guard) ;
    }
    MARS_VDP_DISPMODE = saved_mode;
    raycast_set_brightness(FADE_STEPS);
    sms_audio_restore();
}

/* ---- Asset viewer screen (start menu) --------------------------------
 * Dedicated screen for inspecting assets WITHOUT loading a level: the
 * chair renders as the live clustered 3D mesh, free-rotated on both axes
 * by the D-pad; other assets show as their baked sprites. Self-owned
 * loop like the controls screen — no raycast_render behind it. Backdrop
 * is the brightest wallpaper yellow, not black: dark assets (the PVM's
 * screen) vanished into a black room, and the game's own light tells the
 * eye what an asset will read like in place. START exits to the menu. */
static void asset_viewer_screen(void) {
    /* The hero backdrop painted ALL 256 CRAM entries with its own palette,
     * so every asset previewed here was decoding through the WRONG colors
     * (chair read tan, outlet read blank cream, door looked broken). Load
     * the full gameplay palette at full brightness — the viewer's whole
     * job is showing assets as the game shows them. Handed back to the
     * hero palette on exit below. Index 0 stays black (backdrop). */
    raycast_set_brightness(FADE_STEPS);
    raycast_backdrop_wall(1);                /* CRAM 0 = flat tuned wallpaper yellow */
    HwMdReadPad(0);
    uint16_t prev = MARS_SYS_COMM8;          /* seed: ignore the held commit button */
    const int ink = VIEWER_INK;              /* navy, its own CRAM entry — the
                                              * jamb brown it used sat too close
                                              * in tone to the yellow backdrop */
    int sel = 3;                             /* start on CHAIR — the one with a 3D mesh */
    int variant = 0;                         /* box models: 0 BOXES (live 3D), 1 SPRITE */
    uint8_t rotY, rotX;                      /* engine angle units, 0..255 */
    int zscale;                              /* mesh views: CONTINUOUS screen scale
                                              * (was a 4-notch zoom; UP/DOWN glides it) */
    /* Sprite views: a live world quad (tex_tri, the neanderthal's path).
     * UP/DOWN glides the distance CONTINUOUSLY — smooth scaling through the
     * real rasterizer — LEFT/RIGHT yaws it (edge-on, cardboard back, LOD). */
    fx_t adist;
    /* ONE definition of the default pose. The opening view, the C-tap reset
     * and the per-asset reset all go through it, so they cannot drift — the
     * old C-tap reset restored the angles and the sprite distance but NOT
     * zscale, so resetting a mesh view left it at whatever zoom you had. */
    #define VIEW_RESET() (rotY = 32, rotX = 12, zscale = 126, adist = FX(2.5))
    VIEW_RESET();
    int c_used = 0;                          /* C chord consumed a d-pad edge:
                                              * suppress the tap-release reset */
    int wire = 0;                            /* C+UP/DOWN (or Z) toggles; box fills
                                              * are cheap, so FILL is the default now
                                              * that the hero tri-mesh is gone */
    for (;;) {
        HwMdReadPad(0);
        uint16_t pad = MARS_SYS_COMM8;
        uint16_t pressed  = (uint16_t)(pad & ~prev);
        uint16_t released = (uint16_t)(prev & ~pad);
        prev = pad;
        /* Plain START exits. This was MODE+START, which no 3-button pad can
         * press at all, and the field's flaky 6-button adapters drop MODE
         * mid-hold (the pad_sticky latch exists because of them) — the
         * viewer became a room with no door. Nothing else in here reads
         * START, so the bare edge is safe. */
        if (pressed & SEGA_CTRL_START) break;
        /* CHORDED controls (Mike's scheme): B and C are HELD modifiers, so
         * every viewer function reaches a 3-button pad — X/Z still work as
         * one-tap shortcuts on pads that really have them.
         *   UP/DOWN       smooth SCALE (mesh scale / sprite distance)
         *   LEFT/RIGHT    turntable yaw
         *   B+UP/DOWN     pitch
         *   B+LEFT/RIGHT  swap model type (what X does)
         *   C+UP/DOWN     fill <-> wireframe (what Z does)
         *   C tap         reset pose — fires on RELEASE, and only if the
         *                 hold consumed no chord, so C+UP doesn't also snap
         *                 the view home.
         * Scale sits on the BARE d-pad and pitch behind B, not the other way
         * round: sizing an asset is the thing you reach for on every single
         * one, and pitch is a look you set once. */
        int chord = pad & (SEGA_CTRL_B | SEGA_CTRL_C);
        /* Table-driven: any kind with a boxmodels[] row has a mesh view,
         * and every composite (.alt) is its OWN entry in the A-cycle —
         * first-class, no hidden chords. New models never touch this.
         * Resolved BEFORE the d-pad now: bare UP/DOWN scales, and which of
         * zscale/adist that means depends on mesh_shown. */
        int mk = sel, malt = 0;
        int nvariants = raycast_asset_model(sel, &mk, &malt);
        int model_id = nvariants ? mk : -1;
        int nvar = malt ? 1 : 2;               /* composites: mesh only */
        if (variant >= nvar) variant = 0;
        int mesh_shown = (model_id >= 0 && variant == 0);
        /* Held D-pad, no chord: LEFT/RIGHT spins the turntable, UP/DOWN
         * scales. Mesh views glide the screen scale, sprite views glide the
         * world distance through the real rasterizer. */
        if (!chord) {
            if (pad & SEGA_CTRL_LEFT)  rotY = (uint8_t)(rotY - 2);
            if (pad & SEGA_CTRL_RIGHT) rotY = (uint8_t)(rotY + 2);
            if (mesh_shown) {
                if (pad & SEGA_CTRL_UP)   { zscale += 3; if (zscale > 220) zscale = 220; }
                if (pad & SEGA_CTRL_DOWN) { zscale -= 3; if (zscale < 50)  zscale = 50; }
            } else {
                if (pad & SEGA_CTRL_UP)   { adist -= FX(0.07); if (adist < FX(0.5)) adist = FX(0.5); }
                if (pad & SEGA_CTRL_DOWN) { adist += FX(0.07); if (adist > FX(8))   adist = FX(8); }
            }
        }
        if (pressed & SEGA_CTRL_A) {
            /* sprite_defs[] is kind-indexed and sparse — step over the null
             * padding rows or the viewer lands on an empty asset. */
            int n = raycast_asset_count();
            for (int t = 0; t < n; t++) {
                sel = (sel + 1) % n;
                if (raycast_asset_valid(sel)) break;
            }
            /* Every asset opens in the SAME pose. Carrying the previous
             * asset's angle and zoom across a swap meant a big model could
             * arrive off-screen or edge-on, and judging a new asset against
             * a view tuned for the last one is not a comparison. */
            VIEW_RESET();
        }
        if (pad & SEGA_CTRL_B) {
            /* Pitch, held — moved off the bare d-pad to make room for scale. */
            if (pad & SEGA_CTRL_UP)   rotX = (uint8_t)(rotX + 2);
            if (pad & SEGA_CTRL_DOWN) rotX = (uint8_t)(rotX - 2);
            if (pressed & SEGA_CTRL_RIGHT) variant = (variant + 1) % nvar;
            if (pressed & SEGA_CTRL_LEFT)  variant = (variant + 1) % nvar;
        }
        if (pressed & SEGA_CTRL_C) c_used = 0;             /* fresh hold */
        if (pad & SEGA_CTRL_C) {
            if (pressed & (SEGA_CTRL_UP | SEGA_CTRL_DOWN)) { wire ^= 1; c_used = 1; }
        }
        if ((released & SEGA_CTRL_C) && !c_used) VIEW_RESET();
        if (pressed & SEGA_CTRL_X) variant = (variant + 1) % nvar;
        if (pressed & SEGA_CTRL_Z) wire ^= 1;
        SHARED_UC->frame_count++;

        uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
        /* Clear with 32-bit stores: the 32X framebuffer IGNORES byte writes of
         * zero (hardware sprite-transparency quirk), so a fb[i]=0 byte loop
         * silently does nothing. Word writes always land — same trick as
         * raycast_clear_half. Index 0 is CRAM-painted the FLAT wallpaper
         * yellow for the viewer's lifetime (raycast_backdrop_wall): filling
         * with WALL_BASE itself strobed — the fluorescent shimmer rewrites
         * that entry every frame, invisible on textured walls, a full-screen
         * pulse on a flat fill. Index 0 nothing animates or draws. */
        {
            uint32_t *fb32 = (uint32_t *)fb;
            for (int i = 0; i < (SCREEN_W * SCREEN_H) / 4; i++) fb32[i] = 0;
        }
        if (mesh_shown)
            raycast_model_view(fb, rotY, rotX, zscale, variant, wire, model_id,
                               malt);
        else
            raycast_asset_preview(fb, sel, rotY, adist);

        /* HUD: name + dims, live rotation coordinates, controls. */
        char line[40]; int p = 0;
        const char *nm = raycast_asset_name(sel);
        while (*nm && p < 14) line[p++] = *nm++;
        line[p++] = ' ';
        int w = 0, h = 0; raycast_asset_dims(sel, &w, &h);
        if (w >= 100) line[p++] = (char)('0' + (w / 100) % 10);
        line[p++] = (char)('0' + (w / 10) % 10); line[p++] = (char)('0' + w % 10);
        line[p++] = 'X';
        if (h >= 100) line[p++] = (char)('0' + (h / 100) % 10);
        line[p++] = (char)('0' + (h / 10) % 10); line[p++] = (char)('0' + h % 10);
        line[p] = 0;
        font_draw_string(fb, 8, 8, "ASSET VIEWER", ink);
        font_draw_string(fb, 8, 22, line, ink);
        p = 0;
        line[p++]='Y'; line[p++]=':';
        line[p++]=(char)('0'+(rotY/100)%10); line[p++]=(char)('0'+(rotY/10)%10); line[p++]=(char)('0'+rotY%10);
        line[p++]=' '; line[p++]='X'; line[p++]=':';
        line[p++]=(char)('0'+(rotX/100)%10); line[p++]=(char)('0'+(rotX/10)%10); line[p++]=(char)('0'+rotX%10);
        line[p++]=' ';
        if (mesh_shown) {
            line[p++]='Z'; line[p++]=':';
            line[p++]=(char)('0'+(zscale/100)%10);
            line[p++]=(char)('0'+(zscale/10)%10);
            line[p++]=(char)('0'+zscale%10);
        } else {                              /* live distance in cells, one decimal */
            int d10 = (int)(((int64_t)adist * 10) >> FX_SHIFT);
            line[p++]='D'; line[p++]=':';
            line[p++]=(char)('0' + (d10 / 10) % 10);
            line[p++]='.';
            line[p++]=(char)('0' + d10 % 10);
        }
        line[p]=0;
        font_draw_string(fb, 8, 36, line, ink);
        if (model_id >= 0) {
            /* Two variants since the hero tri-mesh left the build: the live
             * box render (what the game draws up close) and the baked sprite. */
            static const char *const vnames[2] = { "BOXES", "SPRITE" };
            font_draw_string(fb, 8, 50, vnames[variant], ink);
            if (variant == 0)
                font_draw_string(fb, 64, 50, wire ? "WIRE" : "FILL", ink);
        }
        /* SPRITE view of a directional asset: report which baked frame the
         * bearing picker landed on, so rotating can be checked to reach every
         * one of them rather than assumed to. */
        if (!mesh_shown) {
            int nv = 0, dv = raycast_asset_dir_view(sel, rotY, &nv);
            if (nv > 0) {
                char t[16]; int q = 0;
                t[q++]='V'; t[q++]=':';
                t[q++]=(char)('0' + (dv + 1) / 10); t[q++]=(char)('0' + (dv + 1) % 10);
                t[q++]='/';
                t[q++]=(char)('0' + nv / 10); t[q++]=(char)('0' + nv % 10);
                t[q]=0;
                font_draw_string(fb, 8, 50, t, ink);
            }
        }
        font_draw_string(fb, 8, SCREEN_H - 24, "LR TURN UD SIZE  A ASSET  START BACK", ink);
        font_draw_string(fb, 8, SCREEN_H - 12, "B+UD TILT B+LR TYPE C+UD WIRE C RESET", ink);
        swapBuffers();
    }
    /* Drain the exiting START press: the caller reads its own pad edges,
     * and a still-held START on return would register there as a fresh
     * press and immediately re-open a menu over the screen we exited to. */
    do { HwMdReadPad(0); } while (MARS_SYS_COMM8 & SEGA_CTRL_START);
    raycast_backdrop_wall(0);                /* CRAM 0 back to true black */
    /* Restore the palette of the screen we RETURN to: the start menu draws
     * over the LIVE LOBBY render (raycast_render, the "stationary lobby
     * view"), so it needs the full gameplay palette back — the same one we
     * loaded on entry. box3d_load_palette() was wrong here: it re-stamps the
     * cardboard ramp across CRAM 64..79, which survives on the walls/floor/
     * ceiling (those live at 1..52) but paints the outlet (OUTLET_BASE 72..76)
     * cardboard-orange. Loading the game palette at full brightness restores
     * the outlet (and the neanderthal/partition ramps that also sit at 64+). */
    raycast_set_brightness(FADE_STEPS);
}

/* Walk-through-the-EXIT-door portal: fade to black, generate a fresh procedural
 * map, drop the player at the standard spawn, fade back up. The "way out" only
 * loops you deeper into the backrooms. Mirrors the lobby walk-through fade. */
/* Set by the exit-hole climb only: the arrival on the far side is a FALL, not
 * a cut. Doors and menu warps leave it clear and get the plain fade. */
static int g_arrive_drop = 0;
/* Seed decided at climb COMMIT, not at the fade: the destination peek rendered
 * the next map through the hole during the crawl, so the map generated at the
 * blackout must be THE SAME map. climb_commit captures it; the portal spends
 * it. Door portals never set it and keep the at-fade formula. */
static uint32_t g_next_seed = 0;
static int g_next_seed_set = 0;
/* Set when corridor_enter already generated the destination: the portal
 * must not generate it a second time. */
static int g_map_pregen = 0;
/* Climb phases, file-scope so the commit helper and the post-render shadow
 * pass can both see them: pullup beats, then the crawl across the cavity. */
#define PULLUP_FRAMES 10   /* was 16: the whole entry still dragged after the
                            * crawl halved -- ~60% faster passage overall */
#define CRAWL_FRAMES  16   /* now the CORRIDOR: three rendered cells of duct,
                            * drawn over the frame (see raycast_crawl_corridor).
                            * Cheap fills, so these frames run fast. */
static int g_pullup = 0;
static int g_crawl = 0;

static void portal_to_procgen(void) {
    g_custom_current = -1;
    /* Hole exit: NO blackout at all. The window has been showing the LIVE
     * destination the whole crawl; emerging is a straight cut from
     * tunnel-view to falling in the same scene, full brightness. Door
     * portals keep the plain dissolve. */
    if (!g_arrive_drop) {
        /* FAST dissolve (was -= 2, nine frames each way: the door dragged
         * next to the tunnel crawl's zero-blackout cut). Three steps down,
         * endpoint pinned; the fade-in below mirrors it. */
        for (int lvl = FADE_STEPS - 6; lvl > 0; lvl -= 6) fade_step(lvl);
        fade_step(0);
    }
    if (g_next_seed_set) { g_procgen_seed = g_next_seed; g_next_seed_set = 0; }
    else g_procgen_seed = SHARED_UC->frame_count * 1000003u + (uint32_t)player.x;
    if (!g_map_pregen) procgen_run(g_procgen_seed);   /* corridor pre-generated */
    g_map_pregen = 0;
    player.x = FX(16.5); player.y = FX(28.5); player.angle = 192;
    raycast_init();                 /* rebuilds full-bright palette */
    if (g_arrive_drop) {
        /* Already lit, already there: the crawl's window WAS this scene.
         * The fall plays at full brightness -- no black between the tunnel
         * and the floor. Nothing is behind you when you turn around. */
        #define DROP_FRAMES 6   /* was 12: fall at double speed (Mike) */
        g_arrive_drop = 0;
        for (int i = 1; i <= DROP_FRAMES; i++) {
            raycast_arrival_drop(i, DROP_FRAMES);
            fade_step(FADE_STEPS);
        }
        /* Left compressed on purpose: player_update's crouch-release stands
         * the player up from here, floor-glance and all. */
    } else {
        raycast_set_brightness(0);      /* held black until the fade-in */
        for (int lvl = 6; lvl < FADE_STEPS; lvl += 6) fade_step(lvl);
        fade_step(FADE_STEPS);
    }
}

/* Climb COMMIT: the seed is decided the moment the player commits -- that
 * is ALL that happens here. The old world stays whole through the pullup
 * (the back panel shows the glow: the mystery holds until you're inside). */
static void climb_commit(void) {
    g_next_seed = SHARED_UC->frame_count * 1000003u + (uint32_t)player.x;
    g_next_seed_set = 1;
    /* The dithered EXTENSION, from the moment of commitment: the corridor's
     * facets appear through the back panel via the peek dissolve, while the
     * raycaster still owns the frame. The swap later lands on a duct the
     * player has already been looking down. */
    raycast_corridor_orient();
    raycast_duct_preview();
    g_pullup = PULLUP_FRAMES;
}

/* CORRIDOR ENTER -- Mike's trigger: crossing into the wall IS the flush.
 * The corridor owns every pixel from here to the fade, so the old map's
 * last frame has already been seen; generate the destination NOW (once --
 * no restore, no byte-identical dance, no double procgen), render its
 * spawn peek for the corridor's end plane, and let the frames underneath
 * render garbage that is never visible. The corridor is not a cutscene
 * between maps; it is the ENTRANCE HALLWAY of the next one. The secondary
 * keeps its stale map cache through the corridor (its half is overdrawn
 * too); the portal's normal init path makes it coherent at arrival. */
static void corridor_enter(void) {
    /* Start the corridor shuffle loop at the COMMITMENT, not the pullup:
     * the whole passage is ~26 fast frames and the pump adds 64-128 ms
     * of buffer latency, so every frame of head start is audible. The
     * later climb triggers are no-ops against a running loop; the
     * landing scuff takes over at impact. */
    SHARED_UC->slide_sfx = 2;
    if (!g_next_seed_set) {            /* belt: gate always sets it */
        g_next_seed = SHARED_UC->frame_count * 1000003u + (uint32_t)player.x;
        g_next_seed_set = 1;
    }
    raycast_corridor_orient();         /* lit-side capture, pre-flush */
    procgen_run(g_next_seed);
    /* Full load sequence: init_lights bumps the map generation, which is
     * what makes the SECONDARY purge its cached grid -- its half of the
     * live window must render the NEW map, not ghost the old one. */
    raycast_init();
    /* Spawn literals: keep in sync with portal_to_procgen above. The live
     * camera starts up to three open cells behind the spawn and rides the
     * crawl in -- the end window shows the real place approaching. */
    raycast_corridor_travel_init(FX(16.5), FX(28.5), 192);
    g_map_pregen = 1;
}

/* Pause-menu MAPS-tab warp: the same fade/load/fade as the procgen portal, but
 * loads a hand-authored custom map by index (it sets its own spawn). Lets you
 * jump to any compiled-in map mid-session — the editor test-loop + an escape
 * hatch when the player gets stuck or is done with a map. */
static void portal_to_custom(int idx) {
    g_custom_current = idx;
    for (int lvl = FADE_STEPS - 6; lvl > 0; lvl -= 6) fade_step(lvl);
    fade_step(0);
    raycast_load_custom(idx);
    raycast_init();
    raycast_set_brightness(0);
    for (int lvl = 6; lvl < FADE_STEPS; lvl += 6) fade_step(lvl);
    fade_step(FADE_STEPS);
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

    /* The zoom-into-the-glass is the console's intended behavior, not a
     * diagnostic — SMS32X defaults ON. The TESTING row still toggles it
     * for A/B against the MD plane-B path. */
    SHARED_UC->sms_on_32x = 1;
    SHARED_UC->hum_ym = 1;      /* YM bed on; AUDIO>HUM kills it live */
    SHARED_UC->sms_epoch_on = 1;/* dirty-epoch broadcast: soak configuration
                                 * (Mike, 2026-08-13, after the ear A/B said
                                 * "way faster") — TESTING>EPOCH is the live
                                 * rollback while it earns the promotion. */

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

    /* ---- Landing: the SAME fall the exit hole delivers ---------------- *
     * You fell through the box's trap door; now the lobby arrives around
     * you MID-FALL -- identical choreography to the hole's far side, so
     * the game opens with the transition it teaches you later. The fade
     * rides the fall (picture up before you land); the landing leaves you
     * compressed, and the stand-up ease below is the same crouch-release
     * beat the game loop plays after any arrival. */
    raycast_set_brightness(0);               /* held black until the fall */
    #define INTRO_DROP 6
    for (int i = 1; i <= INTRO_DROP; i++) {
        raycast_arrival_drop(i, INTRO_DROP);
        int lvl = (i * FADE_STEPS * 3) / (INTRO_DROP * 2);  /* lit by landing */
        fade_step(lvl > FADE_STEPS ? FADE_STEPS : lvl);
    }
    raycast_exit_pullup(0, 1);               /* zero the fall-pitch channel */
    for (int e = AD_IMPACT_EYE; e < 128; ) {              /* stand up */
        /* ~60% FASTER than the first cut (>>3): up and moving, about
         * seven frames sill to standing. */
        e += ((128 - e) >> 2) + 2; if (e > 128) e = 128;
        SHARED_UC->eye_h = (uint8_t)e;
        SHARED_UC->frame_count++;
        raycast_render();
        swapBuffers();
    }
    raycast_eye_settle();   /* the stand played HERE; without this the drop's
                             * compression replayed as a second bounce on the
                             * first frame of the level the start list picks */

    /* ---- SESSION loop: lobby start list -> level -> game; the GAME tab's
     * EXIT TO LOBBY breaks the game loop and lands back here. The landing
     * cinematic above plays once per power-on only. */
    for (;;) {

    /* --- Lobby: frozen menu, then walk in ---------------------------- *
     * Phase A: the player is FROZEN at the photo vantage; only the text
     * menu is live (UP/DOWN pick the level, any button confirms and
     * dismisses the menu). Phase B: the menu is gone and the choice is
     * locked — you wander the lobby and walk forward into the backrooms
     * to enter the level you picked. */
    /* ---- Unified start list ----------------------------------------------
     * Every destination is ONE row in ONE list. COMMUNITY and STORIES are
     * FOLDING groups: their headers are selectable rows ('>' folded,
     * '|' unrolled); RIGHT unrolls, LEFT folds, confirm toggles. The list is
     * REBUILT whenever a fold changes, and the unroll is animated by a damped
     * integer spring — rows slide out from under the header (clipped until
     * they emerge) and the rows below visibly bounce as the spring settles. */
    enum { IT_MAP, IT_PROC, IT_SEP, IT_FOLD, IT_CTRL, IT_VIEW };
    struct { uint8_t kind; uint8_t map; const char *label; } items[40];
    int n_items = 0;
    /* Tier blocks (see custom_maps.h): core | curated | community. The
     * community block is empty in the flagship ROM, so its group simply never
     * appears — the same menu code serves all three builds. */
    const int n_core  = custom_core_count;
    const int n_cur   = custom_curated_count;
    const int n_comm  = custom_pick_count - custom_core_count - custom_curated_count;
    const int n_start = custom_start_count;
    const int cur0    = n_core;             /* first curated index */
    const int comm0   = n_core + n_cur;     /* first community index */

    /* Story chains (next_map links): a map someone links TO is a CHAPTER —
     * hidden from the flat list (you start a story at its head, not mid-book;
     * the pause MAPS tab still warps anywhere as the escape hatch). A map with
     * a next-link that nobody links to is a story HEAD — listed under STORIES.
     * Core maps always list normally. */
    uint8_t has_in[64] = {0};
    for (int i2 = 0; i2 < custom_pick_count; i2++) {
        int nm2 = custom_maps[i2].next_map;
        if (nm2 >= 0 && nm2 < 64) has_in[nm2] = 1;
    }

    /* 0=MAPS (curated), 1=STORIES (curated chains), 2=TEST, 3=COMMUNITY;
     * 1 = folded. COMMUNITY is deliberately its OWN group, below the game's
     * own maps: fan-made content is one clearly labelled door, never mixed
     * into the lists that make up the game proper. */
    int fold_grp[4] = { 1, 1, 1, 1 };
    int rebuild_items = 1;          /* build on first frame + after toggles */
    int refocus_grp = -1;           /* after rebuild, park cursor on this header */
    int pending_open = -1;          /* group just unfolded: arm the unroll anim */
    int anim_hdr = -1, anim_n = 0;  /* animating header index + its child count */
    int gap_cur = 0, gap_vel = 0, gap_tgt = 0, anim_closing = 0;
    int anim_grp_closing = 0;
    int anim_ticks = 0;           /* failsafe: snap the spring after ~20 frames */
    int cur = 0;

    /* Smooth pixel scroll, smartphone-style: the window eases toward keeping
     * the cursor CENTERED, clamped at the list ends — so riding back up parks
     * the -- START MAPS -- header at the top instead of hiding it, and every
     * step glides instead of jumping a row. */
    const int VIS = 7;            /* visible rows in the window */
    const int ROW_H = 14, LIST_Y = 52, LIST_H = VIS * ROW_H;
    int scroll_px = 0;
    int nav_hold = 0;             /* frames UP/DOWN held, for key-repeat */
    int do_flip = 0;              /* selection committed -> play the flip-out */
    int preview_flip = 0;         /* MODE+A: play a slowed preview this frame */
    uint32_t frame = 0;           /* time-in-lobby — entropy for procgen */
    const uint16_t LOBBY_COMMIT = SEGA_CTRL_START | SEGA_CTRL_A | SEGA_CTRL_B |
                                  SEGA_CTRL_C | SEGA_CTRL_X | SEGA_CTRL_Y | SEGA_CTRL_Z;

    /* Phase A — frozen menu over the still photo-perspective. */
    {
        uint16_t prev_pad = 0xFFFF;
        int committing = 0;       /* map/proc chosen -> capture + break this frame */
        for (;;) {
            HwMdReadPad(0);
            uint16_t pad = MARS_SYS_COMM8;
            uint16_t pressed = (uint16_t)(pad & ~prev_pad);
            prev_pad = pad;

            if (rebuild_items) {
                rebuild_items = 0;
                n_items = 0;
                items[n_items].kind = IT_SEP; items[n_items].map = 0;
                items[n_items].label = "-- START MAPS --"; n_items++;
                for (int i = 0; i < n_start && n_items < 34; i++) {
                    items[n_items].kind = IT_MAP; items[n_items].map = (uint8_t)i;
                    items[n_items].label = custom_maps[i].name; n_items++;
                }
                items[n_items].kind = IT_PROC; items[n_items].map = 0;
                items[n_items].label = "PROCEDURAL"; n_items++;
                /* Curated tier: the game's own map list (+ its story chains). */
                int any_plain = 0, any_head = 0;
                for (int i = 0; i < n_cur; i++) {
                    int mi = cur0 + i;
                    if (has_in[mi]) continue;
                    if (custom_maps[mi].next_map >= 0) any_head = 1; else any_plain = 1;
                }
                if (any_plain) {
                    items[n_items].kind = IT_FOLD; items[n_items].map = 0;
                    items[n_items].label = "-- MAPS --"; n_items++;
                    if (!fold_grp[0])
                        for (int i = 0; i < n_cur && n_items < 37; i++) {
                            int mi = cur0 + i;
                            if (has_in[mi] || custom_maps[mi].next_map >= 0) continue;
                            items[n_items].kind = IT_MAP; items[n_items].map = (uint8_t)mi;
                            items[n_items].label = custom_maps[mi].name; n_items++;
                        }
                }
                if (any_head) {
                    items[n_items].kind = IT_FOLD; items[n_items].map = 1;
                    items[n_items].label = "-- STORIES --"; n_items++;
                    if (!fold_grp[1])
                        for (int i = 0; i < n_cur && n_items < 37; i++) {
                            int mi = cur0 + i;
                            if (has_in[mi] || custom_maps[mi].next_map < 0) continue;
                            items[n_items].kind = IT_MAP; items[n_items].map = (uint8_t)mi;
                            items[n_items].label = custom_maps[mi].name; n_items++;
                        }
                }
                /* Community tier: present only in the community / author ROMs,
                 * and always behind its own header. Story heads and one-offs
                 * share the group — a fan-made chain is still fan-made. */
                if (n_comm > 0) {
                    items[n_items].kind = IT_FOLD; items[n_items].map = 3;
                    items[n_items].label = "-- COMMUNITY --"; n_items++;
                    if (!fold_grp[3])
                        for (int i = 0; i < n_comm && n_items < 37; i++) {
                            int mi = comm0 + i;
                            if (has_in[mi]) continue;      /* chapters: enter at the head */
                            items[n_items].kind = IT_MAP; items[n_items].map = (uint8_t)mi;
                            items[n_items].label = custom_maps[mi].name; n_items++;
                        }
                }
                if (n_core > n_start) {
                    items[n_items].kind = IT_FOLD; items[n_items].map = 2;
                    items[n_items].label = "-- TEST --"; n_items++;
                    if (!fold_grp[2])
                        for (int i = n_start; i < n_core && n_items < 38; i++) {
                            items[n_items].kind = IT_MAP; items[n_items].map = (uint8_t)i;
                            items[n_items].label = custom_maps[i].name; n_items++;
                        }
                }
                items[n_items].kind = IT_SEP; items[n_items].map = 0;
                items[n_items].label = ""; n_items++;   /* gap before CONTROLS */
                items[n_items].kind = IT_CTRL; items[n_items].map = 0;
                items[n_items].label = "CONTROLS"; n_items++;
                items[n_items].kind = IT_VIEW; items[n_items].map = 0;
                items[n_items].label = "ASSET VIEWER"; n_items++;

                if (refocus_grp >= 0) {
                    for (int i = 0; i < n_items; i++)
                        if (items[i].kind == IT_FOLD && items[i].map == refocus_grp) { cur = i; break; }
                    refocus_grp = -1;
                }
                if (cur >= n_items) cur = n_items - 1;
                while (cur < n_items - 1 && items[cur].kind == IT_SEP) cur++;

                if (pending_open >= 0) {    /* arm the unroll spring */
                    anim_hdr = -1;
                    for (int i = 0; i < n_items; i++)
                        if (items[i].kind == IT_FOLD && items[i].map == pending_open) { anim_hdr = i; break; }
                    anim_n = 0;
                    if (anim_hdr >= 0)
                        while (anim_hdr + 1 + anim_n < n_items &&
                               items[anim_hdr + 1 + anim_n].kind == IT_MAP) anim_n++;
                    gap_cur = 0; gap_vel = 0; gap_tgt = anim_n * ROW_H;
                    anim_closing = 0; anim_ticks = 0;
                    if (anim_n == 0) anim_hdr = -1;
                    pending_open = -1;
                }
            }

            /* UP/DOWN move the cursor through the list, hopping separators.
             * Holding a direction auto-repeats after ~a third of a second —
             * most of what makes a long list feel like flick-scrolling. */
            if (pad & (SEGA_CTRL_UP | SEGA_CTRL_DOWN)) nav_hold++; else nav_hold = 0;
            int rep = (nav_hold > 6 && (nav_hold & 1) == 0);
            if ((pressed & SEGA_CTRL_UP) || (rep && (pad & SEGA_CTRL_UP))) {
                int c = cur;
                do { c--; } while (c >= 0 && items[c].kind == IT_SEP);
                if (c >= 0) cur = c;
            }
            if ((pressed & SEGA_CTRL_DOWN) || (rep && (pad & SEGA_CTRL_DOWN))) {
                int c = cur;
                do { c++; } while (c < n_items && items[c].kind == IT_SEP);
                if (c < n_items) cur = c;
            }
            /* Folding-group headers: RIGHT unrolls, LEFT folds, confirm
             * toggles. Collapse keeps the children in the list and springs
             * the gap shut; the rebuild happens when the spring settles. */
            if (items[cur].kind == IT_FOLD && anim_hdr < 0) {
                int g = items[cur].map, open_now = -1;
                if ((pressed & SEGA_CTRL_RIGHT) && fold_grp[g])  open_now = 1;
                if ((pressed & SEGA_CTRL_LEFT)  && !fold_grp[g]) open_now = 0;
                if ((pressed & LOBBY_COMMIT) && !(pad & SEGA_CTRL_MODE))
                    open_now = fold_grp[g] ? 1 : 0;
                if (open_now == 1) {
                    fold_grp[g] = 0;
                    rebuild_items = 1; refocus_grp = g; pending_open = g;
                } else if (open_now == 0) {
                    anim_hdr = cur; anim_n = 0;
                    while (cur + 1 + anim_n < n_items &&
                           items[cur + 1 + anim_n].kind == IT_MAP) anim_n++;
                    gap_cur = anim_n * ROW_H; gap_vel = 0; gap_tgt = 0;
                    anim_closing = 1; anim_grp_closing = g; anim_ticks = 0;
                }
            } else if ((pressed & LOBBY_COMMIT) && !(pad & SEGA_CTRL_MODE)) {
                /* CONTROLS opens its sub-screen and returns here; a map row or
                 * PROCEDURAL confirms and starts. MODE-held presses are debug
                 * combos (MODE+X/Y), not commits. */
                if (items[cur].kind == IT_CTRL) {
                    show_controls_screen();
                    prev_pad = 0xFFFF;       /* swallow the still-held button */
                    continue;
                }
                if (items[cur].kind == IT_VIEW) {
                    asset_viewer_screen();
                    prev_pad = 0xFFFF;       /* swallow the still-held button */
                    continue;
                }
                if (items[cur].kind != IT_FOLD) committing = 1;
            }
            /* MODE+A: cycle the exit transform and preview it slowed,
             * right here, no rebuild — 0 hinge-up, 3 fall, 4 fly-through. */
            if ((pad & SEGA_CTRL_MODE) && (pressed & SEGA_CTRL_A)) {
                g_flip_style = (g_flip_style == 0) ? 3
                             : (g_flip_style == 3) ? 4 : 0;
                preview_flip = 1;
            }
            metrics_mode_check(pad);
            frame++;

            SHARED_UC->frame_count++;
            raycast_render();                    /* stationary lobby view */
            uint8_t *fb_text = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
            /* Smoked-glass panel first, then the text over it. Fixed bounds
             * cover title, the VIS-row window (+ overflow arrows) and the
             * hint line, so the box doesn't pump as the list scrolls. */
            lobby_menu_panel(fb_text, 48, 24, 272, 52 + 7 * 14 + 30);
            /* Title, plus which ROM this is: the flagship says nothing, a
             * community or personal build says so right under the logo so it
             * can never be mistaken for the release everyone plays. */
            font_draw_string(fb_text, (SCREEN_W - 13 * 8) / 2, 32,
                             "BACKROOMS 32X", 49);
            if (custom_build_label[0]) {
                int lbl_n = 0;
                while (custom_build_label[lbl_n]) lbl_n++;
                font_draw_string(fb_text, (SCREEN_W - lbl_n * 8) / 2, 42,
                                 custom_build_label, 49);
            }

            /* The unified list, smooth-scrolled: ease scroll_px a quarter of
             * the remaining distance per frame toward centering the cursor
             * (clamped at the ends), then draw every row at its pixel offset,
             * culling rows outside the window. */
            const int MENU_X = 96;
            {
                int max_scroll = n_items * ROW_H - LIST_H;
                if (max_scroll < 0) max_scroll = 0;
                int tgt = cur * ROW_H - (LIST_H / 2 - ROW_H / 2);
                if (tgt < 0) tgt = 0;
                if (tgt > max_scroll) tgt = max_scroll;
                int d = tgt - scroll_px;
                scroll_px += (d > 3 || d < -3) ? (d >> 2) : (d > 0) - (d < 0);

                /* Unroll spring: stiffness 3/4, damping 5/8 per frame — ONE
                 * overshoot then settle. Damping was 3/8, which claimed a
                 * single overshoot but actually rang three times: simulating
                 * the integer spring at a 3-row target gives peaks at 44 (tgt
                 * 30), then 27, then back over — a visible double-bounce that
                 * took 11 frames to settle. 5/8 overshoots exactly once and
                 * settles in 5, so it reads calmer AND snappier. (3/4 kills
                 * the bounce entirely and feels dead; 1/2 still rings twice.)
                 * When a CLOSE settles, commit the fold and rebuild without
                 * the children. */
                int disp = 0, span = 0;
                if (anim_hdr >= 0) {
                    /* Symmetric integer spring: TRUE division truncates toward
                     * zero for both signs — arithmetic >> floors negatives,
                     * which let the CLOSING spring ring in a small limit cycle
                     * that never hit the settle window and left the header
                     * deaf to input (the expand-after-collapse lockup). The
                     * tick failsafe guarantees settle even so. */
                    gap_vel += (gap_tgt - gap_cur) * 3 / 4;
                    gap_vel -= gap_vel * 5 / 8;
                    gap_cur += gap_vel;
                    span = anim_n * ROW_H;
                    anim_ticks++;
                    int settled = (gap_tgt - gap_cur < 3 && gap_cur - gap_tgt < 3 &&
                                   gap_vel < 3 && gap_vel > -3) || anim_ticks > 20;
                    if (settled) {
                        gap_cur = gap_tgt;
                        if (anim_closing) {
                            fold_grp[anim_grp_closing] = 1;
                            rebuild_items = 1; refocus_grp = anim_grp_closing;
                        }
                        anim_hdr = -1;
                    }
                    disp = span - gap_cur;
                }
                int hdr_ry = (anim_hdr >= 0)
                           ? LIST_Y + anim_hdr * ROW_H - scroll_px : 0;
                for (int i = 0; i < n_items; i++) {
                    int ry = LIST_Y + i * ROW_H - scroll_px;
                    if (anim_hdr >= 0 && i > anim_hdr) {
                        ry -= disp;                       /* rows ride the gap */
                        if (i <= anim_hdr + anim_n && ry <= hdr_ry + 4)
                            continue;                     /* still under the header */
                    }
                    if (ry < LIST_Y - 3 || ry > LIST_Y + LIST_H - 8) continue;
                    if (items[i].kind == IT_SEP)
                        font_draw_string(fb_text, MENU_X + 2 * 8, ry, items[i].label, 49);
                    else if (items[i].kind == IT_FOLD) {
                        char fl[20]; int p = 0;
                        fl[p++] = fold_grp[items[i].map] ? '>' : '|';
                        fl[p++] = ' ';
                        for (const char *q = items[i].label; *q && p < 19; q++) fl[p++] = *q;
                        fl[p] = 0;
                        if (cur == i && (SHARED_UC->frame_count % 6) < 3)
                            lobby_hl_bar(fb_text, MENU_X, ry, p * 8);
                        font_draw_string(fb_text, MENU_X, ry, fl, 49);
                    } else
                        lobby_action_row(fb_text, MENU_X, ry, cur == i, items[i].label);
                }
                /* Up-overflow arrow only — the down arrow kept reading as a
                 * stray glyph, and the smooth scroll + peeking rows already
                 * say "there's more below". */
                if (scroll_px > 0)
                    font_draw_string(fb_text, 248, LIST_Y + 2, "^", 49);
            }
            font_draw_string(fb_text, (SCREEN_W - 26 * 8) / 2, LIST_Y + LIST_H + 16,
                             "U/D: PICK   ANY BUTTON: GO", 49);

            /* PAD-type readout (debug, top-left): 6/3/? button handshake. */
            uint16_t ptype = pad & SEGA_CTRL_TYPE;
            char padline[8] = { 'P','A','D',':',' ',
                (ptype == SEGA_CTRL_SIX) ? '6' : (ptype == SEGA_CTRL_THREE) ? '3' : '?',
                0, 0 };
            font_draw_string(fb_text, 8, 8, padline, 49);
            if (g_metrics_on) { prof_sample_and_draw(fb_text); pos_draw(fb_text); }
            if (g_padtest_on) pad_test_draw(fb_text, pad);
            if (committing || preview_flip) capture_menu_pane(fb_text);
            if (committing) do_flip = 1;
            swapBuffers();
            if (preview_flip) {
                preview_flip = 0;
                menu_flip_out(g_flip_style, 12);   /* slowed so it reads */
                prev_pad = 0xFFFF;                 /* swallow the held combo */
            }
            if (committing) break;
        }
    }

    /* Phase A.5 — procedural weight tuning. Only when PROCEDURAL is chosen:
     * the player dials the generation mix (or leaves the balanced default)
     * before walking out. UP/DOWN pick a knob, LEFT/RIGHT adjust it, C resets
     * to defaults, START locks it in. Drawn over the live lobby view. */
    if (items[cur].kind == IT_PROC) {
        static const char *const labels[6] = {
            "OPENNESS    ", "PARTITIONS  ", "CRAWLSPACES ",
            "OUTLETS     ", "SPOTTED     ", "SEE-OVER    " };
        uint8_t *const wv[6] = {
            &g_procgen_params.openness,  &g_procgen_params.partitions,
            &g_procgen_params.crawlspaces, &g_procgen_params.outlets,
            &g_procgen_params.spotted,   &g_procgen_params.lowdivs };
        int row = 0;
        int committing = 0;       /* START locks in -> capture + break this frame */
        uint16_t prev_pad = 0xFFFF;
        for (;;) {
            HwMdReadPad(0);
            uint16_t pad = MARS_SYS_COMM8;
            uint16_t pressed = (uint16_t)(pad & ~prev_pad);
            prev_pad = pad;
            if ((pressed & SEGA_CTRL_UP)    && row > 0) row--;
            if ((pressed & SEGA_CTRL_DOWN)  && row < 5) row++;
            if ((pressed & SEGA_CTRL_LEFT)  && *wv[row] > 0) (*wv[row])--;
            if ((pressed & SEGA_CTRL_RIGHT) && *wv[row] < PROCGEN_MAX_W) (*wv[row])++;
            if (pressed & SEGA_CTRL_C) procgen_params_default();
            if (pressed & SEGA_CTRL_START) committing = 1;   /* lock in, generate */
            metrics_mode_check(pad);
            frame++;

            SHARED_UC->frame_count++;
            raycast_render();                          /* live lobby behind */
            uint8_t *fb_text = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
            font_draw_string(fb_text, (SCREEN_W - 15 * 8) / 2, 36,
                             "TUNE PROCEDURAL", 49);
            for (int i = 0; i < 6; i++) {
                /* "> OPENNESS    0 --|-- 4" — a slider | along a 0..MAX track,
                 * min on the left, max value on the right, so the level reads
                 * unambiguously (the old [##--] bar was unclear). */
                char line[32]; int n = 0;
                line[n++] = (i == row) ? '>' : ' ';
                line[n++] = ' ';
                for (const char *p = labels[i]; *p; p++) line[n++] = *p;
                line[n++] = '0';
                line[n++] = ' ';
                for (int b = 0; b <= PROCGEN_MAX_W; b++)
                    line[n++] = (b == *wv[i]) ? '|' : '-';
                line[n++] = ' ';
                line[n++] = (char)('0' + PROCGEN_MAX_W);
                line[n]   = 0;
                font_draw_string(fb_text, (SCREEN_W - n * 8) / 2, 64 + i * 14, line, 49);
            }
            font_draw_string(fb_text, (SCREEN_W - 23 * 8) / 2, SCREEN_H - 28,
                             "L/R ADJUST   C DEFAULTS", 49);
            font_draw_string(fb_text, (SCREEN_W - 14 * 8) / 2, SCREEN_H - 14,
                             "START GENERATE", 49);
            if (committing) { capture_menu_pane(fb_text); do_flip = 1; }
            swapBuffers();
            if (committing) break;
        }
    }

    if (do_flip)
        menu_flip_out(g_flip_style, g_flip_style == 4 ? 4 : g_flip_style == 3 ? 3 : 2);

    /* Phase B — menu dismissed, choice locked. Walk up to the black void
     * (world_map cell == 2, the dark exit doorway along the east wall) and
     * step through it. */
    {
        for (;;) {
            HwMdReadPad(0);
            uint16_t pad = MARS_SYS_COMM8;
            metrics_mode_check(pad);
            /* MODE is a combo modifier: while held, UP/DOWN drive the automap
         * zoom, so they must not also walk the player. */
        player_update((pad & SEGA_CTRL_MODE)
                      ? (pad & ~(SEGA_CTRL_UP | SEGA_CTRL_DOWN)) : pad);
            /* Exit when the player's cell sits against a black-void cell (==2),
             * any side. Robust to where on the void edge you arrive — the old
             * fixed x>7 / y<5 box missed the bottom row of the doorway. */
            int pcx = FX_INT(player.x), pcy = FX_INT(player.y);
            if ((pcx + 1 < MAP_W && world_map[pcy][pcx + 1] == 2) ||
                (pcx - 1 >= 0    && world_map[pcy][pcx - 1] == 2) ||
                (pcy + 1 < MAP_H && world_map[pcy + 1][pcx] == 2) ||
                (pcy - 1 >= 0    && world_map[pcy - 1][pcx] == 2)) break;
            SHARED_UC->frame_count++;
            raycast_render();
            uint8_t *fb_text = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
            if (g_metrics_on) { prof_sample_and_draw(fb_text); pos_draw(fb_text); }
            if (g_padtest_on) pad_test_draw(fb_text, pad);
            swapBuffers();
        }
    }

    /* Walk-through transition: fade the lobby to black, swap in the chosen
     * map behind the black, fade it up — reads as the lobby sealing off
     * and the backrooms opening ahead. These two loops predated fade_step
     * and open-coded its body — which also meant they missed the MD-layer
     * fade (HUD red burned through this transition on a CRT). Now they ARE
     * fade_step, at the fast cadence every portal uses (Mike: the map-pick
     * fade dragged; ~2x faster). */
    for (int lvl = FADE_STEPS - 6; lvl > 0; lvl -= 6) fade_step(lvl);
    fade_step(0);

    if (items[cur].kind == IT_PROC) {
        g_custom_current = -1;
        g_procgen_seed = (uint32_t)frame * 1000003u + (uint32_t)player.x;
        procgen_run(g_procgen_seed);
        player.x = FX(16.5); player.y = FX(28.5); player.angle = 192;
    } else if (custom_pick_count > 0) {
        g_custom_current = items[cur].map;
        raycast_load_custom(items[cur].map);   /* core or community; sets its own spawn */
    } else {
        g_custom_current = -1;
        raycast_load_fixed();
    }
    raycast_init();                 /* rebuilds full-bright palette... */
    raycast_set_brightness(0);      /* ...but hold black until the fade-in */

    for (int lvl = 6; lvl < FADE_STEPS; lvl += 6) fade_step(lvl);
    fade_step(FADE_STEPS);

    for (;;) {
        /* Read the joypad up-front so the menu can both react to
         * START and tell player_update to skip movement when open. */
        HwMdReadPad(0);
        uint16_t pad = MARS_SYS_COMM8;

        menu_update(pad);
        raycast_glass_sample();   /* game-on-glass: drink the COMM broadcast */
        /* YM hum service. Two jobs, both on this CPU because only it may
         * drive COMM0:
         * 1) PATCH DRIP-FEED — one register per frame, fire-and-forget.
         *    The 68K serves ~one command per vblank; the synchronous
         *    burst (case-15 op 1) both hung the menu AND landed silent
         *    (B00246), while B00245's frame-spaced stream sounded. The
         *    ~1.2s ramp-in IS the fluorescent tube striking.
         * 2) STING — the secondary's pump rolls the dice, this side keys
         *    ch1 and releases it ~2s later (RR=6 gives the tail). */
        {
            static uint16_t ym_sting_frames = 0;
            /* AUDIO>HUM row: OFF = the YM goes fully dark — bed keyed off
             * once, drip and TL writes suppressed. Live A/B for "is the
             * tick the Yamaha at all", and a mute for anyone who just
             * wants the room quiet. */
            static uint8_t hum_was_on = 0;
            if (!SHARED_UC->hum_ym) {
                if (hum_was_on) {
                    HwMdYmWrite(0x28, 0x01);   /* bed (ch2) key OFF */
                    hum_was_on = 0;
                }
            } else {
            if (!hum_was_on) { hum_was_on = 1; g_ym_upload = 1; /* re-strike */ }
            if (g_ym_upload > 0) {
                if (g_ym_upload <= (int)YM_HUM_PATCH_N) {
                    HwMdYmWrite(ym_hum_patch[g_ym_upload - 1][0],
                                ym_hum_patch[g_ym_upload - 1][1]);
                    g_ym_upload++;
                } else {
                    HwMdYmWrite(0x28, 0xF1);        /* bed on (ch2) */
                    g_ym_upload = 0;
                    g_ym_tl_dirty = 1;  /* apply the slider to fresh TLs */
                }
            }
            /* AMBIENCE -> bed level: one TL write per frame, after any
             * drip. 0.75dB/step beats the PWM path's resolution. */
            if (g_ym_tl_dirty && g_ym_upload == 0) {
                static uint8_t tl_i = 0;
                uint16_t att = (uint16_t)((255 - SHARED_UC->amb_volume) >> 2);
                uint16_t tl = ym_bed_tl_base[tl_i] + att;
                if (tl > 127) tl = 127;
                HwMdYmWrite(ym_bed_tl_reg[tl_i], (uint8_t)tl);
                if (++tl_i >= 4) { tl_i = 0; g_ym_tl_dirty = 0; }
            }
            }   /* hum_ym on-arm */
            /* ch1 NEVER keys in this design: its hum-toned patch at the
             * bed's own frequencies beat against ch2 as cancellation —
             * Mike heard the "sting" as a clipping frequency CUT. The
             * bong stays a PWM sample; an FM bell patch for ch1 is a
             * tuning-lab project. */
            (void)ym_sting_frames;
        }
        /* MAPS tab asked to warp -> fade to the chosen custom map. */
        if (g_warp_request >= 0) {
            int t = g_warp_request; g_warp_request = -1;
            portal_to_custom(t);
            continue;
        }
        /* GAME tab: 3D viewer over the paused game (self-owned screen; it
         * restores the gameplay palette on exit), or back to the lobby. */
        if (g_sms_request) {
            g_sms_request = 0;
            sms_boot_screen();
        }
        if (g_smsboot_frames && --g_smsboot_frames == 0)
            g_smsgame_request = 2;    /* beat over: the zoom takes it from here.
                                       * The PLAYER never moves — position,
                                       * angle, eye are untouched, so the
                                       * return from the session is exactly
                                       * the view that pressed A. */
        if (g_smsgame_request) {
            int from_glass = (g_smsgame_request == 2);
            g_smsgame_request = 0;
            sms_game_screen(from_glass);
            raycast_pvm_desk_off();   /* the console dies as the world returns */
        }
        if (g_viewer_request) {
            g_viewer_request = 0;
            asset_viewer_screen();
            continue;
        }
        if (g_lobby_request) {
            g_lobby_request = 0;
            break;                     /* -> session loop's lobby return */
        }
        metrics_mode_check(pad);
        if (!menu_is_active()) {
            /* EXIT-HOLE climb: input frozen while raycast_exit_pullup drives
             * the four POV beats (plant, haul, hang, shimmy) over
             * PULLUP_FRAMES rendered frames — it owns pitch, eye height AND
             * position. Then the CRAWL beat crosses the cavity toward the
             * back panel (and the destination peek on it), and only then the
             * portal fires — with the hole's walls past the frame edges.
             * These are RENDERED frames, not vblanks, so at ~11fps 21 of
             * them ran close to two seconds and the climb dragged; 16 + 10
             * lands the whole passage around two and a half. */
            if (g_pullup > 0) {
                g_pullup--;
                raycast_exit_pullup(PULLUP_FRAMES - g_pullup, PULLUP_FRAMES);
                if (g_pullup == 0) {
                    corridor_enter();      /* the FLUSH: old world ends here */
                    g_crawl = CRAWL_FRAMES;
                }
            } else if (g_crawl > 0) {
                /* CORRIDOR beat: the overlay draws the duct after the
                 * render; HERE the live camera underneath advances toward
                 * the spawn, so the end window approaches the real place. */
                g_crawl--;
                raycast_corridor_travel(CRAWL_FRAMES - g_crawl, CRAWL_FRAMES);
                if (g_crawl == 0) {
                    SHARED_UC->eye_h = 128;
                    g_arrive_drop = 1;     /* far side is a fall, not a cut */
                    portal_to_procgen();                   /* holes are procgen-only */
                    continue;
                }
            } else {
            /* MODE is a combo modifier: while held, UP/DOWN drive the automap
         * zoom, so they must not also walk the player. */
        player_update((pad & SEGA_CTRL_MODE)
                      ? (pad & ~(SEGA_CTRL_UP | SEGA_CTRL_DOWN)) : pad);
            /* Stepped into the open EXIT door. On a story map (next: set) the
             * door is the CHAPTER TRANSITION — jump to the linked map. Anywhere
             * else it falls through to the endless procgen backrooms (which is
             * also how every story ultimately ends). */
            if (raycast_door_portal_check()) {
                int nx = (g_custom_current >= 0)
                       ? custom_maps[g_custom_current].next_map : -1;
                if (nx >= 0 && nx < custom_map_count) portal_to_custom(nx);
                else                                  portal_to_procgen();
                continue;
            }
            /* At the EXIT HOLE, facing it: the climb is COMMITTED, never
             * stumbled into. From a stop, a fresh forward tap or a fresh A
             * (the interaction button) starts it. Held UP walking in gives
             * no edge, so you bump the sill and stand there; MODE and B are
             * combo modifiers (automap zoom, A+B crouch) and never commit. */
            {
                static uint16_t hole_prev = 0;
                uint16_t fresh = (uint16_t)(pad & ~hole_prev);
                hole_prev = pad;
                if (raycast_exit_hole_check()
                    && !(pad & (SEGA_CTRL_MODE | SEGA_CTRL_B))
                    && (fresh & (SEGA_CTRL_A | SEGA_CTRL_UP)))
                    climb_commit();
                /* Same interaction button, next customer: a fresh A in reach
                 * of a PVM toggles its power (screen static on/off). Gated
                 * exactly like the climb so A+B crouch and MODE combos never
                 * flip a set. */
                else if (!(pad & (SEGA_CTRL_MODE | SEGA_CTRL_B))
                         && (fresh & SEGA_CTRL_A)) {
                    /* Desk console (returns 2): the Master System boots ON
                     * THE MONITOR — chime, TEST PATTERN, the signal locking
                     * on — and after a short beat the screen takes the room.
                     * The handoff keeps the SAME Z80 running, so the music
                     * never restarts across the cut. */
                    if (raycast_pvm_use() == 2 && !g_smsboot_frames
                        && !raycast_glass_active()) {
                        sms_glass_toggle();
                        g_smsboot_frames = GLASS_BEAT_FRAMES;
                    }
                }
            }
            }
        }
        /* Tick the shared frame counter before render so both CPUs
         * read the same value when computing the distant-wall strobe. */
        SHARED_UC->frame_count++;
        raycast_render();
        /* Corridor set piece paints over the whole frame (the render above
         * still ran: it keeps both CPUs' pipeline and the audio pump fed). */
        if (g_crawl > 0)
            raycast_crawl_corridor(CRAWL_FRAMES - g_crawl, CRAWL_FRAMES);
        uint16_t t_post = prof_read_frt();          /* render done; HU starts */
        uint8_t *fb_text = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
        if (g_automap_on) automap_draw(fb_text);   /* red vectors under the text */
        menu_render(fb_text);
        if (g_metrics_on) {
            prof_sample_and_draw(fb_text);
            pos_draw(fb_text);
        }
        if (g_padtest_on) pad_test_draw(fb_text, pad);
        { uint16_t n = prof_read_frt(); prof_post_hud = (uint16_t)(n - t_post); t_post = n; }
        swapBuffers();
        { uint16_t n = prof_read_frt(); prof_post_swap = (uint16_t)(n - t_post); }

        /* ── ULTRA REST PAIR (TESTING>ULTRA) ────────────────────────────────
         * The stillness ratchet already spends stationary frames on full res;
         * this is the rung above it, using the one renderer the 32X gives us
         * for free: the VDP itself. Hold still ~1s and the loop renders a
         * TWIN of the frame on screen — identical world state, camera half a
         * column over — into the other framebuffer, then parks flipping the
         * pair at 60Hz with both SH-2s idle. CRT phosphor + eye blend the two
         * into an effective 640-wide antialiased image (on a sharp scaler it
         * reads as deliberate VHS shimmer instead). Movement never knows this
         * exists: any button/dpad bit breaks the park and the next normal
         * frame simply overwrites the pair — teardown is one loop exit.
         *
         * Arm gates: everything frame-paced must be idle. Held buttons (C
         * tilt, A+B crouch, MODE combos) keep it disarmed via the pad mask;
         * the YM patch drip-feed and TL slider are one-write-per-frame
         * streams that would stall parked; wall_halfres==0 waits out the
         * ratchet's own climb to full so the pair can't capture a half-res
         * frame. The twin shares frame A's frame_count, so the distant-wall
         * strobe and PVM static freeze identically in both buffers; the
         * fluorescent shimmer lives in CRAM, so it stays alive through the
         * park (at 60Hz — nearer a real tube than the render loop gets). */
        /* Dwell is RENDERED frames on top of the arm gates — and it was a
         * second belt on the same braces. The gates already prove the frame
         * is full-res with the dissolve settled, which costs two frames on
         * its own (f1 is_walking clears at half res, f2 reaches full but
         * fires the focus-pull); the dwell only sat on top of that. At 0 the
         * park fires on f3, the first frame the gates are actually open,
         * capturing exactly the same frame three frames sooner. The floor
         * below this is the ratchet's, not ULTRA's. */
        #define ULTRA_DWELL_FRAMES 0
        {
            static uint16_t ultra_dwell = 0;
            /* One bit per gate, published to the HUD — a silent decline is
             * indistinguishable from a merge that changed nothing. */
            uint16_t gate = 0;
            if (!SHARED_UC->ultra_enable)        gate |= 0x0001;
            if ((pad & ~SEGA_CTRL_TYPE) != 0)    gate |= 0x0002;
            if (SHARED_UC->is_walking)           gate |= 0x0004;
            if (SHARED_UC->is_turning)           gate |= 0x0008;
            if (menu_is_active())                gate |= 0x0010;
            if (g_pullup || g_crawl)             gate |= 0x0020;
            if (g_smsboot_frames || g_warp_request >= 0) gate |= 0x0040;
            if (g_ym_upload != 0 || g_ym_tl_dirty)       gate |= 0x0080;
            /* hero_dying is LATCHED, not transient: it pins at 255 for as long
             * as a felled neanderthal lies in the level, so gating on nonzero
             * disabled ULTRA for the whole map after one topple. Only the RAMP
             * needs the frame pacing (it advances per rendered frame and the
             * mixer warps the hello off it); once it tops out, parking is fine. */
            if (SHARED_UC->hero_dying && SHARED_UC->hero_dying < 255) gate |= 0x0100;
            if (SHARED_UC->wall_halfres != 0)    gate |= 0x0200;
            if (SHARED_UC->wall_dissolve != 0)   gate |= 0x0400;
            /* A powered tube is painting live noise somewhere on screen. The
             * park's whole promise is that nothing moves in a held frame, so
             * parking here turns running static into a photograph of static
             * (Mike, 2026-08-12 — the moment ULTRA first started arming).
             * Quiet rooms still supersample; rooms with a live screen don't. */
            if (SHARED_UC->pvm_static_live)      gate |= 0x0800;
            g_ultra_gate = gate;
            int ultra_ok = (gate == 0);
            if (!ultra_ok) ultra_dwell = 0;
            else if (ultra_dwell <= ULTRA_DWELL_FRAMES) {
                if (ultra_dwell++ == ULTRA_DWELL_FRAMES) {
                    /* STATIC SUPERSAMPLE (the B00288→290 arc): a 30Hz/phase
                     * temporal flip never fused on the PVM — texel fields,
                     * texture v-phase and shade bands all read as shimmer,
                     * and pinning one leak only surfaced the next. So the
                     * pair becomes ONE frame instead: render the standing
                     * frame and its half-column twin back to back (adaptive
                     * state frozen for both), then checkerboard-merge them.
                     * Agreeing pixels stay; differing pixels alternate A/B
                     * on the spatial checkerboard, which the CRT fuses the
                     * same way the flip was supposed to — statically. The
                     * park holds one motionless frame; nothing can shimmer. */
                    g_ultra_parks++;
                    SHARED_UC->ultra_twin = 1;   /* pass A: frozen state, no jitter */
                    raycast_render();
                    uint8_t *fbu = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
                    /* Stash A above _end — the hero-overlay region plus the
                     * stack-spill gap. Every tenant up there (menu pane
                     * capture, box-intro verts, hero cels) is dead during
                     * gameplay, and this runs at main-loop depth so the gap
                     * is idle too; 71,680B tops out ~66KB under the Master
                     * stack. The displayed buffer isn't CPU-readable (the FB
                     * window maps the draw side only), hence the re-render
                     * instead of a copy of what's on screen. */
                    uint32_t *ua_src = (uint32_t *)fbu;
                    uint32_t *ua_dst = (uint32_t *)(((uintptr_t)_end + 7) & ~(uintptr_t)7);
                    for (int i = 0; i < (SCREEN_W * SCREEN_H) / 4; i++)
                        ua_dst[i] = ua_src[i];
                    SHARED_UC->ultra_twin = 2;   /* pass B: frozen + half-column jitter */
                    raycast_render();
                    SHARED_UC->ultra_twin = 0;
                    /* Merge A into B, word-granular (a byte store of 0 would
                     * hit the FB's dropped-zero-byte quirk). Row parity flips
                     * which byte of a differing pair comes from which pass —
                     * a 1px checkerboard everywhere the half-column shift
                     * changed anything. */
                    {
                        const uint16_t *upa = (const uint16_t *)ua_dst;
                        uint16_t *upb = (uint16_t *)fbu;
                        int i = 0, nd = 0;
                        for (int y = 0; y < SCREEN_H; y++) {
                            int a_hi = y & 1;
                            for (int x = 0; x < SCREEN_W / 2; x++, i++) {
                                uint16_t a = upa[i], b = upb[i];
                                if (a == b) continue;
                                nd++;
                                upb[i] = a_hi ? (uint16_t)((a & 0xFF00) | (b & 0x00FF))
                                              : (uint16_t)((b & 0xFF00) | (a & 0x00FF));
                            }
                        }
                        g_ultra_diff = (uint16_t)(nd > 9999 ? 9999 : nd);
                    }
                    /* Overlays once, on the merged frame — crisp, no drift. */
                    if (g_automap_on) automap_draw(fbu);
                    if (g_metrics_on) { prof_sample_and_draw(fbu); pos_draw(fbu); }
                    if (g_padtest_on) pad_test_draw(fbu, pad);
                    swapBuffers();
                    /* PARK on the single merged frame: no flipping — just the
                     * vblank tick so the CRAM window stays honest (the
                     * fluorescent shimmer lives on) and the pad poll. Any
                     * button exits; the next loop iteration re-reads the pad
                     * and handles it (START menu, UP walks, ...) one frame
                     * late. The secondary's COMM4 idle loop keeps pumping
                     * ambience on its own the whole time. */
                    for (;;) {
                        HwMdReadPad(0);
                        if (MARS_SYS_COMM8 & ~SEGA_CTRL_TYPE) break;
                        SHARED_UC->primary_vwait = 1;
                        /* The ULTRA park is the worst-case case for a bare
                         * poll: it holds here for as long as the player stands
                         * still — unbounded — spinning on a 32X sysreg the
                         * whole time, with the secondary next door pumping
                         * audio. See BUS_WAIT above. */
                        BUS_WAIT(lastTick == MARS_SYS_COMM12);
                        SHARED_UC->primary_vwait = 0;
                        raycast_shimmer();
                        raycast_pal_flush();
                        lastTick = MARS_SYS_COMM12;
                    }
                    ultra_dwell = 0;
                }
            }
        }
    }

    /* EXIT TO LOBBY: fade the level out, level the camera (stale hold-C
     * tilt or climb pitch would survive), restore the lobby, fade up, and
     * loop back to the start list. */
    for (int lvl = FADE_STEPS; lvl >= 0; lvl -= 2) fade_step(lvl);
    raycast_exit_pullup(0, 1);        /* zero the pitch channel */
    SHARED_UC->eye_h = 128;
    SHARED_UC->pitch_y = 0;
    g_custom_current = -1;
    raycast_load_lobby();
    raycast_init();
    raycast_set_brightness(0);
    for (int lvl = 0; lvl <= FADE_STEPS; lvl += 2) fade_step(lvl);

    }   /* SESSION loop */
    return 0;
}
