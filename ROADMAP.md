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
**Status:** strategic resource — every perf push should check what
d32xr already solved before reinventing it.

[viciious/d32xr](https://github.com/viciious/d32xr) is years of
optimization work to make Doom run smoothly on the actual 23 MHz
SH-2s. We've already borrowed:
- the `| 0x20000000` cache-through SDRAM alias for shared state
- the COMM4 doorbell + COMM6 arg-word convention
- the `MARS_SECCMD_*` command enum + slave polling dispatcher pattern

What we haven't borrowed yet, in rough order of probable payoff:
- **Hand-rolled SH-2 assembly for hot inner loops** — wall column
  draw, texture sample, fixed-point mul. GCC for SH-2 (especially the
  ancient sh-elf branch) leaves obvious wins on the table. d32xr has
  hand-tuned column draws that beat our compiled ones easily.
- **SH-2 internal DMA controller** for SDRAM-to-FB / texture
  decompression. Free cycles while DMA runs. d32xr's `Mars_Secondary`
  inits SH2_DMA_DMAOR etc. — we set none of those.
- **Ring buffer between CPUs** (`mars_ringbuf.h`) for streaming work
  units instead of single-command doorbell. When we want each CPU to
  process N items independently (e.g. per-sprite work, per-zone
  light), ring buffers give us continuous throughput vs. our current
  signal-wait-ACK overhead.
- **Cache-aware data layout** — align hot structs to 16-byte cache
  lines, group fields that are written together. Our shared.h is
  naïve about this.
- **VDP overwrite-buffer tricks** (0x24020000) for sprite over-draw
  without clearing first.
- **PWM channel patterns** for audio — they have a full mixer working.
- **Bank-switched ROM** for >2 MB cart contents, when we eventually
  bundle music or extra maps.
- **Interrupt-driven vsync** (VINT handler) instead of our COMM12
  polling — frees the master to do useful work in vblank.
- **Their level / palette / texture loading pipeline** as a template
  if we ever want runtime-streaming assets.

Whenever we hit a perf wall, the first move should be: search d32xr
for how they solved that exact problem.

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
