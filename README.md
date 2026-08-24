# Backrooms 32X

A first-person **Backrooms raycasting engine for the Sega 32X**, written from
scratch in C and SH-2 assembly. It runs natively on real 32X hardware (and on
a MiSTer FPGA core) — adaptive load-balanced dual-CPU rendering, a textured DDA
raycaster, procedural liminal-space level generation, first-class **slab
partitions** (full-height dividers, half-height cubicle walls, and wood
countertops you can see over), dynamic ceiling lighting, a 3D cardboard-box
intro rendered live (not a video), crawl/crouch with full eye-height
perspective, and a settings menu. All in-game text — debug HUD and menus —
renders on the **Genesis VDP tile layer** (drawn by the otherwise-idle 68K,
composited over the 3D for free), and the iconic cardboard neanderthal can be
shoved over: he tips, falls, and stays on the carpet — face up or face down,
depending on which side you pushed.

It also ships a **browser-based level editor** with a first-person walk
preview, and a **community-map pipeline**: design a map, submit it as a GitHub
pull request, and a merge auto-cuts a playable ROM release — no toolchain
required. See [Level editor & community maps](#level-editor--community-maps).

> **About the repo name:** this started life as a generic "32X builder" and the
> name stuck — it's really a Backrooms raycaster now. The name stays so existing
> followers don't lose the thread. Don't read too much into it.

| | |
|---|---|
| **Platform** | Sega 32X (Genesis/Mega Drive + 32X add-on) |
| **CPUs** | 2× SH-2 (primary + secondary, ~23 MHz) for rendering, 1× MC68000 (Genesis side) |
| **Video** | MARS VDP, 256-color paletted framebuffer, 320×224 |
| **Output** | `rom/backrooms.32x` — runs in 32X emulators and on MiSTer |
| **Language** | C99 (`-ffreestanding`) + hand-rolled SH-2 asm hot loops |

For *why* the engine is built the way it is, read **[DEVLOG.md](DEVLOG.md)** — a
running, taggable log of the hardware truths, optimizations, and bugs found
while building this (it doubles as the narrative of the build process). Future
and deferred work lives in **[ROADMAP.md](ROADMAP.md)**.

---

## Repository layout

| Path | What's in it |
|---|---|
| `sh_src/` | **The engine.** SH-2 code for both CPUs: raycaster (`raycast.c`), 3D box intro (`box3d.c`), primary/secondary entry points (`m_main.c`, `s_main.c`), shared cross-CPU state (`shared.h`/`.c`), menu, font, procgen, sound, and baked asset headers (`*_tex.h`, `box_model.h`, etc.). |
| `md_src/` | Genesis-side **MC68000** code (`md_main.c`, boot/startup `.s`, linker script). The 32X needs both a 68000 and an SH-2 program. |
| `tools/` | Python **asset bakers** — convert PNG/WAV/Blender output into the committed C headers (`bake_wall.py`, `bake_hero.py`, `wav_to_pwm.py`, `export_box.py`, `gen_backrooms_map.py`, …) — plus the map codegen/lint (`gen_maps.py`, `lint_maps.py`, `mapfmt.py`). |
| `tools/map-editor/` | The **web level editor** (Flask + canvas): top-down authoring, first-person walk preview, resource-budget checks, and the community-PR submit flow. |
| `maps/` | Level `.map` files — `core/` (protected canon) and `community/` (contributor submissions). Compiled into the ROM by `gen_maps.py`. |
| `scripts/` | **Blender** generators for the 3D cardboard-box cinematic + hero splash (`genbox.py`, `genhero.py`). |
| `models/`, `sound/`, `images/` | Source art/audio the bakers consume. |
| `rom/` | Committed `.32x` release snapshots (built artifacts, intentionally tracked). |
| `Makefile` | The build. |
| `capture.sh` | Dev helper: extract + dedup frames from a screen recording into `screenshots/`. |

Baked asset headers are **committed**, so a normal build needs neither Blender
nor Python — only the toolchain below.

---

## Prerequisites

1. **A POSIX shell environment** — macOS or Linux (Windows via WSL).
2. **[Marsdev](https://github.com/andwn/marsdev)** — the GNU cross-toolchain
   suite for Sega 32X. It provides the two compilers this project needs:
   - `m68k-elf-gcc` (Genesis / 68000 side)
   - `sh-elf-gcc` (SH-2 / 32X side)

   Build or install it per its README, then note its install root (the
   directory containing `m68k-elf/` and `sh-elf/`).
3. **GNU make**.
4. *(optional)* **ssh/scp** access to a MiSTer for `make deploy`.
5. *(optional, only to regenerate assets)* **Blender 4.2+** and **Python 3**
   with **Pillow** for the texture/audio bakers in `tools/`. See
   `scripts/requirements.txt` (the `fake-bpy-module` there is for IDE
   autocompletion only — Blender ships its own `bpy`).

---

## Building

Point `MARSDEV` at your Marsdev install root and run `make`:

```sh
git clone https://github.com/mholzinger/32x-builder.git
cd 32x-builder

# Path to the dir that contains m68k-elf/ and sh-elf/
export MARSDEV=/path/to/marsdev

make            # == 'make release'
```

This compiles both CPUs and produces:

- **`rom/backrooms.32x`** — the loadable ROM
- `rom/backrooms.lst` / `rom/md_start.lst` — symbol listings for each CPU

`MARSDEV` defaults to `~/mars` if unset. You can also pass it inline:
`make MARSDEV=/path/to/marsdev`.

### Other targets

| Command | Purpose |
|---|---|
| `make` / `make release` | Optimized build (`-Ofast -flto` SH-2, `-O2 -flto` 68000) → `rom/backrooms.32x`. |
| `make community` | Same engine, plus every community map + community asset → `rom/backrooms-community.32x`. |
| `make author AUTHOR=<handle>` | The flagship plus one contributor's own maps → `rom/backrooms-<handle>.32x`. |
| `make debug` | `-Og -g` build with `DEBUG`/`KDEBUG` for GDB tracing (Gens-KMod, BlastEm, UMDK). |
| `make clean` | Remove objects, deps, ELFs, and the built ROM. |
| `make deploy` | Build + `scp` the ROM to a MiSTer (see below). |

### Content tiers — what's in which ROM

Maps and sprites carry a **tier**, and the tier decides which ROM compiles them
in. It's a curation decision, not an identity one: a contributor's map moves
into the main game by moving the file to `maps/curated/` and changing one word.

| tier | lives in | ships in |
|---|---|---|
| `core` | `maps/core/`, `maps/test/` | every ROM |
| `curated` | `maps/curated/` — the project's own maps + promoted community work | every ROM |
| `community` | `maps/community/`, sprites baked with `tier: community` | `backrooms-community.32x` and that author's `backrooms-<handle>.32x` |

The lint enforces the wall: a core or curated map cannot reference a community
asset, so nothing from an unvetted PR can reach the flagship ROM. In-game,
community maps sit behind their own `-- COMMUNITY --` heading in the start
menu, and any non-flagship build prints what it is (`COMMUNITY BUILD`,
`BUILD FOR <NAME>`) under the title. CI attaches all of these to every release.

> **Header changes trigger rebuilds.** The Makefile uses GCC's `-MMD` dependency
> files, so editing a struct in a shared header correctly recompiles every TU
> that includes it — important here, where a stale `.o` compiled against an old
> `shared_t` layout silently corrupts cross-CPU memory.

---

## Running

**Emulator:** load `rom/backrooms.32x` in any 32X-capable emulator — Genesis
Plus GX (RetroArch), Picodrive, BlastEm, Ares, Kega Fusion, etc.

> Note: most software emulators don't emulate the SH-2 free-running timer (FRT),
> so the optional on-screen profiler reads `0`. The build falls back to a
> vblank-counted FPS line, and the FRT-based numbers are accurate on MiSTer /
> real hardware.

**MiSTer FPGA:** copy `rom/backrooms.32x` to `/media/usb*/Games/S32X/` on the
MiSTer, or let the Makefile push it over the network:

```sh
make deploy                              # default host: root@mister.office.local
make deploy MISTER=root@your-mister.local
```

`deploy` probes `usb0` then `usb1` for the `Games/S32X` directory over ssh, so a
USB renumber on reboot won't break the copy. (`make deploy-tv` is a second
preconfigured host.)

### Playing a release build (not your own build)

A local `make` is **not** the released artifact — CI pins its toolchain
(`sh-elf-gcc 13.1.0`), your machine probably has a different one, and the same
commit produces different bytes. To test what people actually downloaded, fetch
it rather than rebuild it:

```sh
./fetch-release.sh                  # latest release
./fetch-release.sh build-135        # a specific one
./fetch-release.sh --deploy         # latest, straight onto the MiSTers
```

ROMs land in `rom/release/` (never clobbering your local `rom/backrooms.32x`),
and the script reads the build stamp back out of the downloaded binary to prove
the tag and the artifact agree. `make deploy-rom ROM=<path>` pushes any given
ROM to a MiSTer without rebuilding.

---

## Controls

Gameplay needs only A / B / C and START, so a **three-button pad has full
parity** — everything else lives in the pause menu. Built around
hold-modifiers rather than toggles:

| Input | Action |
|---|---|
| **D-pad ↑ / ↓** | Walk forward / back |
| **D-pad ← / →** | Turn left / right |
| **A** | Sprint (hold) |
| **A** | Interact — open the EXIT door, shove a standee |
| **A + B** | Crawl / crouch (hold both) — eye drops to the floor, full crawl perspective |
| **B** | Strafe modifier (hold) — ← / → sidestep instead of turning |
| **C** | Look mode (hold); ↑ / ↓ tilt the gaze up / down while held |
| **START** | Pause menu — lands on the **GAME** tab |

**The GAME tab** is the first thing START shows: **MAP** (off / full / local
automap overlay), **ZOOM** (hold ← / → to glide the map scale), **3D VIEWER**
(inspect the game's assets mid-run — returns right back to the game), and
**EXIT → LOBBY** (leave the level for the start menu, no console reset).
The other tabs cover audio, lighting, visuals, the live color lab, testing
toggles, credits, and instant map warps.

**MODE combos** (six-button pads) are optional shortcuts to the same tools,
plus the debug extras — nothing down here is required knowledge:

| Combo | Action |
|---|---|
| **MODE + B** | Cycle the automap overlay (same as GAME → MAP) |
| **MODE + ↑ / ↓** | Automap zoom (same as GAME → ZOOM) |
| **MODE + X** | Cycle wall resolution: FULL → HALF → AUTO → SERL (diagnostic) → LOW |
| **MODE + Y** | Toggle the on-screen profiler (frame/render timers, per-pass ticks) |
| **MODE + Z** | Controller-input tester (raw pad register — for diagnosing emulator key binds) |
| **MODE + C** | Partition-diagnostic mode (HUD `J`): normal / legacy paths / gate-off pricing |
| **MODE + A** | *(start menu)* Cycle the menu-exit transform — hinge-up / fall-forward / fly-through — with an instant slowed preview |

`WALLS: AUTO` (the default) measures frame time and drops to half-res only when
a scene gets heavy, so the image stays crisp when it can afford to. `SERL`
serializes the two CPUs for contention-free profiling and is intentionally slow.
Half is AUTO's floor — measured on hardware, quarter-res tied half on framerate
while looking worse, so it is manual-only (`LOW`) rather than something AUTO
will pick for you.

---

## Regenerating assets (optional)

Everything baked is already committed; you only need this to change art, audio,
or the intro animation.

- **Wall / sprite / splash textures:** `python3 tools/bake_wall.py …`,
  `bake_hero.py`, `bake_neander.py`, `bake_label.py`, `bake_logo.py` — PNG/WebP →
  palette-indexed C header.
- **Audio:** `python3 tools/wav_to_pwm.py …` — WAV → 32X PWM sample header.
- **Level map:** `python3 tools/gen_backrooms_map.py` — emits the hand-tuned
  32×32 floor plan.
- **3D cardboard-box intro:** `blender --background --python scripts/genbox.py`
  builds/animates the mesh; `tools/export_box.py` exports per-frame evaluated
  verts to `sh_src/box_model.h` for **live** SH-2 rendering (it's a real-time
  3D scene, not a prerendered flipbook).

After regenerating any header, rebuild with `make`.

---

## Level editor & community maps

The engine reads its levels from `.map` files that compile into the ROM, and
there's a full **web editor** for authoring them — a top-down grid editor plus
a first-person **walk preview** that renders in the ROM's real palette and
textures, so you can test a level in the browser before building anything.

**New to it? Read [MAPPING.md](MAPPING.md)** — the full authoring guide:
layers, budgets, story chains, submitting, and how to make a level read like the
Backrooms instead of a maze.

- **Try it:** [backrooms-32x-project.fly.dev](https://backrooms-32x-project.fly.dev/)
  (hosted, read-only — your work lives in your browser; use **Export** to save a
  `.map` to disk, or **Submit** to open a pull request).
- **Run it locally:** `cd tools/map-editor && pip install -r requirements.txt &&
  python3 app.py` — the local instance saves maps into `maps/community/`.

Editor features: grid/partition/decal/crawlspace/lights/spawn layers, partition
shape presets (counter, L, U, booth), a universal ✕ delete tool, and a live
**resource budget** panel that shows how many partitions / decals / crawl runs a
map uses against the engine's limits — with a **⚙ Optimize** button that
losslessly merges redundant geometry. You learn a map is over budget *while
building it*, not at submit time.

**Community pipeline:** maps carry a `role` (protected core maps vs. community
submissions). **Submit to game** opens a pre-filled GitHub PR under your own
account; CI lints and builds the map, a bot posts a rendered preview, and once a
maintainer merges it, a GitHub Release with a playable ROM is cut automatically.
Canonical maps and assets are protected by CODEOWNERS and clone-on-edit, so
contributions never overwrite the originals.

---

## Architecture in one paragraph

Two SH-2s split the screen into vertical halves and each renders its own
clear + ceiling grid + carpet + walls in parallel, syncing once per frame
(one COMM4 doorbell) before sprites and lighting. The split column is
**adaptively load-balanced** each frame from measured per-half render time, so
uneven scenes don't leave one CPU idling. Cross-CPU state lives in SDRAM
accessed through the cache-through alias (`addr | 0x20000000`) so neither CPU
sees stale cache. The raycaster is a textured DDA with LOD texture pop-in, flat
per-row floor/ceiling shading, and **first-class slab partitions** — thin walls
that live on cell edges, rasterized to per-edge flags with a directional
proximity gate so the expensive end-cap/recovery math runs only where a slab end
can actually be hit. Look-up/down is a cheap y-shear; eye height varies for
crouch/crawl. The hot wall-pixel loop is hand-written SH-2 assembly. See
**[DEVLOG.md](DEVLOG.md)** and **[ROADMAP.md](ROADMAP.md)** for the full story.

---

## Credits

Built as a two-person collaboration — a programmer driving hardware direction
and optimization, with Claude (Anthropic) as the implementation partner doing
per-step research and code. Toolchain: [Marsdev](https://github.com/andwn/marsdev).
Sound effects sourced from Epidemic Sound. Backrooms concept is community
folklore.
