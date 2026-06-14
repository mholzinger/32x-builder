#include "mars.h"
#include "shared.h"
#include "sound.h"
#include "amb_buzz.h"
#include "amb_neon.h"
#include "amb_hello.h"
#include "amb_step.h"

/* Neanderthal sprite position (matches the entry in raycast.c::standups).
 * Keeping a duplicate here on the audio side means the slave doesn't
 * have to read the standups table — cleaner separation, and we only
 * have one Voyager-broadcasting sprite. */
#define NEANDER_X_CELL 16
#define NEANDER_Y_CELL 23

/* Distance-based hello attenuation: linear fade from full at the
 * neanderthal's cell to silence at HELLO_FADE_RADIUS cells away,
 * applied to the squared distance. */
#define HELLO_FADE_RADIUS_SQ 64   /* 8 cells */

/* PWM duty-cycle compare range — d32xr's standard 10-bit-in-16-bit
 * envelope. The DC center is (MAX+MIN)/2 = 517. Speaker at silence
 * holds this value. */
#define SAMPLE_MIN 2
#define SAMPLE_MAX 1032
#define SAMPLE_CENTER ((SAMPLE_MAX + SAMPLE_MIN) / 2)

/* Step A of the slave-fills-buffer refactor — allocate ping-pong
 * SDRAM buffers that the slave will write into (with optional gain /
 * eventual synth math) while DMA1 drains the other one. DMA reads
 * from cached SDRAM here; SH-2 cache is write-through, so the slave's
 * stores reach memory before the DMA accesses them.
 *
 * 256 samples per buffer at 11025 Hz = ~23 ms per drain — gives the
 * slave 23 ms of headroom between buffer swaps to fill the next one,
 * vs. the actual fill cost of ~150 μs. Plenty of slack even when the
 * slave is doing ceiling+carpet+walls work in foreground.
 *
 * Wired up in Step B (amb_dma_handler swap) and Step C (amb_pump
 * fill loop). For now they're just allocated and initialized to
 * silence so the initial frames after boot don't pop. */
#define AMB_SAMPLES_PER_BUF 256

static uint16_t amb_pwm_buf[2][AMB_SAMPLES_PER_BUF]
                            __attribute__((aligned(16)));
static volatile uint8_t amb_current_buf_idx;
static volatile uint8_t amb_buf_needs_fill;   /* bit i = buf i needs fill */

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

/* Step B: handler swaps to the OTHER ping-pong buffer and marks the
 * just-drained one as needing refill. The pump (step C) reads that
 * flag in the slave's idle loop and refills. While the pump isn't
 * wired in yet, audio is silence after the first buffer pass — the
 * structural plumbing has to land first. */
