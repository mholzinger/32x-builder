# Small batches and box endpoint reuse

This is a correctness and target-timing fixture, not a desktop speed ranking.
The synthetic repeated AABBs contain no game assets. Counts are 8, 24 (three
boxes / desk-sized), 28 (intro-sized), 48 (six boxes), 72 (nine boxes /
chair-sized), 128 and 256 vertices. The last four vertices in the 28 case
use the scalar tail. Repeated identical dimensions deliberately favor generic
coordinate reuse at the two larger counts; these are not representative scene
frequency measurements. Corner order uses binary X/Y/Z bits, so the upstream
`blocks8` pattern is intentionally not selected.

Build from the repository root:

```sh
cmake -S . -B build -DSH2Z_STRICT=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/saturn/sh2zam_small_batch
```

`small_batch.c/.h` are platform-independent; the host executable emits fixture
hashes, never host performance claims. A target frontend provides a timer,
keeps the workspace naturally aligned and measures `sb_run`. It must check
`sb_correct` outside timing and retain/report the output hash. State is caller
owned; the fixed-capacity host workspace is 27,776 bytes on arm64 and varies
with ABI. It is benchmark infrastructure, not library allocation.

| Path | Work included in each timed call |
|---|---|
| GENERAL | Full-range scalar batch |
| BATCH | Matrix gate and ordinary fused batch |
| COLD-PREP | Position scan, plan initialization, prepared execution |
| WARM-PREP | Plan initialization and prepared execution; immutable mesh metadata cached |
| EXEC-ONLY | Prepared execution only; a lower bound when the matrix changes every frame |
| MAC-PREP / DMUL-PREP | Plan initialization and explicit candidate execution |
| REUSE | Matrix update, all contribution table rebuilds and execution |
| BOX8 | Bounds/alias checks, endpoint products and execution, scalar partial tail |

`sb_init` performs immutable-mesh preparation before timing. Report that startup
cost separately if mesh construction is part of the workload. REUSE must report
`reuse_plan.enabled`: upstream's count >=128 gate means the smaller cases use
prepared fallback, even though they have repeated coordinates. Availability
queries must run on the executing CPU; MAC requires SR.S clear. No silent
algorithm dispatch or saturation-mode changes are introduced.

The added box API shares 18 endpoint products rather than 72 per box. The
full-range result remains floor-once, translated, saturated Q16.16. It is
experimental: output stores, wide arithmetic, checks and setup can dominate.
It is not an animated arbitrary mesh transform, and tapered/wedge geometry
must use an ordinary vertex path.

The Q16 DIVU wrapper is explicitly selected. It preserves scalar `sh2z_div`
zero behavior, including 0/0=0, without changing the existing raw64 API.
Hardware ownership, interrupt preservation and OVFIE rules remain those in
`divu_experimental.h`. `sb_check_divu` checks 320 edge/random operand pairs and
one simulated interrupt save/use/restore sequence. It does not qualify a real
interrupt handler or physical SH7604 silicon.

A 32X frontend and MiSTer evidence are maintained with the integration package
in 32x-builder. That frontend parks CPU2 in BIOS, reuses title scratch, warms
each path eight times and measures 64 calls using the primary FRT at Phi/128.
All candidates are in the same ROM; variants change only diagnostic byte 31.
Each individual call must finish before the 16-bit timer wraps (~365 ms).
Timer-read/call overhead remains in results. Report min/max and repeat passes.
The divider page measures 32 operations per call with identical independent
integer work; DIV1, blocking Q16 DIVU, split Q16 DIVU and raw64 DIVU all check
the same hash. Split timing exposes overlap opportunity, not a guarantee.

MiSTer is an FPGA core, not a physical Saturn/32X SH7604. Do not promote these
numbers to silicon timings, whole-game FPS, or a general SH2ZAM speedup.
