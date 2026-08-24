# Backrooms 32X — roadmap of unresolved / future work

Each item below is something we attempted, hit a wall on, or deliberately
deferred. Listed in roughly the order I think they'd be productive to
revisit.

**Working order right now (decided 2026-08-06), in this sequence:**

1. **The wall hole** — look ✅ and animation ✅ done 2026-08-07; only the
   procgen frequency is left. See *Exit hole* under Level / geometry.
2. **The PVM** — first customer for the GLB box-import path, then an animated
   static screen on it. See *GLB import polish* under Tools / infra.
3. **Master System** — the Mode 4 look first, real SMS binaries only after.
   See *Master System arc* under Tools / infra.

## Story / triggered lighting  (requested 2026-07-24)

**Status:** feature request — not started. Mike wants lighting to become a
narrative/scripting tool, not just static ambiance.

The vision, smallest-first:

- **Walk-on trigger lights.** A cell (or a tagged space) is dark until the
  player steps onto/into it, then its light turns on. The first building block:
  event-driven, per-cell light activation tied to player position.
- **Sequenced cell lighting.** Once single-cell triggers work, chain them: a
  corridor that lights up cell-by-cell ahead of (or behind) you, timed light
  sequences, a room that powers on in a pattern. Lighting as a scripted beat.
- Beyond that: "lighting all around" — this is meant to grow into a whole
  system (reactive dark rooms, flicker-to-life, follow/lead lights, maybe
  scripted light cues that pair with the audio work).

**Infra we already have to build on:** `cell_light[MAP_H][MAP_W]` (per-cell
light level + the CELL_DARK top-bit for dark rooms), `init_lights` that seeds
it per map, and the gen-gated secondary purge so both CPUs stay coherent when
it changes ([[reference_secondary_cpu_cache_coherency]]). The NEW part is a
*runtime* mutation path: mark cells for trigger/sequence activation at map load,
then flip their `cell_light` (and re-fog / re-shade) as the player crosses
tiles, with the secondary purging the changed lines. A small per-map "light
script" (cell + condition + timing) is the likely data model — mirrors how the
crawlspace/dark-room rects are authored per map.

Open design questions to settle when we start: authored (per-map light-script
table, like decals/dark rects) vs procgen-tagged; instant vs flicker-to-life
ramp; one-shot vs re-armable; how a sequence is timed (frame counter vs
player-crossing chained triggers).

## Visual / atmospheric

### Game-on-glass: the live SMS picture on the PVM  (NEXT UP, queued 2026-08-12)
**Status:** designed, unblocked — full plan in `sms/DESIGN.md` 4c. The
old VRAM-snoop question is moot: the SMS mini-game's display never
touches VRAM; TILEBUF in Z80 RAM is the whole picture and the 68K
already copies it out every dirty frame (`sms_game_tiles`). Work: 68K
free-runs an index+word rotation on COMM4/COMM6 (no handshake — the
joypad-bridge starvation lesson), SH-2 samples a few dozen pairs per
frame into a local copy, renders 32x24 lit/unlit phosphor cells on the
PVM glass texture (v2: real glyphs via the SH-2 font). Static until the
Z80's first DIRTY; the convergence tearing is the signal-lock look and
stays. ~1 day; the only tuning risk is the sampling rate. Pairs with
the diegetic PVM+console trigger that replaces the TESTING menu rows.

### Zoom-into-the-glass transition  (Mike, 2026-08-12 — the long thread)
**Status:** ✅ BUILT 2026-08-13 (local, tuning). Shipped shape differs
from the original sketch in one important way: there is NO world-space
camera dolly — the player never moves (position, angle, eye untouched;
the return from the session is the exact view that pressed A). The move
is a screen-space camera-zoom-to-rect: the rasterizer publishes the
glass's projected rect each frame (face quad corners inset by the glass
texel fractions, mirrored-U accounted, uncached for the CPU split); at
session entry a virtual window eases from full-screen down onto that
rect while the picture — re-rendered from tile ids at every size, never
scaled pixels — rides it. 5 frames of the world magnifying in place
(in-place buffer zoom, centre-out row order, per-parity window tracking
because the buffers alternate) with a checker dissolve sparing a
bezel-sized zone, then 4 black-surround frames of the final rush. The
last frame is pixel-identical to the session renderer's native draw, so
the handover is invisible. The reverse (session → world) is still a
cut; run the same ladder backwards when it matters.

### SMS fidelity tuning  (Mike, 2026-08-13 — standing ambition)
**North star: we have the REAL Z80. Nothing in the 32X layer is allowed
to be the reason the Master System feels less than authentic** — the
game logic, music, and timing are genuine SMS-class hardware; the 32X
side is only a display and must behave like a transparent CRT.

