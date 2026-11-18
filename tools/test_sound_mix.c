/* Compile the REAL mixer twice and compare samples and state. No MMIO or IRQ
 * handlers run here; only shared-address translation is replaced for the host. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../sh_src/shared.h"
#undef SHARED_UC
#define SHARED_UC (&shared)
#define SOUND_UC_ADDR(p) ((uintptr_t)(p))
#include "../sh_src/sound.c"
shared_t shared;
uint8_t hero_scratch[71680];
#ifndef QUIET_TEST_MODE
#define QUIET_TEST_MODE 0
#endif
const volatile uint8_t diag_cfg[32] = {[30] = QUIET_TEST_MODE};

int main(void) {
    for (unsigned fixture = 0; fixture < 2048; ++fixture) {
        unsigned flags = fixture & 511;
        memset(&shared, 0, sizeof shared);
        memset(amb_pwm_buf, 0x5a, sizeof amb_pwm_buf);
        amb_active_storage = !(flags & 256);
        amb_buf_len_storage = ((fixture >> 9) & 1) ? AMB_SAMPLES_PER_BUF : AMB_SAMPLES_OLD;
        amb_buf_needs_fill = 3;
        shared.amb_volume = (fixture * 17) & 255;
        shared.step_volume = (fixture * 31) & 255;
        shared.is_walking = !!(flags & 1);
        shared.is_running = !!(flags & 2);
        shared.eye_h = (flags & 4) ? 64 : 128;
        shared.player.x = (flags & 8) ? FX(16) : FX(1);
        shared.player.y = (flags & 8) ? FX(23) : FX(1);
        const unsigned death[4] = {0, 40, 120, 220};
        shared.hero_dying = (flags & 16) ? death[fixture >> 9] : 0;
        prng_state = fixture == 0 ? 0 : 0xCAFEBABE;
        neon_active = !!(flags & 32);
        neon_pos = AMB_NEON_SAMPLE_COUNT - 8;
        slide_active = !!(flags & 64);
        slide_loop = !!(fixture & 1);
        slide_step_fx = slide_loop ? SLIDING_STEP_FX : SHUFFLE_STEP_FX;
        slide_pos = (slide_loop ? AMB_SLIDING_SAMPLE_COUNT : AMB_SHUFFLE_SAMPLE_COUNT) - 8;
        slide_frac = 32000;
        shared.slide_sfx = (fixture % 7 == 0) ? 1 : (fixture % 11 == 0) ? 2 : 0;
        crt_active = !!(flags & 128);
        crt_rev = !!(fixture & 1);
        crt_pos_fx = ((crt_rev ? AMB_CRT_OFF_SAMPLE_COUNT : AMB_CRT_ON_SAMPLE_COUNT) - 32) << 16;
        shared.crt_sfx = (fixture % 13 == 0) ? 1 : (fixture % 19 == 0) ? 2 : 0;
        step_pos_fx = (AMB_STEP_SAMPLE_COUNT - 32) << 16;
        crawl_pos = AMB_SLIDING_SAMPLE_COUNT - 8; crawl_frac = 32000;
        death_pos_fx = 100 << 16; death_was_active = fixture & 1;
        const unsigned speed_pct[3] = {90, 100, 150};
        hello_step_storage = HELLO_STEP_FX * speed_pct[fixture % 3] / 100;
        hello_wr = 600; hello_rd = 100; hello_rd_frac = 32000;
        spx_ready = 1;
        for (unsigned i = 0; i < HELLO_RING_SAMPLES; ++i)
            hello_ring[i] = (int16_t)(i * 137);
        amb_pump();
        amb_pump();
        if (fixture == 512 || fixture == 1024 || fixture == 1536) {
            for (int b = 0; b < 2; ++b)
                for (unsigned i = 0; i < amb_buf_len_storage; ++i)
                    assert(amb_pwm_buf[b][i] == SAMPLE_CENTER);
        }
        if (amb_active_storage) assert(amb_buf_needs_fill == 0);
        else assert(amb_buf_needs_fill == 3);
        assert(fwrite(amb_pwm_buf, sizeof amb_pwm_buf, 1, stdout) == 1);
        uint32_t state[] = {prng_state, neon_active, neon_pos, slide_active,
            slide_loop, slide_pos, slide_frac, slide_step_fx, crt_active,
            crt_rev, crt_pos_fx, step_pos_fx, crawl_pos, crawl_frac,
            death_pos_fx, death_was_active, hello_wr, hello_rd, hello_rd_frac,
            spx_ready, amb_buf_needs_fill, shared.slide_sfx, shared.crt_sfx};
        assert(fwrite(state, sizeof state, 1, stdout) == 1);
    }
    fprintf(stderr, "2048 mixer fixtures: samples and state emitted (mode %d)\n", QUIET_TEST_MODE);
    return 0;
}
