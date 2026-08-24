#include "mars.h"
#include "raycast.h"
#include "shared.h"
#include "sound.h"
#include "box3d.h"

/* FRT runs at Phi/128 ~= 179.8 kHz, so one tick is ~5.56 us; 6 ~= 33 us. */
#define BUS_IDLE_FRT_TICKS 6

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
    SH2_FRT_TCR   = 0x02;   /* Phi/128 ~= 180kHz: 364ms before wrap */
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
             * instant return otherwise). amb_audio_idle() decodes at
             * most ONE 20 ms Speex frame per visit and belongs ONLY
             * here — in the vblank slack, never at render checkpoints. */
            amb_pump();
            amb_audio_idle();
            /* Throttle bumped 64→256 because primary got faster after
             * the DIVU/sine LUT optimizations, shifting the bus-
             * contention balance enough that controller-input stalls
             * re-appeared.
             *
             * BUS A/B (TESTING>IDLE, shared.h). The volatile-counter arm
             * is a DELAY THAT COSTS BUS: `volatile int i` lives on the
             * stack in SDRAM and SH-2 cache is write-through, so all 256
             * iterations issue real SDRAM stores, aimed at the same SDRAM
             * the primary renders from, for the whole idle stretch.
             *
             * A NOP COUNT IS THE WRONG REPLACEMENT -- the first A/B showed
             * that. Those stalling stores made the old loop accidentally
             * SELF-TUNING: it stretched precisely when the bus was busy,
             * which is backpressure, not waste. A fixed nop count runs the
             * same length regardless, so under render load the COMM4 poll
             * rate climbs -- the exact failure the 64->256 bump was made to
             * stop. Delay on the FRT instead: on-chip (no bus at all) and a
             * real wall-clock wait, so the poll rate is pinned by
             * construction instead of by a guessed iteration count.
             * Unsigned subtraction makes the 16-bit FRT wrap a non-event. */
            if (SHARED_UC->bus_idle) {
                uint16_t t0 = secondary_frt_read();
                while ((uint16_t)(secondary_frt_read() - t0)
                       < BUS_IDLE_FRT_TICKS) { }
            } else {
                for (volatile int i = 0; i < 256; i++);
            }
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
            /* amb_pump() checkpoints between passes: a 64 ms audio buffer
             * can drain past its half-way point inside ONE dense render
             * chunk, so waiting for the idle loop risked replaying stale
             * samples (the PWM chop). Between passes the pump's worst
             * stall is a single pass (~10-30 ms). Costs nothing when no
             * buffer is flagged; ~0.6 ms at most once per 64 ms when one is. */
            raycast_clear_half(split, SCREEN_W);
            amb_pump();
            raycast_draw_ceiling_grid(split, SCREEN_W);
            amb_pump();
            raycast_draw_carpet(split, SCREEN_W);
            amb_pump();
            raycast_purge_cell_light();        /* fresh cell_light on map change */
            raycast_draw_walls(split, SCREEN_W);
            SHARED_UC->secondary_render_ticks = (uint16_t)(secondary_frt_read() - t0);
            break;
        }
        case MARS_CMD_TAIL: {
            /* Second render phase (after the wall barrier): the crawlspace slab
             * + bulkhead caps for the secondary's column half. WALL_DIST is now
             * committed for all columns; we touch only [split, SCREEN_W), disjoint
             * from the primary's [0, split). Purge the crawlspace geometry first
             * so we don't draw stale caps from a previous level. */
            int split = (int)SHARED_UC->split_col;
            amb_pump();   /* audio checkpoint — see CMD_HALF */
            raycast_purge_lowceil_cache();
            raycast_purge_sprite_cache();
            raycast_draw_tail(split, SCREEN_W);
            amb_pump();
            /* Sprites AFTER the tail so the slab's z-stamp occludes them. Uses
             * the DECOUPLED sprite_split (set by the primary before it raised
             * CMD_TAIL) so a near screen-filling standup is shared, not dumped
             * on one cpu — disjoint [sprite_split, W) from the primary's half. */
            int sprite_split = (int)SHARED_UC->sprite_split;
            raycast_draw_sprites(sprite_split, SCREEN_W);
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
