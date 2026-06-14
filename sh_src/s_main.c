#include "mars.h"
#include "raycast.h"

/* Slave SH-2 worker — proof-of-life iteration.
 *
 * We're done assuming the cache-through SDRAM alias works on 32X. This
 * iteration uses the MARS_SYS_COMM0 MMIO register (0x20004020) for
 * sync — guaranteed uncached because it's a hardware register, not RAM.
 *
 *   master sets COMM0 = 1 to start slave
 *   slave clears COMM0 to 0 when finished
 *
 * For this first iteration, slave does the most trivial possible thing:
 * paint a vertical stripe of bright color at column 0 of the framebuffer.
 * If we see the stripe, the slave is alive and can write to the FB.
 * That's the foundation — every subsequent expansion to real rendering
 * work rests on this signal-and-write pattern actually functioning.
 */
void s_main(void) {
    for (;;) {
        /* Busy-wait for master signal via MMIO. */
        while (MARS_SYS_COMM0 != 1) { }

        /* Proof of life: paint column 0 in a clearly recognizable color. */
        uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
        for (int y = 0; y < 224; y++) {
            fb[y * 320 + 0] = 1;   /* WALL_BASE + 0 = brightest wall color */
            fb[y * 320 + 1] = 1;
            fb[y * 320 + 2] = 1;
        }

        /* Done — clear the signal so master can join. */
        MARS_SYS_COMM0 = 0;
    }
}
