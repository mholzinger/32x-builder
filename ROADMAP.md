# Backrooms 32X — roadmap of unresolved / future work

Each item below is something we attempted, hit a wall on, or deliberately
deferred. Listed in roughly the order I think they'd be productive to
revisit.

## Visual / atmospheric

### Ceiling lights as actual grid-tile illumination
**Status:** unresolved (multiple attempts, none reading right)

The intent is for "lit" ceiling fixtures to BE specific tiles of the
existing drop-ceiling grid, not separate overlay sprites. The user wants
to pick any random tile in the perspective-correct ceiling grid and have
it light up — so the lit area tracks the same vanishing-point as the
surrounding grid lines.

What we've tried:
- 2:1 billboard panels with center at the ceiling row (felt floating)
- Flat 8:1 panels with bottom edge at the ceiling row (still felt like
  hanging overlay)
- 4-corner projection with bbox fill (no visible difference from user
  POV — likely needs actual scanline polygon fill instead of bbox)

Probable correct approach: project the 4 corners of the actual ceiling
tile to screen, then fill the resulting trapezoid via scanline edge
walking (left-edge + right-edge interpolation between top and bottom
rows). The bounding-box approximation isn't good enough; we need the
trapezoid edges to converge toward the vanishing point so the lit area
visually feels like an axis-aligned tile in the same plane as the
ceiling grid pattern.

Alternative: full per-pixel floorcast of just the ceiling tiles flagged
as illuminated, with the existing per-row grid-line code extended to
also stamp bright pixels inside designated tile cells. Cost would be
roughly the same as the floor-side carpet wear pass (~2-3ms).

### SH-2 dual-CPU split
**Status:** ready to retry — root causes of both prior failures now
identified by deep research into d32xr (Doom 32X) and the 32XDK wiki.

Goal: move wall column rendering or ceiling grid work onto the slave
SH-2 (currently spinning in `for(;;)`) to parallelize ~5ms of work per
frame.

#### What went wrong before

Attempt 1 (`dea2383`, black screen): we put shared state in SDRAM but
accessed it via normal C pointers, which the linker placed in the
*cached* `0x06xxxxxx` alias. The master's writes hit its own cache and
were never visible to the slave; the slave read zeros and crashed.
Possible secondary cause: both CPUs writing the same SDRAM word →
documented hardware lockup per 32XDK wiki "Bugs and quirks".

Attempt 2 (`9fd44f5`, no controller input): we used **COMM0** as the
M↔S doorbell. COMM0–COMM3 are reserved for the **68K↔Master** channel
(controllers, system services). Polling/clearing COMM0 from the slave
destroyed the 68K's outgoing controller-poll command before the master
could read it. `Mars_GetPad()` returned stale values forever.

#### Correct architecture (verified against d32xr `marsnew.c`)

