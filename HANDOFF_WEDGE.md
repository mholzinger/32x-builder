# Handoff: wedge primitive for box models

## State at handoff

20 unpushed commits sit on top of release `1d0acc0`. Current build is B00265 (`cfe20d1`),
cold-boot verified. The batch is squash-ready but NOT squashed and NOT pushed; Mike gives
that word. Full arc: the desk-mounted PVM console, its SMS boot transition, Mike's
hand-painted bezel and rear panel plus the editor kits, the viewer overhaul, and five
render bugs fixed.

Uncommitted working files (do not lose, do not blind-commit): `sh_src/pvm_front_tex.h`,
`pvm_bezel_edit.png` (Mike's in-progress bezel art), `sh_src/sms3d.h` (res-48 console bake,
currently unused by the compose tool), `capture.sh`, plus the rebuilt ROM.

## The task

Add a wedge primitive to the box-model format so sloped shapes are expressible. The
immediate customer is the Master System console on the desk: it is a flat-bottomed
trapezoid rising to a flat top, currently approximated by a 3-step ziggurat in
`tools/compose_desk_pvm.py`. Mike's reference render is in the conversation; the model is
`models/sega_master_system.glb`.

## Why the format is axis-aligned today

The renderer draws arbitrary projected triangles fine. The constraint is one layer up, in
the asset format, and it buys three things:

- **Occlusion without a z-buffer.** Convex axis-aligned boxes sort exactly far-to-near by
  separating-axis tests (`box_nearer` in `sh_src/raycast.c`), and faces within one box
  never overlap. Per-pixel depth is not affordable in this frame budget.
- **Free lighting.** A box face points one of six ways, so shading is a table lookup
  (`chair_face_shade`).
- **A trivial bake.** `tools/bake_boxes.py` voxelizes and merges greedy AABBs. A sloped
  shell can only come back as a staircase.

Wedges stay convex, so the sort survives with a smarter comparator. This is a real
feature, roughly an evening.

## Implementation sketch

- **Format:** extend `cbox_t` (or add a parallel type) with a slope descriptor. Minimum
  viable: a box whose top face is a plane sloping along one horizontal axis, defined by two
  heights.
- **Comparator (`box_nearer`):** the separating-axis test needs a wedge-aware case. A wedge
  is still convex, so the existing per-axis interval logic holds for the non-sloped axes;
  the sloped axis needs the plane test.
- **Face builder (`draw_chair_3d`'s per-box loop):** the sloped face is a quad with
  non-coplanar corner heights; the two triangles it splits into already go through
  `tex_tri`.
- **Shading:** compute the slope normal at bake time and store a shade index, so the
  runtime stays a lookup.
- **Bake tool:** detect slabs whose voxel occupancy forms a ramp and emit a wedge instead of
  a staircase. Optional; hand-authored wedges in the compose tool are enough to ship the
  console.

## Rules that will bite you

- **Painter cycles are real.** No box may take a Y-verdict against one neighbor and an
  X/Z-verdict against a box that neighbor also touches. That intransitivity produced
  angle-dependent draw order (the desk drawing through the monitor). Fix is data: split the
  spanning box. See the addendum in the box-painter memory and the slab split in
  `compose_desk_pvm.py`. Note that the ziggurat is currently cycle-proof *by construction*
  ("steps nest strictly inside each other, so every pair resolves on Y" — the comment above
  `console_boxes`). A wedge gives that property up, so this rule applies to it directly.
- **`BX_MAXBOXES` is a hard cap** (currently 10, sized for the desk composite). The
  composite is at **9 of 10 today** — one slot spare. Every model reaching the render or
  viewer path must fit; there is a `_Static_assert`. The wedge cuts the other way too:
  replacing 3 ziggurat boxes with 1 frees 2 slots.
- **The `_Static_assert` has a gap.** It covers `CHAIR_NBOXES`, `DESK_NBOXES`,
  `PVM_NBOXES`, `DESK_PVM_NBOXES` — but not `SMS_NBOXES`. `sms3d.h` is the uncommitted
  res-48 bake and is unused today, which is exactly why it was missed. When sms3d.h enters a
  render or viewer path, add it to that assert.
- **Test from a cold boot.** Ares savestates carry 68K work RAM, where `do_commands` lives,
  so loading an old state onto a new ROM runs the old build's code. This cost a full day
  during the YM arc.
- **Verify the artifact, never version.h.** Same-second mtimes defeat the dependency; make
  can link a stale binary while the stamp lies. Check `strings rom/backrooms.32x`. Never ask
  Mike to verify a build; read the stamp from his screenshots.
- **Mike's screenshots carry coordinates.** The HUD X/Y/A line is placement input, not just
  a test receipt.
- **Texture round-trips are WYSIWYG** and the flips live in `tools/pvm_bezel_edit.py`. Front
  face samples mirrored; rear face has its own UV corner assignment. Do not add a second
  flip.

## Banked, not in scope

Scan-sweep idle effect (reuses the power-off falling-line machinery). Boot sequence on the
glass. Live minigame miniature on the glass (the payoff feature; game state is readable from
Z80 RAM). Random on-desk monitor variants. Composite far-LOD dirset (past 4 cells the desk
set still shows the floor-stand billboard). Red top label on the console (needs a top-face
texture slot, same mechanism as the rear panel). YM hum amplitude dips. Crawl/shuffle sample
replacement (Mike is sourcing audio).

## Do not touch

The Voyager broadcast is canon. Outward-facing materials never reveal that the PVM's second
screen shows the exit; discovery is the feature.
