# Rejected wall shade-table cache — 2026-11-16

Tried a per-invocation one-entry memo of the main wall's 16/64-byte shaded
texture column, keyed by texture-column pointer, texture height, wall shade
and detail factor. Both CPUs owned independent stack tables. A diagnostic
byte selected baseline/candidate in one binary. Four ares scene comparisons
matched a completed framebuffer outside the benchmark text.

Office MiSTer: corridor reused 11 columns, partition view reused none. The
corridor wall-pass mean was 12,495 ticks baseline versus 12,480 with the cache;
LUT-building time fell only from 740 to 715 ticks. Whole-render mean moved
from 20,033 to 20,060 ticks and delivered vblanks stayed 8.36 in both arms. That does not justify the
key checks, persistent stack state or code footprint. Candidate removed from
the engine; no normal build enables it. The patches preserve the attempted
implementation for review, not as a recommendation to apply it.

Retained the general W/L average counters and selectable resolution/scenes in
the benchmark tooling. Do not infer a whole-frame win from this cache: unrelated
audio timing can move the total even though the target pass does not improve.

Caption values above were decoded exactly from the canonical project font.
