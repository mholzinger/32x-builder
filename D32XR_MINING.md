# d32xr Mining Notes — Road to 30fps

Systematic mining of **Doom 32X Resurrection** (`srcref/d32xr/`, viciious, `master@a229d8e`)
for techniques to lift our dual-SH-2 Backrooms raycaster (`sh_src/`) from ~12–15fps to a
**minimum 30fps** target. Five parallel deep-dive passes (draw asm, fixed-point/DIVU,
renderer pipeline/CPU split, MARS hardware/DMA, CPU sync/caching). Citations are `file:line`
on both sides.

The framing throughout: **where our guesswork was incomplete, and where their real
engineering (years of it) makes the right call.**

---

## TL;DR — the gap is not where we thought

We assumed the bottleneck was the pixel blit and that framebuffer DMA was the big prize.
**Both wrong.** Our hand-rolled column loop and 32-bit clear already match or beat d32xr's,
and d32xr deliberately does **not** DMA the framebuffer (the 32X DMA FIFO runs the wrong
direction — peripheral→SDRAM — and is gated by the RV bit). The real, mineable wins are:

| # | Lever | Why it's slow for us | Est. impact | Risk |
|---|-------|----------------------|-------------|------|
| 1 | **Run hot code + textures from cacheable SDRAM, not ROM** | Our entire raycaster executes from ROM (slow I-fetch); wall texels sampled straight from ROM | **4–12 fps** | med |
| 2 | **Stop always-uncaching hot arrays** (`pface_*`, `cell_light`) | ~12 cyc/read, thousands of reads/frame in the per-ray loop | **4–8 fps** | med |
| 3 | **Dynamic dual-CPU load balance** (split point / coarse work-steal) | Static 50/50 column split idles one CPU when halves are uneven | **3–6 fps** | low→med |
| 4 | **Remaining software divides → hardware DIVU / LUTs** | DDA `deltaDist` + per-row `rowDist` still use software `FX_DIV` / per-frame divides | **4–7 fps** | low |
| 5 | **Tighten the inner loops** (pipeline wall loop, word-store flats, kill scalar tail) | Load-use stalls; byte stores on contiguous spans; up to 3 slow tail px/col | **3–6 fps** | low→med |
| 6 | **Deferred palette upload in vblank + non-blocking input** | Mid-frame CRAM stalls; SH-2 spins on the 68K for every pad read | small fps + correctness | med |

