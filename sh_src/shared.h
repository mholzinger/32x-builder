#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>

/* Inter-CPU shared state between the master and slave SH-2s.
 *
 * Lives in normal SDRAM (.bss). BOTH CPUs MUST access fields via the
 * SHARED_UC macro below — plain `shared.field` access goes through
 * each SH-2's 4KB write-through cache and the other CPU sees stale
 * values. The cache-through alias (any SDRAM address OR'd with
 * 0x20000000) bypasses the cache entirely; reads always hit SDRAM,
 * writes commit directly. Slower than cached (~12 cycles per read)
 * but coherent across CPUs. Confirmed working on real 32X hardware
 * — see ROADMAP.md → SH-2 dual-CPU split. */

/* COMM4 command codes (master → slave doorbell). 0 = no command /
 * ACK. The slave's polling loop in s_main.c reads COMM4, executes
 * the named work, then writes 0 back. Master waits for the 0 before
 * proceeding past the sync point. */
#define MARS_CMD_NONE     0
#define MARS_CMD_CEILING  1
#define MARS_CMD_WALLS    2   /* Slave draws wall columns 160..319 */

/* Snapshot of the master's player state for the slave to render from.
 * Master writes this just before signaling CMD_CEILING; slave reads
 * it via cache-through.
 *
 * Fields are `volatile` to defeat -flto hoisting. When the slave's
 * draw functions get inlined into the s_main dispatch loop, GCC can
 * notice that no code visible to it writes to these fields and hoist
 * the loads OUT of the for(;;) loop — caching the initial frame's
 * values forever. That's what manifested as "ceiling grid rotates
 * with the player's angle but doesn't translate when walking": the
 * angle field was being re-read (likely due to the sin/cos lookup
 * creating a downstream load dependency) but x/y were hoisted out
 * once and never refreshed. */
typedef struct {
    volatile int32_t  x, y;     /* FX 16.16 world position */
    volatile uint16_t angle;    /* 0..255 in low byte */
    uint16_t _pad;
} player_snap_t;

typedef struct {
    /* Monotonic counter the slave increments forever in its idle loop.
     * Master reads it as a "slave alive" indicator. */
    volatile uint32_t slave_heartbeat;
    /* Player snapshot for cross-CPU rendering. */
    player_snap_t player;
    /* Test-and-set byte for the work-stealing wall-column counter
     * (stored in MARS_SYS_COMM6). Either CPU acquires this lock via
     * sh2_spin_tas before reading-and-incrementing the counter. */
    volatile uint8_t wall_lock;
    /* Runtime audio gain. The slave's amb_pump reads this on every
     * buffer refill and multiplies samples by (amb_volume / 128.0):
     *   0   = mute
     *   128 = unity (play ROM samples as-baked)
     *   255 = ~2× (will clip if the ROM was baked hot already)
     * Master can write this freely; cache-through alias keeps the
     * slave's reads coherent without explicit flushes. */
    volatile uint8_t amb_volume;
    /* Set by the master each frame to 1 when the player is moving,
     * 0 when stationary. Slave's pump uses this to gate the carpet
     * footstep audio — advances and mixes the step sample when set,
     * silent otherwise. */
    volatile uint8_t is_walking;
    /* Carpet footstep volume — separate knob from amb_volume so the
     * player can mix the footsteps independently in the settings
     * menu. 0..255, 128 = current half-amp baseline, 256 would be
     * full but capped at 255. Applied as a >> 8 scale in the pump. */
    volatile uint8_t step_volume;
} shared_t;

extern shared_t shared;

/* Cache-through pointer to the shared struct. Use on BOTH CPUs for
 * every read and write of any shared field. */
#define SHARED_UC ((shared_t *)((uintptr_t)&shared | 0x20000000))

#define SLAVE_HEARTBEAT (SHARED_UC->slave_heartbeat)

/* Pointer to the wall-column-counter lock byte, via the cache-through
 * alias. Pass to sh2_spin_tas / sh2_release_tas. */
#define WALL_LOCK_PTR \
    ((volatile uint8_t *)((uintptr_t)&shared.wall_lock | 0x20000000))

#endif
