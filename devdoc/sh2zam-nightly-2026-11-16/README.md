# SH2ZAM overnight integration results — night of November 16–17, 2026

All five requested candidates were exercised with actual project geometry and
MiSTer measurements. **No experimental default was enabled.** The known
BALANCED ROM is restored on office MiSTer:
`ac7608db4acd1c6c352a3e1662ee2677c7b0de4479ef44eb750fcf269745408f`.
Full-height/native rows and the established BALANCED walking profile remain.
Nothing was pushed or submitted upstream.

| Candidate | Result | Decision |
|---|---|---|
| Prepared MAC on real furniture | Faster isolated world-pose stage; exact engine-compatible correction passes. Complete renderer does not improve overall, including a RAM-kernel trial. | Keep opt-in; excluded from normal renderer allocation/execution. |
| Fused transform/projection | Strong isolated speedup; up to 1px X / 2px Y difference and 36 near-plane classification differences in the compatibility sweep. | No drop-in promotion. |
| Near-plane polygon clipping | Correct across real mesh crossing cases; retains faces the current path drops. Hardware projection helps, but this is extra work. | Geometry prototype retained; textured UV/fan integration is still future work. |
| Shared geometry between CPUs | Uncached sharing loses; explicit invalidation ties or loses after synchronization. Stale-cache negative control catches wrong results. | Keep duplicate computation in gameplay. |
| Larger-mesh coordinate reuse | Selected repeated chair/desk batches win after table update; mixed composite loses; dense hero exhausts key budget and falls back. Cold preparation is expensive. | Use only for an eligible future batched mesh; no blanket replacement. |

These are MiSTer FPGA-core results, **not physical SH7604/Saturn timings**.
Core: S32X_20260603, SHA256
`da7694406c4268aedeb7d5789bf20eb5502c2eff45c1fdedb009f217709b9d9a`.
Compiler: Marsdev SH GCC 15.2.0, -m2 -mb, -O2/LTO, existing release settings.
Library base: `fac62ff50699c57279a3fa27381b792ec0ef4e8e` plus the previously
packaged box/Q16-divider additions. The temporary RAM-placement edit was
reverted; the vendor MAC source matches that pinned contribution checkout.

## Evidence and method

76 selected captures: 44 math/geometry/dual-CPU captures and 32 complete-renderer
captures, each candidate sampled in two passes. All selected diagnostic FAIL
counters are zero. The deliberately stale sharing path is a negative control,
not a valid performance candidate. Exact project-font decoders reject ambiguous
captions and unfinished/zero-count renderer averages.

- [Selected native measurements](selected-native-results.json): exactly two
  captures for every page 9..30.
- [Full-renderer MAC measurements](game-mac-results.json) and
  [RAM-kernel measurements](game-mac-ram-results.json).
- [Actual measured base ROMs and symbol lists](measured-roms.zip), with
  [SHA256 identities](measured-bases.json). These are local game fixtures,
  separate from the asset-free upstream contribution patch.
- [Host oracles](final-host-oracles.log), [GCC](gcc-host-oracles.log),
  [ASan/UBSan](sanitized-host-oracles.log), and
  [existing intro/audio regressions](existing-regressions.log).
- [Exact adapter pixel checks](game-mac-pixel-checks.json).

Standalone pages park CPU2, except the explicitly labeled dual-CPU pages.
Each path warms eight calls, measures 64 calls, and checks outputs outside the
timed interval. Displayed means are floor(mean ticks); min/max remain in JSON.
FRT is Phi/128 (~179,766 ticks/s); each individual call is below its ~365ms
wrap. Call/timer overhead remains. **These call times are not game FPS.**

Full-renderer fixtures use shipping rendering, both CPUs, audio, fixed split
160, native full resolution, frozen animation input and facing 37. Both passes
compare the same linked binary, changing only diagnostic 29. Pass B reverses
arm order. Sampling is eight warmup frames plus 64 frames; G/R/C captions round
means. C covers the primary furniture pass, including rasterization; R is the
whole renderer and includes partner/bus/audio effects.

