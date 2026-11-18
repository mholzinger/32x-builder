# SH2ZAM small-batch / DIVU measurements — 2026-11-16

Measured on office MiSTer, S32X_20260603 FPGA core. This validates that core;
it is not physical SH7604/Saturn qualification or whole-game FPS evidence.
Core SHA256: `da7694406c4268aedeb7d5789bf20eb5502c2eff45c1fdedb009f217709b9d9a`.
Upstream base: `fac62ff50699c57279a3fa27381b792ec0ef4e8e`.
Normal source ROM SHA256: `6d93fd7be5428d78fb564977fae33a354319be613b3a57a5c57ab0892f9d0384`.
Per-page ROM hashes are in [rom-manifest.json](rom-manifest.json); each differs
from that source only at diagnostic byte 31. Release SH GCC 15.2.0, -O2,
LTO, -m2 -mb, matching this project's release flags. No per-candidate relink.

CPU2 stays in its BIOS wait; primary timer is Phi/128 (~179,766 ticks/s).
Eight warmups and 64 measured calls per path; output/hash comparisons happen
outside timing. Timer and call overhead are retained. Each call is well below
the 16-bit timer's ~365 ms wrap. Two complete boot/capture passes. MAC was
available (SR.S clear) on every batch page. All 16 captures report zero failures.

Numbers below are floor(mean ticks), pass A / pass B. They are not per-vertex
cycles. See [results.json](results.json) for decoded min/max and each capture.

| Vertices | General | Batch | Cold prep | Warm prep | Execute only | MAC + prep | DMUL + prep | Reuse + update | Box8 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 28/27 | 22/22 | 25/25 | 18/18 | 12/12 | 14/14 | 20/20 | 27/27 | 21/21 |
| 24 | 83/83 | 59/59 | 53/53 | 42/42 | 30/30 | 31/31 | 45/45 | 53/53 | 62/62 |
| 28 | 97/97 | 69/69 | 60/60 | 48/48 | 35/35 | 35/35 | 51/51 | 58/58 | 80/80 |
| 48 | 170/170 | 121/121 | 96/96 | 77/77 | 63/63 | 52/52 | 78/78 | 85/85 | 126/126 |
| 72 | 257/257 | 184/184 | 134/135 | 109/109 | 93/93 | 75/75 | 107/107 | 124/124 | 190/190 |
| 128 | 466/466 | 329/329 | 230/230 | 185/185 | 170/170 | 134/134 | 182/182 | 133/133 | 345/345 |
| 256 | 931/931 | 652/652 | 436/436 | 349/349 | 332/332 | 247/247 | 342/342 | 210/210 | 676/676 |

| Divider workload (32 ops + identical integer work) | Pass A / B ticks |
|---|---:|
| DIV1 | 30/30 |
| DIVU16 | 25/25 |
| SPLIT16 | 21/22 |
| DIVU64 | 24/24 |

The fixture is repeated synthetic AABBs, not copied chair/desk assets. Counts
match relevant mesh sizes, but do not reproduce camera conversion, wedge
geometry, clipping, rasterization, audio or normal bus contention. Coordinate
reuse is disabled below 128 vertices by upstream's gate; at 128/256 this very
repetitive fixture enables it. Binary corner order does not enable blocks8.

MAC + per-matrix preparation is the useful small-batch candidate here. It
requires immutable mesh metadata and the documented MACH/MACL ISR contract.
This does not overturn the earlier actual animated-intro result, where the
original engine transform won. Execute-only excludes setup and is explicitly
a lower bound for changing matrices. Cold preparation includes a position scan.

The exact box kernel reduces products but loses to ordinary batch for 24+
vertices and to MAC throughout; it remains experimental, with no gameplay
promotion. At eight vertices its tiny advantage over batch is one displayed
tick; that is not a general speed claim. REUSE includes table rebuilds each
call, but immutable mesh/key preparation belongs to startup and is excluded.

Explicit split Q16 DIVU takes 21–22 ticks versus scalar DIV1's 30 for this
32-op workload. Hardware tests also compare 320 edge/random operand pairs,
blocking/split results and one save/use/restore sequence. This is not an
asynchronous interrupt test. No default scalar dispatch or divider ownership
policy changed. Existing measured gameplay divider code stays in place.

Decode captures with:

```sh
python3 tools/sh2zam_benchmark_results.py devdoc/sh2zam-contribution-2026-11-16/*-final.png
```

The decoder requires exact project-font matches and rejects nonzero failures.
The `superseded-captions/` pictures came from an earlier label layout and are
excluded from final results. The ordinary BALANCED game was restored after the
sweep (SHA256 `ac7608db4acd1c6c352a3e1662ee2677c7b0de4479ef44eb750fcf269745408f`).
