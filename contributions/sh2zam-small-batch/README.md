# SH2ZAM contribution package

Exact box transforms, explicit Q16 DIVU helpers, and a reproducible small-batch
benchmark. Local feature work based on SH2ZAM
`fac62ff50699c57279a3fa27381b792ec0ef4e8e` (2026-11-16 checkout).
Nothing has been pushed or submitted upstream.

[SH2ZAM.patch](SH2ZAM.patch) applies cleanly to that exact upstream commit.
It contains all new library code, host tests, portable fixtures and CMake
registration. Apply from an upstream checkout with `git apply SH2ZAM.patch`.
Keep the upstream MIT LICENSE and NOTICE.md. The vendored source in
`third_party/SH2ZAM` is synchronized with this patch.

Changes:

- `sh2z_mat34_transform_box8`: binary X/Y/Z corner order, 18 shared endpoint
  products where safely bounded, exact full-range fallback. Floor once, add
  translation, saturate; documented null/bounds/overlap failure behavior.
- `sh2z_divu_start16` and `sh2z_divu_div16`: explicit opt-in wrappers over
  upstream's existing DIVU API, preserving scalar Q16 zero behavior including
  0/0=0. Existing raw64 semantics and default scalar dispatch stay intact.
- 160,000 box-corner checks against an independent wide oracle, plus 100,064
  Q16 division checks, active in Release builds.
- Portable 8/24/28/48/72/128/256-vertex fixtures covering general/batch,
  cold/warm/execute-only preparation, MAC, DMUL, coordinate reuse and box8.
  Timed per-matrix setup is explicit; availability/fallback is reported.

Validation: all 25 upstream tests pass with GCC 15 Release, Clang Release and
Clang Debug ASan/UBSan. The complete library cross-compiles with strict warnings
using SH GCC 15.2.0 (-m2 -mb); the selected kernels link and boot in the 32X ROM.
This is a Marsdev 32X link, not a Yaul/Saturn image qualification. The existing
120-frame intro oracle and 2,048 audio fixtures also pass. Logs/compiler IDs
are in [validation/](validation/). The recorded toolchain file contains local
paths; replace those with your SH compiler path when reproducing.

Two complete MiSTer passes, 16 captures: zero correctness failures.
[Measurements, caveats and captures](../../devdoc/sh2zam-contribution-2026-11-16/README.md).
Prepared MAC is promising for immutable furniture-sized meshes. The new box
kernel loses beyond eight vertices, so it remains experimental. Split DIVU
shows a workload-specific reduction. None of these microbenchmarks establishes
whole-game FPS or physical SH7604 timing. No new kernel was promoted to gameplay;
full-height BALANCED rendering and the previously measured gameplay paths remain.

Reproduce the integration benchmark from 32x-builder:

```sh
make -j4 MARSDEV=/path/to/marsdev/mars TARGET=/tmp/sh2zam-lab release
python3 tools/sh2zam_benchmark_roms.py /tmp/sh2zam-lab.32x --output-dir /tmp/sh2zam-roms
```

The generator refuses overwrites and emits eight layout-identical ROMs, MGLs
and SHA256 manifest. Put ROMs on `/media/usb1/Games/S32X/`, MGLs on `/media/fat/`.
[mister-sweep.sh](mister-sweep.sh) records two passes and restores the existing
`backrooms-balanced.mgl`; adjust these paths for a different installation.
The frontend is `sh_src/sh2zam_bench.c`, gated by diagnostic 31 before CPU2 is
released. It reuses title scratch with a compile-time capacity check, has no
normal-game execution cost, and labels timings as calls rather than FPS.
The build itself can change code/cache layout, so gameplay performance would
need a fresh same-binary comparison before any future kernel promotion.

The new kernels are selected only by diagnostic 31. No hidden allocation,
cache/DMA manipulation, divider dispatch, approximation or image-quality
reduction is added. Actual ISR/silicon qualification remains future target
validation rather than being implied by portable tests or MiSTer results.