## 1. Prepared MAC: exact math still needs a pipeline win

Mesh generation uses the actual `cbox_corner`, including wedges. Counts are
chair 72, desk 24, PVM 48, current desk/PVM composite **64**, not the older
80-vertex assumption in comments. The composite scale is exactly
`26214 * 294 / 256 == 30105`, not rounded 0.46. A preliminary composite fixture
used the rounded value; it is superseded by the selected final captures.

World scale is immutable for these meshes and is cached in generated ROM
vertices; per-call matrix construction and plan qualification remain timed.
ROM and SDRAM inputs are both measured. Plain MAC floors the whole dot once;
the engine floors individual terms. A correction using four low-half products
restores the original X and Z equations exactly, including the negation outside
one product's floor. 106,496 actual-mesh vertices over every facing and both
storage modes pass independent scale/rotation oracles. The production adapter
rejects unsupported heights/bases, with the original path available as fallback.

| Mesh | Engine stage | Exact MAC / SDRAM | Exact MAC / ROM |
|---|---:|---:|---:|
| Chair | 143/143 | 108/108 | 116/116 |
| Desk | 49/49 | 42/42 | 39/39 |
| PVM | 96/96 | 74/74 | 80/80 |
| Desk + PVM | 127/127 | 95/95 | 101/101 |

Stage savings do not survive the complete pipeline. Materializing and rereading
an intermediate batch, call/qualification cost and cache effects matter. The
original transform is inside the RAM-resident renderer. Moving the library's
114-byte MAC kernel to `.ramtext` was also tested; that edit is preserved as
[ram-placement.patch](ram-placement.patch), not enabled in the vendor.

Below, each entry is pass A / B primary furniture ticks. Do not compare absolute
ROM-build and RAM-build times as a causal optimization: code placement shifts
layout and other renderer timings. Compare arms within each binary.

| Mesh | Original / ROM-kernel build | Exact MAC / ROM-kernel build | Original / RAM-kernel build | Exact MAC / RAM-kernel build |
|---|---:|---:|---:|---:|
| Chair | 1243/1242 | 1279/1278 | 1240/1235 | 1269/1271 |
| Desk | 1121/1119 | 1159/1159 | 1094/1093 | 1124/1116 |
| PVM | 1485/1488 | 1526/1526 | 1505/1502 | 1540/1543 |
| Desk + PVM | 2407/2409 | 2457/2429 | 2386/2383 | 2359/2357 |

The RAM composite has a small primary-pass improvement, but its total-renderer
R rises in both passes. No candidate has an overall renderer win worth enabling;
delivered vblank pacing does not improve. Ares comparisons at facing 37 show
zero differences in all 49,280 geometry-region pixels per fixture in a completed
framebuffer bank, for all four meshes; diagnostic rows are excluded.

Memory: generated immutable world positions total 2,496 ROM bytes. The opt-in
adapter has about 80 bytes of immutable mesh descriptors plus its ready flag,
and a 960-byte per-call pose array. Compiler stack usage for the frozen RAM
experiment reports the combined furniture frame as 4,680 bytes, dynamic/bounded;
[the report](static-stack-usage.json) comes from a byte-identical instrumented
link. This is static usage, not a measured full-game stack watermark. A runtime
write-hook attempt produced no events and is not used as safety evidence.

`FURNITURE_MAC_EXPERIMENT=0` is the normal build. It compiles out the adapter's
renderer scratch and initialization. Explicit opt-in builds carry `FURNMAC1`;
the A/B generator refuses a MAC experiment on a ROM without that capability.
Unsupported shapes/heights retain the old path. New box/wedge header changes
regenerate the cached vertices automatically.

## 2. Fused view: promising speed, different contract

Raw 8.8 model coordinates are retained as whole model ticks; all translations,
eye heights and clipping distances are rescaled to the same unit. Simply
truncating model coordinates to whole world units would be wrong. The adapter
still quantizes a composed camera basis to Q1.14 and uses the library's direct
projection convention rather than the engine's staged reciprocal/floor rules.

