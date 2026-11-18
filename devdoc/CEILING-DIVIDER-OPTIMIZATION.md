# Exact ceiling-grid hardware division

The two ceiling-grid scale calculations still used FX_DIV, a signed software
64-bit division, even though the wall renderer already uses the SH-2 divider.
The experiment keeps both paths in one binary, selected by diagnostic byte 28.

## Why the replacement is exact

Each site is reached only when abs(dX) or abs(dY) >= FX_ONE (65536).
The dividend is `(SCREEN_W << 16) << 16`, with SCREEN_W=320; its quotient's
magnitude is at most `SCREEN_W << 16` = 20,971,520. It therefore fits the signed
32-bit hardware quotient. The existing fx_div_hw helper supplies the same
signed 64-bit dividend and divisor and truncates toward zero, matching FX_DIV.
There is no approximation, precision reduction or changed divide-by-zero path.
The existing guards also bypass cardinal-axis zero spans.

Only raycast_draw_ceiling_grid changes. Grid crossing locations, colors,
occlusion, carpet, geometry and framebuffer store paths are unchanged.

## Reproduction

```
make release MARSDEV=/path/to/marsdev TARGET=/tmp/gameplay-div
python3 tools/gameplay_benchmark_ab.py /tmp/gameplay-div.32x --output-dir /tmp/gameplay-ab
```

Each scene's two ROMs differ at exactly DIAGCFG1+28. The generator writes a
manifest with hashes and flags. Byte 27 launches directly into an authored,
fixed-camera scene without menus or attract progression. Byte 9 pins the split
at 160, byte 13 pins full resolution, byte 18 disables governor padding. The
world animation clock stays at 64; both CPUs render through the normal pipeline
and audio service remains enabled. The benchmark skips player/game logic; it
is a renderer comparison, not an end-to-end gameplay benchmark.

Scenes 1..4 use BACKROOMS at corridor (16.5,28.5,192), room (1.3,13.8,0), crawl
(1.3,22.3,64), and partitions (27.8,14.4,212). Scene 5 is optional TESTBED spawn.
Eye height is pinned to 128; the crawl view is a fixed standing-eye renderer
stress view rather than a collision-valid crouched playthrough.

After eight warmup frames, the benchmark averages 64 frames once and holds
those results. The small white line is G (primary ceiling-pass average), R
(whole raycast_render average, including the secondary barrier), and V (mean
delivered vblanks multiplied by 100). G/R use FRT Phi/128 ticks (~5.56us).
V must read COMM12's high word; the low word is SMS status, not the clock.
Wait until G/R/V are nonzero before recording; heavy scenes need more time.
The ordinary red HUD remains instantaneous and has its own FPS correction.
The benchmark HUD adds work to both arms, so V describes instrumented delivery.

## Validation

- Marsdev GCC 15.2.0 O2/LTO builds succeeded.
- Four ares fixed-scene comparisons matched all 320x224 world pixels in a
  completed 32X framebuffer page, excluding only benchmark text rows 32..48.
  The alternate page can be partway through rendering at an asynchronous dump.
- Four screenshot world-region comparisons also matched exactly.
- The box-transform oracle remains passing (120 baked poses).

## Office MiSTer results — 2026-11-16

Each row averages 64 frames after eight warmup frames. Audio remains enabled.

| Scene | Ceiling G, old → new | Whole render R, old → new | Delivered vblanks, old → new |
|---|---:|---:|---:|
| Corridor | 979 → 769 | 19811 → 19524 | 8.25 → 8.11 |
| Room | 2701 → 2503 | 19044 → 18828 | 8.00 → 8.00 |
| Crawl stress | 2394 → 2155 | 27394 → 27152 | 11.00 → 11.00 |
| Partitions | 1105 → 712 | 37208 → 36749 | 14.00 → 14.00 |

Verdict: **SHIP the exact divider replacement**, retaining byte 28=0 as the
software reference. Normal builds now set byte 28=1; all benchmark bytes remain
zero and the original box transform stays selected. The crawl and partition
samples gain no delivery bucket, while the corridor delivery mean improves slightly; room delivery stays unchanged.
Ceiling-pass averages improve 7–36%; whole-render savings are only 0.9–1.5%.
Single-frame ceiling savings looked larger and must not replace these averages.
There is no general FPS claim and no new 30fps claim. The HUD's bridge traffic,
fixed split, full resolution, frozen animation clock and synthetic standing-eye
crawl view limit extrapolation to ordinary play.

[Raw captures, transcribed values and build provenance](ceiling-div-hardware-2026-11-16/).
Heavy scenes did not finish 64 frames within the first 18-second capture window;
those incomplete zero-average frames were discarded and retaken after 35 seconds.
The final normal-play build also fixes the intro box's old COMM12 low-word FPS
read; that reporting correction does not alter its transform algorithm.

Numeric captions were subsequently decoded directly against the project font;
this corrected manual transcription of three values without changing the verdict.
