# Balanced walking: quality floor instead of a 2x2 frame

The goal is smooth movement with useful detail. FULL-only addressed the blocky
image but gave back the speed; AUTO/SCALE's 160x112 image was too coarse.
BALANCED is now the default WALLS choice (mode 7).

- All 224 framebuffer rows stay native. The line table does not duplicate rows.
- While moving or turning, eligible wall quads cast three rays across four
  columns (offsets 0,1,2; only offset 2 duplicates into column 3).
- Contact-distance faces (one cell) and door-sign spans keep native columns.
  A close hit discovered after the previous-quad estimate restores the native
  ray before drawing the pair. Both CPUs use aligned pairs and stamp the
  matching depth/occlusion metadata.
- Standing returns to native wall sampling. Near furniture, sprites, lighting,
  floor and ceiling keep their existing native-height rendering paths.
- FULL, legacy HALF/LOW and AUTO remain available explicitly. The adaptive
  style row continues configuring AUTO. Frame pacing stays enabled normally.

This is a depth-protected 75% horizontal sampling floor for walls, not a
240x168 framebuffer, not a universal pixel budget reduction, and not a promise
of 30/60fps. Nearby overrides mean it often casts more than 75% of the columns.
There is no whole-frame half-resolution fallback in BALANCED.

## Hardware comparison with walking paths active

Same binary, pinned split, authored fixed cameras, moving render/audio paths
forced on, eight warmup frames then 64 samples. Governor bypassed for attribution;
HUD overhead is present in delivered vblanks. Both the ceiling and quiet-audio
optimizations are enabled; walking causes the general footstep mixer to run.

| View | Renderer ticks FULL → BALANCED | Delivered vblanks FULL → BALANCED |
|---|---:|---:|
| Corridor | 20270 → 19053 | 8.36 → 8.00 |
| Open room | 19316 → 17474 | 8.00 → 7.69 |
| Crawl stress | 29346 → 27604 | 11.50 → 11.00 |
| Partitions | 38604 → 31676 | 14.61 → 12.16 |

Renderer cost falls about 6–18%. The partition delivery mean improves about
20% in rate, but these instrumented fixed-pose figures are not normal-play FPS
or a stutter histogram. This is a useful quality/speed compromise; the legacy
HALF mode is still faster and substantially coarser. User playtesting remains
the judge of perceived smoothness and the remaining horizontal aliasing.

A first, more conservative near-detail guard barely helped the partition view
and was rejected. The final build uses the contact-distance guard above.

## Validation and reproduction

Four ares framebuffer comparisons found changes only in the intended fourth
columns, excluding benchmark text rows 32..57. Standing BALANCED matches FULL.
The column loop resets actual stride and pixel-store width together; paired
writes occur only at aligned offset 2 of a quad. No reduced-row store path is
used. Native moving-state trace and hardware smoke check are recorded alongside
the final normal ROM. The normal ROM differs from the timed binary at only
its default resolution-mode byte, so the qualified renderer layout is preserved.

```
make release MARSDEV=/path/to/marsdev TARGET=/tmp/balanced
python3 tools/gameplay_benchmark_ab.py /tmp/balanced.32x \
  --experiment balanced-walking --output-dir /tmp/balanced-ab
```

Within each generated pair, only DIAGCFG1+13 differs: FULL vs BALANCED. All
numbers are decoded exactly from the project font, rather than estimated from
small HUD text. [Raw captures, pixel checks and hashes](balanced-walking-2026-11-16/).