Current state of the display chain (fullscreen SMS32X, default ON):
Z80 frame → 68K TILEBUF copy → full-rate COMM rotation (SMS_TILE_DIV=1;
the /2 divider was stretching the rotation, and the rotation period IS
the input-to-photon delay — halving it cost nothing, Z80 music verified
fine) → whole-picture promote → native 8x8 paint. Roughly one frame
behind the old MD-blit path, worst case.

**BUILT FLAGGED 2026-08-13** (TESTING>EPOCH, default OFF; legacy rotation
is the shipped arm — promote on the bake-off, not the vibe). As designed
below, plus: payloads are ABSOLUTE content so a lost epoch only leaves
stale cells; repair slots ride epoch tag 63 (full rate when idle, 1-in-32
under deltas); the whole picture queues as epoch 0 at arm so the first
paint is atomic; the COMM10 word keeps 6 epoch bits + bit15 for markers,
leaving the format room for the per-cell attribute bits the accuracy arc
wants. Original design rationale:

Dirty-epoch deltas: the Z80 already flags DIRTY
and the 68K knows exactly which words changed at copy time, so
broadcast only the changed words tagged with a frame epoch; the SH-2
applies epoch N atomically. Collapses small-change latency to transport
time. Why it is NOT built yet: it trades the rotation's self-healing
property (a lost slot just comes around again) for bookkeeping — a
missed delta is a permanently stale cell, and the no-handshake rule
(joypad-bridge starvation) bans naive ACKs. If built, build it WITH the
slow background repair rotation from day one, and only after hardware
confirms the residual frame is actually felt. Accuracy is NOT the axis:
promoted pictures are already bit-exact; epochs buy latency only.

Beyond latency, the accuracy backlog for a "most accurate SMS engine"
pass: per-cell colour (the broadcast carries ids only — ink is one
palette entry; real SMS tiles carry palette bits), sprite layer (the
harness is tile-only today), and the mode-4 duet (~64a0a21) for real
.sms compatibility. Each widens the channel or the Z80 contract; sized
separately when the arc is scheduled.

### Shadow VDP: real mode 4 on the 32X  (Mike, 2026-08-13 — the killer feature)
**North star:** the console in the backrooms shows ACTUAL Master System
output. The Z80 is real silicon (cycle-identical, better than emulated);
the only missing chip is the VDP, and the VDP is write-only in practice —
so it reduces to a state machine eating port writes plus a renderer.
The zoom then dives into a real machine: the storytelling element.

**Slice 1 ✅ BUILT + HOST-VERIFIED 2026-08-13:** sh_src/smsvdp.c — true
mode-4 shadow VDP: control-port latch semantics, 16KB VRAM + CRAM + regs,
name table with h/v flip + palette select + priority, 4bpp planar
patterns, sprites (8x8/8x16, lowest-number-wins, priority-respecting),
X/Y scroll, left-column blank, border, display gate. `make vdp-test`
drives it with port writes only, asserts 11 pixels, and drops a PPM test
card. Not yet: zoomed sprites, per-line raster, scroll locks, the
8-per-line overflow flag.