2,981,888 host vertex comparisons across four models, facings, camera yaw,
seven distances and standing/crouch heights show 36 near-plane disagreements,
up to 1px horizontal and 2px vertical difference. This prevents an invisible
replacement despite the isolated speed result:

| Mesh | Engine transform/project | Fused C + setup | Fused hardware + setup | Hardware execute only |
|---|---:|---:|---:|---:|
| Chair | 456/455 | 635/635 | 192/192 | 120/120 |
| Desk | 148/148 | 248/248 | 101/101 | 40/41 |
| PVM | 304/304 | 429/429 | 150/150 | 83/83 |
| Desk + PVM | 399/399 | 557/557 | 175/175 | 109/109 |

Execute-only omits view construction and is a lower bound. The old camera's
rounding and near-plane behavior need an explicit compatible API/adapter before
this becomes a gameplay replacement; there is no resolution reduction involved.

## 3. Near-plane clipping: validated geometry prototype

1,118,208 actual-mesh faces include 200,560 partial faces. All 401,120 crossing
intersections match an independent signed-rational oracle; C and explicit DIVU
results match. Clipping retains 801,360 polygon vertices that whole-face
rejection discards in this sweep. Actual planar box/wedge faces produce at most
five vertices, with every retained point on/in front of the near plane.

[Clipping visualization](clipping-diagnostic.png) shows the geometry consequence;
it deliberately omits textures, shadows and painter ordering. It is not a
shipping-game screenshot. Source polygons are [saved here](clipping-polygons.json).

| Mesh | Whole-face drop | Clip C | Clip hardware |
|---|---:|---:|---:|
| Chair | 198/198 | 767/767 | 639/639 |
| Desk | 45/45 | 712/712 | 347/347 |
| PVM | 100/100 | 1082/1082 | 584/584 |
| Desk + PVM | 131/131 | 1158/1158 | 738/738 |

This is a visual/correctness feature with extra work, not a speed optimization.
It remains a prototype: the shipping textured quad/four-triangle-center path
needs proper clipped UV interpolation, variable-vertex triangulation, winding
and painter/occlusion integration before enabling this for PVM front/rear faces.
The existing renderer is not silently switched to untextured or distorted fills.

## 4. Real dual-CPU sharing: tie or loss with correct coherence

Both real SH-2s use separate local workspaces in title scratch. Each request
changes facing, and each mode uses the **same** facing sequence. The primary
publishes geometry only after completion; the slave either recomputes it, reads
the uncached alias, or invalidates the published cache lines before reading.
Both consumer hashes and the primary's result are checked against the engine
oracle. Timings include the request/publication/acknowledgment protocol and both
consumers finishing. The idle/publish backoff is six FRT ticks, avoiding a tight
bus-hammering poll. These dedicated pages run before audio initialization;
they are a geometry-sharing test, not a full-game performance claim.

| Mesh | Duplicate compute | Shared uncached | Shared with invalidation | Stale negative control |
|---|---:|---:|---:|---:|
| Chair | 144/144 | 167/167 | 144/145 | 135/135 |
| Desk + PVM | 121/121 | 143/144 | 123/123 | 115/115 |

The stale path is faster but wrong on 71/72 calls in each run (one requested
facing matches the cached value). That negative control demonstrates why cache
freshness must be tested, not assumed. Correct sharing ties or loses here;
there is no basis to add a synchronization dependency to the gameplay path.
Earlier exploratory dual runs used different facing subsequences per mode;
[selected results](selected-native-results.json) replace them with matched-workload
captures. They are not mixed into the conclusion.

## 5. Larger actual meshes: selective reuse, expensive cold setup

The first three fixtures use real model vertices in placed grids, not identical
coincident AABBs: four chairs (288 vertices), sixteen desks (384), and four
mixed/wedge composites (256). The fourth is the complete retired 731-vertex
chair hero mesh, benchmark-only; it is not re-enabled in gameplay. Key budgets
are explicit: 128 X keys, 128 YZ keys. No allocations or automatic budget growth.
265,440 additional randomized-matrix vertex oracle checks pass.

