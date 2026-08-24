#include "mars.h"
#include "shared.h"
#include "sound.h"
#include "raycast.h"   /* RAYCAST_PEEK_CLAIM: the peek's hero_scratch span */
#include "amb_neon.h"
#include "amb_hello_adp.h"
#include "amb_death_tape.h"
#include "amb_shuffle.h"
#include "amb_sliding.h"
#include "amb_step.h"
#include "amb_crt_on.h"
#include "amb_crt_off.h"

/* Neanderthal sprite position (matches the entry in raycast.c::standups).
 * Keeping a duplicate here on the audio side means the secondary doesn't
 * have to read the standups table — cleaner separation, and we only
 * have one Voyager-broadcasting sprite. */
#define NEANDER_X_CELL 16
#define NEANDER_Y_CELL 23

/* Distance-based hello attenuation: linear fade from full at the
 * neanderthal's cell to silence at HELLO_FADE_RADIUS cells away,
 * applied to the squared distance. */
#define HELLO_FADE_RADIUS_SQ 64   /* 8 cells */

/* PWM duty-cycle compare range. Widened from the old [2..1032] (half-
 * span 515) to [8..1430] (half-span 711) so the per-sample mix sum has
 * ~38% more headroom before clipping. At our 16kHz output rate
 * PWM_CYCLE = 1438; we use 1430 of those ticks leaving 8 ticks
 * (~0.6%) of safety margin. Keeps buzz/neon levels intact (they're
 * the constant ambient bed) while giving room for hello+step to mix
 * in without summing past the rails. */
/* The mixer's output rate. Was spelled AMB_MIX_RATE while the
 * buzz sample was the bed; the bed is the YM2612 now, the rate is ours. */
#define AMB_MIX_RATE 16000

#define SAMPLE_MIN 8
#define SAMPLE_MAX 1430
#define SAMPLE_CENTER ((SAMPLE_MAX + SAMPLE_MIN) / 2)   /* 719 */

/* Soft-clip thresholds — about 80% of the symmetric budget around
 * SAMPLE_CENTER. Above SOFT_HIGH or below SOFT_LOW the mix sum is
 * compressed 4:1 toward the rails (smooth harmonic warmth) instead
 * of hard-clipped (harsh digital crunch). The hard clip at
 * SAMPLE_MIN/MAX remains as a final backstop. */
#define SOFT_HIGH 1290   /* SAMPLE_CENTER + 571 (~80% of 711 budget) */
#define SOFT_LOW   148   /* SAMPLE_CENTER - 571 */

/* Ping-pong SDRAM buffers the secondary mixes into while DMA1 drains
 * the other one. DMA reads from cached SDRAM here; SH-2 cache is
 * write-through, so the secondary's stores reach memory before the DMA
 * accesses them.
 *
 * SIZED FOR RENDER STARVATION. amb_pump() historically ran only in the
 * secondary's idle COMM4 loop — during a CMD_HALF/CMD_TAIL render
 * chunk nothing pumped for tens of ms. The original 2×256 @16kHz gave
 * 16 ms per buffer / 32 ms total runway, LESS than one render chunk in
 * a dense scene, so DMA looped back into a stale buffer mid-chunk and
 * replayed 16 ms fragments — the audible PWM chop. The fix is two
 * halves: 1024 samples = 64 ms per buffer here, and pump checkpoints
 * BETWEEN the secondary's render passes (s_main.c) so the longest
 * pump-starved stretch is one pass (~10-30 ms), not a whole chunk.
 * Cost: 4 KB SDRAM, fill ~0.6 ms per 64 ms.
 *
 * 2048 (128 ms) would be nicer margin but does NOT fit: .bss ends
 * ~7 KB below the primary stack top (0x0603F000, mars_start.s), and
 * 2×2048×2B pushed _end past it — silently, since mars.ld's ram
 * LENGTH runs to 0x0603FC00. Check `nm -n | grep _end` against
 * 0x0603F000 before growing ANYTHING in .bss. */
#define AMB_SAMPLES_PER_BUF 1024   /* 64 ms per buffer @ 16 kHz */
#define AMB_SAMPLES_OLD      256   /* pre-fix size, kept as the A/B arm */

static uint16_t amb_pwm_buf[2][AMB_SAMPLES_PER_BUF]
                            __attribute__((aligned(16)));
static volatile uint8_t amb_current_buf_idx;
static volatile uint8_t amb_buf_needs_fill;   /* bit i = buf i needs fill */

/* Title-screen gate. The ambient pump stays fully idle (skipping its
 * fill work) until the game world loads — so the title burns no
 * secondary cycles on audio, and the PWM is free for dedicated title SFX.
 * Primary flips it on via amb_set_active(); the secondary reads it through
 * the cache-through alias for coherency. */
static uint8_t amb_active_storage = 0;
#define AMB_ACTIVE (*(volatile uint8_t *)((uintptr_t)&amb_active_storage | 0x20000000))

void amb_set_active(int on) { AMB_ACTIVE = (uint8_t)(on ? 1 : 0); }

/* Runtime buffer length — the underrun fix's same-binary A/B knob
 * (AUDIO menu tab, BUFFER row: 64MS vs 16MS). The primary toggles it;
 * the secondary's IRQ handler and pump read it. Cache-through alias
 * for coherency, same pattern as AMB_ACTIVE below. Flipping mid-stream
 * causes one transient glitch (a partially-stale buffer plays once) —
 * fine for a diagnostic knob. */
