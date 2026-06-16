# Backrooms 32X — roadmap of unresolved / future work

Each item below is something we attempted, hit a wall on, or deliberately
deferred. Listed in roughly the order I think they'd be productive to
revisit.

## Visual / atmospheric

### Ceiling lights as actual grid-tile illumination
**Status:** ✅ done — scanline trapezoid fill from 4 projected corners
of each axis-aligned ceiling tile. Per-edge slope precomputed once,
per-row left/right reconstructed by linear interpolation, fill row
with z-test against walls. Plus a 2-bulb fluorescent troffer pattern
inside each tile (dim outer frame, two bright bulb bands, medium
gap), and a grid of fixtures populated at init from `world_map` at
every 2nd cell. Per-light flicker stays as a brightness offset on top
of the bulb pattern, gated by the LIGHTING_FLICKER toggle in the menu.

### SH-2 dual-CPU split
**Status:** ✅ done — multiple iterations. Current architecture is the
column-split: each CPU owns a vertical half of the screen and does its
own clear + ceiling grid + carpet + walls in parallel; one COMM4 sync
per frame before sprites/lights. Shared state uses the `| 0x20000000`
cache-through alias as planned. The hand-rolled SH-2 asm wall pixel
loop (see Perf section) sits inside this split.

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
**Status:** ✅ done — START opens a nix-terminal-style overlay with
AMBIENCE and FOOTSTEPS sliders (0–100). Implementation in
`sh_src/menu.c` + hand-rolled 8×8 bitmap font in `sh_src/font.c`. The
audio decoupling (ambient slider scales buzz/neon/hello but not steps)
ships with this. Still in the not-done pile from the original spec:
turn/walk speed, view distance, head bob amplitude — none are blocking
anyone, easy to add later.

### Sega 32X boot logo
**Status:** designed by research agent, not implemented. Candidate
catalog in the session transcript; recommended option is **Candidate
2: palette-cycle shimmer** — static 32X panel (orange/yellow rounded
rect + blue SEGA + red 32X), animated yellow border via CRAM rotation
(same trick `raycast_shimmer()` already uses on the lights). ~190 LOC,
~70KB ROM. Natural pairing with the start menu below.

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

#### Procgen tuning knobs (post-redesign refinement)

**Status:** deferred — set up after the building-blocks-based generator
ships, to make the "feel" tunable without code changes.

Once the new generator is in place (spine corridor + side rooms +
clustered room pairs + pockets + partitions), build a small constants
block at the top of `procgen.c` that lets us dial the procgen "feel"
in one place:

- `PROC_NUM_SIDE_ROOMS`        — how many rooms attach to the spine
- `PROC_NUM_CLUSTER_PAIRS`     — connected room-pair count
- `PROC_ROOM_SIZE_MIN/MAX`     — room dimensions in cells
- `PROC_POCKET_DENSITY`        — fraction of corridor cells getting an alcove
- `PROC_PARTITION_DENSITY`     — fraction of rooms ≥ 4×4 getting a partition
- `PROC_CORRIDOR_WIDTH`        — 1 or 2 cells wide
- `PROC_SPINE_ORIENTATION`     — horizontal / vertical / both
- `PROC_PILLAR_BUDGET`         — explicit cap; default 0 (zero stray pillars)

Eventually wire these into the in-game menu's TUNING tab so the
player can A/B procgen feels without rebuilding. For now just expose
them as compile-time `#define`s once the generator is shipping —
makes A/B testing in iteration cycles ~one line of code each.

