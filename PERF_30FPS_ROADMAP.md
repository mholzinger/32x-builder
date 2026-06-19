# Road to 30fps — corrected cost model (2026-06-19)

**This supersedes the lever priorities in `D32XR_MINING.md`.** That doc's top
bets (SDRAM code/texture placement) were chasing the wrong bottleneck. Built
from 4 parallel deep-dives (our cost accounting, d32xr viewport/budget, 32X
framebuffer write cost, why texture-staging failed) + direct verification of
`raycast.c`. Citations are `file:line`.

---

## Headline

We're stuck at ~15fps because **(1) we draw 2–4× the pixels d32xr does**, and
**(2) every pixel is an uncached byte-store to the VDP framebuffer** — so the
wall loop is *store-bound, not compute/fetch-bound* (which is why staging
textures into SDRAM did nothing). On top of that, **~25% of the frame runs
single-CPU after the sync barrier and is completely unmeasured.**

It is NOT a micro-optimization problem. The path to 30 is *fewer pixels + keep
both CPUs busy the whole frame* — d32xr's actual recipe.

---

## Verified cost model (MiSTer, ~47,000 ticks/frame ≈ 15.3 fps)

FRT runs Φ/32 ≈ 720 kHz (1.39 µs/tick); ~12,000 ticks/vblank; 15fps ≈ 48,000
ticks/frame. The frame splits:

```
T (47k) = parallel half (max(H,S) ≈ 35k)  +  serial tail (~12k)
```

### Parallel half (~35k) — MEASURED, load-balanced across both SH-2s
Both CPUs render their column range `[0,split)` / `[split,320)`:
clear → ceiling_grid → carpet → **walls**. `raycast_render` 2908–2915; secondary
mirror in `s_main.c:57–70`. The adaptive `split_col` keeps H≈S (34k/35k).
- **W (walls) ≈ 18–21k — ~60% of H. The long pole.**

