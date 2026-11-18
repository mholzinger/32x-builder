> 2026-11-16 follow-up: the vendor now tracks `fac62ff` plus local
> experimental box/Q16-divider additions. See
> [the contribution package](../contributions/sh2zam-small-batch/README.md)
> for the patch, small-batch measurements and updated provenance. The pilot
> measurements below remain historical results for their original binaries.

# SH2ZAM box-transform experiment

Pinned upstream: `3ddb84ce2e4537395b76ad28efabe5845924fb41`.
See `third_party/SH2ZAM/UPSTREAM.md` for provenance and license notices.

The default remains the original camera transform. Diagnostic byte 25 selects
the original path (0), SH2ZAM's ordinary prepared batch (1), experimental
native MAC batch (2), or experimental native DMULS batch (3). Both paths live
in the same ROM, so switching does not relink or change code/data placement.
The native kernels are explicit diagnostic candidates and preserve upstream
bounds/SR.S qualification and fallback. Renderer division, clipping, projection,
shading, CPU scheduling and framebuffer algorithms remain unchanged.

## Build and create the pair

```
make box-transform-test MARSDEV=/path/to/marsdev
make release MARSDEV=/path/to/marsdev TARGET=/tmp/sh2zam-backrooms
python3 tools/box_transform_ab.py /tmp/sh2zam-backrooms.32x --output-dir /tmp/box-ab
```

The output directory must not already contain those two ROM names. The tool
requires a unique DIAGCFG1 marker, enables metrics (byte 10) in both arms,
and selects byte 25. The pair differs at exactly byte 25; other diagnostic
settings are inherited from the input. Use an unpatched release build initially.
Do not publish either diagnostic variant as a release.

For a repeatable hardware capture, diagnostic byte 26 accepts 1..120 to
repeat baked pose 0..119 indefinitely in the intro loop. The pair generator
accepts `--pose 45 --include-native` to generate all four matching variants. Leave it zero for
normal playback. START exits the loop. The first frame still loads the box
palette; subsequent frames reuse it. Use the same byte 26 in both arms.

## Implementation and numeric contract

`sh_src/box_transform.c` is the production adapter, also compiled by the host
harness. It copies camera-relative positions into caller-owned typed storage,
builds a mesh view and transform plan, executes, then negates the back-axis
result. Camera subtraction and depth negation stay on the same sides of the
dot product as the original code. Scratch/output storage belongs to the primary
and occupies 672 bytes in the box's existing low overlay; it is not shared with
the secondary. The box's morph changes each frame, so bounds and qualification
are rebuilt each frame and included in timing. This deliberately tests the real
28-vertex batch, including its overhead, rather than a synthetic large mesh.

The adapter is scoped to the bounded baked box poses: coordinate subtraction
and negated depth must fit int32_t. It is not a full-range public camera API.

SH2ZAM floors once per row and saturates; the original floors each product
separately. Expect small differences, not bit-identical geometry. The host
harness independently accumulates the baked products and checks exact prepared
results, the rounding-difference bound and near-plane classifications.
It does not establish pixel identity after clipping and rasterization.

The Makefile compiles only the needed scalar, trig, transform, prepared and experimental native-batch
translation units as C11 with `-m2 -mb` and `SH2Z_REQUIRE_SH2=1`. Existing engine
translation units retain C99. The rest of the unmodified vendored core is
available for later experiments. Runtime helpers use this project's libgcc and
libmem. Existing interrupts must continue preserving MACH/MACL when used;
the secondary DMA C-handler wrapper already saves both. The current experiment
executes on the primary and adds no interrupt handlers or cross-CPU protocol.

## Measurement

The existing box overlay now includes `X:xxxxx B:n P:nnnnn`: X is elapsed camera-transform
work in FRT ticks at Phi/128 (~5.56 microseconds/tick); B identifies the requested
path and P is the baked pose index for matching captures. Press MODE during
the intro to toggle metrics if necessary. X includes subtraction, bounds/plan setup, output conversion and world
vertex copying. If preparation fails the old transform is used. Other overlay
rows retain total primary/secondary render times and vblanks/FPS.

Capture both arms from a fresh boot on the same MiSTer setup, compare matching
box-animation poses, and repeat. Record X alongside total M/S and vblank counts.
The first frames include cold-cache effects; compare later poses as well. The
28-vertex transform may be too small for preparation to pay off. Keep a slower
result and keep the original default unless whole-frame hardware results justify
changing it. Additional HUD work affects both arms; do not compare this build's
FPS directly with an older build that lacks the new overlay row.

Use ares for execution and visual checks. Its timing is not evidence for the
32X's cache/bus behavior. Do not extrapolate upstream instruction-model speedups
to game FPS. This experiment optimizes the intro/fall box, not gameplay walls.

## Validation (2026-11-16)

- Marsdev SH-2 GCC 15.2.0 release build linked successfully.
- All 120 baked poses (3,360 vertices): exact independent prepared-dot oracle.
- 1,552 coordinate components differ from the old path; maximum difference
  1/65536; no near-plane classification changes.
- Host AddressSanitizer/UndefinedBehaviorSanitizer run passed.
- Both paths executed through the intro/fall into the lobby in ares; sampled
  images inspected. Matching emulator frame numbers can show different poses,
  so use the P field for comparisons. This is a smoke check, not pixel identity.
- Office MiSTer fixed-pose measurements completed: original 63–65 ticks;
  prepared 145–146; native MAC 129; native DMULS 141–142. The old box FPS overlay read the wrong half of
  COMM12, so its frame-rate conclusion is withdrawn; the X timings are valid.
  Counter fixed for future builds. Keep original default. See
  [captures and result limits](sh2zam-hardware-2026-11-16/README.md).