Also worth adding when this lands: a "validate / re-roll" pass that
checks min walkable cell count (e.g. >= 200), max isolated pillar
count (e.g. <= 4), and floodfill reachability from spawn. Re-rolls
the seed if any check fails. Cheap on a 1024-cell grid.

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
- ✅ partial — distant fluorescent strobe on walls past `FOG_RAMP_DIST`.
  Per-cell hash + shared frame counter make distant dark cells
  occasionally flicker to dim-yellow ("a fluorescent panel trying
  to start in the haze"). Lives in `draw_walls`.
- Watcher figure REMOVED — the silhouette standup that vanished on
  approach was alluding to something we hadn't built. The infra
  (`standup_t.silhouette` field + draw_standups branch + per-standup
  vanish-on-distance check) is still in place for future reuse.
- Open: occasional 1-frame full-screen palette shift (chromatic
  glitch)
- Open: corridor that loops you back where you started (loop-warp
  zone)

## Audio

### PWM ambient fluorescent drone
✅ done — slave SH-2 mixes a 30s buzz loop + occasional neon sting on
the PWM mono channel via DMA1 ping-pong buffers. Plus the Voyager
Golden Record neanderthal-positional hello (distance-attenuated) and
carpet footsteps gated on `is_walking`. See `sh_src/sound.c`.

### Footstep sounds on carpet
✅ done — shipped with the audio buildout. Sample baked from
`sound/ES_Footsteps...`; runtime `step_volume` is independent of the
ambient slider.

### Kane Parsons-style ambient score
After ambient drone is in place. Slow swells, sub-bass, distant rumbles.
Still pending — would layer over the existing buzz/hum bed.

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

**1. SH-2 hardware DIVU latency-hiding for `1/perpDist`.** ✅ done.
Wired up via `divu_start_u32` / `divu_read` in `sh_src/sh2_asm.h`.
Wall column code starts the divide, then computes `wall_shade` +
texture coordinate setup during the 39-cycle latency.

**2. Hand-roll SH-2 asm wall column inner draw loop.** ✅ done.
4-pixel-per-iter inline asm block in `draw_walls` — keeps `tex_pos`,
`p`, `shade_lut`, `step`, `mask` in registers, uses indexed byte load
via `@(R0,Rm)` and `dt`/`bf` for the count-down. Measured on
hardware: master half-render time dropped from 44000→33500 FRT ticks
(24%), slave from 44000→25000 (43%). Frame crossed a vsync boundary
in the wall scene (15fps → ~20fps).

**3. Work-stealing wall split via COMM6.** ✅ done, then reverted.
Implemented per d32xr's pattern. Reverted in favor of the column-
ownership split (each CPU owns a half), which has no per-column TAS
overhead and gives natural load balance since walls cluster predict-
ably with view direction. Kept the COMM6 infrastructure in shared.h
for future use.

**4. GBR thread-local-storage for per-CPU state.**
Still open. Now unblocked by #2 — the inline asm in `draw_walls` would
benefit. Would let each CPU's `shade_lut`/`screen_w`/`tex_h_mask`
fetches become single `mov.l @(disp,gbr),r0` instead of stack-passed
arguments. Estimated ~5% additional inner-loop win.

**5. Compact sine LUT.** ✅ done. `sh_src/sin_table.h` is a
`uint16_t[256]` quarter-wave; `COS_FX`/`SIN_FX` macros do the
quadrant folding via `swap.w` + sign flip.

**6. Cache-line invalidate macro.** ✅ done. `Mars_ClearCacheLine`
and `Mars_ClearCacheLines` in `sh_src/sh2_asm.h`.

**7. SH-2 DMA + completion-interrupt audio mixer.** ✅ done.
`sh_src/sound.c` mixes a ping-pong `amb_pwm_buf[2][256]` on the slave;
DMA1 streams the active buffer to `MARS_PWM_MONO`; `amb_dma_handler`
swaps + re-arms. Mixes buzz + neon + positional hello + footsteps.

**8. Other clever tricks worth piecemeal adoption:**
- `0xFFFFFF00` 2-instruction materialization — used in `divu_*` helpers
- `muls.w` over `dmuls.l` — opportunistic; `mul_hi32_s` helper uses
  `dmuls.l` where 64-bit precision is actually needed
- 4bpp textures with pre-swapped nibbles — not adopted; our 8bpp
  framebuffer already fits the use case
- Sort drawables by texture identity — not adopted; standup count
  is small enough that the cache hit pattern doesn't dominate

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
✅ partial — distance-based LOD swap between 16×16 lo-res and 64×64
hi-res wall_tex, threshold `WALL_LOD_THRESHOLD = FX(2)`. Hi-res is
column-major for cache-friendly per-stripe scans. Same LOD pattern
also applies to the neanderthal sprite (32×64 lo-res ↔ 128×256
hi-res column-major, threshold `FX(3)`). Three or more bands could be
added later; not currently a bottleneck.

### Floor-cast carpet at proper LOD
Still open — currently every-4th-column stamp covers the full bottom
half. Could compress further with a sparser near-row pattern and skip
the horizon-band entirely (already half-done — we skip max-dark rows).

## Tools / infra

### `make deploy` to MiSTer
✅ done — auto-scp's the .32x to `root@mister.office.local`.

### Squash-before-push workflow
✅ done — agreed protocol: WIP commits stay local, only squashed
commits push to GitHub.

### Blender floor-plan extractor
✅ done — `tools/extract_floorplan.py` runs in headless Blender and
emits both an ASCII visualization and a ready-to-paste C array.

### Wallpaper + sprite baker tools
✅ done — `tools/bake_wall.py` and `tools/bake_neander.py` quantize
PNGs to palette-indexed C headers. Both emit column-major output by
default for SH-2 cache friendliness.

### `make deploy-tv` to second MiSTer
✅ done — `make deploy-tv` SSHes the TV MiSTer (`mister.tv.local`)
and probes `/media/usb0` then `/media/usb1` for the S32X dir before
scp'ing, so USB renumber doesn't break the push.

### FRT-based on-screen profiler
✅ done — top-right overlay shows `T:NNNNN H:NNNNN S:NNNNN` (frame
total, master half-render time, slave half-render time) sampled from
SH-2 free-running timer at Φ/32 (1.39μs per tick). Both CPUs init
their own FRT; slave publishes its delta via `SHARED_UC->slave_render_ticks`.
Remove the overlay before shipping a release build.
