#include "mars.h"
#include "shared.h"

/* Slave SH-2 worker loop.
 *
 * Powers up alongside master, then busy-waits on the shared go_signal
 * flag. When master sets go_signal=1, slave snapshots the framebuffer
 * pointer and renders wall columns 160..319 (the right half). When done,
 * slave clears go_signal back to 0 — master joins on the same flag.
 *
 * Wall_dist[] writes for slave's column range live in uncached shared
 * SDRAM (shared->wall_dist[]) so master can read them after the join
 * for standup/light z-tests.
 */
void s_main(void) {
    for (;;) {
        /* Busy-wait for master's signal. */
        while (shared->go_signal != 1) { }

        /* Same framebuffer pointer math as fb_pixels(): line table is at
         * 0x24000000, pixel data starts at byte offset 0x200. */
        uint8_t *fb = (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);

        /* Render the right half of the screen's wall columns. */
        render_wall_columns(fb, SCREEN_W / 2, SCREEN_W);

        /* Signal done by clearing the flag. */
        shared->go_signal = 0;
    }
}
