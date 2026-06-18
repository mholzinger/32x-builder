#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>

/* Inter-CPU shared state between the primary and secondary SH-2s.
 *
 * Lives in normal SDRAM (.bss). BOTH CPUs MUST access fields via the
 * SHARED_UC macro below — plain `shared.field` access goes through
 * each SH-2's 4KB write-through cache and the other CPU sees stale
 * values. The cache-through alias (any SDRAM address OR'd with
 * 0x20000000) bypasses the cache entirely; reads always hit SDRAM,
 * writes commit directly. Slower than cached (~12 cycles per read)
 * but coherent across CPUs. Confirmed working on real 32X hardware
 * — see ROADMAP.md → SH-2 dual-CPU split. */

/* COMM4 command codes (primary → secondary doorbell). 0 = no command /
 * ACK. The secondary's polling loop in s_main.c reads COMM4, executes
 * the named work, then writes 0 back. Primary waits for the 0 before
 * proceeding past the sync point. */
#define MARS_CMD_NONE     0
#define MARS_CMD_HALF     1   /* Secondary draws clear+ceiling+carpet+walls for cols 160..319 */
#define MARS_CMD_BOX      2   /* Secondary rasterizes the box title's bottom band (rows 112..223) */

/* Snapshot of the primary's player state for the secondary to render from.
 * Primary writes this just before signaling CMD_CEILING; secondary reads
 * it via cache-through.
 *
 * Fields are `volatile` to defeat -flto hoisting. When the secondary's
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
    /* Monotonic counter the secondary increments forever in its idle loop.
     * Primary reads it as a "secondary alive" indicator. */
    volatile uint32_t secondary_heartbeat;
    /* Player snapshot for cross-CPU rendering. */
    player_snap_t player;
    /* Test-and-set byte for the work-stealing wall-column counter
     * (stored in MARS_SYS_COMM6). Either CPU acquires this lock via
     * sh2_spin_tas before reading-and-incrementing the counter. */
    volatile uint8_t wall_lock;
    /* Runtime audio gain. The secondary's amb_pump reads this on every
     * buffer refill and multiplies samples by (amb_volume / 128.0):
     *   0   = mute
     *   128 = unity (play ROM samples as-baked)
     *   255 = ~2× (will clip if the ROM was baked hot already)
     * Primary can write this freely; cache-through alias keeps the
     * secondary's reads coherent without explicit flushes. */
    volatile uint8_t amb_volume;
    /* Set by the primary each frame to 1 when the player is moving,
     * 0 when stationary. Secondary's pump uses this to gate the carpet
     * footstep audio — advances and mixes the step sample when set,
     * silent otherwise. */
    volatile uint8_t is_walking;
    /* Carpet footstep volume — separate knob from amb_volume so the
     * player can mix the footsteps independently in the settings
     * menu. 0..255, 128 = current half-amp baseline, 256 would be
     * full but capped at 255. Applied as a >> 8 scale in the pump. */
    volatile uint8_t step_volume;
    /* Profile counter: secondary's own FRT ticks spent processing CMD_HALF.
     * Secondary initializes its FRT to match primary's prescaler (Φ/32) and
     * writes its render delta here at the end of each command. Primary
     * reads via cache-through for the on-screen overlay so we can see
     * if secondary is the long pole regardless of the primary's idle wait. */
    volatile uint16_t secondary_render_ticks;
    /* Primary-incremented per-frame counter. Used by draw_walls on both
     * CPUs so the fluorescent-strobe RNG produces the same per-cell
     * flash pattern across the column split — flashes that straddle
     * the primary/secondary boundary look continuous. */
    volatile uint32_t frame_count;
    /* Per-effect enable bits, gated by the in-game menu's LIGHTING
     * tab. Default 0x07 = all on. Each raycaster effect early-outs
     * (or behaves as if at base value) when its bit is clear. */
    volatile uint8_t lighting_flags;
    /* Camera pitch shift in screen pixels (positive = look down, horizon
     * slides UP on screen). Written by primary in raycast_render every
     * frame, read by both CPUs' wall/floor/ceiling draws to position
     * the horizon. Cheap y-shear: walls slide with the horizon, depth
     * formula uses the unshifted SCREEN_H/2 as the focal-length
     * constant so perspective stays calibrated. */
    volatile int8_t pitch_y;
    /* Eye height as a fraction of room height in 8.8 (128 = mid-wall =
     * standing; lower = crouched/crawling, eye toward the floor). Read by
     * both CPUs' wall draw to split the wall column asymmetrically about
     * the horizon (floor close, ceiling looms when low). */
    volatile uint8_t eye_h;
    /* Adaptive dual-CPU split column: the primary renders columns
     * [0, split_col), the secondary [split_col, SCREEN_W). The primary nudges
     * it each frame to equalize the two halves' measured FRT times (load
     * balancing). Always a multiple of 4 (the clear pass writes 4-px words).
     * Defaults to SCREEN_W/2; stays there on emulators where the FRT reads 0. */
    volatile uint16_t split_col;
} shared_t;

#define LIGHTING_FLICKER  0x01   /* per-panel random brightness rolls */
#define LIGHTING_STROBE   0x02   /* distant fog-wall fluorescent bursts */
#define LIGHTING_SHIMMER  0x04   /* CRAM palette rotation on bright entries */

extern shared_t shared;

/* On-screen debug metrics (X/Y/A, frame timers, box profiler) — off by
 * default, toggled by the six-button controller's MODE button. Primary-only
 * (no cross-CPU access), so a plain global rather than a shared field. */
extern uint8_t g_metrics_on;

/* Cache-through pointer to the shared struct. Use on BOTH CPUs for
 * every read and write of any shared field. */
#define SHARED_UC ((shared_t *)((uintptr_t)&shared | 0x20000000))

#define SECONDARY_HEARTBEAT (SHARED_UC->secondary_heartbeat)

/* Pointer to the wall-column-counter lock byte, via the cache-through
 * alias. Pass to sh2_spin_tas / sh2_release_tas. */
#define WALL_LOCK_PTR \
    ((volatile uint8_t *)((uintptr_t)&shared.wall_lock | 0x20000000))

#endif
