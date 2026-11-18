# Furniture projection: explicit SH2ZAM divider — 2026-11-16

The library split divider is numerically compatible in the three fixed views,
but this trial does not demonstrate a worthwhile gameplay performance win.
Blocking library calls are slower than the existing hand-written hardware
helper. Diagnostic 29 defaults to zero; neither candidate is promoted.

The actual chair, desk and free-standing PVM geometry is placed at (16.5,26.0)
in TESTBED, viewed from its normal spawn (16.5,27.5), facing north. Existing
standups are cleared for these diagnostic fixtures. Scene selectors 6/7/8
exercise the normal renderer, including shadows, textures, audio, secondary
CPU, occlusion and face ordering. They are not the repeated-AABB microbench.
Native full-resolution rendering, fixed split 160, frozen animation input and
existing quiet-audio/ceiling optimizations are identical across arms.

Modes (diagnostic 29): 0 existing inline hardware assembly; 1 explicit library
blocking Q16; 2 explicit library start/finish with lateral-coordinate arithmetic
between them. Both the eight corners and textured face centers use the
candidate. The depth has passed the existing near gate, so reciprocal output
fits int32. Pure multiply/subtract operations are the only work between start
and finish; current ISRs do not use DIVU. This adds no interrupt handler.
[Disassembly excerpts](split-scheduling.txt) confirm that three DMULS operations
remain between divider start and quotient read at both sites in this build.

Every arm uses the same linked ROM, changing only byte 29 within each fixture.
[manifest.json](manifest.json) records uploaded ROM identities. GCC 15.2.0,
Marsdev, existing -O2/LTO/-m2/-mb release flags; MiSTer office S32X_20260603 core,
SHA256 `da7694406c4268aedeb7d5789bf20eb5502c2eff45c1fdedb009f217709b9d9a`.
MiSTer validates that FPGA implementation, not physical SH7604/Saturn silicon.

Each boot warms eight frames, averages 64 frames and captures after 22 seconds.
Order is original / split / blocking / original per fixture. The repeated
baseline exposes drift rather than assuming one sample is exact. Times are FRT
Phi/128 ticks (~179,766/s), not CPU cycles. C is the complete primary furniture
pass, including face handling and rasterization; R is total elapsed renderer
including partner/audio interference. Model vertex counts are primary-only,
include clipped corners and exclude additional textured-face centers.

| Fixture | Primary model vertices | Original C (opening/closing) | Split C | Blocking C | Original R (opening/closing) | Split R | Blocking R |
|---|---:|---:|---:|---:|---:|---:|---:|
| Chair | 72 | 870 / 868 | 865 | 930 | 25322 / 25359 | 25315 | 25381 |
| Desk | 24 | 936 / 937 | 934 | 962 | 24992 / 24984 | 24968 | 25003 |
| PVM | 48 | 1429 / 1430 | 1424 | 1468 | 25320 / 25287 | 25281 | 25326 |

Split mode saves only 2–6 primary furniture ticks versus the opening/closing
baselines. Total-renderer differences are small relative to the observed
baseline variation; all arms deliver the same 10.00-vblank average in these
fixtures. This is not evidence of a useful FPS improvement. Library blocking
costs about 3–7% more primary furniture time, showing why replacing an existing
hardware helper with a generic API is not automatically beneficial.

Correctness: Ares frame 400 dumps of all three modes compare exactly across
49,280 pixels (320 x rows 70..223) in a completed framebuffer bank for each
fixture. The top diagnostic region is excluded; three fixed views are not
exhaustive camera/near-plane testing. The other bank can be partway through a
render at the emulator cutoff and is not treated as a completed comparison.
[Pixel checks/hashes](emulator-pixel-checks.json). Existing 120-frame intro
oracle and 2,048 real audio fixtures also pass; [test log](regression-tests.log).

The exact numeric HUD decoder refuses non-matching glyphs, zero vertex counts
and unfinished averages:

```sh
python3 tools/read_furniture_benchmark.py devdoc/furniture-divu-2026-11-16/*png
```

[results.json](results.json) retains all 12 captures' decoded values.
[candidate-kernel.patch](candidate-kernel.patch) isolates the library adapter
for review; source currently retains it behind diagnostic 29. The new branches
can affect code layout even while disabled, so a future normal-game binary
still needs performance verification before deployment. The known BALANCED
ROM was restored after the sweep; no new quality settings were enabled.

The next candidate is immutable real-mesh preparation plus MAC, with complete
setup/conversion cost and rounding/clipping checks. See the
[prioritized integration list](../SH2ZAM-NEXT-INTEGRATIONS.md).
