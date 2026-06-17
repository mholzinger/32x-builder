# Backrooms 32X

A first-person **Backrooms raycasting engine for the Sega 32X**, written from
scratch in C and SH-2 assembly. It runs natively on real 32X hardware (and on
a MiSTer FPGA core) — dual-CPU rendering, a textured DDA raycaster, procedural
liminal-space level generation, freestanding partition walls, dynamic ceiling
lighting, a 3D cardboard-box intro rendered live (not a video), crawl/crouch
with full eye-height perspective, and a settings menu.

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
| `tools/` | Python **asset bakers** — convert PNG/WAV/Blender output into the committed C headers (`bake_wall.py`, `bake_hero.py`, `wav_to_pwm.py`, `export_box.py`, `gen_backrooms_map.py`, …). |
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
| `make` / `make release` | Optimized build (`-Ofast -flto` SH-2, `-O2 -flto` 68000). |
| `make debug` | `-Og -g` build with `DEBUG`/`KDEBUG` for GDB tracing (Gens-KMod, BlastEm, UMDK). |
| `make clean` | Remove objects, deps, ELFs, and the built ROM. |
| `make deploy` | Build + `scp` the ROM to a MiSTer (see below). |

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

---

## Controls

Six-button Genesis controller (works with three-button too, minus the extra
buttons). Built around hold-modifiers rather than toggles:

| Input | Action |
|---|---|
| **D-pad ↑ / ↓** | Walk forward / back |
| **D-pad ← / →** | Turn left / right |
| **A** | Sprint (hold) |
| **X** | Crawl / crouch (hold) — eye drops to the floor, full crawl perspective |
| **C** | Look mode (hold); ↑ / ↓ tilt the gaze up / down while held |
| **START** | Settings menu (ambience / footstep volume) |
| **MODE** | Toggle on-screen debug metrics (X/Y/A, timers, profiler) — off by default |

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

## Architecture in one paragraph

Two SH-2s split the screen into vertical halves and each renders its own
clear + ceiling grid + carpet + walls in parallel, syncing once per frame
(one COMM4 doorbell) before sprites and lighting. Cross-CPU state lives in
SDRAM accessed through the cache-through alias (`addr | 0x20000000`) so neither
CPU sees stale cache. The raycaster is a textured DDA with LOD texture pop-in,
flat per-row floor/ceiling shading, freestanding "partition" wallpaper dividers,
look-up/down via a cheap y-shear, and variable eye height for crouch/crawl. The
hot wall-pixel loop is hand-written SH-2 assembly. See **[DEVLOG.md](DEVLOG.md)**
and **[ROADMAP.md](ROADMAP.md)** for the full story.

---

## Credits

Built as a two-person collaboration — a programmer driving hardware direction
and optimization, with Claude (Anthropic) as the implementation partner doing
per-step research and code. Toolchain: [Marsdev](https://github.com/andwn/marsdev).
Sound effects sourced from Epidemic Sound. Backrooms concept is community
folklore.
