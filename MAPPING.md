# Making maps for Backrooms 32X

Everything here happens in the browser — no toolchain, no compiler. Build a
level, walk around in it, submit it, and once it's merged it ships in the next
ROM release automatically.

**Editor:** [backrooms-32x-project.fly.dev](https://backrooms-32x-project.fly.dev/)

---

## The 60-second version

1. Open the editor, pick **New map** (or **Open** an existing one to study).
2. Draw walls on the **Grid** layer.
3. Set **Spawn** so the player starts somewhere sensible.
4. Hit **▶ Walk** and actually walk around in it.
5. Watch the **Budget** panel — stay in the green.
6. **⬇ Export .map** to download the file, then **🚀 Submit to game**.

The one rule that catches everyone: **commit the exported `.map` file itself.**
Not a description of the map, not a screenshot — the file the Export button
gives you. A file with no `[grid]` section isn't a map, and the build will say
so.

---

## Layers

| Layer | What it does |
|---|---|
| **Grid** | Walls, floor, and void. The bones of the level. |
| **Crawlspace** | Low-ceiling tunnels. The player is forced to crouch through them. Click two cells in the same row/column. |
| **Lights** | Ceiling fixtures. Leave the layer empty and the engine lights the map on an automatic grid; place even one and *your* layout takes over completely. |
| **Partitions** | Free-standing walls that live on cell edges — full height, low dividers, or half-height counters you can see over. |
| **Decals** | Outlets, the exit door, and neanderthal cutouts. |
| **Spawn** | Where the player starts, and which way they face. |
| **✕ Delete** | Removes whatever's under the cursor, on any layer. |

**Left-drag** to place, **right-click** to erase.

---

## Budgets — the thing to actually watch

The engine has hard limits, and the **Budget** panel shows where you stand
against them, live, as you build. Yellow means you're at the cap; red means
you're over and it won't ship.

| Resource | Limit | Notes |
|---|---|---|
| Grid | 32×32 | The engine's map size. Not negotiable. |
| **Partitions** | 64 segments | *And* a hard 255-cell-edge ceiling — see below. |
| Decals | 16 | Outlets, doors, cutouts. |
| Crawl runs | 8 | Counts *runs*, not cells. |
| Lights | 512 | Plenty. Place freely. |

### The partition edge ceiling (the sneaky one)

Partitions are capped two ways: **64 segments**, and **255 cell-edges**. A
single long partition is one segment but many edges — a 10-cell wall is 10
edges. The Budget panel counts segments; the build enforces edges. It's
possible to be under 64 segments and still blow the edge limit with a few very
long runs.

If the build tells you you're over on edges, that's what happened.

### Why partitions are the expensive thing

**Grid walls are nearly free.** A ray stops at the first wall it hits, so *more*
grid walls means *less* work. **Partitions are the priciest object in the
engine** — they're thin, they don't stop rays the same way, and they were the
subject of an entire optimization campaign.

So: build rooms and structure out of **grid walls**, and use **partitions as
furniture** — counters, cubicle dividers, half-height islands. That's cheaper
*and* it reads more like the Backrooms.

### ⚙ Optimize

The **Optimize** button losslessly shrinks a map's encoding: it merges
collinear partition segments, re-covers crawlspaces with the fewest runs, and
drops duplicate decals. The map looks and plays identically — it just uses less
of the budget. Worth a click if you're near a cap.

It can't perform miracles. If you have 300 genuinely distinct partitions,
they're 300 partitions.

---

## Exits: doors, void doorways, and the dark

Three things get confused, so here's the difference:

- **Exit door** (Decals layer) — a real door you open with A and walk through.
  The deliberate, framed exit. A normal way to end a map or chain to the next.
- **Void exit** (Grid layer, the `X` cell) — a **missing wall cell**. Put one on
  a border and the wall is simply *gone* there: through the gap the floor and
  ceiling run out and fade into the fog — the endless expanse, like the lobby
  doorway. You **walk out through it** to leave. It goes **on a border** (the
  map edge, or a wall), never floating in the middle of a room — one empty cell
  is all it takes.
- **Dark room** (Dark rooms layer) — this is the one for a **dark area you walk
  around in**: unlit space, fog, lights down. If you want somewhere to *feel*
  dark, use a Dark room. A void exit is a way *out*, not a shadow.

A `next:` chain needs an exit the player can reach: an exit door,
`place_exit_door`, or a void opening on a border.

## Story chains

A map's exit door normally dumps you into a randomly generated level. Point it
at another map instead with the **Next** dropdown in the header.

Chains link **by map name**, so you can build everything first and decide the
order afterward. Leave it on `— procedural —` and the exit goes somewhere random
(which makes a good "…and now you're lost again" ending).

Two things the build enforces:

- **A map can't point at itself.** The dropdown won't offer it.
- **Renaming breaks links.** If you rename a map something points at, the build
  stops and tells you exactly which `next:` broke. Nothing fails silently.

You **only commit the map you changed.** Editing one map's layout doesn't
require touching anything else in its chain — the link is just a name.

---

## Getting it into the game

Two paths:

**Submit (anyone).** Sign in with GitHub in the editor (top of the file bar),
then hit **🚀 Submit to game**: the editor makes your own copy of the repo, commits
your map to it and opens the pull request for you. CI lints it, a bot posts a
rendered preview of your map, and merging cuts a ROM release automatically. Not
signed in, Submit still works — GitHub walks you through the same steps by hand,
starting with a **fork** (your own copy of the project; clicking it is expected).

**Push (if you have write access).** Drop the `.map` in `maps/community/` and
push to `main`. Faster; skips the preview bot.

### Your own copy of the game

Sign in and the editor offers to make you a **fork** — your own copy of the
whole project. It is worth taking:

- **Save to my copy** commits your map straight into it. No review, no waiting.
- Your fork builds **its own ROM** on every save (enable Actions on the fork
  once, under its Actions tab), so you can play your work immediately.
- The asset budget is **yours alone** there. The console's community palette
  arena fits about 14 custom sprites and the main repo shares that across
  everybody; in your fork you spend all of it on your own work, and split your
  art into as many separate sprites as you like.

**Submit to game** is the other door, and it stays open: it opens a pull request
against the main repo for work you want in everyone's game. Fork for building,
submit for sharing.

### Which ROM your map lands in

Every release ships more than one ROM, and where your map appears depends on
its **tier**, not on who you are:

| ROM | contains |
|---|---|
| `backrooms.32x` | the game: canon plus the maps the project's authors made or the maintainer promoted |
| `backrooms-community.32x` | all of that **plus every community map and asset** |
| `backrooms-<yourname>.32x` | the game plus **your** maps — your own build, cut for you on every release |

A submitted map starts in `maps/community/` (tier `community`) and appears in
the last two, behind the start menu's own **`-- COMMUNITY --`** heading. If the
maintainer wants it in the main game, they move the file to `maps/curated/` and
change one word — `role: community` → `role: curated`. Nothing else about the
map changes.

Community **sprites** work the same way: they're tiered `community` when baked,
so they compile into the community and personal ROMs only. A map that ships in
the flagship can't reference one (the lint says so), which is why you can't
place your own sprite in a core or curated map.

### Updating a map you already submitted

Just **overwrite the same file**. Community maps aren't protected — edit away.

The one trap: **don't end up with two files claiming the same map name.** If
your browser saves `subfloor(1).map` next to `subfloor.map`, the build fails
with `duplicate map name`. Same name, same file, overwrite it.

Maps in `maps/core/` are protected canon — the editor clones them if you edit
one, rather than overwriting.

---

## Making it look like the Backrooms

The most common instinct is to build a **maze**. Mazes are tight, uniform
corridors with constant turns. The Backrooms is almost the opposite:

- **Big, irregular, open bays** — long sightlines that let the fog eat the far
  wall.
- **Repetition** — rooms similar enough that you're not sure you've moved.
- **Very few doors.** Space flows into space.
- **Wrong-sized rooms.** Too wide, too long, no reason for it.

Then use partitions as the furniture inside those bays: a counter here, a run of
cubicle dividers there. Open space carved by grid walls, cluttered by
partitions, reads far more "abandoned office" than a corridor labyrinth does.

Use **▶ Walk** constantly. A layout that looks great top-down often feels
completely different at eye level, and eye level is the only view that counts.

---

## Reference

- **Roles:** `community` is where contributions live. `core` is protected canon.
- **Partition styles:** chevron (matches the walls) or spotted (olive divider).
- **Partition heights:** full, low (¾), or half (a counter you can see over).
- **Decal kinds:** outlet, door (the exit), neanderthal (a free-standing
  cardboard cutout — walk into one and it stops you; push again and it topples).
- **Which build am I running?** Pause → **CREDITS** shows the build number, date,
  and commit. Every ROM stamps its own identity.

Source and the map format itself: [github.com/mholzinger/32x-builder](https://github.com/mholzinger/32x-builder)