static uint16_t amb_buf_len_storage = AMB_SAMPLES_PER_BUF;
#define AMB_BUF_LEN \
    (*(volatile uint16_t *)((uintptr_t)&amb_buf_len_storage | 0x20000000))

/* Underrun counter — incremented by amb_dma_handler when it swaps into
 * a buffer the pump never got to refill (i.e. DMA is about to replay
 * stale samples: the audible chop, made countable). Primary reads it
 * for the metrics HUD (AU:). Only counted while AMB_ACTIVE — at the
 * title the pump idles by design and every swap would false-positive. */
static uint16_t amb_underruns_storage = 0;
#define AMB_UNDERRUNS \
    (*(volatile uint16_t *)((uintptr_t)&amb_underruns_storage | 0x20000000))

void     amb_toggle_buf_len(void) {
    AMB_BUF_LEN = (AMB_BUF_LEN == AMB_SAMPLES_PER_BUF)
                      ? AMB_SAMPLES_OLD : AMB_SAMPLES_PER_BUF;
}
int      amb_buf_len_is_big(void) { return AMB_BUF_LEN == AMB_SAMPLES_PER_BUF; }
uint16_t amb_get_underruns(void)  { return AMB_UNDERRUNS; }

/* Mars_InitPWM — lifted verbatim from d32xr's marshw.c. Three writes
 * to the MONO register flush the FIFO; PWM_CYCLE sets the period
 * (= SH-2 clock / sample_rate); CTRL=0x0185 enables mono output with
 * DREQ on FIFO-empty + PWM interrupt. The trailing ramp from
 * min_sample up to center slowly biases the speaker's DC voltage —
 * skipping it produces a loud pop on power-on.
 *
 * NTSC clock (23.0114 MHz); PAL would need the 22.8015 MHz variant,
 * but MiSTer ships NTSC and we haven't validated PAL yet. */
static void Mars_InitPWM(int sample_rate, int min_sample, int max_sample) {
    int centre_sample = (max_sample - min_sample) / 2;

    MARS_PWM_MONO = 1;
    MARS_PWM_MONO = 1;
    MARS_PWM_MONO = 1;

    MARS_PWM_CYCLE = (uint16_t)((((23011361 << 1) / sample_rate + 1) >> 1) + 1);

    /* CTRL = 0x0185:
     *   [1:0] TM  = 01 -> mono output
     *   [3:2] RMD = 01 -> right = pulse
     *   [5:4] LMD = 00 -> left  = same as right (mono mode)
     *   [7]   AL  = 1  -> DREQ enable on FIFO-empty
     *   [8]   --  = 1  -> PWM interrupt enable on FIFO-empty
     *   [11:8] TM-count = 1
     */
    MARS_PWM_CTRL = 0x0185;

    /* DC-bias ramp to suppress power-on pop. */
    int sample = min_sample;
    while (sample < centre_sample) {
        int reps = (sample_rate * 2) / (centre_sample - min_sample);
        for (int ix = 0; ix < reps; ix++) {
            while (MARS_PWM_MONO & 0x8000) {}   /* wait for FIFO slot */
            MARS_PWM_MONO = (uint16_t)sample;
        }
        sample++;
    }
}

/* DMA-complete handler: swaps to the OTHER ping-pong buffer and marks
 * the just-drained one as needing refill. The pump reads that flag in
 * the secondary's idle loop and refills. */
void amb_dma_handler(void) {
    /* The buffer we WERE draining is now empty — flag for refill. */
    amb_buf_needs_fill |= (uint8_t)(1 << amb_current_buf_idx);

    /* Swap to the other buffer. */
    amb_current_buf_idx ^= 1;

    /* UNDERRUN: the buffer we're about to drain still has its
     * needs-fill bit set — the pump never got scheduled between swaps,
     * so DMA will replay stale samples. This is the chop, counted.
     * Gated on AMB_ACTIVE: at the title the pump idles by design and
     * both buffers hold silence, so a swap there proves nothing. */
    if (AMB_ACTIVE && (amb_buf_needs_fill & (uint8_t)(1 << amb_current_buf_idx)))
        AMB_UNDERRUNS++;

    /* Point DMA at the new current buffer. */
    SH2_DMA_SAR1  = (uint32_t)(uintptr_t)amb_pwm_buf[amb_current_buf_idx];
    SH2_DMA_TCR1  = AMB_BUF_LEN;
    SH2_DMA_CHCR1 = 0x14E5;
}

/* CHCR1 = 0x14E5 (mono word transfer):
 *   [1:0]   = 01 -> DE=1 (enable), TE=0
 *   [2]     = 1  -> IE (interrupt enable on transfer end)
 *   [6]     = 1  -> DS (DREQ edge-triggered)
 *   [7]     = 1  -> AL (auto-request level)
 *   [11:10] = 01 -> TS = word (16-bit transfer)
 *   [13:12] = 01 -> SM = source-increment
 *   [15:14] = 00 -> DM = destination-fixed (PWM register)
 *   All other bits 0.
 */

/* Step C: pump. Called from the secondary's idle polling loop. Checks if
 * either ping-pong buffer needs a refill (set by amb_dma_handler when
 * a buffer completes draining), and if so reads AMB_SAMPLES_PER_BUF
 * samples from the ROM source into it. The read position advances
 * across the source and wraps at the end so the loop seamlessly
 * repeats. Step D adds runtime gain; step E exposes the gain knob to
 * the primary via shared memory. */
