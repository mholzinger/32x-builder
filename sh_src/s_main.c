#include "mars.h"
#include "raycast.h"
#include "shared.h"
#include "sound.h"
#include "box3d.h"

static inline uint16_t secondary_frt_read(void) {
    uint8_t hi = SH2_FRT_FRCH;
    uint8_t lo = SH2_FRT_FRCL;
    return ((uint16_t)hi << 8) | lo;
}

/* Secondary SH-2 entry point. The crt0 jumps here once the primary clears
 * the secondary's S_OK wait at COMM4 — see m_main.c for the release fix.
 *
 * d32xr/Doom-32X-style polling dispatcher: watch COMM4 for a non-zero
 * command code, execute it, write 0 back. The heartbeat counter
 * increments every iteration so the primary's debug indicator can tell
 * "secondary alive" from "secondary hung". */
void s_main(void) {
    /* Initialize ambient looping audio once at secondary startup. PWM
     * hardware is configured here, DMA channel 1 streams the buffer
     * into MARS_PWM_MONO, and the DMA-complete IRQ (see mars_start.s
     * slav_dma_irq) re-arms it forever. The polling loop below then
     * runs unaffected — SH-2 interrupts preempt it cleanly. */
    amb_sound_init();

    /* Secondary-side FRT init for profiling parity with primary. Φ/32
     * prescaler = ~720kHz, 1.39μs per tick — same as m_main.c. */
    SH2_FRT_TIER  = 0x01;
    SH2_FRT_TCR   = 0x01;
    SH2_FRT_FTCSR = 0;

    for (;;) {
        /* Throttled COMM4 poll. A tight poll (read-compare-branch
         * with no delay) hits the COMM4 MMIO ~3M times/sec, which on
         * MiSTer's FPGA timing seems to starve the 68K→SH2 bridge
         * just enough that the 68K's joypad-poll writes to COMM8
         * occasionally land late, making the player appear to "stop
         * walking" until the next button event. A short busy-wait
         * loop between polls drops the rate to ~30K/sec while keeping
         * latency below one frame. */
        uint16_t cmd = MARS_SYS_COMM4;
        if (cmd == MARS_CMD_NONE) {
            /* Service audio first — keep the PWM ping-pong fed.
             * amb_pump() is cheap (~150 μs when a fill is needed,
             * instant return otherwise). */
            amb_pump();
            /* Throttle bumped 64→256 because primary got faster after
             * the DIVU/sine LUT optimizations, shifting the bus-
             * contention balance enough that controller-input stalls
             * re-appeared. */
            for (volatile int i = 0; i < 256; i++);
            continue;
        }
        switch (cmd) {
        case MARS_CMD_HALF: {
            /* Secondary owns columns [split_col, SCREEN_W). The primary set
             * split_col before raising COMM4 (adaptive load balance); read it
             * via cache-through so this CPU sees the fresh value. No overlap
             * with primary's [0, split_col), so no mid-frame sync. */
            int split = (int)SHARED_UC->split_col;
            uint16_t t0 = secondary_frt_read();
            raycast_clear_half(split, SCREEN_W);
            raycast_draw_ceiling_grid(split, SCREEN_W);
            raycast_draw_carpet(split, SCREEN_W);
            raycast_draw_walls(split, SCREEN_W);
            SHARED_UC->secondary_render_ticks = (uint16_t)(secondary_frt_read() - t0);
            break;
        }
        case MARS_CMD_BOX: {
            /* Title screen: secondary rasterizes the box's bottom band from
             * the primary-built shared draw-list. Disjoint framebuffer
             * rows from the primary's top band — no mid-frame sync. */
            uint16_t t0 = secondary_frt_read();
            box3d_render_band(1);   /* bottom half */
            SHARED_UC->secondary_render_ticks = (uint16_t)(secondary_frt_read() - t0);
            break;
        }
        }
        SECONDARY_HEARTBEAT++;
        MARS_SYS_COMM4 = MARS_CMD_NONE;   /* ACK */
    }
}
