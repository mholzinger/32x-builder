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
#define MARS_CMD_TAIL     3   /* Secondary draws low-ceiling slab+bulkheads for cols [split,320) */

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
    /* Turning in place (angle changed this frame, no positional move needed).
     * The res/LOD gates treat it as motion -- a spin re-renders every column,
     * so it deserves the same near-band drop walking gets; chunkiness is
     * masked by the rotation just like it is by the stride. Does NOT drive
     * footsteps/bob (those stay on is_walking). */
    volatile uint8_t is_turning;
    /* Set by the primary to 1 when the player is moving AND sprinting (A
     * held). The pump advances the footstep sample ~1.5x faster while set,
     * so the carpet cadence quickens to match the run. Ignored unless
     * is_walking is also set. */
    volatile uint8_t is_running;
    /* Carpet footstep volume — separate knob from amb_volume so the
     * player can mix the footsteps independently in the settings
     * menu. 0..255, 128 = current half-amp baseline, 256 would be
     * full but capped at 255. Applied as a >> 8 scale in the pump. */
    volatile uint8_t step_volume;
    /* Exit-passage SFX request. Primary sets the MODE; the secondary's
     * pump latches it at the next buffer fill and writes 0 back.
     *   1 = landing scuff (amb_shuffle), one-shot at native rate
     *   2 = corridor loop (amb_sliding) until the landing takes over */
    volatile uint8_t slide_sfx;
    /* ONE-SHOT CRT power SFX request (the PVM's A toggle). Primary sets
     * 1 = power-on clip, 2 = power-off clip; the mixer starts the chosen
     * bake once and writes 0 back. */
    volatile uint8_t crt_sfx;
    /* 1 while the primary sits in swapBuffers' vblank-tick wait — the
     * only window where BOTH CPUs are off the ROM/SDRAM bus. The
     * secondary gates Speex hello decode on this: decode is bus-heavy
     * (code fetches thrash the 4 KB cache, every write-through store
     * hits SDRAM), so running it while the primary still renders slows
     * the PRIMARY — measured as ~2,800 ticks (one vblank, 9→7 fps)
     * near the neanderthal even though the secondary itself was idle. */
    volatile uint8_t primary_vwait;
    /* 1 = kill the Speex hello decode entirely (AUDIO menu, HELLO row).
     * The same-binary A/B knob for the decode's true cost: toppling the
     * neanderthal is NOT a valid A/B — it also removes a screen-filling
     * sprite worth multiple fps on its own. Toggle this at a fixed
     * standing spot and read T/F with nothing else changing. */
    volatile uint8_t voice_off;
    /* 1 = the whole hum system is YM2612 FM synthesis (68K-driven over
     * COMM 0x0E): channel 2 sustains the buzz bed, channel 1 fires the
     * neon sting, and the mixer mutes BOTH sample voices (437KB buzz +
     * 31KB neon). The A/B for the hum-synthesis experiment (AUDIO menu,
     * HUM row); if the Yamaha passes the ear, both bakes get deleted. */
    volatile uint8_t hum_ym;
    /* Sting handshake: the secondary's audio pump rolls the sting dice
     * but only the primary may drive COMM0 — pump sets 1, primary
     * key-ons YM ch1 and clears it. */
    volatile uint8_t ym_sting;
    /* Speex decode profiling (secondary writes, primary HUD reads):
     * ticks of the LAST frame decode on the secondary's FRT (same
     * prescaler as secondary_render_ticks) and a cumulative decoded-
     * frame counter (wraps; the HUD reader shows its climb rate).
     * Real-time playback needs 50 decodes/s — if DX climbs slower
     * than ~50/s while the hello is audible, the ring is starving
     * and you hear snippets. DT x 50 = ticks/second of decode load. */
    volatile uint16_t spx_dec_ticks;
    volatile uint16_t spx_dec_count;
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
    /* Half-res wall render toggle (VISUALS menu tab). 1 = draw every other
     * column and word-store the pair — chunky horizontally, ~halves the wall
     * pass. 0 = full per-column detail. Lives here (not a plain global) because
     * draw_walls runs on BOTH CPUs; cache-through keeps the two screen halves
     * at the same resolution the instant it's toggled. */
    volatile uint8_t wall_halfres;
    /* WALLS menu mode: 0=FULL, 1=HALF, 2=AUTO. The primary folds this with the
     * lobby override and (for AUTO) the measured frame time into wall_halfres
     * above each frame — that effective flag is what both CPUs' draw_walls read.
     * AUTO uses hysteresis on the frame period so it doesn't flip-flop per frame. */
    volatile uint8_t wall_res_mode;
    /* Vertical half-res toggle. 1 = every render pass writes only EVEN framebuffer
     * rows and the display line table maps each row-pair to that even row (2px-tall
     * blocks) — halves the whole frame's stores at once (walls + clear + ceiling +
     * carpet + sprites), the one lever horizontal half-res can't reach (wall stores
     * are 320-strided vertical). Like wall_halfres it lives here because every pass
     * on BOTH CPUs reads it; cache-through keeps the two halves in lockstep. */
    volatile uint8_t wall_vert;
    /* Seam-smoothing toggle (VISUALS: SEAMS HARD/SMOOTH). When half/quarter-res
     * coarsens the wall fill, the wall→ceiling and wall→floor silhouette turns
     * into a 2/4px staircase. SMOOTH interpolates each block's top/bottom edge
     * between the raycast anchor columns (exact for a flat face — its edges
     * project to straight lines) and repaints the fringe, so the silhouette
     * reads as a clean diagonal over the still-coarse interior. */
    volatile uint8_t wall_seam_smooth;
    /* Half→full dissolve level (0 = off, else 1..N). There's no resolution
     * between half and full, so the up-transition would otherwise be one hard
     * flip. During it we render FULL (affordable while still) and this many
     * "steps" of a dither-selected fraction of column pairs get re-doubled back
     * to half over the wall band — a focus-pull sweep from half to full. Decays
     * to 0 across a few frames. Both CPUs' wall post-pass read it. */
    volatile uint8_t wall_dissolve;
    /* Quarter-res boundary DITHER (1 = on). Real spatial ordered-dither: a post-
     * pass ramps the right edge of each 4px block toward the next block's color
     * (ordered Bayer threshold over the wall band), turning the hard 4px shade
     * step into a dithered gradient. Static per frame (no stutter). On the PVM the
     * dither fuses toward smooth; on sharp HDMI it reads as an intentional dither
     * texture rather than a blocky step. Both CPUs read it, each dithers its half. */
    volatile uint8_t wall_qdither;
    /* WALLS=LOD prototype (1 = on). Full-res render + a post-pass that re-blocks
     * each 4-col quad by DEPTH: near = leave full, mid = half (2px pairs), far =
     * quarter + dither toward the neighbour. Visual proof of the three-band zoning
     * — it ADDS cost (no render saving yet); the perf version skips rendering. */
    volatile uint8_t wall_lod;
    /* Partition-dense hint for LOD's near band (1 = drop the nearest quads to
     * half-res even while standing still). The near band normally stays full-res
     * when stationary (the "taking stock" sharpen), but in a slab-heavy view the
     * near partitions are ~84% of the wall pass, so full-res-standing there is the
     * F:05 pit. Primary latches this from last frame's wall cost (hysteresis) and
     * publishes it cache-through so both CPUs drop their near band together. */
    volatile uint8_t wall_dense;
    /* Bulkhead kill-switch (TESTING menu): 1 = skip the bulkhead-height ceiling
     * slab + cap passes, for same-binary A/B of their cost (HUD L:). Both CPUs
     * read it cache-through in raycast_draw_tail. Crawl passes unaffected. */
    volatile uint8_t bulk_kill;
    /* Carpet VERTICAL depth LOD: stamp stains on every OTHER screen row once the
     * row's fog shade reaches the mid band, mirroring the x_step 4/8/16 banding
     * that already runs horizontally. Each row costs a DIVU plus ~6 muls of setup
     * before a single stain lands, so halving rows halves setup AND the column
     * walk -- measured via the blunt all-rows VERT toggle, carpet went 2,068 ->
     * 888 ticks (-57%). Same-binary A/B lives in TESTING>CARPETLOD. */
    volatile uint8_t carpet_vlod;
    /* UNLIT-FLOOR kill: skips BOTH carpet zone re-stamps (dark-room fill +
     * crawlspace darken) for a same-binary A/B of their cost inside HUD R:.
     * Phase-1 hardware baseline read R:8,122 in the crawl scene vs ~2-4k
     * elsewhere — this toggle tells us how much of that is the zone fills
     * (double-painting the floor with per-pair grid tests) vs the base
     * stain pass, BEFORE any refactor spends effort on the wrong half.
     * Diagnostic only: ON leaves unlit floors rendered LIT. */
    volatile uint8_t unlit_kill;
    /* Exit-hole close-up work: the vertical JAMB (the wall's cut thickness at
     * the aperture's left/right edges) and the chevron skin on the cavity side
     * walls. Both only render on side_hit columns, so the cost is bounded by
     * the hole's own width and vanishes past ~2 cells where the jamb goes
     * sub-pixel. Same-binary A/B lives in TESTING>HOLEJAMB. */
    volatile uint8_t hole_jamb;
    /* AUTO's motion-gated QUARTER rung. Off = the shipped behaviour (AUTO
     * floors at half). On = drop to quarter while MOVING on a very heavy frame
     * (frame_ema past AUTO_QTR_ON), snapping back the instant you stand still.
     * A toggle rather than a decision because the A/B that removed it measured
     * a standing corridor at F:07-11 and the rung only arms moving at ~F:09 or
     * worse -- it never entered its own trigger condition. TESTING>AUTOQTR. */
    volatile uint8_t auto_qtr;
    /* ULTRA rest pair (TESTING>ULTRA). After ~1s of stillness the primary
     * renders a TWIN of the frame on screen — same world state, camera shifted
     * HALF A COLUMN — into the other framebuffer, then parks in a loop that
     * flips the pair at 60Hz with both CPUs idle. On a CRT the phosphor+eye
     * blend the two into an effective 640-wide antialiased image. Any input
     * breaks the park and the normal loop simply overwrites the pair. */
    volatile uint8_t ultra_enable;
    /* Nonzero while an ULTRA pass renders: 1 = pass A (no jitter), 2 = pass
     * B (every pass on both CPUs shifts sampling half a column). m_main
     * checkerboard-merges the two into ONE static frame — differences are
     * spatial dither the CRT fuses, never temporal (the B00288-290 lesson:
     * a 30Hz flip pair reads as shimmer no matter which leak you pin).
     * Either value makes raycast_render freeze EVERY piece of adaptive
     * state (frame EMA, AUTO/ratchet/dissolve, split nudge, dense latch) so
     * the passes stay decision-identical and differ only by the jitter. */
    volatile uint8_t ultra_twin;
    /* Set by EITHER CPU during a render that actually painted live PVM static
     * (a powered screen with no telegraph/glass picture on it). The ULTRA park
     * holds one motionless frame by design — "nothing can shimmer" — and the
     * tube's noise IS shimmer, so a parked frame freezes it into a still
     * photograph of static. Standing still to look at a monitor is exactly the
     * condition that arms the park, so the two features met head-on the moment
     * ULTRA started arming. m_main clears this before each render and gates the
     * park on it: supersample the quiet rooms, leave the live tubes alone. */
    volatile uint8_t pvm_static_live;
    /* Caveman death: 0 = alive, 1..255 = the "broken analogue tape" death phase.
     * Primary ramps it over ~2.5s once the neanderthal is knocked down; the audio
     * mixer reads it to warp the Voyager hello — speed up, reverse, drift to
     * nothing. Cache-through so the secondary sees each frame's value. */
    volatile uint8_t hero_dying;
    /* Door swing animation, 0 = closed .. 16 = fully open. Primary eases it each
     * frame toward the target (toggled by the interact button near the door);
     * both CPUs' wall-embedded door fill read it to foreshorten the leaf and
     * reveal the dark void behind. */
    volatile uint8_t door_open;
    /* Bumped by the primary each time init_lights rebuilds cell_light (map load).
     * The secondary watches it to purge its stale cached cell_light lines once
     * per change, so cell_light can be read cached instead of uncached. */
    volatile uint8_t cell_light_gen;
    /* Partition-campaign diagnostic (MODE+C cycles, HUD J): 0 = normal,
     * 1 = run-extent LUT instead of the per-column run walk (candidate fix),
     * 2 = slab gate OFF — partitions don't render at all; DIAGNOSTIC ONLY,
     * prices the whole edge-partition machinery for the same pose. Read by
     * both CPUs' wall pass (hoisted to a local at pass start). */
    volatile uint8_t part_diag;
    /* Floor shadows master switch (VISUALS menu row): 1 = skip every standup/
     * chair floor shadow. Lives here so BOTH CPUs' sprite halves agree, and
     * so shadow cost can be A/B'd same-binary per the perf discipline. */
    volatile uint8_t shadows_off;
    /* DEBUG A/B (MODE+A cycles): 0 = chair faces flat-filled (shipping),
     * 1 = chair faces routed through the textured tex_tri path (sampling
     * wall_tex as a throwaway) to measure textured-quad cost against the
     * flat baseline. Read by both CPUs' draw_chair_3d; the flip changes only
     * the fill method, not geometry, so the primary-half prof_pass_chair
     * delta between arms is the true per-pixel texturing cost. */
    volatile uint8_t chair_tex;
    /* TESTING>SMS32X: 1 = the modal mini-game's picture is rendered by the SH-2
     * into the 32X framebuffer from broadcast tile ids, 0 = the shipping path
     * (68K blits TILEBUF to MD plane B over a black 32X frame). Step 1 of the
     * zoom-into-the-glass arc: the transition needs its start and end on ONE
     * renderer, and this is the end. */
    volatile uint8_t sms_on_32x;
    /* TESTING>EPOCH (default OFF): the SMS picture channel runs the
     * dirty-epoch delta protocol instead of the full-picture rotation —
     * only changed cells cross, stamped with a frame epoch and applied
     * atomically; a slow absolute repair rotation bounds how long any
     * lost delta can survive. The legacy rotation is the fallback arm. */
    volatile uint8_t sms_epoch_on;
    /* The desk console's GLASS as projected in the most recent world frame
     * (screen px, x1/y1 exclusive; x1<=x0 = not drawn). Written by whichever
     * CPU rasterizes the front face, read by the primary to birth the zoom's
     * picture rect exactly on the tube — uncached so the halves never skew. */
    volatile int16_t glass_sr_x0, glass_sr_y0, glass_sr_x1, glass_sr_y1;
    /* BUS A/B, SPLIT INTO THREE (TESTING>SPIN / IDLE / 68K). They shipped as
     * one flag and the first A/B came back NET WORSE (T 22018 -> 23654) with a
     * clear win buried inside it -- exactly what a bundled toggle cannot tell
     * apart. Per-pass costs were IDENTICAL across both arms (W/R/G/I/P to the
     * tick), so none of this touches render work: every delta lives in the
     * overhead paths, and each flag now has a metric that measures it alone.
     *
     * bus_spin -> NOT CONFIRMED, and SW does not confirm it. SW is the primary
     *   WAITING for the flip -- it is slack, not cost. When work grows there is
     *   less slack left to wait in, so SW shrinks: its 751 -> 359 was a symptom
     *   of the regression alongside it (T/H/S/HU all rose), not a win. The
     *   change is still sound in principle -- the COMM12 tick, FBCTL flip and
     *   ULTRA park are bare polls of 32X sysregs at ~3M reads/sec, and
     *   raycast.c's barrier waits have carried this 16-nop throttle for years
     *   -- but throttling a wait cannot make the wait shorter. Only the
     *   contention it relieves can pay, and see VENUE. Default OFF.
     * bus_idle -> H/S. The secondary's idle throttle. The old volatile-counter
     *   loop was ACCIDENTALLY SELF-TUNING -- its write-through stores stall on
     *   the write buffer, so it stretched exactly when the bus was busy. A
     *   constant nop count deletes that backpressure and the COMM4 poll rate
     *   climbs under load: the failure the original 64->256 bump existed to
     *   stop. Now an FRT delay -- on-chip timer, zero bus, true wall clock.
     * bus_68k -> HU (post-render HUD). Backing the 68K's COMM0 poll off when
     *   idle cost 11% there. The HUD is not one burst -- it is many short
     *   HwMdPuts runs with SH-2 work between them, so the 68K goes idle and
     *   re-backs-off before every one, paying the latency again each time.
     *
     * VENUE -- READ THIS BEFORE MEASURING. All three of these trade CPU cycles
     * for reduced bus contention. Ares models the cycles faithfully and the
     * cart-bus arbitration between the 68K and two SH-2s much less so, which
     * means it shows the full COST of each and little or none of the benefit.
     * That is not a flaw in the experiment, it is the wrong instrument: the
     * Ares run above measured all three costs correctly (the 68K backoff really
     * does cost ~369 ticks of HU, a fixed nop count really does lose the old
     * loop's backpressure) and could not price a single benefit. Every flag
     * therefore defaults OFF and stays unproven until a MiSTer sitting.
     *
     * DO NOT re-run this A/B in Ares and conclude anything from it. */
    volatile uint8_t bus_spin;
    volatile uint8_t bus_idle;
    volatile uint8_t bus_68k;

    /* Sprite-pass column split, decoupled from the wall split_col. A large
     * screen-filling standup (the world-quad neanderthal up close) can land
     * almost entirely in ONE cpu's wall-half, leaving the other idle in the
     * post-wall barrier while it draws ~30k textured pixels alone. The primary
     * centers this on the dominant standup so both SH-2s draw half of it in
     * parallel. Written by the primary before it raises CMD_TAIL; the secondary
     * reads it for raycast_draw_sprites (the tail/slab pass still uses split_col). */
    volatile uint16_t sprite_split;
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