/* xorshift32 PRNG — cheap, decent quality. State seeded by something
 * non-zero so the sequence isn't degenerate. */
static uint32_t prng_state = 0xCAFEBABE;
static inline uint32_t prng_next(void) {
    prng_state ^= prng_state << 13;
    prng_state ^= prng_state >> 17;
    prng_state ^= prng_state << 5;
    return prng_state;
}

/* Neon-sting playback state. When neon_active is true, the pump mixes
 * amb_neon_samples on top of the buzz; when neon_pos reaches the end,
 * it deactivates. */
static uint32_t neon_pos = 0;
static int      neon_active = 0;

/* Voyager Golden Record "hellos in many languages" — the FULL 4:25 /
 * 55-language recording, headerless IMA ADPCM (4-bit, 6 kHz, 795 KB
 * ROM vs 1.59 MB as 8-bit PCM). The secondary decodes 160-sample
 * chunks into a PCM ring just ahead of the mixer's read cursor; decode
 * only runs while the player is inside the fade radius.
 *
 * WHY NOT SPEEX: the Speex port (sh_src/speex/, kept as reference)
 * worked and cost 3x less ROM, but one 20 ms frame took ~8 ms to
 * decode on this hardware (DT:1416 measured, B00253 — ~30 KB of hot
 * path vs the 4 KB cache, cartridge-wait-state code fetches, write-
 * through stores). Real-time needed ~40% of the secondary and its bus
 * traffic taxed the primary ~2 fps. The IMA decoder below is ~40
 * lines of table lookups that live entirely in cache: measured-class
 * cost <1%. ROM was the cheaper currency.
 *
 * The read cursor is SPLIT (integer sample counter + 16-bit fraction
 * accumulator) instead of the old single 16.16 index: 16.16 overflows
 * at sample 65,536 — the old 30 s bake silently looped only its first
 * ~11 s. The full recording is 1.59 M samples, so absolute u32 sample
 * counters do the addressing and the fraction stays in its own
 * accumulator. */
/* MEMORY OVERLAY: the PCM ring lives in a slice of box_hero's
 * hero_scratch — the 71,680 B title-intro buffer that is DEAD during
 * gameplay, exported precisely for aliasing (the exit-hole peek bitmap
 * borrows its low RAYCAST_PEEK_CLAIM bytes, live during the climb — the
 * slice starts PAST that claim. It used to start at +3 KB against the
 * peek's stale 64x44 comment while the real peek was 96x64: the decoder
 * wrote live PCM into the peek's middle rows and the crawl's end window
 * showed the Voyager broadcast as a band of confetti). The ring is live only while
 * AMB_ACTIVE, i.e. only during gameplay, so the phases are disjoint; a
 * title replay scribbles over the slice and the lazy re-init below
 * (spx_ready) rebuilds it before the next decode. Stack safety: the
 * gameplay stack spills into hero_scratch's TOP; the ring ends ~65 KB
 * below that. (The Speex arena that used to follow the ring is gone —
 * IMA decoder state is three scalars in .bss.) */
extern uint8_t hero_scratch[];               /* box_hero.c title scratch */
#define SPX_SLICE       (hero_scratch + RAYCAST_PEEK_CLAIM)
#define HELLO_RING_SAMPLES 1024              /* 2 KB, power of two */
#define HELLO_RING_MASK    (HELLO_RING_SAMPLES - 1)
static int16_t *const hello_ring = (int16_t *)(void *)SPX_SLICE;
static uint32_t hello_wr;        /* absolute samples decoded into ring */
static uint32_t hello_rd;        /* absolute samples consumed (integer) */
static uint16_t hello_rd_frac;   /* fractional-step accumulator */
/* IMA ADPCM decoder state — three scalars, plain .bss. They SURVIVE a
 * title round-trip on purpose: predictor/index/position stay valid
 * against the ROM stream (only the ring in hero_scratch gets
 * scribbled), so the broadcast resumes exactly where it left off. */
static int16_t  adp_pred;
static int8_t   adp_index;
static uint32_t adp_pos;         /* next sample index in the ROM stream */
/* Cleared whenever ambient audio is inactive; the first active idle
 * tick re-primes the (possibly title-scribbled) ring. */
static uint8_t spx_ready;

static const uint16_t ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
    41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173,
    190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289,
    16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