**Slice 2 (next):** the wire. Z80-side driver = a port-write ring in Z80
RAM (real SMS VRAM can't fit in 8KB — the ring IS the latch Mike named);
68K drains the ring into the dirty-epoch channel (built, flagged, ear-
verified faster today); SH-2 feeds smsvdp and blits its 256x192 into the
session frame with the SMS palette bridged into 32X CRAM. Sim harness
extension before build roulette, per the house rule.

**Slice 3:** a Z80 program that draws like an SMS — TEST PATTERN card
rebuilt in real mode 4 (colour, sprites); then the mini-game inherits
the whole SMS art vocabulary. The fidelity ceiling stops being the
channel and becomes the art.

### Every game ships as a real .sms  (Mike, 2026-08-13 — artifact pass)
Each compiled SMS game also emits a `games/<name>/<name>.sms` playable
on actual Master System hardware — the proof the games are real SMS
programs, not demos. hello already dual-targets (hello.sms passed real
hardware 2026-08-09); the maze needs four pieces, all scoped: (1) PSG
write indirection (the $7F11 memory-mapped write is an MD-ism; real SMS
is OUT $7F — one routine, conditionally assembled, byte-parity sim
guards the refactor), (2) an SMS harness shim implementing the same
mailbox contract from vblank ISR + pad port + real VDP blit (hello.asm
has the VDP init, font upload, and SEGA header to lift), (3) a baked
canonical map in the MAP_BITS hole at assembly time (no 68K patcher on
real hardware), (4) the Makefile artifact target. OpenEmu loads .sms
for the verify loop. Sized for the SMS polish thread: pure sms/ work,
zero 32X coupling.

### SMS games gate the backrooms  (Mike, 2026-08-13 — the engine becomes a game)
**North star:** the console stops being a diegetic toy and becomes the
key. Some procgen rooms generate with NO exit; the player discovers this
on a terminal (the START overlay already speaks nix — an `exits: none`
readout is the discovery beat) and the way out is on the other side of
the glass: something done in the Master System game reveals or opens an
exit in the room the player is standing in.

**The interference channel:** map items inside the SMS ROM carry real
procedural-map consequences in the backrooms. This is the piece that
turns the display plumbing (game-on-glass, zoom, shadow VDP) into a
game loop — state flows back OUT of the Z80 for the first time, not
just pictures. Plumbing exists in sketch form: the joypad bridge and
COMM channel already run both directions; what's new is a small
Z80→68K→SH-2 *event* contract ("level exited, solved=Y/N", "item
touched") distinct from the tile stream.

**Exit-revealing mechanisms** (each is a distinct authored device, all
end in "the room now has an exit"):
1. **Interfacing a game** — play the SMS game to a goal state.
2. **Power** — leveraging power somewhere in the world.
3. **A switch** — the direct version; probably the first one built.
4. **An anomaly** — no design yet, but Mike flags it as a key feature.
   See *Mind-bending anomalies* under Level / geometry; those stop
   being ambience and become load-bearing.

**Solved-flag contract:** exiting a maze level flags it SOLVED. Exiting
a maze level UNSOLVED and being returned to the Master System game is a
meaningful state, not a dead end — by design, something must then
happen, either in the backrooms or in the SMS game, to move the player
forward. The unsolved return is itself a trigger the design spends.

Open questions to settle at build time: where solved flags live (Z80
RAM dies with the session — 68K or SDRAM owns persistence); whether a
no-exit room is a procgen tag or authored; how the exit appears
(reveal an always-there hidden exit vs carve one — `place_exit_door`
and the exit-hole path are the reuse candidates); what the terminal
readout looks like; anomaly design entirely.

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

### Wall-profile system — carved architecture ("architecture by omission")  (2026-07-25)
**Status:** designed, not built. The cheap-path answer to arches/openings/doorways
— distinct from the MAP_RES fork (that's for free-standing thin geometry + floor
heights). Everything here stays on the grid-wall DDA path, so **zero partition
tax**; the only new per-frame cost is "continue the ray past a carved cell,"
which void/exit cells already pay.

**The primitive:** a wall cell stops being a boolean (wall/void/exit) and carries
a small per-cell **profile** — where the solid part of the column lives:
- floor-anchored partial (floor->H) — ALREADY EXISTS as `part_height` (see-over dividers)
- **ceiling-anchored partial (ceiling->D) — the missing inverse; the key new primitive**
- mid-carve (header + opening + optional sill)

Reuses already in the tree: void/exit ray-continue (template for "draw a band,
keep marching"), the crawlspace bulkhead/mouth cap (a ceiling-anchored band
draw), `FRAME_BASE` (jamb/casing palette), `g_door_target` (swinging door), the
crawlspace eye-height gate (walk-under).

**Staged deliverables (shared foundation first):**
1. **Foundation** — per-cell profile field + column draw clips to it + DDA
   continues past a partial (void-cell path is the template). Everything below rides this.
2. **Bulkhead** — ceiling-anchored partial (hang a soffit/beam, walk under). It's
   `part_height` mirrored top-down; highest reuse, first to land. NOTE: we do NOT
   currently do top-down — we do floor-up (`part_height`) and lowered *ceilings*
   (`CEIL_H`). This is the new one.
3. **Arch** — walk-through opening: ceiling-anchored header + open-below + passable
   cell. Flat lintel first; curve is polish.
4. **Doorway** — mid-carve + jamb REVEAL (draw the wall-thickness side faces in
   `FRAME_BASE` so the opening has depth = "the interior cell") + the door leaf
   recessed in it. Ties into the "different door types" list.
5. **Curve LUT** — round/pointed arch tops (polish).

### Exit hole  (2026-08-07)
Three fixes were queued in the order 3 → 2 → 1. **3 and 2 are done**; 1 is all
that is left.

**3. Believable up close — ✅ done.** The head and sill always had reveals; the
left/right edges never did, so the wallpaper met the cavity on a hard line and
the opening read as painted on. `HOLE_REVEAL_D` gives it the wall's cut
thickness, the door recess's trick. Three findings worth keeping:

- The wall ramp **bottoms at luma 73**, so an interior painted from it reads
  mid-brown however hard the shade is pushed. The hole shades through its own
  `hole_ramp` — the wall ramp continued one warm step past its floor
  (`DOOR_DARK+2`, luma 60). A tail run all the way to near-black was *worse*:
  an uncanny blob behind a glowing frame. Depth here is a deep colour, not an
  absence of one.
- What made it read as a **frame rather than a room** was not the palette. Every
  surface converged on the same murk, so the corners were one dark dithering
  into another — no edges anywhere. Only the back panel reaches murk now.
- Interior darkness must be **absolute, not N-steps-below-the-face**, or
  standing next to a hole lights its inside.

`TESTING>HOLEJAMB` A/Bs the whole close-up path in one binary.
`tools/sim_exit_hole.py` renders the real code path at a replayable camera
(`--dist`, `--off`, `--fix`) — every look decision was made there first and
confirmed on hardware. Use it before touching this again.

**2. Climb + arrival — ✅ done.** The pull-up glanced UP and rode one linear
ramp forward, which reads as levitating. Four scripted beats now (plant, haul,
hang, shimmy) and the tell is where the eye looks: at the hands, then down at
the floor of the hole for the whole lift, and only at the end back up and
forward. **Pitch is positive DOWN** — the old `-22` pointed the wrong way for
every frame of the lift. `PULLUP_FRAMES` 21 → 16, since those are *rendered*
frames and at ~11fps 21 ran close to two seconds.

The far side is now a FALL, not a cut: the picture comes up during the drop,
lit by the landing, compressed into a crouch. The stand is a **handoff** —
`player_update`'s existing crouch-release finishes it, `standup_dip`
floor-glance included. Nothing is behind you when you turn around.
Knobs: `PU_*` and `AD_*` in raycast.c, `DROP_FRAMES` in m_main.c. Tuning pass
still wanted.

**1. Too frequent in procgen — still open.** Spawn-weight problem. Test with
`place_exit_door` / exit-hole placement side by side.

**Interior crawl — ✅ shipped as the CORRIDOR (2026-08-07).** Not a raycaster
mode: a scripted one-point-perspective duct (`raycast_crawl_corridor`),
three cells, axis-aligned ring fills, bathtub lighting (both mouths lit,
middle dark), destination peek on the end plane growing to a near
match-cut. Entering it IS the map flush (`corridor_enter`): the old world
is discarded at the threshold and the corridor is the next map's entrance
hallway — which deleted the commit-time generate/peek/restore dance.
Remaining polish: peek-render fidelity ("the sample screen") and a
rear-view shrinking aperture behind you if it ever earns its cost.

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
Now load-bearing, not just ambience: the *SMS games gate the backrooms*
arc (Visual / atmospheric) wants an anomaly as one of its
exit-revealing mechanisms.
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
✅ done — secondary SH-2 mixes a 30s buzz loop + occasional neon sting on
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

### Stereo directional hello  (nice to have, 2026-08-09)
The neanderthal's Voyager hello is distance-attenuated but mono: DMA1 is
destination-fixed on `MARS_PWM_MONO`, so both PWM FIFOs always get the
same pulse width. Directional needs the stereo output path: interleave
the ping-pong buffers L,R and switch DMA1 to longword transfers at the
LCH/RCH register pair (one DREQ = one stereo frame), then pan ONLY the
hello by relative bearing (compass-dot trick from the directional
sprites); buzz/neon/steps stay centered so the mix cost barely moves
(fill is ~321 ticks mono, estimate +1/3). Build behind a same-binary
AUDIO>STEREO toggle with a hard-panned test tone — this reopens the
DMA/DREQ engine from the 16 ms-chop saga, and Ares-vs-MiSTer pacing must
be verified by ear before it defaults on.

## Performance

### The frame is now measured — and the bottleneck moves  (2026-08-06)
The HUD accounts for ~93% of frame time (gap: 58 ticks). New counters: `I`
(lights, split out of `P`), `HU` (post-render draw), `SW` (swapBuffers), `ID`
(secondary barrier), `OD` (wall coverage %).

**There is no single bottleneck. It flips with scene type:**

| scene | dominant | ticks |
|---|---|---|
| corridor | walls | 6,268 |
| open room | floor + ceiling | 7,929 (walls only 3,581) |
| crawl / dark room | slab + sprites | 7,349 |

Every resolution tier in this engine is on the *wall* pass, which tops the list
in one of those three. Ask which scene before picking a target.

**Closed with numbers — do not re-litigate:** the dual-CPU split is already
optimal (`ID` 93, H and S within 51 ticks). Lights are ~1.5% of frame. Quarter
res is nerfed out of AUTO and LOD.

**Still open:** ceiling has no LOD at all (3,286 ticks in open rooms) and cannot
take the covered-row skip as-is — its grid lines compare world coords between
*consecutive* rows, so skipping breaks the chain. Slab (`L` 4,657 in crawl
scenes) is the largest untouched pass and already has a BULKHEAD A/B toggle.
Unexplained: `C` (clear) jumped 373 → 1,015 between two frames at the same
camera, outside its usual 336–762 band.

**Measurement traps that cost hours here.** `WALLS=AUTO` is path-dependent —
the stillness ratchet means standing still to read the HUD changes the number,
so pin `WALLS` before any capture. The profiler itself costs 2,255 ticks and is
present in every historical number in this file. Uncommitted iterations share
one build stamp, so commit between A/B arms or you cannot tell which binary you
measured.

### Bus-contention hunt — PARKED until hardware  (2026-08-12)
**Status: three toggles built, all defaulting OFF, none proven. Do not measure
them in Ares. Retest on MiSTer, September 2026.**

Premise: removing a constantly-streamed audio sample from the mixer bought real
frames, so the same shape — something running continuously that nobody counts —
was worth hunting elsewhere. Three sites found, all confirmed by reading:

| site | what it does | toggle |
|---|---|---|
| `s_main.c` idle loop | `for (volatile int i…)` — 256 write-through SDRAM stores per COMM4 poll, continuously, aimed at the SDRAM the primary renders from | `TESTING>IDLE` |
| `m_main.c` ×6 | bare polls of 32X sysregs at ~3M/sec, including the FBCTL flip wait (up to a full vblank) and the unbounded ULTRA park | `TESTING>SPIN` |
| `md_main.c` `do_commands` | 68K reads COMM0 every loop iteration, forever — the only actor never throttled | `TESTING>68K` |

**Why it is parked, and this is the whole point:** all three trade CPU cycles for
reduced bus contention. Ares models the cycles faithfully and cart-bus
arbitration between the 68K and two SH-2s much less so, so it shows the full
*cost* of each change and little or none of the *benefit*. The instrument cannot
see the thing being measured. The one Ares A/B run (B00299, same pose both arms)
came back net worse — T 22018 → 23654, H +647, S +509, HU +369 — and its only
honest content is that the costs are real:

- The 68K backoff genuinely costs ~369 ticks of `HU`. The HUD is not one burst;
  it is many short `HwMdPuts` runs with SH-2 work between them, so the 68K goes
  idle and re-backs-off before every one, paying the latency each time.
- A fixed nop count is the wrong replacement for the volatile loop. Those
  stalling stores made the old loop **accidentally self-tuning** — it stretched
  precisely when the bus was busy, which is backpressure, not waste. `IDLE` now
  delays on the FRT (on-chip, zero bus, true wall clock) so the poll rate is
  pinned by construction rather than by a guessed iteration count.

**Two measurement traps this arc walked into, both worth the file space:**

`SW` **is slack, not cost.** It is the primary waiting for the flip. When work
grows there is less slack left to wait in, so `SW` shrinks. Its 751 → 359 in that
A/B was read as a win; it was a symptom of the regression sitting next to it.
A throttle on a wait cannot make the wait shorter — only the contention it
relieves can pay.

**A bundled toggle cannot attribute.** All three shipped behind one flag, so the
net-worse result could not be split into the change that hurt and the change that
did not. They are three flags now, each with the metric that measures it alone:
`SPIN`→`SW`, `IDLE`→`H`/`S`, `68K`→`HU`.

Per-pass costs were **identical** across both arms (`W`/`R`/`G`/`I`/`P` to the
tick), which is the useful invariant: none of this touches render work, so any
real delta lives entirely in the overhead paths.

**Still untouched from the original candidate list:** the box renderers are
deliberately kept out of `.ramtext` (`raycast.c` — LTO folding them into the
`draw_standups` blob overflowed the ram region), so they stream instructions off
the cart every frame a chair/desk/PVM is visible — the same bus the 68K is
flooding. Never measured.

**Ruled out, do not re-derive:** game-on-glass is not a bus problem. A glass
session lives exactly `GLASS_BEAT_FRAMES` (8) frames and the only way in is
`raycast_pvm_use() == 2`, i.e. standing at the desk with the tube filling the
view. A visibility cull for it was built and reverted — it guarded a state that
cannot occur and added an uncached write per face on both CPUs to save nothing.

### Partition parity — deferred items + the cost wall  (2026-07-24)
**Status:** parity batch banked (commit "Partition parity: near-slab LOD…").
The slab/overlay path is a second-class render citizen: it re-implements, by
hand, every system the wall path gets for free. This session ported the
high-impact ones (texture LOD, per-column LOD, see-over PART_TOP, cap-plane
occlusion, topple line-of-sight). **Deferred, low-impact, cosmetic — do NOT
invest pre-pivot:**
- **#8 SEAMS** — overlay records no silhouette into `seam_top/seam_bot`, so
  counter top edges staircase at coarse res and the smoother can chew the wall
  edge behind a slab.
- **#9 DITHER** — slab bands never dithered (keyed on the #8 seam buffers).
- **#12 dark-room / crawlspace shade** — overlay shade block lacks the
  `g_lowceil` / `g_dark` modifiers, so a counter glows like a lit plank on dark
  maps / under low headers.
- **Top-cap lip dropout** — the 0.35-cell lip clamp (`raycast.c` countertop
  block) foreshortens to sub-pixel, so half-height counters go topless at
  distance. Relaxing it reintroduces "THE seam" (build-127). Needs a real fix,
  not just a bigger clamp.
- **Topple-through-end-cap** — `standup_wall_reach` marches only the fall
  centerline; a glancing topple parallel to a counter's end-cap isn't capped and
  the flat body draws beside/through the thin end slab. Test-fixture edge case.
- **#2 `cap<0` guard** (1-line safety), **#11 vert-mode branch** (wasted odd-row
  writes, invisible), **#13 far strobe** (cosmetic).

**The wall behind all of it:** measured `F:` in a slab-heavy view is F:05
standing / F:09 walking — *below the 15 floor regardless*, and near-independent
of standup count (partitions dominate, the 3 neanderthals added ~0 standing).
Parity makes partitions **correct, not fast**. Getting above the floor with
partitions in view needs the structural fork, not more parity patches:
decouple grid resolution from cell size (`MAP_RES`) so a thin divider becomes a
first-class DDA cell and inherits depth/LOD/occlusion natively — cost is DDA
steps + map RAM. **STATUS 2026-08-09: formally BOOKED as its own arc, scheduled
after the SMS-boot feature.** (The staircase/pivot framing is dropped — Mike
filed it as a half-idea — but this fork stands on the hardware numbers alone:
pit F:05 FULL / F:07 HALF, W 24,836 + O 6,304, and parity patching is spent.)
Opens with a design doc: sub-grid factor, RAM cost, procgen/editor/spawn blast
radius, and the DDA step budget at 2x vs 4x.

### Mine the Doom 32X Resurrection codebase for techniques
**Status:** strategic resource — deep-mine report landed; concrete
adopt-list below in ranked-ROI order.

[viciious/d32xr](https://github.com/viciious/d32xr) is years of
optimization work to make Doom run smoothly on the actual 23 MHz
SH-2s. We've already borrowed:
- the `| 0x20000000` cache-through SDRAM alias for shared state
- the COMM4 doorbell + COMM6 arg-word convention
- the `MARS_SECCMD_*` command enum + secondary polling dispatcher pattern

#### Ranked adopt-list (from the deep-mine research agent)

**1. SH-2 hardware DIVU latency-hiding for `1/perpDist`.** ✅ done.
Wired up via `divu_start_u32` / `divu_read` in `sh_src/sh2_asm.h`.
Wall column code starts the divide, then computes `wall_shade` +
texture coordinate setup during the 39-cycle latency.

**2. Hand-roll SH-2 asm wall column inner draw loop.** ✅ done.
4-pixel-per-iter inline asm block in `draw_walls` — keeps `tex_pos`,
`p`, `shade_lut`, `step`, `mask` in registers, uses indexed byte load
via `@(R0,Rm)` and `dt`/`bf` for the count-down. Measured on
hardware: primary half-render time dropped from 44000→33500 FRT ticks
(24%), secondary from 44000→25000 (43%). Frame crossed a vsync boundary
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
`sh_src/sound.c` mixes a ping-pong `amb_pwm_buf[2][1024]` on the secondary
(64 ms per buffer + amb_pump checkpoints between the secondary's render
passes — so render chunks can't starve the ping-pong; underruns counted
on the HUD as AU:, old 16 ms arm kept as a menu A/B);
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
✅ done (2026-08-06). Two cuts, both measured on hardware. **Vertical depth
LOD**: the pass already stepped x_step 4/8/16 by fog band but ran every screen
row, and each row costs a DIVU plus ~6 muls of setup before one stain lands.
Row step now doubles at the mid band, keyed to the same geo_shade. **Covered-row
skip**: ceiling and carpet paint every pixel *before* the wall pass, so rows the
walls bury were drawn for nothing — the new OD counter reads 8% coverage in open
rooms, 32% in corridors, 92% in partition views. The band collapses to a scalar
pair so the per-row test is free. Partition view, same camera: R 1,147 → 492,
T 19,290 → 18,191. TESTING>CARPETLOD A/Bs it in one binary.

## Polish

### Higher-res chair billboard via `hero_scratch` reuse
The directional chair sprite is baked at 56px (`tools/bake_dir_sprites.py
--height`). An 84px bake is crisper and the sprite data is free (it's
`const`/ROM), but the viewer's `chair_pv` decode scratch scales with
sprite size and, as a static `.bss` buffer, steals from the primary
stack. At 84px it left only ~1100 bytes of stack headroom (the same
class of silent-overflow trap as the audio 2×2048 buffer), so we kept
56px. It's academic at the current 3.5-cell LOD swap (chair is ~24px on
screen there, already 2.3× oversampled), but if the perf pass ever
pushes the swap *closer* the billboard gets big enough to want the
detail. Safe path when we do: host `chair_pv` in the idle `hero_scratch`
buffer (71 KB, dark during the asset viewer) instead of its own static —
zero new `.bss`, full-res sprites. Cost: couples box_hero into the
renderer with a "hero_scratch is free during the viewer" lifetime
assumption. The bake tool already emits `CHAIR_DIR_WMAX` so the scratch
auto-sizes to any `--height`.

## Community pipeline / forking model  (2026-08-05, build-216)

**Shipped — don't rebuild it.** Content carries a **tier** that decides which
ROM compiles it: `core` (maps/core, maps/test), `curated` (maps/curated — the
project's own maps plus outside work the maintainer promoted), `community`
(maps/community + sprites flagged `"tier": "community"`). Tier is a curation
call, not an identity one. `make` builds the flagship, `make community` builds
everything, `make author AUTHOR=<handle>` builds one contributor's ROM (kept for
local use; upstream CI no longer runs it, see below). Promotion = move the file
to maps/curated/ + change one word. Lint enforces the wall: a core/curated map
cannot reference a community asset, and an unknown decal kind is an error.

The editor is **fork-first**: sign in, "Your copy" creates the contributor's
fork, `Save to my copy` commits a map or baked sprite straight onto it, and
`/fork/assets` reads their fork's registry + `spr_*_tex.h` back so their own
sprites appear in the palette and Walk preview. Their fork's CI builds their ROM.

### The numbers that decide every design question here

| limit | value | where |
|---|---|---|
| community palette arena | **14 sprites total** (CRAM 144..255, 8 entries each) | `COMM_BASE`, `tools/lint_maps.py:arena_usage` |
| maps in a build | 64 | `has_in[64]`, m_main.c start menu |
| start-menu rows | 40 | `items[40]`, m_main.c |
| wall decal texels | 224x224, ~49 KB | `MAX_W_WALL`/`MAX_TEXELS_WALL` |
| standee texels | 64x96, ~4 KB | `MAX_W`/`MAX_TEXELS` |
| community ROM today | 1.83 MB (cart room to 4 MB) | build-216 |

**The scarce resource is palette, not ROM.** One contributor's decal set took 4
of the 14 slots, so a shared "everybody's work" ROM fills after ~3 contributors
no matter how CI is arranged. That is the whole reason for the fork model, and
why upstream CI stopped building a ROM per author (one more compile per
contributor per release, forever).

### Next steps, in order

1. **First-run fork validation** — the one path never exercised: a real OAuth
   session calling `ensure_fork`. Forks also have Actions **disabled by
   default**, so the first build needs a human click; confirm the flow and
   whether the editor should detect and say so. Do this before pointing a new
   contributor at it.
2. **Community map bucket in the editor** (the agreed next build). Browse
   community maps read from GitHub — upstream `maps/community/` plus forks that
   opt in — and import one into the session. Maps are unbounded and shareable;
   assets are not, so import must check the map's asset dependencies against the
   session's free palette slots and say "needs torn, torn2, torn3; you have 8
   slots free" rather than importing something unbuildable.
3. **Declared asset dependencies per map.** A `.map`'s sprite needs are implicit
   in its decal kinds today. The bucket needs them explicit (emit at lint time
   or in a header line) to answer "can I import this".
4. **Fork discovery.** The bucket needs a list of participating forks: GitHub
   fork-search vs an opt-in file in the repo. Unsolved; pick before building 2.
5. **Engine caps when the community grows**: `has_in[64]` and `items[40]` bound
   the shared build long before ROM size does.

### Website model (what the fly.io app is, and isn't)

Stateless. The image has no volume, and GitHub is the store — maps, assets and
identity all live in repos, the app only ever proxies the user's own token. It
serves the editor, the shared lint/bake, and the fork/PR plumbing. It must
**not** become an asset host or a moderation queue: anything that needs storage
belongs in a repo, and anything that needs judgement belongs in a PR. A
"community bucket" is therefore a *view over GitHub*, not a bucket on fly.

Gotcha found the hard way: werkzeug 3.1 strips a trailing CR belonging to an
upload's payload, so any file whose last byte is 0x0D arrives one byte short
(fatal for WebP). `/bake_sprite` retries with the byte restored.

## Tools / infra

### GLB import polish — make an imported model "just work" end to end
The desk was the first GLB import (build-205). The pipeline ships, but four
things still need a human to notice them, and each one bit during that import.

- **Accurate sizing on rebake.** `world_hw` is hand-entered in registry.json and
  was simply wrong for the desk: 0.310, copied from `world_h` by the standee
  pipeline, when the model measures 0.333. The bakers should emit measured world
  dimensions, and lint should FAIL when registry disagrees with the baked model
  rather than letting the editor quietly draw the wrong size.
- **One ramp per asset across the LOD swap.** The near 3D model rides
  `CHAIR_BASE` (4-deep shared wood ramp) while the far billboard fogs into the
  asset's own `COMM_BASE` block (7 steps, its own median-cut palette). Both are
  fogged, so nothing floats bright in the distance, but they are quantized
  differently out of different palettes, so the swap has no reason to be
  colour-continuous. The chair dodges this by being special-cased into
  CHAIR_BASE in BOTH paths — an imported asset should get that for free.
- **Editor previews box models as flat billboards.** The walk preview hardcodes
  the chair's box list (`CHAIR_BOXES`/`CHAIR_H` in raycast.js) and draws
  everything else as a standee, so the editor and the game disagree about what
  a desk looks like.
- **`box_model` flag set by the importer.** registry decals.kinds[].box_model
  exists now and drives preview scale; the import tool should set it so render
  path and scale follow the asset with no hand edits.

Related trap worth keeping: `drawSprite` in the editor takes FULL width, but the
caller passed `world_hw` (a half-extent), so every generic sprite previewed at
exactly half width for as long as that path has existed. The neanderthal hid it
by hardcoding already-full dimensions.

**First customer for the polished path: the PVM.** One model, PVM + desk fused,
through `bake_boxes.py` (the desk went through at 3 boxes / 18 faces, and the
separating-axis ordering is already solved). Then an animated static screen on
it: a flat quad on a known face, cheap procedural noise.


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
total, primary half-render time, secondary half-render time) sampled from
SH-2 free-running timer at Φ/32 (1.39μs per tick). Both CPUs init
their own FRT; secondary publishes its delta via `SHARED_UC->secondary_render_ticks`.

