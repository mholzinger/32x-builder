#ifndef SHARED_H_INCLUDED
#define SHARED_H_INCLUDED

#include <stdint.h>
#include "raycast.h"

/* SH-2 dual-CPU shared state.
 *
 * Lives at 0x26008000 — the SH-2's CACHE-THROUGH (uncached) view of SDRAM.
 * Both master and slave see the same physical memory here without any
 * cache coherency dance: writes go straight through, reads come straight
 * from RAM. Costs an extra ~5 cycles per access vs cached memory, but
 * with only ~10 fields written/read per frame the total overhead is
 * effectively zero.
 *
 * Synchronization is a single 16-bit go_signal:
 *   master writes 1 to start the slave
 *   slave clears it to 0 to signal "I'm done"
 *   master spins on (go_signal != 0) before joining
 */

#define SHARED_BASE 0x26008000

typedef struct {
    /* Camera state — master writes once per frame before signaling slave. */
    fx_t dirX, dirY;
    fx_t planeX, planeY;
    fx_t player_x, player_y;

    /* Wall column z-buffer. Master writes its column range, slave writes
     * its column range, no overlap. Standup/lights read the full array
     * after the master/slave wall-rendering sync barrier. */
    fx_t wall_dist[SCREEN_W];

    /* Synchronization: master sets 1 to start slave; slave clears to 0
     * when its work is complete. */
    volatile uint16_t go_signal;
    uint16_t _padding;
} shared_state_t;

extern volatile shared_state_t * const shared;

/* Render a range of wall columns. Callable from either CPU.
 * Reads camera state from the shared struct; writes wall pixels to fb
 * and z-distances to shared->wall_dist[]. */
void render_wall_columns(uint8_t *fb, int col_start, int col_end);

#endif