static const int8_t ima_index_table[16] =
    { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

/* Ring re-prime after inactivity. The ROM-stream cursor deliberately
 * keeps its place (see decoder-state comment above). */
static void spx_hello_reinit(void) {
    hello_wr = 0;
    hello_rd = 0;
    hello_rd_frac = 0;
}

/* Distance-attenuated hello volume — emitted by the neanderthal.
 * Squared-distance linear fade: full inside the cell, zero at
 * sqrt(HELLO_FADE_RADIUS_SQ) cells, square-law dropoff between. */
static int hello_dist_amp(void) {
    int player_x_cell = (int)(SHARED_UC->player.x >> 16);
    int player_y_cell = (int)(SHARED_UC->player.y >> 16);
    int dx = player_x_cell - NEANDER_X_CELL;
    int dy = player_y_cell - NEANDER_Y_CELL;
    int dist_sq = dx * dx + dy * dy;
    if (dist_sq >= HELLO_FADE_RADIUS_SQ) return 0;
    return ((HELLO_FADE_RADIUS_SQ - dist_sq) * 256) / HELLO_FADE_RADIUS_SQ;
}

/* Keep the ring this many samples ahead of the read cursor. One fill
 * consumes at most 768 (1024 output samples at the 150% trim ceiling);
 * the tick tops the ring back up between fills. */
#define HELLO_DECODE_AHEAD 768

/* Secondary FRT (same init/prescaler as s_main's profiling reads) —
 * times individual decode chunks for the HUD. */
static inline uint16_t spx_frt_read(void) {
    uint8_t hi = SH2_FRT_FRCH;
    uint8_t lo = SH2_FRT_FRCL;
    return (uint16_t)((hi << 8) | lo);
}

/* Hello decode work (IMA chunks), IDLE TIME ONLY (the secondary's
 * COMM4 wait loop — never the between-render-pass checkpoints, where a
 * decode burst pushes the next pass across the vblank edge and eats a
 * whole frame; that was the "massive CPU hit" on first hardware test).
 * Lazy init also lives here so the rebuild after a title round-trip is
 * idle work too. Costs nothing when the ring is topped up, the player
 * is out of range, or the death tape has the channel. */
void amb_audio_idle(void) {
    if (!AMB_ACTIVE) return;                  /* pump owns the reset */
    if (!spx_ready) {
        spx_hello_reinit();
        spx_ready = 1;
    }
    /* Cheapest test first: hello_wr/rd are same-CPU statics (cached),
     * so the common topped-up case exits without touching uncached
     * shared memory — this runs every idle-loop iteration.
     *
     * NO vblank/both-idle gating here. Two hardware rounds of gating
     * (B00250/51) starved the ring into silence, and the honest
     * like-for-like comparison (standing shots, decode-on vs decode-
     * off) put the ungated idle decode at ~250 ticks (~1%) — the
     * "massive hit" A/B was confounded by the toppled neanderthal
     * removing a screen-filling sprite. HELLO OFF in the AUDIO menu
     * (voice_off) is the same-binary A/B if this ever needs re-litigating. */
    if ((uint32_t)(hello_wr - hello_rd) >= HELLO_DECODE_AHEAD) return;
    if (SHARED_UC->voice_off) return;
    if (SHARED_UC->hero_dying) return;
    if (hello_dist_amp() == 0) return;

    /* Top the ring up in 160-sample chunks. IMA decode is ~15 cycles a
     * sample of cache-resident table math — a full top-up is well under
     * a millisecond, so no burst rationing needed; the cap is a
     * belt-and-suspenders bound. Nibble packing: even sample = low
     * nibble (matches tools/adpcm_bake.py; adp_pos stays even at chunk
     * boundaries because chunk size and stream length are both even). */
    int cap = 8;
    while ((uint32_t)(hello_wr - hello_rd) < HELLO_DECODE_AHEAD && cap--) {
        uint16_t t0 = spx_frt_read();
        int pred = adp_pred, idx = adp_index;
        uint32_t pos = adp_pos;
        for (int k = 0; k < 160; k++) {
            uint8_t b = amb_hello_adp_data[pos >> 1];
            int nib = (pos & 1) ? (b >> 4) : (b & 0x0F);
            int step = ima_step_table[idx];
            int delta = step >> 3;
            if (nib & 4) delta += step;
            if (nib & 2) delta += step >> 1;
            if (nib & 1) delta += step >> 2;
            pred += (nib & 8) ? -delta : delta;
            if (pred > 32767) pred = 32767;
            else if (pred < -32768) pred = -32768;
            idx += ima_index_table[nib];
            if (idx < 0) idx = 0;
            else if (idx > 88) idx = 88;
            hello_ring[(hello_wr + (uint32_t)k) & HELLO_RING_MASK] =
                (int16_t)pred;
            if (++pos >= AMB_HELLO_ADP_SAMPLE_COUNT) {
                pos = 0;         /* 4:25 elapsed — loop the recording */
                pred = 0;        /* encoder started from silence too */
                idx = 0;
            }
        }
        hello_wr += 160;
        adp_pred = (int16_t)pred;
        adp_index = (int8_t)idx;
        adp_pos = pos;
        /* Decode profiling for the HUD (see shared.h). DT was 1416 on
         * Speex; the whole point of this decoder is that number. */
        SHARED_UC->spx_dec_ticks = (uint16_t)(spx_frt_read() - t0);
        SHARED_UC->spx_dec_count++;
    }
}

#define HELLO_STEP_FX \
    ((uint32_t)(((uint64_t)AMB_HELLO_ADP_SAMPLE_RATE << 16) / AMB_MIX_RATE))

/* Broken-tape death effect needs reverse + variable-rate scrubbing,
 * which a compressed stream can't do — so it warps this small 4 s
 * 8-bit PCM excerpt (24 KB ROM) instead, through the same phase A/B/C
 * math as always. Position is 16.16 (24 K samples fits comfortably). */
static uint32_t death_pos_fx = 0;
static int      death_was_active = 0;
#define DEATH_STEP_FX \
    ((uint32_t)(((uint64_t)AMB_DEATH_TAPE_SAMPLE_RATE << 16) / AMB_MIX_RATE))

/* Voyager-hello playback-speed trim. On real hardware the hellos drag
 * slightly slower than in Ares while the buzz bed sounds right, so this
 * scales ONLY the hello's fractional read step: the voice speeds up and
 * rises in pitch, buzz/neon/footsteps are untouched. Baseline
 * HELLO_STEP_FX = 100%. Primary adjusts it from the AUDIO menu (VOICE
 * row); the secondary snapshots it once per buffer fill. Once the value
 * that matches Ares on hardware is found, bake it as the default step
 * and drop the knob. Cache-through alias so the two CPUs stay coherent. */
static uint32_t hello_step_storage = HELLO_STEP_FX;
#define HELLO_STEP (*(volatile uint32_t *)((uintptr_t)&hello_step_storage | 0x20000000))
#define HELLO_STEP_PCT_STEP ((HELLO_STEP_FX + 50) / 100)   /* ~1% per menu press */
#define HELLO_STEP_MIN      ((HELLO_STEP_FX * 90)  / 100)  /* 90%  floor */
#define HELLO_STEP_MAX      ((HELLO_STEP_FX * 150) / 100)  /* 150% ceiling */

/* Menu hooks (called on the PRIMARY). dir = -1/+1 steps the voice speed
 * by ~1%; the pct query drives the "VOICE 1xx%" readout. */
void amb_voice_speed_adjust(int dir) {
    int32_t s = (int32_t)HELLO_STEP + dir * (int32_t)HELLO_STEP_PCT_STEP;
    if (s < (int32_t)HELLO_STEP_MIN) s = HELLO_STEP_MIN;
    if (s > (int32_t)HELLO_STEP_MAX) s = HELLO_STEP_MAX;
    HELLO_STEP = (uint32_t)s;
}
int amb_voice_speed_pct(void) {
    return (int)((HELLO_STEP * 100 + (HELLO_STEP_FX / 2)) / HELLO_STEP_FX);
}

/* Carpet footstep — 3 s of continuous walking sounds, plays/loops only
 * while SHARED_UC->is_walking is set. Same 8-bit s8 / fractional-rate
 * scheme as the hello. When walking stops, step_pos_fx freezes so the
 * loop resumes mid-stride on the next walking interval instead of
 * restarting from the beginning (sounds more natural). */
static uint32_t step_pos_fx = 0;

/* Exit-passage shuffle (Mike's shuffle.wav, 0.51 s) — one sample, two
 * behaviors, replacing the old cardboard slide+bang. The primary
 * requests via SHARED_UC->slide_sfx:
 *   1 = landing scuff: ONE-SHOT at native rate when the player drops
 *       into a room. Always takes over (stops a running loop — the
 *       climb ends by falling, so this doubles as the loop's off).
 *   2 = corridor shuffle: LOOP at 30% speed (~1.7 s per pass) while
 *       climbing/scraping through the exit passage. Re-requests while
 *       already looping are ignored so the later scrape triggers don't
 *       click the loop back to its start. */
/* SPLIT cursor (integer sample + 16-bit fraction), NOT a single 16.16:
 * 86,031 samples overflows a 16.16 index at 65,536 — the same trap the
 * 30 s hello shipped with (it looped its first ~11 s for weeks). */
static uint32_t slide_pos = 0;      /* integer sample index */
static uint16_t slide_frac = 0;     /* fractional-step accumulator */
static uint32_t slide_step_fx = 0;  /* 16.16 per-output-sample advance */
static int      slide_active = 0;
static int      slide_loop = 0;     /* mode 2: wrap at the end instead of stopping */
#define SHUFFLE_STEP_FX \
    ((uint32_t)(((uint64_t)AMB_SHUFFLE_SAMPLE_RATE << 16) / AMB_MIX_RATE))
#define SLIDING_STEP_FX \
    ((uint32_t)(((uint64_t)AMB_SLIDING_SAMPLE_RATE << 16) / AMB_MIX_RATE))

/* Crawl-move drag (amb_sliding, native rate): loops while the player
 * moves CROUCHED (crawlspaces), replacing the carpet footsteps — knees
 * and fabric, not soles on carpet. Freezes in place when movement
 * stops and resumes mid-drag, same behavior the footsteps use. */
static uint32_t crawl_pos = 0;
static uint16_t crawl_frac = 0;
static int      crt_active = 0, crt_rev = 0;
static uint32_t crt_pos_fx = 0;
#define STEP_STEP_FX \
    ((uint32_t)(((uint64_t)AMB_STEP_SAMPLE_RATE << 16) / AMB_MIX_RATE))

/* Buzz envelope state for the "fluorescent flicker" effect. Most of
 * the time amp = 256 (full); occasionally the envelope dips to ~12%,
 * holds, then rises back, simulating the dim-and-restore behavior of
 * a failing ballast. Phase state machine:
 *   0 = steady   (full amp, random chance to trigger phase 1)
 *   1 = fading down
 *   2 = hold low
 *   3 = fading up
 */

void amb_pump(void) {
    if (!AMB_ACTIVE) {
        /* Title owns hero_scratch (our decoder's home): mark the state
         * torn down and the ring empty so nothing reads the scribbled
         * slice. amb_audio_idle rebuilds on the next active idle. */
        spx_ready = 0;
        hello_wr = hello_rd = 0;
        return;          /* title: silent, zero fill cost */
    }
    uint8_t needs = amb_buf_needs_fill;
    if (needs == 0) return;

    int buf_idx = (needs & 1) ? 0 : 1;
    /* Snapshot shared state once per buffer: (a) the primary's mid-fill
     * writes can't produce a discontinuity within a buffer, and (b) the
     * per-sample loop stays free of ~12-cycle uncached SDRAM reads —
     * at 1024 samples per fill those reads were about to become the
     * fill's dominant cost. Walking latency doesn't suffer: the buffer
     * plays 64-128 ms after it's mixed regardless, so per-sample
     * re-reads of is_walking bought nothing. */
    int vol       = (int)SHARED_UC->amb_volume;
    int walking   = (int)SHARED_UC->is_walking;
    int step_vol  = (int)SHARED_UC->step_volume;
    int len       = (int)AMB_BUF_LEN;
    uint32_t hstep = HELLO_STEP;   /* voice-speed trim, snapshot per fill */
    /* Footstep cadence: 1.5x the read step when sprinting so the carpet
     * steps quicken to match the run (STEP_STEP_FX + half). Snapshot once
     * per fill — the state only needs to be fresh per buffer, not per sample. */
    uint32_t step_adv = ((int)SHARED_UC->is_running)
                        ? (STEP_STEP_FX + (STEP_STEP_FX >> 1)) : STEP_STEP_FX;
    /* Crouched movement swaps footsteps for the sliding drag. eye_h is
     * uncached — read once per fill. 80 ≈ committed crouch (128 stand,
     * 40 full crawl); the exit-drop's 56 impact can't false-positive
     * because input is frozen there (walking = 0). */
    int crawling = walking && ((int)SHARED_UC->eye_h <= 80);

    /* Exit-passage SFX request from the primary. Mode picks sample AND
     * behavior — two different sounds sharing one voice:
     * 1 = landing scuff (amb_shuffle), ONE-SHOT, always takes over,
     * 2 = corridor loop (amb_sliding, Mike's dedicated corridor sound,
     *     native rate); no-op if already looping. */
    {
        int req = (int)SHARED_UC->slide_sfx;
        if (req) {
            SHARED_UC->slide_sfx = 0;
            if (!(req == 2 && slide_active && slide_loop)) {
                slide_active = 1;
                slide_loop = (req == 2);
                slide_pos = 0;
                slide_frac = 0;
                slide_step_fx = (req == 2) ? SLIDING_STEP_FX
                                           : SHUFFLE_STEP_FX;
            }
        }
    }

    /* CRT power one-shot from the primary (PVM A toggle). Two real bakes
     * now (the reversed-clip trick bugged Mike since day one): power-on =
     * the degauss THUNK, power-off = its own short clip. A new request
     * retriggers from the top — mashing A restarts the click, which is
     * what a real switch does. */
    if (SHARED_UC->crt_sfx) {
        crt_rev = (SHARED_UC->crt_sfx == 2);
        SHARED_UC->crt_sfx = 0;
        crt_active = 1;
        crt_pos_fx = 0;
    }

    /* Neon sting trigger — rare, 1/512 ≈ avg 12 s. The sting stays the
     * PWM sample in BOTH hum modes: the recording is a struck-tube BONG
     * the current FM patch can't imitate (Mike missed it immediately).
     * YM mode owns only the bed; an FM bell patch for the sting is a
     * tuning-lab project, not a default. */
    if (!neon_active && (prng_next() & 0x1FF) == 0) {
        neon_active = 1;
        neon_pos = 0;
    }

    /* (Buzz envelope machine retired with the sample: the bed is the
     * YM2612 now — its LFO does the wavering. A future ambience event
     * can dip the bed via YM TL writes from the primary.) */

    int hello_amp = hello_dist_amp();

    /* Broken-tape death of the hello (SHARED_UC->hero_dying, 0 = alive). Three
     * phases warp the sample: A) overload speed-up (forward), B) the motor
     * gives out and it plays in REVERSE, C) a slow drift + fade to silence. Bit-
     * crush grows throughout for the lo-fi/mechanical grit. Snapshot per fill;
     * hero_dying ramps over ~2.5s so the 64 ms step is smooth.
     *
     * The warp scrubs the dedicated 4 s PCM excerpt (amb_death_tape) —
     * reverse and rate games are impossible on the compressed Speex
     * stream, which simply freezes in place while dying. */
    int hd = (int)SHARED_UC->hero_dying;
    int32_t dstep_eff = (int32_t)DEATH_STEP_FX;   /* signed: negative = reverse */
    int death_amp = 256, crush = 0;
    if (hd) {
        if (!death_was_active) {          /* rising edge: tape starts fresh */
            death_was_active = 1;
            death_pos_fx = 0;
        }
        if (hd < 85) {                 /* A: surge 1x -> ~2.6x forward */
            dstep_eff = (int32_t)((DEATH_STEP_FX * (256u + (uint32_t)hd * 5u)) >> 8);
            crush = hd >> 6;                                  /* 0..1 */
        } else if (hd < 190) {         /* B: reverse ~1.5x, warbling lo-fi */
            dstep_eff = -(int32_t)((DEATH_STEP_FX * 3u) >> 1);
            crush = 1 + ((hd - 85) >> 6);                     /* 1..2 */
        } else {                       /* C: slow drift, fade to nothing */
            dstep_eff = (int32_t)(DEATH_STEP_FX >> 2);
            death_amp = 256 - (hd - 190) * 256 / 65;
            if (death_amp < 0) death_amp = 0;
            crush = 3;
        }
    } else {
        death_was_active = 0;
    }

    /* NO Speex decode here. Decoding lives exclusively in
     * amb_audio_idle() (the secondary's COMM4 wait loop): this function
     * runs at the between-render-pass checkpoints, where a decode burst
     * pushes the next pass across the vblank edge — measured on
     * hardware as a massive fps hit near the neanderthal. If the ring
     * runs dry the mixer below freezes the hello mid-word and resumes
     * when idle decode catches up: a radio dropout, not a frame drop. */

    for (int i = 0; i < len; i++) {
        /* The bed is the YM2612 (drip-fed patch, m_main.c) — the PWM mix
         * carries only the events on top: neon bong, hello, steps. */
        int delta = 0;

        /* Neon sting at quarter amplitude (was >>1) — at >>1 it dominated
         * the mix and blew through the soft-clip headroom; at >>2 it sits
         * in line with buzz/hello/step. 8-bit bake, normalized; RESCALE
         * reconstructs the original ±317 PWM-word delta. */
        if (neon_active) {
            int neon = ((int)amb_neon_samples[neon_pos] * AMB_NEON_RESCALE) >> 7;
            delta += neon >> 1;   /* half amplitude: doubled 2026-08-10 —
                                    * the PWM bed is gone, headroom freed */
            neon_pos++;
            if (neon_pos >= AMB_NEON_SAMPLE_COUNT) neon_active = 0;
        }

        /* Hello, volume by distance to neanderthal. Alive: 16-bit ring
         * samples scale to the same centered ~10-bit mix range the old
         * 8-bit <<2 landed on (>>6), at unity >>8 — hello carries the
         * voyager voice and audibility matters more than peak budgeting.
         * Worst-case overlap (close + walking + neon) goes through the
         * soft-clipper. Dying: the death tape scrubs instead, signed
         * advance for phase-B reverse, clamp at 0 on underflow. */
        if (hd) {
            if (hello_amp > 0 && death_amp > 0) {
                int s = (int)amb_death_tape_samples[death_pos_fx >> 16];
                if (crush) s = (s >> crush) << crush;   /* lo-fi quantize (tape grit) */
                int hello = s << 2;
                delta += (((hello * hello_amp) >> 8) * death_amp) >> 8;
            }
            if (dstep_eff >= 0) {
                death_pos_fx += (uint32_t)dstep_eff;
                if ((death_pos_fx >> 16) >= AMB_DEATH_TAPE_SAMPLE_COUNT)
                    death_pos_fx = 0;
            } else {
                uint32_t dec = (uint32_t)(-dstep_eff);
                death_pos_fx = (death_pos_fx > dec) ? (death_pos_fx - dec) : 0;
            }
        } else if (hello_amp > 0 && (uint32_t)(hello_wr - hello_rd) > 0) {
            int hello = (int)hello_ring[hello_rd & HELLO_RING_MASK] >> 6;
            delta += (hello * hello_amp) >> 8;
            /* Split-cursor advance: fraction accumulates, integer sample
             * counter carries — no 16.16 overflow at 65 K samples. */
            uint32_t adv = (uint32_t)hello_rd_frac + hstep;
            hello_rd      += adv >> 16;
            hello_rd_frac  = (uint16_t)adv;
        }

        /* Carpet footstep — bypasses primary `vol` (amb_volume) below.
         * >>9 (was >>8) halves its contribution so it shares the
         * budget evenly with buzz/neon/hello. Source baked 11kHz/8-bit. */
        int step_delta = 0;
        if (crawling) {
            /* Crouched + moving: the sliding drag replaces the carpet,
             * footstep loudness class (it's locomotion, not an event). */
            int sd = (int)amb_sliding_samples[crawl_pos] << 2;
            step_delta = (sd * step_vol) >> 8;   /* 2x per Mike's ears (was >>9) */
            uint32_t adv = (uint32_t)crawl_frac + SLIDING_STEP_FX;
            crawl_pos  += adv >> 16;
            crawl_frac  = (uint16_t)adv;
            if (crawl_pos >= AMB_SLIDING_SAMPLE_COUNT) crawl_pos = 0;
        } else if (walking) {
            uint32_t step_idx = step_pos_fx >> 16;
            int step = ((int)amb_step_samples[step_idx] * AMB_STEP_RESCALE) >> 7;
            step_delta = (step * step_vol) >> 9;
            step_pos_fx += step_adv;
            if ((step_pos_fx >> 16) >= AMB_STEP_SAMPLE_COUNT) step_pos_fx = 0;
        }
        /* Exit-passage voice — 8-bit sources, << 2 to the mix scale, a
         * step louder than the carpet: it's the event. Landing plays
         * amb_shuffle once; the corridor loops amb_sliding until the
         * landing takes the voice over. */
        if (slide_active) {
            const int8_t *ssrc = slide_loop ? amb_sliding_samples
                                            : amb_shuffle_samples;
            uint32_t slim = slide_loop ? AMB_SLIDING_SAMPLE_COUNT
                                       : AMB_SHUFFLE_SAMPLE_COUNT;
            int sl = (int)ssrc[slide_pos] << 2;
            /* Corridor sliding loop 2x per Mike's ears; landing stays put. */
            step_delta += (sl * step_vol) >> (slide_loop ? 7 : 8);
            uint32_t adv = (uint32_t)slide_frac + slide_step_fx;
            slide_pos  += adv >> 16;
            slide_frac  = (uint16_t)adv;
            if (slide_pos >= slim) {
                if (slide_loop) slide_pos = 0;   /* corridor: seamless wrap */
                else            slide_active = 0;
            }
        }
        /* CRT power one-shot — same 8-bit path as the slide, same loudness
         * (it's the event you just caused, inches from your hand). */
        if (crt_active) {
            uint32_t ci = crt_pos_fx >> 16;
            uint32_t n  = crt_rev ? AMB_CRT_OFF_SAMPLE_COUNT
                                  : AMB_CRT_ON_SAMPLE_COUNT;
            int cv = crt_rev ? (int)amb_crt_off_samples[ci] << 2
                             : (int)amb_crt_on_samples[ci] << 2;
            step_delta += (cv * step_vol) >> 8;
            crt_pos_fx += ((uint32_t)AMB_CRT_ON_SAMPLE_RATE << 16)
                          / AMB_MIX_RATE;
            if ((crt_pos_fx >> 16) >= n) crt_active = 0;
        }

        /* Overall gain on ambient sources; footstep added post-gain. */
        int s = ((delta * vol) >> 7) + step_delta + SAMPLE_CENTER;

        /* Soft clip — piecewise linear 4:1 compression above/below
         * the SOFT_HIGH/SOFT_LOW thresholds (about 80% of the budget
         * each way), with the hard clip as a backstop for catastrophic
         * peaks. Sounds like tube saturation instead of harsh digital
         * clipping when the mix sum overshoots. */
        if (s > SOFT_HIGH) s = SOFT_HIGH + ((s - SOFT_HIGH) >> 2);
        if (s < SOFT_LOW)  s = SOFT_LOW  - ((SOFT_LOW  - s) >> 2);
        if (s > SAMPLE_MAX) s = SAMPLE_MAX;
        if (s < SAMPLE_MIN) s = SAMPLE_MIN;
        amb_pwm_buf[buf_idx][i] = (uint16_t)s;

    }

    /* Clear only OUR bit, against a FRESH read — not the `needs` value
     * snapshotted before the fill. The DMA IRQ can set the other
     * buffer's bit during the ~0.6 ms fill; storing the stale snapshot
     * would erase that request for a whole swap cycle. Re-reading here
     * shrinks the race window from the full fill to a few instructions. */
    amb_buf_needs_fill = (uint8_t)(amb_buf_needs_fill & ~(1 << buf_idx));
}

void amb_sound_init(void) {
    /* Default runtime audio volumes — unity gain for ambient, the
     * same half-amp baseline for footsteps that we used before.
     * Set here on the secondary at boot for robustness against whether
     * crt0 actually copies .data from ROM to SDRAM at startup. */
    SHARED_UC->amb_volume  = 128;
    SHARED_UC->step_volume = 140;   /* 25% above the 11kHz/16-bit re-bake baseline */
    SHARED_UC->voice_off   = 0;     /* hello decode on until the menu says otherwise */
    AMB_ACTIVE = 0;                 /* gated silent until the game starts */

    /* Initialize the ping-pong state and prefill both buffers FULLY
     * with silence (DC center) — the whole array, not just AMB_BUF_LEN,
     * so an A/B toggle to the long buffer never drains uninitialized
     * SDRAM. Runtime knobs re-initialized here (like the volumes above)
     * for the same crt0 .data-copy robustness. */
    amb_current_buf_idx = 0;
    amb_buf_needs_fill  = 0;
    AMB_BUF_LEN   = AMB_SAMPLES_PER_BUF;
    AMB_UNDERRUNS = 0;
    HELLO_STEP    = HELLO_STEP_FX;   /* voice speed 100% until tuned */

    /* Hello ring priming is deliberately NOT here: at boot the title
     * intro owns hero_scratch (the ring's home — see the overlay block
     * above), so the ring re-primes lazily in amb_audio_idle on the
     * first active tick, and again after every title round-trip. */
    spx_ready = 0;
    adp_pred = 0;
    adp_index = 0;
    adp_pos = 0;
    for (int b = 0; b < 2; b++)
        for (int i = 0; i < AMB_SAMPLES_PER_BUF; i++)
            amb_pwm_buf[b][i] = SAMPLE_CENTER;

    Mars_InitPWM(AMB_MIX_RATE, SAMPLE_MIN, SAMPLE_MAX);

    /* DMA destination = PWM mono register, fixed. */
    SH2_DMA_DAR1  = (uint32_t)(uintptr_t)&MARS_PWM_MONO;
    SH2_DMA_DRCR1 = 0;                /* external DREQ source = PWM */
    SH2_DMA_DMAOR = 1;                /* DMAOR.DME — enable the DMAC */

    /* SH-2 IPRA layout (SH7095): [15:12]=DIVU, [11:8]=DMAC, [7:4]=WDT,
     * [3:0]=REF. Setting DMA priority to 4 — secondary's SR mask is 2, so
     * priority 4 is high enough to be taken. */
    SH2_INT_IPRA = (SH2_INT_IPRA & 0xF0FF) | 0x0400;

    /* DMA1 uses a USER-DEFINED interrupt vector. VCR1 holds the vector
     * number; SH-2 reads VBR[VCR1*4] to get the handler. We point it
     * at vector slot 66 = "Level 4 & 5" entry in the secondary vector
     * table (mars_start.s line 203), which already holds slav_irq.
     * slav_irq's dispatch chain has the new `cmp/eq #0x10` branch to
     * slav_dma_irq for level-4 source = DMA. */
    SH2_DMA_VCR1 = 66;

    /* Fire the first transfer. Both ping-pong buffers start at silence
     * (DC center), so the first ~128 ms is silent until the pump gets
     * going — inaudible at boot; the game gates audio on amb_set_active
     * anyway. After that, completions are handled by the IRQ →
     * amb_dma_handler swap chain. */
    SH2_DMA_SAR1  = (uint32_t)(uintptr_t)amb_pwm_buf[0];
    SH2_DMA_TCR1  = AMB_BUF_LEN;
    SH2_DMA_CHCR1 = 0x14E5;
}