| Mesh | Reuse enabled | MAC + plan | Reuse + matrix/table update | Cold reuse preparation + execution | Reuse execution only |
|---|---:|---:|---:|---:|---:|
| 4 chairs | 1 | 275/275 | 261/261 | 1138/1138 | 192/193 |
| 16 desks | 1 | 357/357 | 320/320 | 1698/1698 | 248/248 |
| 4 composites | 1 | 248/248 | 256/256 | 1204/1204 | 179/179 |
| 731-vertex hero | 0 | 652/652 | 957/957 | 2545/2545 | 931/931 |

Warm reuse saves roughly 5% versus MAC for the chair cluster and 10% for the
desk cluster, but loses on the composite. The dense hero exhausts its YZ budget
and correctly falls back; its “reuse” rows must not be advertised as table-path
wins. Execute-only excludes per-frame table construction; cold metadata
preparation belongs to startup for immutable meshes. The fixture workspace is
41,856 host bytes and uses title scratch only, not gameplay BSS. Binary corner
order does not select the upstream blocks8 specialization.

The first hero captures were taken before its full warmup/measurement sweep
finished and were blank; they are excluded under `insufficient-startup/`.
The selected hero captures allow 45 seconds before capture. No timing is inferred
from blank or unfinished screens. Existing gameplay caps nearby models and does
not yet build a common-matrix scene batch, so these are library opportunities,
not claimed current-game gains.

## Reproduce / review

Build and run the host checks:

```sh
make furniture-lab-test view-lab-test clip-lab-test reuse-lab-test
make box-transform-test sound-mix-test
```

Build the current diagnostic image and generate standalone pages:

```sh
make -j4 MARSDEV=/path/to/marsdev/mars TARGET=/tmp/nightly release
python3 tools/nightly_sh2zam_roms.py /tmp/nightly.32x --output-dir /tmp/nightly-roms
```

Copy generated ROMs to `/media/usb1/Games/S32X/`, MGLs to `/media/fat/`, then
run `experiments/sh2zam-nightly/mister_sweep.sh` ON MiSTer. It uses a longer
allowance for page 30 and restores `backrooms-balanced.mgl` on exit. Adjust the
restore path with RESTORE_MGL on another installation.

To inspect the real-renderer candidate explicitly:

```sh
make -j4 MARSDEV=/path/to/marsdev/mars FURNITURE_MAC_EXPERIMENT=1 TARGET=/tmp/mac-test release
python3 tools/gameplay_benchmark_ab.py /tmp/mac-test.32x --experiment furniture-mac --scene 6 --scene 7 --scene 8 --scene 9 --output-dir /tmp/mac-scenes
# Restore the normal configuration after experiments:
make -j4 MARSDEV=/path/to/marsdev/mars TARGET=/tmp/normal release
```

Current builds can differ in code/cache layout from the measured images. To
reproduce the **exact measured ROM bytes**, use the frozen archive replay tool;
it verifies every output against the recorded SHA256 before writing:

```sh
python3 experiments/sh2zam-nightly/replay_frozen.py native --output-dir /tmp/exact-native
python3 experiments/sh2zam-nightly/replay_frozen.py view --output-dir /tmp/exact-view
python3 experiments/sh2zam-nightly/replay_frozen.py dual --output-dir /tmp/exact-dual
python3 experiments/sh2zam-nightly/replay_frozen.py game --output-dir /tmp/exact-game
python3 experiments/sh2zam-nightly/replay_frozen.py game-ram --output-dir /tmp/exact-game-ram
```

Provisional composite results and the unmatched exploratory sharing timings are
superseded. The selected JSONs and replay profiles deliberately exclude them.
Library performance observations remain specific to these fixtures and this
MiSTer core; no new overall FPS, frame-pacing or physical-silicon claim is made.