These overlap (you can't just sum them), but the structural three — **SDRAM placement +
caching discipline + load balance** — plausibly close most of the 12→30 gap on their own.

---

## Myth-busters — do NOT spend a session on these

- **Framebuffer DMA.** Confirmed d32xr writes *every* visible pixel with parallel SH-2 byte
  stores (`srcref/d32xr/sh2_draw.s`). Its only DMA (`dma_to_32x` `src-md/crt0.s:3172`,
  `Mars_HandleDMARequest` `marshw.c:877`) targets **SDRAM** for WAD/CD asset loading, gated by
  the RV bit — never the framebuffer. Wiring SH-2 DMA to `0x24000000` buys nothing.
- **Transposed / burst-write framebuffer for vertical columns.** Their 8bpp column layout is
  byte-strided by 320 (`sh2_draw.s:35-38`), identical to ours. No magic buffer to mine.
- **Replacing the COMM-port spin-wait.** d32xr ships the *identical* mailbox primitive
  (`mars.h:82` `Mars_R_SecWait`, `marsnew.c:355-410` dispatcher). The spin is not the cost —
  idle time and uncached reads are. Keep our COMM4 handshake.

---

## What we already got right (don't regress)

- **Single per-column `shade_lut`** (`raycast.c:~1999`): one indexed load/pixel. d32xr's
  textured path does *two* loads (texel + colormap, `sh2_draw.s:57-78`). We're ahead — do
  **not** adopt their GBR two-load colormap.
- **32-bit framebuffer clear** with color broadcast (`raycast_clear_half`): 4 px/store, dual-CPU
  split, gradient folded in. Matches `I_ClearFrameBuffer` (`marsnew.c:809-815`). (Nit: pin it
  `optimize("O1")` like d32xr to guarantee GCC doesn't substitute a slow `memset`.)
- **DIVU latency-hiding** for `lineHeight`/`tex_step` (`raycast.c:~1799,~1949`): start →
  ~256 cyc of useful work → read. Exactly d32xr's `r_phase6.c:190-247` pattern.
- **`fx_div_hw`** (`raycast.c:241-255`) is a faithful — and *slightly better* — port of their
  `_FixedDiv` (`sh2_fixed.s:39-50`): we synthesize the `0xFFFFFF00` base with `mov #-128/add`
  instead of a literal-pool load (1 cyc cheaper).
- **`cameraX_table`** avoids runtime atan in ray setup — equivalent to their `tantoangle`/
  `viewangletox` LUTs.
- **COMM4 mailbox + NOP-throttled poll** is the same family as `Mars_R_SecWait`.

---

## Lever 1 — Run hot code & textures from cacheable SDRAM (single biggest lever)

**Their call:** d32xr tags the per-frame-hot renderer functions
`ATTR_DATA_CACHE_ALIGN = __attribute__((section(".sdata"), optimize("Os")))`
(`doomdef.h:61-62`; applied to `Mars_Sec_R_WallPrep`, `R_DrawPlanes`, `finesine`, etc.,
`mars.h:64-71`). `.sdata` is collected into the **RAM** region at `0x06000000`
(`mars-ssf.ld:113-114` `> ram`), so the hot path executes from **cacheable SDRAM**, not ROM.
Textures get the same treatment via `R_AddToTexCache` (`r_cache.c:173-250`): `D_memcpy` the
flat/texture out of ROM into a `Z_Malloc`'d SDRAM zone and redirect the sampler there.

**Our gap (incomplete guesswork):** `mars.ld:37-71` sends **all** `*(.text*)` to ROM at
`0x02000000`. The entire raycaster — `raycast_draw_walls` and the per-pixel inner loop — runs
from ROM, where SH-2 instruction fetch is slow and uncacheable through the bank mapper. Our
wall textures (`wall_tex.h`, `wall_tex_hi.h`, `partition_tex.h`, `raycast.c:6-11`) are
`.rodata` → ROM, sampled per-pixel straight from ROM.

**Adopt:**
1. Add a `.ramtext` (or `.sdata`-style) output section in `mars.ld` mapped to RAM, **loaded
   from ROM and copied by crt0/startup** the way `.data` already is. Tag `raycast_draw_walls`
   + the column inner loop (and `raycast_draw_carpet`/ceiling) into it.
2. Stage the active wall/partition textures into a `.bss` SDRAM buffer once at level load;
   point the sampler at the SDRAM copy so per-pixel fetches are cacheable. Our texture set is
   small and fixed — no LRU machinery needed, just a `memcpy` at startup.

**Risk:** med — crt0 must copy the section before first call; verify free RAM budget in
`mars.ld` (the `0x3FC00` region). **Impact:** 4–12 fps combined (the inner loop is plausibly
ROM-I-fetch-bound). **Verify first** with an FRT bracket: temporarily `memcpy` the wall
texture to a `.bss` buffer and sample from it; measure `W` delta.

Refs: `srcref/d32xr/doomdef.h:61-62`, `mars.h:64-71`, `mars-ssf.ld:113-114`,
`r_cache.c:173-250`, `r_main.c:589`; ours `sh_src/mars.ld:37-71`, `raycast.c:6-11,~403`.

---

## Lever 2 — Cache the hot arrays, purge per-line (don't always-uncache)

**Their call:** d32xr keeps shared data in **normal cached** memory and invalidates only the
specific lines at the cross-CPU handoff, via the `0x40000000` purge alias:
`Mars_ClearCacheLine(addr) = *(volatile uintptr_t*)((addr)|0x40000000) = 0` (`marshw.h:75`),
`Mars_ClearCacheLines(paddr,nl)` loops 16 bytes/line (`marshw.h:82-90`). The ring buffer
purges just its 3-line header before each read (`mars_newrb.c:82,136,…`,
`mars_ringbuf.h:142,162`). Only the tiny rover/handshake words use the `0x20000000`
cache-through alias (`mars_ringbuf.h:37-38`); the bulk payload is cached.

**Our gap (incomplete guesswork):** We **over-applied** a good pattern. `shared.h:14` even notes
"~12 cycles per read." Every `PFACE_*(i)` and `CELL_LIGHT(y,x)` read goes through
`((volatile*)(ptr|0x20000000))[i]` — *always uncached* — in the per-ray hot loop
(`raycast.c:209-216,~302`). But `pface_*` is **written once/frame by the primary, then read
thousands of times by both CPUs**; `cell_light` likewise. We pay the 12-cyc tax on every
element of a hot array when we should pay it only on the handful of words that actually changed.

**Adopt:** Add a `clear_cache_lines(ptr,n)` macro (the `|0x40000000` store loop). After the
`COMM4=HALF` kick, **on each CPU**, purge the `pface_*` and `cell_light` lines *once*, then
change `PFACE_*` / `CELL_LIGHT` to **cached** access for the rest of the frame. Keep
`0x20000000` only for the small `SHARED_UC` doorbell/snapshot fields (those are correct —
written once, read once).

**Risk:** med — must purge on *both* CPUs every frame *after* the producer's writes drain; a
missed purge = stale geometry. **Impact:** 4–8 fps (this is the per-ray inner loop).

Refs: `srcref/d32xr/marshw.h:75,82-90`, `mars_ringbuf.h:37-38,142,162`, `mars_newrb.c:82`;
ours `sh_src/shared.h:14,114`, `raycast.c:209-216,~302,~1786`.

---

## Lever 3 — Dynamic dual-CPU load balance

**Their call:** d32xr never statically owns "left half / right half." Work is split by *kind*
(phase pipeline, `r_main.c:1124-1183`) and *dynamically within* a phase:
- **Work-stealing via 32X System Register comm ports, not SDRAM atomics.** The shared work
  cursor `pl_next` *is* `MARS_SYS_COMM6` = `*(volatile short*)0x20004026` (`r_phase7.c:56`,
  `32x.h:48`). `R_GetNextPlane` does `p = pl_next; pl_next = p+1;` (`r_phase7.c:272-291`).
  WallPrep uses the same register as an unlocked producer/consumer cursor (`Mars_R_WallNext`
  `mars.h:100-103`, `r_phase2.c:377-401`). The only real `test_and_set` (`pl_lock`,
  `r_phase7.c:257-270`) guards a 2-instruction critical section entered tens of times/frame.
- **Pixel-weighted split point**, not 50/50: `R_Sprites` computes `half` = pixel-count-weighted
  centroid column, master draws `[0,half)`, secondary `[half,width)` (`r_phase8.c:530-586`).
- **Largest-job-first ordering** so barrier idle is bounded by the *smallest* remaining job
  (`Mars_R_SortPlanes`, `r_phase7.c:480-510`).

**Our gap (incomplete guesswork — this explains our reverted attempt):** Our note at
`raycast.c:1615-1622` records a TAS+COMM6 work-steal that did **~190K atomic bus ops/sec** and
starved the 68K→SH2 joypad bridge. We blamed "work-stealing." The real causes:
1. We stole **too fine-grained** (per-column) → far too many steals.
2. We put the cursor/lock in **SDRAM** (TAS on an SDRAM word) → every steal hit the SDRAM bus
   that the joypad bridge needs. d32xr's cursor lives in the **System Register mailbox
   (`0x2000402x`)** — dedicated hardware, off the SDRAM bus — so high-frequency polling there
   is free. Also `partition_build_faces()` runs serial on the primary *before* the kick
   (`raycast.c:~2303`), so the secondary idles through it.

**Adopt (sequenced low→high risk):**
1. **Dynamic split column (no atomics):** before the wall pass, sum a cheap per-column cost
   proxy (partition-face count + 1) into a prefix total; pick the column where the prefix
   crosses half. Master `[0,split)`, secondary `[split,320)`. Kills the documented imbalance,
   lock-free. *Best first step.*
2. **Coarse work-steal via comm port:** put `wall_next_col` in `MARS_SYS_COMM6` (or the unused
   COMM8/10/12, `32x.h:49-52`), hand out **8-column batches** (~40 steals/frame ≈ 2–3K ops/sec,
   ~60× under the 190K that broke us), guard with a single `tas.b` on a System-Register byte —
   or unlocked like WallPrep.
3. **Largest-batch-first** ordering once a queue exists.
4. Overlap `partition_build_faces()` with the secondary instead of running it serial.

**Risk:** low (split point) → med (work-steal). **Impact:** 3–6 fps; directly recovers
`prof_primary_idle_ticks`.

Refs: `srcref/d32xr/r_phase7.c:56,257-291,480-510`, `32x.h:48-52`, `r_phase8.c:530-586`,
`mars.h:100-103`, `r_phase2.c:377-401`; ours `raycast.c:1599-1622,~2303,~2338`.

---

## Lever 4 — Kill the remaining software divides

**Their call:** d32xr does almost no runtime division. The few it needs go through the hardware
DIVU with latency hidden (`r_phase6.c:190-247`, `r_phase2.c:178-213`), and the per-row/per-
column scaling is **precomputed reciprocal tables**: `yslope[i] = FixedDiv(stretchWidth,|y|)`
once per resolution (`r_data.c:597-603`), then per-scanline is just
`FixedMul(height, yslope[y])` (`r_phase7.c:80`); `distscale[x] = FixedDiv(FRACUNIT,cos)>>1`
once (`r_data.c:606-611`).

**Our gaps:**
1. **DDA `deltaDistX/Y`** (`raycast.c:~1677,1679`) = `FX_DIV(FX_ONE, |rayDir|)` — **software
   int64 divide** (`__divdi3`, ~200 cyc), run 2×/column × 320 × 2 CPUs ≈ **1280/frame**. The
   single largest software-divide cost in the renderer. `FX_ONE` numerator, bounded `rayDir`
   → no overflow → drop in `fx_div_hw`. **3–5 fps.**
2. **Per-row `rowDist`** (`raycast.c:~1425,~1554`) = `focal<<16 / p` via `divu_u32` **every row,
   every frame** (~112 distinct `p` × 2 halves × 2 CPUs ≈ 450 DIVUs/frame). It's a pure
   function of `p` and `focal_const` (changes only on look up/down). Build a
   `rowdist_lut[SCREEN_H]`, rebuild only on pitch change — d32xr's `yslope`. **1–2 fps**, and
   frees the DIVU slot that #1 needs.
3. **`FX_MUL` fallback check:** disassemble the wall/DDA hot loop; if any of the 52 `FX_MUL`
   sites emit `__muldi3` (a `bsr`/`jsr` in the `.lst`) instead of inline `dmuls.l`, add an asm
   `fx_mul` mirroring their `_FixedMul2` (`dmuls.l/sts mach/sts macl/xtrct`, `sh2_fixed.s:28-33`).
   **0–2 fps** depending on whether GCC already inlines it.

**Note:** the hardware DIVU returns a saturated sentinel (`0x7FFFFFFF`) on overflow after 6 cyc
(`sh2_fixed.s:47`), so we can lean on it instead of the int64 `fx_div_sat` path
(`raycast.c:223-229`) in more places — clamp inputs, trust the unit (d32xr's philosophy,
`r_main.c:114-115`).

**Risk:** low. Refs: `srcref/d32xr/sh2_fixed.s:28-50`, `r_data.c:595-611`, `r_phase7.c:80-96`;
ours `raycast.c:223-229,241-255,~1425,~1554,~1677-1679`, `sh2_asm.h:37-95`.

---

## Lever 5 — Tighten the inner loops

**5a. Software-pipeline the wall column loop.** d32xr interleaves the *next* pixel's index math
(`mov/swap.w/and`) into the load-use shadow of the current pixel's colormap load
(`sh2_draw.s:57-78`), hiding the `mov.b @(r0,…)` stall. Ours (`raycast.c:2069-2110`) is a
4× unroll but each pixel is an isolated chain: `mov.b @(r0,lut),r1` is used by `mov.b r1,@p`
one instruction later — a load-use stall on **every pixel**. Same 7 instr/px, just reorder to
compute pixel N+1's index between pixel N's load and store. **Risk:** med, zero memory/quality
cost. **Impact:** ~5–12% of wall-draw cycles, ~1–2 fps.

**5b. Word-store the horizontal flat/ceiling/floor spans.** Floor/ceiling pixels are
*horizontally contiguous*, so d32xr's span path uses `mov.w` (2 px/store) and `mov.b @-Rm`
auto-decrement to fold the pointer advance (`sh2_draw.s:305-329`, low-detail
`sh2_drawlow.s:320-345`). Our flat fills use byte stores + explicit `p++`. Word stores +
auto-increment roughly **halve** the store cost on flats — and flats have **no quality cost**
from word stores. **Risk:** low→med. **Impact:** 2–4 fps (floor/ceiling are a big chunk of frame).
*(Note: vertical wall columns are 320-strided and can't word-store — flats only.)*

**5c. Kill the scalar remainder loop.** Our `while (tail-- > 0)` (`raycast.c:~2115`) runs up to
3 fully-branched, un-pipelined pixels per column. d32xr handles the odd pixel with a mid-loop
entry (`shlr/movt/bt-s` into the body, `sh2_draw.s:51-54`) — zero-overhead. **Impact:**
0.5–1 fps (matters most with many short columns).

**5d. (Optional) low-detail mode.** d32xr's blunt lever: render half-width and double
horizontally (word stores throughout, `sh2_drawlow.s`). A toggle could approach 2× at a visible
resolution cost — keep in the back pocket for a "performance mode."

Refs: `srcref/d32xr/sh2_draw.s:51-78,305-329`, `sh2_drawlow.s:58-77,320-345`;
ours `raycast.c:2069-2119`.

---

## Lever 6 — Palette upload & controller (correctness + smoothness)

**6a. Deferred, batched palette upload in vblank.** d32xr's `Mars_SetPalette` just stashes a
pointer (`marshw.c:145-148`); the real CRAM write happens once/frame in the **VBLANK handler**
(`pri_vbi_handler` → `Mars_UploadPalette`, `marshw.c:1044,154-184`), one tight 256-entry pass
with **fade folded in** (`int r = br + *palette++`, clamped), guarded by `MARS_SH2_ACCESS_VDP`,
high bit `0x8000` set on every entry. Ours (`raycast.c:467-498` `raycast_set_brightness` /
`build_palette` → `Hw32xSetBGColor` `mars.c:19-24`) writes CRAM **per-entry, synchronously,
mid-frame**, no VDP-ownership guard, and our `COLOR` macro omits `0x8000`. **Adopt:** dirty-flag
+ single vblank upload with fade folded in; add the guard and high bit. **Impact:** small fps,
removes mid-frame VDP contention, fixes potential fade glitching. **Risk:** med (needs a primary
vblank hook).

**6b. Non-blocking controller read.** d32xr's 68K reads pads in **its own vblank ISR**
(`src-md/crt0.s:2817-2834`) and caches them; the SH-2 reads the cached value with **no spin**
(`Mars_ReadController`, `marshw.c:767-782`). Ours (`mars.c:177-181` `HwMdReadPad`) writes
`COMM0` and **spins** the full 68K round-trip every poll. Our `read_joypad`
(`md_src/md_start.s:214-251`) is byte-identical in 6-button technique to their `get_pad` —
**but lacks the `andi.w #0x0C00; bne no_pad` present-check guard** (`crt0.s:2870-2895`) that
returns `0xF000` instead of garbage on a disconnected/odd port. **Adopt:** 68K reads pads in
vblank into a shared register the SH-2 reads without handshaking; add the no-pad guard. Directly
relevant to our recent 6-button debugging. **Impact:** small fps + input latency/robustness.
**Risk:** med (changes the 68K/SH-2 contract).

Refs: `srcref/d32xr/marshw.c:145-184,767-782,1044-1053`, `src-md/crt0.s:2817-2906`;
ours `sh_src/mars.c:19-24,177-181`, `raycast.c:467-498`, `md_src/md_start.s:214-251`.

---

## Suggested sequencing (measure with the C/G/R/W + F: profiler between each)

1. **Lever 4** (software divides → `fx_div_hw` + `rowdist_lut`) — lowest risk, immediate,
   self-contained. Confirms the FRT harness, frees DIVU slots. *Start here.*
2. **Lever 3.1** (dynamic split column) — lock-free, low risk, attacks the documented idle.
3. **Lever 1** (SDRAM hot code + texture staging) — biggest single lever; verify with a scoped
   `memcpy`+bracket before the full `.ramtext` linker work.
4. **Lever 2** (cache + purge hot arrays) — pairs naturally with Lever 1's cache work.
5. **Lever 5** (loop pipeline, word-store flats, tail) — incremental polish once the structural
   wins land.
6. **Lever 3.2** (coarse comm-port work-steal) + **Lever 6** (palette/input) — last, highest
   contract-risk.

Each lever should be its own measured commit. Re-read `prof_pass_*` (`C/G/R/W`) and `F:` before
and after — the per-pass profiler we built is exactly d32xr's `r_main.c:1130-1182` FRT timing,
so we already have the instrument to attribute every change.

---

*Agent transcripts (5 subsystem deep-dives) condensed here; `srcref/d32xr/` is gitignored
reference. Generated as the pre-work map for the 30fps optimization arc.*