**COMM register partition (don't violate):**
- COMM0–COMM3 = 68K ↔ Master. *Never touch from slave.*
- COMM4 = Master → Slave command (doorbell)
- COMM6 = Master → Slave argument word

Master writes a non-zero command code to COMM4; slave's polling loop
sees it, executes, writes 0 back. Master uses `while (MARS_SYS_COMM4
!= 0);` to wait for ACK before issuing the next command.

**Shared-state cache rule:** any byte that both CPUs touch must be
accessed via the cache-through alias. Take any cached pointer and
OR with `0x20000000`:

```c
#define UNCACHED(p) ((typeof(p))((uintptr_t)(p) | 0x20000000))
```

The slave is already running from boot via its own reset vector — no
"start slave" call is needed. The crt0 handshake (M_OK / S_OK / G_OK)
already brings both CPUs up before `m_main()` / `s_main()` run.

**Framebuffer ownership:** partition by region (master = cols 0–159,
slave = cols 160–319, or master = walls, slave = ceiling). Both CPUs
can write `0x24000000` directly with ~6–10% bus-contention tax. The
hard rule: no byte may be written by both CPUs.

#### Recommended first split — ceiling grid

Read-only inputs (just player angle/position), writes a disjoint Y
range from walls, pure compute, no game state mutation. Master signals
`MARS_SECCMD_CEILING` on COMM4 → master draws walls → both wait for
each other → flip framebuffer.

#### Sources

Definitive references in case we lose context again:
- viciious/d32xr `marsnew.c::Mars_Secondary()` — the slave loop
- viciious/d32xr `mars.h` — `Mars_R_SecWait`, `MARS_SECCMD_*` enum,
  cache-flush macros, master-side inlines
- viciious/d32xr `mars_ringbuf.h` — the `| 0x20000000` uncached-alias
  pattern, literal
- viciious/d32xr `crt0.s` — dual vector tables, M_OK/S_OK handshake
- viciious/yatssd `32x.h` — MMIO address constants
- matiaszanolli/sega-vr-disasm `analysis/68K_SH2_COMMUNICATION.md` —
  the COMM partition diagram
- viciious/32XDK wiki "Bugs and quirks" — the
  "simultaneous-writes-to-same-SDRAM-word locks the system" rule
- https://www.chibiakumas.com/sh2/32x.php — Keith S Smith's tutorial
  series (not directly readable from sandbox but cited in the d32xr
  community thread)

## Level / geometry

### Bigger / more authentic map
**Status:** ✅ done — settled on a hand-tuned 32×32.

Tried three map sources in order: original Sketchfab lobby (22×22,
felt like "one big room"), movie.blend extraction at 32×32 (lost all
the doorways), movie.blend at 64×64 (had doorways but felt like one
long corridor in any direction). Final answer:
`tools/gen_backrooms_map.py` — a hand-designed 32×32 with five
distinct zones (NW office cubicles, NE nested rooms, central band
with pillars, SW twisty maze, SE lounge with stub walls) all meeting
at the spawn. Plays well on real hardware.

### In-game settings menu
**Status:** planned — audio volume the primary motivator.

Accessible via START (pause). Settings to expose:
- **Audio volume** — applies a runtime gain to the PWM samples
  before they're DMA'd. Simplest: pre-multiply each sample as we
  fill the buffer (when we move from straight ROM-DMA to a slave-
  generated buffer for the synthesis route). For ROM-DMA-only,
  rebuild with `--volume N` at build time.
- **Turn / walk speed** — currently hardcoded constants.
- **View distance / fog cutoff** — currently `MAX_VIEW_DIST = 6`.
- **Head bob amplitude** — currently `±2` pixels.
- **Toggle for the SH-2 sanity-check indicator square** (when we
  eventually re-enable that overlay for debug builds).

Storage: SRAM-backed if the cart has SRAM, otherwise per-boot. The
start menu (below) is the natural pre-game container for the same
options.

### Start menu / map selection
**Status:** planned

Boot screen that lets the player choose between the shipped hand-tuned
map and a procedurally-generated one. The procedural option leans
directly into the "AI dreamt this place" Backrooms vibe — each run is
a different layout, never the same place twice.

#### Menu UX

Title screen (`MARS BACKROOMS` or similar), three options:
1. **EXPLORE THE LOBBY** — boots into the hand-tuned 32×32 from
   `gen_backrooms_map.py`.
2. **WANDER A NEW PLACE** — generates a fresh 32×32 layout at boot
   using a PRNG seeded from the framecounter at the moment START is
   pressed. Same player presses START at slightly different times →
   never the same map.
3. **CONTINUE WHERE I WAS** (later) — restore last seed + position
   from save RAM.

D-pad to cycle, START to confirm. Background of the menu = static
ceiling-grid render with the title overlaid; reuse the raycaster's
existing palette and texture pipeline so it's almost free to render.

#### Procedural-generator design (the meat)

The pure-random "carve some rooms, drill some corridors" approach
will produce maps that feel like a maze game, not Backrooms. To
preserve the iconic Backrooms feel we want **zone-templated
generation**:

- Map is partitioned into a grid of 4 quadrants (16×16 each).
- Each quadrant gets assigned ONE template at random from a pool:
  `office_cubicles`, `nested_rooms`, `twisty_maze`, `pillar_lounge`,
  `long_hallway`, `dead_end_warren`, `false_partitions`, etc.
- Each template is a procedural sub-generator with its own parameters
  (e.g. cubicle grid 3×3 or 4×3, nested rooms 2 or 3 levels deep).
- A central spawn vestibule is carved in the middle, with four
  doorways into the four quadrants.
- A connectivity validator floodfills from spawn and re-rolls if any
  quadrant is unreachable. Cheap on a 32×32 grid (1024 cells).

Result: every map FEELS like Backrooms (because every zone is
recognizably Backrooms-y) but no two maps are the same.

#### Implementation notes

- PRNG: 32-bit xorshift, fits in ~10 SH-2 instructions, deterministic
  from seed → same seed = same map (good for "share a seed" and for
  debugging weird layouts).
- Seed source: free-running framecounter latched at START button press
  on the menu. ~60 unique seeds per second of menu time.
- Memory cost: zero — generation runs at boot, writes into the same
  `world_map[32][32]` buffer the hand-tuned map currently lives in.
  We'll need to change `world_map` from `const` to mutable plus a
  separate `const` table of templates.
- Time cost: budget ~50ms one-time at boot. Negligible.
- Save seed in save RAM so "Continue" can re-generate the exact same
  map without storing the whole grid.

#### Stretch: AI-generated map oracle

Way down the road — host-side tool that asks an LLM for "describe a
Backrooms layout as a 32×32 grid" and bakes the output into a
template pool the cart can pick from. Lets us seed the procedural
templates with actual AI imagination instead of hand-design. Pure
roadmap dreaming; needs nothing else first.

### Backrooms couch (8-angle directional billboard)
**Status:** designed, not implemented. Deep-research agent landed a
concrete recommendation.

A mustard-yellow vinyl 3-seater sofa in the SE lounge and NE nested
rooms — matches the "70s/80s waiting-room furniture, condition
slightly worn but not destroyed" canonical Backrooms vocabulary.
Mustard yellow uses the existing wall family palette so the couch
reads as "the wallpaper color but stained darker."

**Technique: 8-angle billboard with Doom-style mirroring** (4 unique
front-half textures + 1 side, mirror for back half). Same per-frame
cost as the existing neanderthal standup (~1.5 ms per visible couch).
Multi-angle gives "appears to rotate as you circle it" without the
~9-18 ms cost of true sprite stacking — which would bust the budget
on 23 MHz SH-2.

**Files to add:**
- `sh_src/couch_tex.h` — 5 × 64 × 32 × 1 byte = 10 KB of texture data
  (palette-indexed, baked by a new `tools/bake_couch.py`)
- New `couch_t` struct + `couches[]` array in `raycast.c` next to
  the existing `standups[]`
- `draw_couches` function modeled on `draw_standups`, with angle
  quantization (atan2 → 8 slots, mirror slots 5/6/7 → 3/2/1)
- `COUCH_BASE = 72` in the palette (8 shades, slot 72-79; existing
  layout has slot 80+ free)
- `point_in_couches(px, py)` AABB collision wrap in `player_update`

**Asset pipeline:** Blender or MagicaVoxel low-poly model →
fixed-camera render at 8 yaw angles → `tools/bake_couch.py`
quantizes each to the 8-color couch palette → emits `couch_tex.h`.
Mirrors the existing `tools/extract_floorplan.py` pattern.

**Scalability:** texture data is shared, only per-instance placement
adds bytes (~20 bytes per couch). 5-6 visible couches comfortable
per frame; 10+ would tighten budget. In practice 1-2 per major room
(~4-8 total in the map but usually 0-2 visible).

Full report from the deep-mine agent is in the session transcript;
the recommendation maps cleanly to the existing infrastructure with
~150 LOC + the texture data.

### Sprites populating the rooms
- Folding chair cardboard cutout (sprite pipeline already in place)
- Vent grate (could be drawn on a wall face or a free-standing standup)
- TV / pile of papers / other office detritus

### Door/exit silhouette
Single dark rectangle on one wall hinting at "the way out."

### Mind-bending anomalies
- Occasional 1-frame "glitch" (palette shift, brief static overlay)
- Corridor that loops you back where you started
- Watcher figure that appears briefly when you turn

## Audio

### PWM ambient fluorescent drone
Foundational before any music. The 32X has two PWM channels — even a
simple low sawtooth + filtered noise gives the iconic "the lights are
buzzing" presence that anchors the whole place.

### Footstep sounds on carpet
Triggered when player position advances.

### Kane Parsons-style ambient score
After ambient drone is in place. Slow swells, sub-bass, distant rumbles.

## Performance

### Mine the Doom 32X Resurrection codebase for techniques
**Status:** strategic resource — deep-mine report landed; concrete
adopt-list below in ranked-ROI order.

[viciious/d32xr](https://github.com/viciious/d32xr) is years of
optimization work to make Doom run smoothly on the actual 23 MHz
SH-2s. We've already borrowed:
- the `| 0x20000000` cache-through SDRAM alias for shared state
- the COMM4 doorbell + COMM6 arg-word convention
- the `MARS_SECCMD_*` command enum + slave polling dispatcher pattern

#### Ranked adopt-list (from the deep-mine research agent)

**1. SH-2 hardware DIVU latency-hiding for `1/perpDist`.**
The SH-2 has a hardware divider at `0xFFFFFF00`. Fire-and-forget the
64/32 divide, do other work for 39 cycles, then read the quotient.
At the top of every wall column we need `1/perpDist` for the texture
step — start the DIVU, do all ~30 instructions of lighting / clip-
bound / texture-offset setup during the divide, read the result at
the end. Reference: d32xr `r_phase6.c:190-247`. Expected: ~0.5-1ms/
frame savings on 320 columns.

```c
__asm volatile (
   "mov #-128, %1\n\t"
   "add %1, %1     /* %1 is now 0xFFFFFF00 */\n\t"
   "mov.l %2, @(0, %1)   /* divisor */\n\t"
   "mov #0, %0\n\t"
   "mov.l %0, @(16, %1)  /* dividend high = 0 */\n\t"
   "mov #-1, %0\n\t"
   "mov.l %0, @(20, %1)  /* dividend low = 0xFFFFFFFF; starts divide */\n\t"
   : "=&r" (t), "=&r" (divunit) : "r" (scalefrac));
/* ...do column setup work here for 39+ cycles... */
__asm volatile (
    "mov #-128, %0\n\t"
    "add %0, %0\n\t"
    "mov.l @(20, %0), %0  /* read quotient */\n\t"
    : "=r" (iscale));
```

**2. Hand-roll SH-2 asm wall column inner draw loop.**
The single biggest win. d32xr's `sh2_draw.s:56-74` is ~8 cycles per
pixel; our C compiles to ~15-20. For ~30K visible wall pixels per
frame, the asm saves ~9ms — **this one change likely puts us under
16.7ms (60 fps)**. Techniques inside the asm:
- 2× unrolling with odd-count peel (so the loop body always
  processes exactly 2 pixels)
- `swap.w` extracts `frac >> 16` in 1 cycle (vs `shlr16` = 2)
- `dt Rn` decrement-and-test sets the T-bit; `bf` consumes it →
  one-instruction loop terminator
- GBR-relative colormap fetch: `mov.l @(disp,gbr),r0` in 1
  instruction with no literal pool

**3. Work-stealing wall split via COMM6.**
Replace our fixed col 0-159 / 160-319 split with a single atomic
next-column counter in COMM6. Each CPU does
`R_Lock(); int c = next_col++; R_Unlock(); if (c >= 320) break; draw(c);`.
Naturally load-balances — eliminates the case where one half of the
screen has more close walls and the other CPU sits idle. Reference:
d32xr `R_GetNextPlane` / `R_LockPln`.

**4. GBR thread-local-storage for per-CPU state.**
SH-2 has a GBR register usable as a base pointer for 8-bit
displacement loads. d32xr keeps a per-CPU `mars_tls_t` struct
(colormap, fb, validcount, etc.) and sets GBR = `&mars_tls_pri` on
master, `&mars_tls_sec` on slave at boot. Then hot paths fetch
fields via single `mov.l @(disp,gbr),r0`. Eliminates stack-passed
arguments in the column-draw inner loop. Reference: `marsnew.c:471,
doomdef.h:1354-1370`.

**5. Compact sine LUT.**
Our 32-bit `sin_table.h` is 4KB and blows the entire 4KB L1 cache.
d32xr uses a `uint16_t[256]` quarter-wave with quadrant folding:
```c
q = angle / (FINEANGLES / 4);
if (q & 1) angle = ~angle;       /* reflect symmetric quadrants */
res = quarter_table[angle & mask];
return q >= 2 ? -res : res;
```
512 bytes total. Reference: d32xr `tables.c:2604-2619`.

**6. Cache-line invalidate macro.**
`*((volatile uintptr_t *)(addr | 0x40000000)) = 0` invalidates one
cache line in ~2 cycles. Currently we sidestep cache entirely with
the `| 0x20000000` alias, but cached + selective-invalidate is
faster when we know reads dominate writes. Reference: d32xr
`marshw.h:75`.

**7. SH-2 DMA + completion-interrupt audio mixer.**
Entire PWM audio blueprint is in `marssound.c` + `sh2_mixer.s`. Two
SDRAM ping-pong buffers, slave mixes one while DMA1 drains the
other into the PWM stereo register, completion IRQ swaps. ~1.5ms/
frame for 8 channels at 22 kHz. When we add audio, copy this
wholesale.

**8. Other clever tricks worth piecemeal adoption:**
- Materialize `0xFFFFFF00` in 2 instructions: `mov #-128, r1; add r1, r1`.
  Avoids 4-byte literal pool entry + data fetch.
- `muls.w` (1 cycle, 16×16→32) instead of `dmuls.l` (2 cycle, 32×32→64)
  when 32-bit result precision is enough (e.g. BSP sign tests).
- 4bpp textures with pre-swapped nibbles so the per-pixel
  unpacking is one shift + mask, no conditional.
- Sort drawables by texture identity to keep the 16-byte cache
  line hot across consecutive draws.

#### Critical correction from the research

SH-2 cache is **write-through**, not write-back. So writes via the
cache-through alias AND writes via the cached alias both reach
memory immediately. The "explicit flush before another CPU reads"
concern from our earlier work is unfounded for WRITES — flushes
only matter when one CPU previously *read* a value into its cache
and needs the next read to see the *other* CPU's update.

This means we can be smarter: shared-write-only state can use the
cached alias for speed, only the reader side needs occasional
`Mars_ClearCacheLine` calls.

### Texture mipmaps for walls
Multiple wallpaper texture resolutions per distance band. Close walls
sample 32×32, mid-distance 16×16, far 8×8. Cuts cache pressure on the
SH-2's tiny cache and looks crisper close-up.

### Floor-cast carpet at proper LOD
Currently every-4th-column stamp. Could compress further with a sparser
near-row pattern and skip floor pixels at horizon-band entirely (already
half-done — we skip max-dark rows).

## Tools / infra

### `make deploy` to MiSTer
✅ done — auto-scp's the .32x to `root@mister.office.local`.

### Squash-before-push workflow
✅ done — agreed protocol: WIP commits stay local, only squashed
commits push to GitHub.

### Blender floor-plan extractor
✅ done — `tools/extract_floorplan.py` runs in headless Blender and
emits both an ASCII visualization and a ready-to-paste C array.
