/* Host harness for the dirty-epoch protocol — BOTH sides, interleaved the
 * way the hardware interleaves them: the 68K broadcaster emits one slot per
 * "call" (~20us), the SH-2 reader does ~10 register reads in that window.
 * Content script mimics the DIAGNOSTICS page that starved on hardware: one
 * 90-cell static paint epoch, then a 4-cell digit epoch every frame forever.
 * Assert: the static cells all reach the reader's tiles within a few frames.
 *
 *   cc -o /tmp/epochtest tools/test_epoch.c && /tmp/epochtest
 *
 * The register pair is modelled as plain variables; the reader samples them
 * mid-stream exactly as the SH-2 samples COMM6/COMM10. Code is transcribed
 * from md_main.c (broadcaster) and m_main.c (reader) — keep them in sync.  */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define WORDS 384
#define REPAIR 63

/* ---- the "COMM registers" ---- */
static uint16_t comm10 = 0xFFFF, comm6 = 0;

/* ---- 68K side ---- */
static uint8_t  tiles68[WORDS * 2];      /* sms_game_tiles */
static uint16_t delta_q[WORDS];
static uint16_t delta_n = 0, delta_rot = 0;
static uint16_t repair_rot = 0, post_pass_repair = 0;
static uint8_t  epoch = 0;

static void bcast_step(void) {
    uint16_t slot, tag;
    if (delta_n && post_pass_repair == 0) {
        slot = delta_q[delta_rot];
        tag  = (uint16_t)((epoch << 9) | slot);
    } else {
        slot = repair_rot;
        tag  = (uint16_t)((REPAIR << 9) | slot);
        repair_rot = (uint16_t)((repair_rot + 1 >= WORDS) ? 0 : repair_rot + 1);
        if (post_pass_repair) post_pass_repair--;
    }
    comm10 = 0xFFFF;
    comm6  = (uint16_t)(tiles68[slot * 2] | (tiles68[slot * 2 + 1] << 8));
    comm10 = tag;
    if (delta_n && (tag >> 9) != REPAIR && ++delta_rot >= delta_n) {
        delta_rot = 0;
        comm10 = (uint16_t)(0x8000 | (epoch << 9) | delta_n);
        post_pass_repair = 8;
    }
}

/* a "dirty frame": cells [base..base+n) get value v; diff vs cache */
static void dirty(int base, int n, uint8_t v) {
    uint16_t dn = 0;
    for (int w = 0; w < WORDS; w++) {
        uint8_t a = tiles68[w * 2], b = tiles68[w * 2 + 1];
        uint8_t na = a, nb = b;
        if (w >= base && w < base + n) { na = v; nb = v; }
        if (na != a || nb != b) {
            tiles68[w * 2] = na; tiles68[w * 2 + 1] = nb;
            delta_q[dn++] = (uint16_t)w;
        }
    }
    if (dn) { delta_rot = 0; delta_n = dn; epoch = (uint8_t)((epoch + 1) % REPAIR); }
}

/* ---- SH-2 side (transcribed reader) ---- */
static uint8_t  tiles32[WORDS * 2];
static uint16_t pend_idx[WORDS], pend_dat[WORDS];
static uint8_t  seen[WORDS];
static uint16_t ep_n = 0;
static uint8_t  ep_cur = 0xFF;
static int      churn = 0;

static void ep_reset(void) { memset(seen, 0, sizeof seen); ep_n = 0; ep_cur = 0xFF; }
static void apply_word(uint16_t s, uint16_t w) {
    uint8_t lo = (uint8_t)w, hi = (uint8_t)(w >> 8);
    if (tiles32[s * 2] != lo)     { tiles32[s * 2] = lo;     churn++; }
    if (tiles32[s * 2 + 1] != hi) { tiles32[s * 2 + 1] = hi; churn++; }
}

/* one reader "iteration" = the loop body; the caller interleaves */
static int reader_step(void) {          /* returns 1 on epoch apply */
    uint16_t i1 = comm10;
    uint16_t w  = comm6;
    if (comm10 != i1) return 0;
    if (i1 == 0xFFFF) return 0;
    if (i1 & 0x8000) {
        uint8_t  e = (uint8_t)((i1 >> 9) & 63);
        uint16_t n = (uint16_t)(i1 & 0x1FF);
        if (e == ep_cur && n && ep_n >= n) {
            for (uint16_t k = 0; k < ep_n; k++) apply_word(pend_idx[k], pend_dat[k]);
            ep_reset();
            return 1;
        }
        return 0;
    }
    {
        uint8_t  e = (uint8_t)((i1 >> 9) & 63);
        uint16_t s = (uint16_t)(i1 & 0x1FF);
        if (s >= WORDS) return 0;
        if (e == REPAIR) { apply_word(s, w); return 0; }
        if (e != ep_cur) {
            for (uint16_t k = 0; k < ep_n; k++) apply_word(pend_idx[k], pend_dat[k]);
            ep_reset();
            ep_cur = e;
        }
        if (!seen[s]) { seen[s] = 1; pend_idx[ep_n] = s; pend_dat[ep_n] = w; ep_n++; }
    }
    return 0;
}

int main(void) {
    int fails = 0;
    /* frame script: 68K does ~1100 bcast calls per 60Hz frame; reader does
     * ~10 iterations per call while gathering, but is AWAY painting/vblank
     * for a chunk of each frame after an apply. Model: reader present for
     * P calls after which an apply sends it away for the rest of the frame. */
    /* HONEST reader-absence model (the ghost-cell lesson): after an apply
     * the reader paints + waits vblank — away for HALF the frame's calls,
     * every frame. And the assertion is 3 frames, not 20: Mike's eyes
     * judge within the first second, and repair-crawl "eventually" is a
     * fail, not a pass. */
    dirty(0, 90, 7);                     /* the static paint epoch */
    int away = 0;
    for (int frame = 1; frame <= 3; frame++) {
        if (frame > 1) dirty(80, 4, (uint8_t)frame);   /* digits tick */
        for (int call = 0; call < 1100; call++) {
            bcast_step();
            if (away) { away--; continue; }
            for (int r = 0; r < 10; r++)
                if (reader_step()) { away = 550; break; }
        }
    }
    int missing = 0;
    for (int w = 0; w < 80; w++)         /* statics outside the digit range */
        if (tiles32[w * 2] != 7) missing++;
    printf("static cells missing after 3 frames: %d of 80\n", missing);
    if (missing) { printf("FAIL: ghosts/starvation reproduced\n"); fails++; }
    else printf("statics arrived at delta speed\n");
    return fails;
}