void amb_dma_handler(void) {
    /* The buffer we WERE draining is now empty — flag for refill. */
    amb_buf_needs_fill |= (uint8_t)(1 << amb_current_buf_idx);

    /* Swap to the other buffer. */
    amb_current_buf_idx ^= 1;

    /* Point DMA at the new current buffer. */
    SH2_DMA_SAR1  = (uint32_t)amb_pwm_buf[amb_current_buf_idx];
    SH2_DMA_TCR1  = AMB_SAMPLES_PER_BUF;
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

/* Step C: pump. Called from the slave's idle polling loop. Checks if
 * either ping-pong buffer needs a refill (set by amb_dma_handler when
 * a buffer completes draining), and if so reads AMB_SAMPLES_PER_BUF
 * samples from the ROM source into it. The read position advances
 * across the source and wraps at the end so the loop seamlessly
 * repeats. Step D adds runtime gain; step E exposes the gain knob to
 * the master via shared memory. */
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

/* Voyager Golden Record "hellos in many languages" — loops continuously
 * but its mix amplitude is scaled by distance from the neanderthal
 * sprite. Out beyond HELLO_FADE_RADIUS_SQ cells, contributes zero.
 *
 * Stored as int8_t at AMB_HELLO_SAMPLE_RATE (4000 Hz) — 82% smaller
 * than 16-bit at 11025 Hz, with a "lo-fi radio" character that fits
 * the Backrooms aesthetic. Played back at the buzz's 11025 Hz rate,
 * so hello_pos is a 16.16 fixed-point index that advances by
 * HELLO_STEP_FX per output sample. */
static uint32_t hello_pos_fx = 0;
#define HELLO_STEP_FX \
    ((uint32_t)(((uint64_t)AMB_HELLO_SAMPLE_RATE << 16) / AMB_BUZZ_SAMPLE_RATE))

/* Carpet footstep — 3 s of continuous walking sounds, plays/loops only
 * while SHARED_UC->is_walking is set. Same 8-bit s8 / fractional-rate
 * scheme as the hello. When walking stops, step_pos_fx freezes so the
 * loop resumes mid-stride on the next walking interval instead of
 * restarting from the beginning (sounds more natural). */
static uint32_t step_pos_fx = 0;
#define STEP_STEP_FX \
    ((uint32_t)(((uint64_t)AMB_STEP_SAMPLE_RATE << 16) / AMB_BUZZ_SAMPLE_RATE))

/* Buzz envelope state for the "fluorescent flicker" effect. Most of
 * the time amp = 256 (full); occasionally the envelope dips to ~12%,
 * holds, then rises back, simulating the dim-and-restore behavior of
 * a failing ballast. Phase state machine:
 *   0 = steady   (full amp, random chance to trigger phase 1)
 *   1 = fading down
 *   2 = hold low
 *   3 = fading up
 */
static int buzz_env_amp   = 256;
static int buzz_env_phase = 0;
static int buzz_env_timer = 0;

void amb_pump(void) {
    uint8_t needs = amb_buf_needs_fill;
    if (needs == 0) return;

    int buf_idx = (needs & 1) ? 0 : 1;
    /* Snapshot the volume once per buffer to avoid the master's
     * mid-fill writes producing a discontinuity within a buffer. */
    int vol = (int)SHARED_UC->amb_volume;

    /* Neon sting trigger — rare, 1/512 ≈ avg 12 s. */
    if (!neon_active && (prng_next() & 0x1FF) == 0) {
        neon_active = 1;
        neon_pos = 0;
    }

    /* Buzz envelope state machine — random fade-out events. */
    switch (buzz_env_phase) {
    case 0:  /* steady full */
        if ((prng_next() & 0x3FF) == 0) {       /* 1/1024 → avg ~24 s */
            buzz_env_phase = 1;
        }
        break;
    case 1:  /* fade down: 256 → 32 in ~88 buffers (~2 s) */
        buzz_env_amp -= 3;
        if (buzz_env_amp <= 32) {
            buzz_env_amp = 32;
            buzz_env_phase = 2;
            buzz_env_timer = 24;                /* ~0.5 s hold */
        }
        break;
    case 2:  /* hold low */
        if (--buzz_env_timer <= 0) buzz_env_phase = 3;
        break;
    case 3:  /* fade up */
        buzz_env_amp += 3;
        if (buzz_env_amp >= 256) {
            buzz_env_amp = 256;
            buzz_env_phase = 0;
        }
        break;
    }

    /* Distance-attenuated hello volume — emitted by the neanderthal.
     * Squared-distance linear fade: full inside the cell, zero at
     * sqrt(HELLO_FADE_RADIUS_SQ) cells, square-law dropoff between. */
    /* Player position is fx_t 16.16 — shift to integer cell coord. */
    int player_x_cell = (int)(SHARED_UC->player.x >> 16);
    int player_y_cell = (int)(SHARED_UC->player.y >> 16);
    int dx = player_x_cell - NEANDER_X_CELL;
    int dy = player_y_cell - NEANDER_Y_CELL;
    int dist_sq = dx * dx + dy * dy;
    int hello_amp;
    if (dist_sq >= HELLO_FADE_RADIUS_SQ) {
        hello_amp = 0;
    } else {
        hello_amp = ((HELLO_FADE_RADIUS_SQ - dist_sq) * 256)
                    / HELLO_FADE_RADIUS_SQ;     /* 0..256 */
    }

    static uint32_t buzz_pos = 0;
    for (int i = 0; i < AMB_SAMPLES_PER_BUF; i++) {
        /* Buzz with envelope. */
        int buzz = (int)amb_buzz_samples[buzz_pos];
        int delta = ((buzz - SAMPLE_CENTER) * buzz_env_amp) >> 8;

        /* Neon sting at half amplitude when active. */
        if (neon_active) {
            int neon = (int)amb_neon_samples[neon_pos];
            delta += (neon - SAMPLE_CENTER) >> 1;
            neon_pos++;
            if (neon_pos >= AMB_NEON_SAMPLE_COUNT) neon_active = 0;
        }

        /* Hello looping continuously, volume by distance to neanderthal.
         * int8_t samples expand to centered ~10-bit range via << 2.
         * hello_pos_fx advances at fractional rate so the 4 kHz source
         * plays correctly at the 11 kHz output. */
        if (hello_amp > 0) {
            uint32_t hello_idx = hello_pos_fx >> 16;
            int hello = (int)amb_hello_samples[hello_idx] << 2;
            delta += (hello * hello_amp) >> 8;
        }
        hello_pos_fx += HELLO_STEP_FX;
        if ((hello_pos_fx >> 16) >= AMB_HELLO_SAMPLE_COUNT) hello_pos_fx = 0;

        /* Carpet footstep — only advance + mix when player is moving.
         * step_volume gives independent mix control (separate from
         * amb_volume so the menu can adjust ambient and footsteps
         * independently). 128 ≈ the old hard-coded `>> 1` baseline. */
        if (SHARED_UC->is_walking) {
            uint32_t step_idx = step_pos_fx >> 16;
            int step = (int)amb_step_samples[step_idx] << 2;
            int step_vol = (int)SHARED_UC->step_volume;
            delta += (step * step_vol) >> 8;
            step_pos_fx += STEP_STEP_FX;
            if ((step_pos_fx >> 16) >= AMB_STEP_SAMPLE_COUNT) step_pos_fx = 0;
        }

        /* Runtime gain (>> 7, 128 = unity), re-center, clip. */
        int s = ((delta * vol) >> 7) + SAMPLE_CENTER;
        if (s < SAMPLE_MIN) s = SAMPLE_MIN;
        if (s > SAMPLE_MAX) s = SAMPLE_MAX;
        amb_pwm_buf[buf_idx][i] = (uint16_t)s;

        buzz_pos++;
        if (buzz_pos >= AMB_BUZZ_SAMPLE_COUNT) buzz_pos = 0;
    }

    amb_buf_needs_fill = (uint8_t)(needs & ~(1 << buf_idx));
}

void amb_sound_init(void) {
    /* Default runtime audio volumes — unity gain for ambient, the
     * same half-amp baseline for footsteps that we used before.
     * Set here on the slave at boot for robustness against whether
     * crt0 actually copies .data from ROM to SDRAM at startup. */
    SHARED_UC->amb_volume  = 128;
    SHARED_UC->step_volume = 112;   /* user-tuned baseline */

    /* Step A: initialize the ping-pong state and prefill both buffers
     * with silence (DC center). Steps B+C swap to using these instead
     * of streaming straight from ROM. */
    amb_current_buf_idx = 0;
    amb_buf_needs_fill  = 0;
    for (int b = 0; b < 2; b++)
        for (int i = 0; i < AMB_SAMPLES_PER_BUF; i++)
            amb_pwm_buf[b][i] = SAMPLE_CENTER;

    Mars_InitPWM(AMB_BUZZ_SAMPLE_RATE, SAMPLE_MIN, SAMPLE_MAX);

    /* DMA destination = PWM mono register, fixed. */
    SH2_DMA_DAR1  = (uint32_t)&MARS_PWM_MONO;
    SH2_DMA_DRCR1 = 0;                /* external DREQ source = PWM */
    SH2_DMA_DMAOR = 1;                /* master enable for the DMAC */

    /* SH-2 IPRA layout (SH7095): [15:12]=DIVU, [11:8]=DMAC, [7:4]=WDT,
     * [3:0]=REF. Setting DMA priority to 4 — slave's SR mask is 2, so
     * priority 4 is high enough to be taken. */
    SH2_INT_IPRA = (SH2_INT_IPRA & 0xF0FF) | 0x0400;

    /* DMA1 uses a USER-DEFINED interrupt vector. VCR1 holds the vector
     * number; SH-2 reads VBR[VCR1*4] to get the handler. We point it
     * at vector slot 66 = "Level 4 & 5" entry in the slave vector
     * table (mars_start.s line 203), which already holds slav_irq.
     * slav_irq's dispatch chain has the new `cmp/eq #0x10` branch to
     * slav_dma_irq for level-4 source = DMA. */
    SH2_DMA_VCR1 = 66;

    /* Fire the first transfer. Both ping-pong buffers start at silence
     * (DC center), so the first ~46 ms is silent until the pump (step
     * C) gets going. After that, completions are handled by the IRQ
     * → amb_dma_handler swap chain. */
    SH2_DMA_SAR1  = (uint32_t)amb_pwm_buf[0];
    SH2_DMA_TCR1  = AMB_SAMPLES_PER_BUF;
    SH2_DMA_CHCR1 = 0x14E5;
}