### Master System arc  (third in the working order, 2026-08-06)
The goal is SMS-style games on this hardware. Two things that sound like one
thing, and separating them is most of the design:

**VDP Mode 4 is a register bit.** Clear bit 2 (M5) of VDP register 1 and the MD
VDP renders the SMS-derived mode. Reachable in software from a normal cart, no
adapter, register writes still going through the MD control port at `0xC00004`
(the SMS-style `0xBE`/`0xBF` ports are the *Z80's* view and we never need them).

**SMS mode is a cartridge-port pin.** What the Power Base Converter asserts —
it hands the Z80 bus mastery over an SMS-shaped memory map, remaps VDP I/O, and
holds the 68K in reset. Three reasons that is not a game path: the 68K being off
means there is no return, only a reset; it is a pin rather than a register, so
it likely does not exist on the MiSTer core or a flashcart (unconfirmed, and
worth confirming before anyone plans around it); and the MD Z80 has no I/O port
decoding of its own, so SMS code's `OUT (0xBE),A` goes nowhere without the
machine's own compat mapping doing the work.

Ranked:

1. **Shim — Mode 4 look, our code.** A snake game that looks like SMS. Days.
   No research risk. This is the one that ships.
2. **SH-2 emulator — real SMS binaries.** Z80 interpreter plus a Mode 4
   renderer into the 32X framebuffer, PSG passed through to the real chip. The
   only path that runs actual SMS code on hardware we deploy to. Budget: SMS Z80
   is 3.58 MHz / ~900K instructions per second, so one 23 MHz SH-2 gives ~25
   host cycles per Z80 instruction *before* VDP work — tight for a plain
   interpreter, and the second SH-2 taking the VDP/scanline side is the natural
   split (same shape as the column split we already run). Weeks. Full speed is
   not a given; feasibility is not the open question, speed is.
3. **Native SMS mode.** Not viable per above. Recorded so it stops being
   re-proposed.

**The gate, and it is cheap: does Mode 4 survive the 32X mixer?** The research
ROM should answer exactly one question — put the VDP in Mode 4, draw a
recognizable tile grid, and see whether it reaches the screen through the 32X
video path on real hardware. If it does not, both 1 and 2 change shape. Second
unknown, for later: Mode 4 CRAM is 6-bit against Mode 5's 9-bit, so a
Mode 5 → Mode 4 → Mode 5 round trip almost certainly does not preserve the
palette.
Remove the overlay before shipping a release build.