### Serial tail (~12k = a FULL QUARTER of the frame) — UNMEASURED, single-CPU
Runs full-width on the **primary alone** after the barrier, while the secondary
is already idle (ACK'd COMM4, back to `amb_pump`). `raycast.c`:
- `raycast_draw_low_ceiling(0, SCREEN_W)` — 2941
- `raycast_draw_bulkheads(0, SCREEN_W)` — 2942
- `draw_lights` — 2948
- `draw_standups` — 2953
- head-bob line table — 2972

**The profiler's last FRT bracket ends at 2915 (walls); idle latched at 2931.
Everything from 2941 on is invisible to the C/G/R/W overlay.** This block grows
with crawlspaces (the new slab+bulkhead passes live here) — i.e. crawlspace-heavy
scenes get slower in a way we currently cannot see.

---

## Root cause 1 — pixel budget (the d32xr gap)

d32xr is NOT full-screen and NOT full-detail:
- 3D viewport default **320×180**, minus a **40px status bar** → **320×140**
  textured world window (`r_main.c:232–237`, `r_local.h:28`, `doomdef.h:562,785`).
- **Low detail is the DEFAULT** (`detmode_lowres`, `r_local.h:57–64`,
  `marssave.c:291`): half-rate flat span drawers; optional `lowres` halves
  columns 160→320 (`r_main.c:260–261`, `marsnew.c:778`).
- **BSP culling**: walls only where visible segs are; floors/ceilings only as
  visplanes, clipped per column. It does NOT texture every column.
- **30fps cap** (frame-time driven, `MINTICSPERFRAME=2` → 60/2; `marsnew.c:973–980`).

We render **full 320×224, a wall slice in every one of 320 columns, a full floor
cast AND a full ceiling cast, every frame, no culling, no status bar, no
low-detail** → ~71,680 px vs their ~44,800-and-usually-much-less. **2–4× the
mandatory textured pixels.**

## Root cause 2 — uncached framebuffer store binds the wall loop

Both engines write pixels with uncached SH-2 `mov.b` straight to `0x24000000`
(`fb_pixels()` raycast.c:715; d32xr `sh2_draw.s:64`, `marsnew.c:81`). Confirmed:
no cached-SDRAM-render-then-copy path exists; FB DMA is a genuine dead end.

In our wall inner loop (raycast.c:2550–2591, 4px/iter hand asm): per pixel is
`mov.b @(r0,lut),r1` (cached on-stack LUT — cheap) … `mov.b r1,@p` (uncached FB
store, **~8–12 cyc, ~55–65% of the per-pixel cost**) … `add sw,p`. The load-use
slot is already filled by `add step,tp` (good). **The store is the bottleneck,
not the texture read** — so texture-in-SDRAM staging was inert by construction.

Wall columns are **320-strided (vertical)** → those stores **cannot** be packed
into wider words. The only way to spend fewer wall-store cycles is to draw fewer
wall pixels (→ low-detail / smaller viewport). Per-column **setup** (DDA 2×
`fx_div_hw`, the partition loop's `volatile` `pface_*` reads + divides, the
shade_lut build) is a secondary ~20–35% of W.

One hardware lever BOTH engines miss: VDP **auto-fill** (`MARS_VDP_FILLEN/FILADR/
FILDAT`, `32x.h:67–69`) for solid clears/spans. Minor for us (clear is already
4px/word, raycast.c:2790).

## Why the prior session's optimizations didn't move the needle
- **Texture→SDRAM staging**: wall loop is store-bound, not fetch-bound. Inert.
- **`fx_div_hw` in the DDA / adaptive split / cached `pface_*`**: real but small;
  they trim setup and idle, not the dominant store-bound pixel count.

---

## Prioritized levers (don't cleanly sum)

| # | Lever | What | Est. | Risk |
|---|-------|------|------|------|
| **E** | **Make the profiler honest** | FRT-bracket the serial tail (low_ceiling/bulkheads/lights/standups) + show it. We're optimizing blind on 25% of the frame. | enables everything | trivial |
| **A** | **Parallelize the serial tail** | Re-home low_ceiling/bulkheads/lights/standups into a SECOND parallel phase across both CPUs (they need the combined WALL_DIST z-buffer, so they run post-walls, split by column). 12k→~6k. | ~15→17–18 fps | med |
| **B** | **Low-detail mode** | Render 160 columns, double to 320. ~Halves walls + setup + floor/ceiling. Parallel half 35k→~19k. The d32xr default. **Biggest single lever (~1.6–2×).** | large | med |
| **C** | **Smaller 3D viewport** | 320×~160 + HUD/letterbox band → ~30% fewer wall/floor/ceiling px. Stacks with B. | ~10–20% | low |
| **D** | **Per-column setup trim** | Snapshot `volatile pface_*` to locals; cut a partition-loop divide. | ~1–2 fps | low |

**A + B together plausibly reach ~28–30 fps** (parallel half ~19k + parallel
tail ~6k ≈ 25k ≈ 29fps). That's the realistic road to 30.

## Sequencing (each its own measured commit; read C/G/R/W + F between)
1. **E** — bracket the serial tail (makes the invisible 12k visible). *Doing now.*
2. **A** — second parallel phase for the tail. Biggest structural win, no quality cost.
3. **B** — low-detail toggle as an opt-in "performance mode" first.
4. **C / D** — viewport trim + setup trim, incremental.

## Open caveats / things to re-verify on hardware
- A: low_ceiling/bulkheads must run after BOTH halves' WALL_DIST is committed;
  splitting them by column in a 2nd phase preserves that (the barrier already
  passed). lights/standups already z-test the full buffer — safe to split.
- B: low-detail needs the floor/ceiling/wall passes + the slab/bulkhead casts to
  all honor a horizontal scale; column-doubling on display via the line table or
  a 2px write. Measure W and the flats separately.
- The agent subagent IDs (if continuation needed this session): cost-model
  `ac48ce2cece02efa7`, d32xr-budget `a950657591b6d51b0`, fb-cost
  `a26aa48a962918979`, wall-bind `ac8fe8cde34240c6d`.
