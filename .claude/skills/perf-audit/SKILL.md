---
name: perf-audit
description: Run a pre-ship performance audit on a 32X build — gameplay frame stutter, CPU/bus contention, and per-pass attribution from the in-ROM HUD counters. Use when asked to "audit perf", "check for stutter", "scan for bus saturation", "is this build fast enough", or before calling a build good. Also use before proposing ANY optimization, to check the target is actually the bottleneck in the scene that matters.
---

# Perf audit pass

The engine is **vblank-locked**. Render finishes with slack and idles to the next
vblank boundary, so fps is quantized to 60/N. **Shaving a pass moves 0 fps until
you cut a whole vblank (~2,800 ticks — nearly an entire pass.)** Every audit ends
in one of two verdicts, and "we made pass X 12% cheaper" is not one of them:

- **SHIP** — no frame crosses a vblank boundary it didn't cross before.
- **HOLD** — a named scene gained a vblank; here is the pass that did it.

## Step 0 — arm the capture (do this first, it invalidates everything else)

1. **Pin `WALLS`** to a fixed mode in the pause menu. `AUTO` makes the renderer
   path-dependent — resolution is picked from a frame EMA plus a *stillness
   ratchet*, so standing still to read the HUD ratchets you toward FULL. The same
   camera has read W:3,898 and W:6,258 with zero code change. Observing it
   changes it.
2. **Pin the split** (HUD `K:`). The adaptive balancer hands each CPU a different
   column set; cross-build W/H/S numbers are meaningless unless `K:` matches.
3. **Close the menu.** Pause-menu frames add heavy fill load. Never mix them in.
4. **Commit between arms.** The build stamp tracks commit count, so several
   rebuilds in one session all stamp the same B00xxx and the ROM cannot identify
   itself.

Capture with `./capture.sh raw` — **raw mode, never the default dedup.** Dedup
deletes static frames but always keeps red-border overrun frames (the border
flipping black↔red *is* a visual difference), so the surviving set is enriched
for exactly the thing being counted.

## Step 1 — validate the frame before believing it

Discard any frame failing these. A wrapped or racy counter read has produced days
of wrong theory:

- `O ≤ W ≤ H ≈ C+G+R+W` and `H ≤ T`
- `T` in a sane band (60fps ≈ 3,000 FRT ticks at Φ/128; `F = 180000/ticks`)

Subtract nothing for the profiler, but know it is there: **the profiler costs
~2,255 ticks and is inside every number this project has ever captured**
(measured dead flat across wildly different loads, so it is fixed overhead, not
load). Players don't pay it — `g_metrics_on` defaults 0. A half-res corridor
reading F:10 is nearer F:12 in the wild.

## Step 2 — attribute, per scene, not globally

**There is no single bottleneck. It flips with scene type.** Audit all four or
the audit is worthless:

| scene | dominant pass | reference ticks |
|---|---|---|
| corridor | `W` walls | 6,268 |
| open room | `R`+`G` floor/ceiling | 7,929 (W only 3,581) |
| crawl / dark room | `L`+`P` slab/sprites | 7,349 |
| partition view | `W` walls, `OD` 92% | 7,121 |

Counters: `C/G/R/W` = clear/ceiling/carpet/walls (parallel half) · `L/O/U/P/I` =
slab/overlay/overlay-px/standups/lights (serial tail) · `HU` post-render draw ·
`SW` swapBuffers · `ID` primary spinning on the secondary barrier · `OD` % of
screen covered by wall strips.

Frame ≈ `max(H,S) + ID + L + I + P + HU + SW`. That accounts for ~93% of the
frame; a gap much over ~58 ticks means something new is unmeasured — find it
before optimizing anything.

## Step 3 — the contention / saturation read

There is **no direct bus-saturation counter in this ROM.** Use the proxies, and
say "proxy" out loud when reporting:

- **`ID`** — primary idling on the secondary's barrier. This is the CPU-balance
  signal. `ID` 93 with H and S within 51 ticks means the dual-CPU split is
  already optimal. A large `ID` is a *balance* problem, not a bus problem.
- **`H` vs `S` skew** — almost always "one CPU's columns are heavier", not "one
  CPU is slower". Per-column cost spreads ~7x across a single pose (overlay+door
  columns ~650 ticks/col vs ~94 in a corridor).
- **Framebuffer writes are NOT the lever.** Measured: writing walls to the
  uncached FB costs the same as cached SDRAM, because the SH-2 cache is
  write-through. Word-pair tricks and DMA-to-FB are off the table. The lever is
  offload to idle silicon.
- **`WALLS=SERL`** serializes the halves (primary waits, then renders alone):
  pure work, no contention. The frame feels awful by design — only the pass row
  is meaningful. Diff SERL against normal to separate work from contention.

## Step 4 — verdict

Report per scene: pass ticks, `T`, `F`, whether `F` changed, and which vblank
bucket the frame sits in. Then:

- `F` unchanged and no frame near a boundary → **SHIP**.
- `F` dropped, or a frame moved from "just under" to "just over" ~2,800-tick
  boundary → **HOLD**, name the pass and the scene.
- `F` flapping 12↔15 → the work sits *on* a boundary. Read the pass brackets for
  attribution; never read `T`.

## Do not re-litigate (closed with numbers)

- Dual-CPU rebalancing — `ID` 93, already optimal.
- Lights — ~1.5% of frame; no spatial index needed.
- `SW` ~2,500 — display sync, under one vblank, mostly not ours.
- Quarter-res — tied HALF on fps while looking worse. HALF is the shipping floor.
- Micro-optimizations in general — see the vblank lock at the top.

**Known open:** ceiling `G` has no LOD of any kind and can't take the covered-row
skip (its grid lines compare world coords between *consecutive* rows). Slab `L`
is the largest untouched pass and already has a BULKHEAD A/B toggle.

## A/B rules, if the audit turns into a fix

1. Cross-build A/Bs on MiSTer are untrustworthy to ±1,600 ticks (~5-10%) — any
   size change shifts addresses into different SH-2 cache sets. Two consecutive
   A/Bs once gave opposite signs from layout noise alone.
2. The only trustworthy A/B is **both variants in ONE binary behind a runtime
   toggle**, flipped in place on the same scene.
3. Model cycles from `sh-elf-objdump -d -j .ramtext`, not from the C. A change
   that looked 40 cyc/candidate cheaper on paper lost 1,700 ticks by starving the
   register allocator.
4. The lobby is useless for A/B (standup animation desyncs the EMAs). Use
   BACKROOMS spawn or a static procgen vantage, `WALLS:FULL`, flip both ways
   twice.
