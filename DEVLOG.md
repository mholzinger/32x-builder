# 32X Builder — Development Log

A running record of what we've learned while building a Sega 32X
Backrooms-themed first-person raycaster from scratch. The project is a
two-person collaboration: a programmer with ~20 years of experience
driving direction, picking optimization targets, and reading the hardware
behavior; Claude (the LLM) doing the per-step research, writing the
code, and reasoning through the dual-CPU + cache-coherency models.

This file is the source material for a future article on what it's
actually like to do retro-platform programming with an LLM as your
implementation partner. Each entry is meant to be self-contained — you
should be able to drop any section into a blog post without further
context.

## Format

Each entry has:

- **Tag** — `HW-TRUTH` (a hardware behavior we discovered that isn't in
  the obvious documentation), `OPTIMIZATION` (a perf win), `BUG`
  (a tricky bug + its diagnosis), `ARCHITECTURE` (a design pattern
  we settled on), `COLLAB` (a process / workflow learning).
- **Context** — what we were trying to do.
- **Outcome** — what happened, with concrete numbers where possible.
- **Insight** — the takeaway worth citing.
- **Files** — code references to the canonical location of the pattern.
- **Excerpt** — short code snippet when it makes the point.

Append new entries to the bottom. Don't edit old ones — if a finding
turns out to be wrong, write a new entry that corrects it.

---

## 2026-06 — HW-TRUTH: The marsdev slave SH-2 is broken at boot

**Context.** First attempt at the dual-CPU split. The slave was sitting
in `for (;;);` and we wanted to give it real work. Standard procedure
was to write a command code into a COMM register, have the slave's
`s_main()` poll for it, execute, ACK. None of that worked — the slave
never responded to anything.

**Outcome.** The marsdev skeleton's `mars_start.s` crt0 *intends* to
release the slave from its boot wait by writing 0 to the slave's status
register (`MARS_SYS_COMM4` at `0x20004024`). The code at lines 271-273:

```asm
mov.l   _master_do_init,r0
jsr     @r0
nop
! let Slave SH2 run
mov     #0,r1
mov.l   r1,@(4,r0)  /* clear slave status */
```

After the `jsr @r0` (call into `__INIT_SECTION__`), `r0` is *whatever
that function left in it* — which on the 32X with an empty init section
is the address of the init section itself, in cart ROM. So
`mov.l r1,@(4,r0)` writes 0 to **cart ROM**, which is silently dropped
on real hardware. The slave never sees COMM4 change from the "S_OK"
boot-handshake value (`0x535F4F4B`) and loops forever in its wait.

The diagnostic that proved it: a two-square debug overlay on the
framebuffer, one square driven by an MMIO-incremented counter
(`MARS_SYS_COMM4++` from slave), one driven by an uncached-SDRAM
counter (`SLAVE_HEARTBEAT++` from slave via the cache-through alias).
Before the fix both were red (slave never running). After explicitly
clearing COMM4 from `m_main()` before initialization, both went green
in one shot — proof that (a) the slave boots, and (b) the cache-through
SDRAM trick works on this hardware.

**Insight.** When porting from a third-party SH-2 skeleton, do not
trust that the slave actually starts. The visible symptom is "rendering
works fine because the master never needed the slave" — masking the
real problem until you go to add slave work.

**Files.** `sh_src/m_main.c:18-27` (the workaround); `sh_src/mars_start.s:271-273` (the bug).

**Excerpt** (the fix, with comment that documents the trap):

```c
int m_main(void) {
    /* Release the slave SH-2. The crt0 (mars_start.s:271-273) intends
     * to do this after the init JSR but uses a stale r0 — the write
     * to "clear slave status" goes to ROM and is silently dropped.
     * Without this, the slave loops forever in its S_OK wait at
     * 0x20004024 (= MARS_SYS_COMM4) and never reaches s_main(). */
    MARS_SYS_COMM4 = 0;
    Hw32xInit(MARS_VDP_MODE_256, 0);
    ...
}
```

---

## 2026-06 — HW-TRUTH: COMM register partition is load-bearing

**Context.** Second attempt at the dual-CPU split. We had the slave
running but couldn't get a clean signaling channel between master and
slave. The first try used `MARS_SYS_COMM0` as the doorbell ("master
writes a command, slave polls and ACKs"). Visually the game rendered
correctly, but the controller stopped responding completely.

**Outcome.** The 32X COMM register space `0x20004020-0x2000402E` is
partitioned by convention:

| Register | Purpose | Don't touch from |
|---|---|---|
| `COMM0`, `COMM2` | 68K → Master SH-2 services (joypad polling, VDP commands, save RAM) | Slave |
| `COMM4`, `COMM6` | Master → Slave doorbell + argument word | 68K |
| `COMM8` | Controller 1 current state (written by 68K, read by master) | Anyone but the 68K |
| `COMM10` | Controller 2 current state | Same |
| `COMM12` | VBlank counter (timer) | Same |

We were polling and clearing `COMM0` from the slave, which raced with
the 68K's controller-poll requests that go through the same register
(see `sh_src/mars.c:177` for `HwMdReadPad`). The 68K writes
`COMM0 = 0x0300|port` to ask for a joypad read; the master clears it
when the read completes; the master then reads `COMM8` for the result.
When the slave was *also* writing 0 to COMM0 as an ACK, it was
clobbering the 68K's outgoing controller-poll request before the master
could see it. `Mars_GetPad()` returned the same value forever, so the
player appeared frozen.

The d32xr convention from `viciious/d32xr@master::marsnew.c::Mars_Secondary`
is: COMM4 = master-to-slave command code, COMM6 = master-to-slave
argument word. We switched to COMM4 and the controller worked
immediately.

**Insight.** The 32X is a four-CPU system if you count the Z80
(M68K + Master SH-2 + Slave SH-2 + Z80). Two CPUs sharing a register
file means the register partition is part of the ABI, not optional.
Document it in any shared header.

**Files.** `sh_src/shared.h:23-26` (our codified partition);
`sh_src/mars.c:177-181` (the 68K-to-master pattern); `sh_src/s_main.c`
(slave-side polling on COMM4 only).

---

## 2026-06 — HW-TRUTH: The `| 0x20000000` cache-through alias is real

**Context.** Once the boot path and COMM4 channel worked, we needed
shared state between master and slave. The d32xr docs hint at a "cache-
through" SDRAM alias — bitwise-OR any cached SDRAM address with
`0x20000000` and you get an uncached pointer to the same physical
bytes. We weren't sure if this was a real hardware feature or a
software convention, and the SH-2 architecture manual is ambiguous.

**Outcome.** It's a real bus-level feature on the 32X SDRAM controller.
We proved it with the same two-square debug overlay: a shared
heartbeat counter at SDRAM address `0x060010b0` (cached alias). The
slave writes via `((shared_t *)(0x060010b0 | 0x20000000))->slave_heartbeat++`,
i.e. through `0x260010b0`. The master reads through the same alias.
If the alias is real, the master sees the counter advancing every
frame. It does.

The C-side macro pattern we settled on:

```c
extern shared_t shared;
#define SHARED_UC ((shared_t *)((uintptr_t)&shared | 0x20000000))
```

Every read or write from either CPU to anything in `shared` goes
through `SHARED_UC`. The cached symbol `shared` itself is only used
for `&shared` to compute the address — never dereferenced directly.

**Insight.** This is the foundational trick for any dual-SH-2
32X work. There is no other practical inter-CPU shared-state mechanism
on this platform — explicit cache invalidates work but cost a load per
line; the cache-through alias is one extra OR in the address.

**Files.** `sh_src/shared.h` (the macro);
`sh_src/raycast.c` (consumers).

---

## 2026-06 — HW-TRUTH: SH-2 cache is write-through, not write-back

**Context.** Working through the d32xr findings the agent surfaced.
Documentation across the web is inconsistent on whether the SH-2 cache
is write-through or write-back. The implication is huge: if it's
write-back, every cross-CPU shared write needs an explicit cache flush
before the other CPU can read it; if it's write-through, only *reads*
need invalidation (so the reader gets fresh data instead of its own
stale cache line).

**Outcome.** Write-through, confirmed by the cache-through-alias
experiment above. Master writes through the cache-through alias; the
write commits to SDRAM immediately; slave reads through the same alias
and sees the new value, with no explicit flush call between them.

The corollary that simplifies everything: if a shared struct is
**only written by one CPU**, the writer doesn't need to do anything
special — its writes commit to SDRAM as part of normal store activity.
Only the reader needs to bypass its cache (via the alias) to see fresh
values. For our project, that's exactly the master-writes-player /
slave-reads-player pattern: master writes through `SHARED_UC`, slave
reads through `SHARED_UC`, and we never call any cache-flush macro.

**Insight.** The mental model "cache flushes are required around all
shared writes" is wrong on SH-2. Flushes are for the *reader* who has
stale cache, not the writer.

**Files.** `sh_src/sh2_asm.h` (where `Mars_ClearCacheLine` lives, for
the cases where the *reader* needs explicit invalidation).

---

## 2026-06 — OPTIMIZATION: SH-2 hardware DIVU at 0xFFFFFF00

**Context.** Our raycaster's wall pass computes `lineHeight =
(SCREEN_H << 16) / perpDist` once per visible column (up to 320 of
them) plus `tex_step = (TEX_H << 16) / lineHeight`. GCC's SH-2 backend
doesn't have a 32-bit divide instruction available, so it emits a call
to a libgcc software routine — ~50-100 cycles per divide. Two divides
per column × 320 columns = up to ~64,000 cycles per frame in software
divide alone.

**Outcome.** The SH-2 has a memory-mapped hardware Divide Unit (DIVU)
at `0xFFFFFF00`. The protocol:

1. Write the divisor to `base + 0`
2. Write the dividend's high 32 bits to `base + 16`
3. Write the dividend's low 32 bits to `base + 20` — this *starts* the
   divide
4. ~39 cycles later, read the quotient from `base + 20` — the CPU
   stalls automatically if the divide is still in progress

The "39 cycles" is where it gets interesting: those 39 cycles run in
parallel with whatever else the CPU does. We adopted d32xr's
latency-hiding pattern from `r_phase6.c`: start the divide, do
unrelated column-setup work (wall shading, texture coordinate, hit
position), then read the result. The 39-cycle stall disappears under
~40 cycles of useful arithmetic.

The address-materialization trick from the same d32xr code (saves a
4-byte literal pool entry):

```c
__asm__ __volatile__ (
    "mov #-128, r1\n\t"         /* r1 = 0xFFFFFF80 */
    "add r1, r1\n\t"            /* r1 = 0xFFFFFF00 */
    "mov.l %0, @(0, r1)\n\t"
    ...
```

**Insight.** The SH-2 on the 32X is a low-clock CPU (23 MHz) doing a
lot of fixed-point math. Anything you can move off `libgcc`'s software
routines onto the on-chip peripherals is a step-change, not a marginal
improvement. The DIVU is the easiest such win — it's a single
peripheral, ~50 lines of inline asm to wrap, and it composes with
every existing arithmetic-heavy code path.

**Files.** `sh_src/sh2_asm.h:30-65` (the `divu_u32` blocking helper +
`divu_start_u32` / `divu_read` split form); `sh_src/raycast.c` (uses
in wall + ceiling + carpet passes).

**Excerpt** (the split form for latency hiding):

```c
divu_start_u32((uint32_t)(SCREEN_H << FX_SHIFT), (uint32_t)perpDist);

/* ~40 cycles of useful work that doesn't depend on the divide... */
int wall_shade = ...;
fx_t wall_hit  = ...;
int texX       = ...;

int lineHeight = (int)divu_read();  /* result is ready */
```

---

## 2026-06 — OPTIMIZATION: Quarter-wave sine LUT with quadrant folding

**Context.** The original sine table was 256 entries × 4 bytes
(int32_t in 16.16 fixed) = 1024 bytes, spanning 64 cache lines. The
SH-2's L1 cache is **4 KB total**, 4-way set-associative, 16-byte
lines. The sine table alone was 25% of L1, fighting for residency
with code, the row_color shading table, the camera-X column LUT, and
the level data.

**Outcome.** Stored just the first quadrant (sin in [0, ~1]) as
`uint16_t[64]` = 128 bytes, 8 cache lines. The full sine is
reconstructed by quadrant folding (pattern lifted from d32xr's
`tables.c::finesine`):

```c
static inline int32_t sin_fx(uint8_t a) {
    uint8_t q = (uint8_t)(a >> 6);          /* 0..3 = quadrant */
    if (q & 1) a = (uint8_t)~a;              /* mirror Q1, Q3 */
    int32_t res = (int32_t)quarter_sin[a & 63];
    return (q & 2) ? -res : res;             /* negate Q2, Q3 */
}
```

Precision tradeoff: `sin(pi/2)` is supposed to be exactly 65536 in
16.16, but a 64-entry quarter table only samples up to entry 63
(=65516). 20-ULP error at the peak, invisible at the precision we use
the result for. Acceptable.

**Insight.** When you can't make the cache bigger, make the data
smaller. The cache-pressure-per-byte calculation is more important
than the per-call cycle cost — the function is 2-3× slower per call
than a single-array-load, but called only ~15 times/frame total
(player update, sprite projection, wall setup, head bob), so the
~90 cycles of additional work are nothing compared to freeing 56
cache lines of L1 for the hot paths.

**Files.** `sh_src/sin_table.h` (the new table + macros).

---

## 2026-06 — BUG: `-flto` hoists volatile-only-through-deref reads

**Context.** After splitting ceiling-grid rendering onto the slave,
walking around showed a mysterious symptom: the ceiling grid rotated
correctly when the player turned, but did not translate when the
player walked. The wall pass and carpet pass — using the same
`SHARED_UC->player.x`/`.y` reads — animated correctly.

**Outcome.** The slave's `raycast_draw_ceiling_grid()` was being
inlined into the dispatch loop in `s_main.c` by `-flto`. After
inlining, GCC saw that nothing in the slave's view *writes* to
`shared.player.x`/`y` — the writes are in `raycast_render()` (master
side, different translation unit). LTO hoisted the loads of
`SHARED_UC->player.x`/`.y` out of the `for(;;)` dispatch loop and
cached the initial-frame values in registers forever.

The fix was a one-character change to the struct definition:

```c
typedef struct {
    volatile int32_t  x, y;     /* FX 16.16 world position */
    volatile uint16_t angle;
    uint16_t _pad;
} player_snap_t;
```

With the fields `volatile`, every read through the dereferenced
`SHARED_UC->player.x` is a forced memory access — LTO can't hoist
it out of the inlined function. Walking immediately animated the
ceiling grid correctly.

**Insight.** `volatile` on the *pointer* (`volatile shared_t *`) is
not enough when GCC has visibility into the struct definition and
can prove no other code path writes to the field. The volatility has
to be on the *field* to defeat LTO's analysis. This is a sharp edge
of LTO that doesn't show up without inter-translation-unit inlining
+ data-flow analysis — both of which `-flto` enables.

Also: the bug only manifested on the slave-side reads. The carpet pass
on the slave used the same struct but the carpet function was
*larger* than the ceiling function and got inlined less aggressively,
so its reads weren't hoisted. This is why "carpet animates, ceiling
doesn't" — the same root cause hit one path and not the other based
on optimizer heuristics.

**Files.** `sh_src/shared.h:30-37` (the volatile fields with the
explanation comment).

---

## 2026-06 — BUG: 68K↔SH-2 bridge has a bandwidth limit

**Context.** Twice during the optimization push, the controller
started "stalling" — walking forward would intermittently stop
mid-corridor, requiring the player to release and re-press the
direction button to resume. The symptom was infuriating because the
rendering still ran at the same framerate; only the joypad reads
were affected.

**Outcome.** The 68K updates `MARS_SYS_COMM8` (controller 1 state) by
processing a command request that arrives via `COMM0`. The bridge
that carries these MMIO writes between the 68K bus and the SH-2 bus
has a finite throughput. When the slave's polling loop on `COMM4`
runs at ~3M reads/sec (a tight `while (cmd == NONE)` with no delay),
the bridge spends all its time serving slave reads and occasionally
drops or delays a 68K-to-COMM8 update. The master then reads stale
COMM8 in `player_update`, sees no button pressed, doesn't move the
player. On the next frame, fresh poll = fresh COMM8 = player moves
again — *unless* the player is still holding the same direction, in
which case the kernel doesn't generate a new "press" event and the
state stays "released" until the player physically lifts and
re-presses.

The fix is mechanical: throttle the polling. The slave's idle COMM4
poll now has a `for (volatile int i = 0; i < 256; i++);` busy-wait
between checks, dropping the rate to ~30K/sec. Symmetrically, the
master's two ACK-wait points in `raycast_render` (`while (COMM4 !=
0);` between CMD_CEILING and CMD_WALLS, and after CMD_WALLS before
sprites) got 16 NOPs per iteration to drop their polling rate from
~5M/sec to ~700K/sec.

The same effect re-appeared briefly when we tried work-stealing wall
splitting via TAS-byte locks — each column claim was ~6 atomic bus
operations and at 320 columns × 60fps = 115K atomic operations/sec,
which by itself was enough to starve the bridge. We reverted that
change.

**Insight.** On any system with a CPU-to-CPU bridge, treat MMIO bus
bandwidth as a budgeted resource. The fastest CPU isn't allowed to
use more than its share — even when polling for things, throttle the
poll rate. Documenting the 68K↔SH-2 bridge as a "shared serialized
channel" with a back-of-envelope cycle budget would have saved hours.

**Files.** `sh_src/s_main.c:10-22` (slave-side throttle);
`sh_src/raycast.c` (master-side throttle on both ACK waits).

---

## 2026-06 — OPTIMIZATION: Vertical head-bob via line-table re-mapping

**Context.** Wanted the iconic Backrooms "walking, slightly bobbing,
feels like the floor is alive" effect. The obvious implementation —
recomputing the player position with a vertical-bob offset and
re-rendering all walls/floor/ceiling — costs a frame's worth of
extra work per frame.

**Outcome.** The 32X displays through a 224-entry line table at the
start of the framebuffer (`0x24000000` to `0x240001FF`). Each entry
is a word pointing to where the pixels for that displayed row should
be fetched from. By rewriting the table every frame to map
`displayed_row[i] → source_row[i + bob_y]` for a small `bob_y`, the
entire image shifts vertically by `bob_y` pixels with **no re-render**.
Cost: 224 word writes ≈ 0.05ms.

```c
volatile uint16_t *line_table = &MARS_FRAMEBUFFER;
for (int i = 0; i < SCREEN_H; i++) {
    int src = i + bob_y;
    if (src < 0)         src = 0;
    if (src >= SCREEN_H) src = SCREEN_H - 1;
    line_table[i] = (uint16_t)(src * 160 + 0x100);
}
```

The 1-3 row duplication at the bob boundary is invisible against the
smooth ceiling/floor gradient.

**Insight.** When the display hardware has a programmable mapping
between rendered rows and displayed rows, any effect you can express
as "shift the whole image around" is free. Head bob, screen shake,
flash transitions — all of them are pure line-table tricks. Don't
re-render the scene to add motion you can fake at display time.

**Files.** `sh_src/raycast.c` (the head-bob block at the bottom of
`raycast_render()`).

---

## 2026-06 — ARCHITECTURE: Hand-tuned 32×32 map after three attempts

**Context.** Wanted a level that "feels" like the Backrooms — the
specific dream-logic spatial arrangement of an actually-generated
infinite space. Tried three sources before settling.

**Outcome.** A hand-designed 32×32 map with five distinct zones meeting
at a central spawn corridor:

- **NW (rows 1-8, cols 1-13)** — office cubicles. Regular 4-cell grid
  with irregular doorways.
- **NE (rows 1-8, cols 17-30)** — nested rooms. Three concentric
  rectangles, each with one doorway out. The iconic "doorway through
  a doorway" Backrooms shot.
- **Central (rows 10-15)** — open band with pillar islands. A pause
  zone between the four corners.
- **SW (rows 17-30, cols 1-14)** — twisty maze. Irregular partial
  walls forming dead-end choices.
- **SE (rows 17-30, cols 17-30)** — open lounge with random pillars
  + two "stub walls that make no sense" (the uncanny "why is this
  here" Backrooms vibe).

Generated by `tools/gen_backrooms_map.py` — a Python script that
carves zones and adds detail. Total map data: 1024 bytes (one
`uint8_t[32][32]`).

The three rejected alternatives:

1. **Original Sketchfab lobby (22×22)** — felt like "one big room
   with no false walls" per the playtester. Too few interior
   partitions.
2. **movie.blend extraction at 32×32** — the model is 82 world units
   wide × 21 tall, so 32×32 cells gave 2.56-unit-wide cells in X.
   Doorways narrower than 2.56 world units got swallowed and the
   level became "one isolated corridor in any direction."
3. **movie.blend at 64×64** — preserved doorways but each cell was
   only 0.33 world units in Y, so the corridor "feels infinitely
   long" and there's nowhere meaningful to go.

**Insight.** For a small map with strong-character distinct zones, a
hand-tuned approach beats both photorealistic extraction and
procedural generation. The cells in a raycaster map ARE the design
elements — each cell wall has a strong perceptual weight in the
rendered image. 1024 bytes of design intent is more impactful than
4096 bytes of extracted geometry that loses doorways at the
resolution we can afford.

**Files.** `tools/gen_backrooms_map.py` (the generator);
`sh_src/raycast.c::world_map` (the resulting 32×32 array).

---

## 2026-06 — BUG: Static perspective bands faked the ceiling grid

**Context.** Before the per-row band fallback (described separately
below), the ceiling looked like it had a grid of horizontal bands.
We assumed the bands were world-Y grid lines drawn by the raycaster.
They weren't.

**Outcome.** They were a pre-rendered perspective-compression effect
in the `row_color[]` lookup. The original `build_shading_tables()`
darkened every `CEIL_GRID_DENSITY`-th screen-row by a small amount
(`shade += 2`), creating visible bands on the displayed ceiling. The
bands were tied to **screen row**, not world position, so they were
visually convincing as long as the camera was static — but they
didn't slide toward the player when walking, didn't shift left/right
when strafing, didn't do anything when the player turned. They were
a still picture pretending to be a grid.

The discovery happened because the player observed that "the ceiling
grid stays as a static grid only moving to show player direction." At
that point the actual grid (the dynamic raycast pass) was running but
producing nothing visible when the player faced cardinal directions
(see below). What the player saw was just the static perspective
shading.

**Insight.** It's easy to ship a static representation of a thing and
mistake it for the dynamic version, especially when the static
version is more visually present. When debugging "this isn't
animating," check whether the thing you're looking at is actually the
thing you think it is, or just a related cached artifact.

**Files.** `sh_src/raycast.c::build_shading_tables` (now stripped of
the ring-drawing code).

---

## 2026-06 — BUG: Ceiling raycaster needs per-row band fallback

**Context.** Once we knew the dynamic ceiling-grid pass was producing
zero output when the player faced cardinal directions, we had to
figure out why.

**Outcome.** The ceiling grid is computed in two passes per row: an
"X-axis pass" that detects integer world-X crossings as we sweep
across the row (drawing perspective-correct lines that converge to
the vanishing point), and a "Y-axis pass" that does the same for
world-Y. Both passes are gated on the row having a nonzero
**spread** — `dX = wxR_s - wxL_s` must be nonzero for the X-pass to
run, `dY` for Y-pass.

When the player faces **exactly north** (angle 192 in our 0-255
convention), `dirX = cos(192) = 0` and `planeX ≈ 0.66`. The left
and right view-ray Y-components are both `dirY = -1`, identical, so
`dY = 0` and the Y-pass is skipped entirely. No Y-axis grid lines
visible.

When facing **exactly east**, `dirY = 0` and the left and right
view-ray X-components are both 1.0. `dX = 0` and the X-pass is
skipped. No X-axis grid lines.

This is a degenerate-case math issue, not a precision issue —
`dX = 0` is real, not "very small."

The fix: when one axis has zero spread, that axis's grid lines are
perpendicular to view direction and appear as **bands**, not
perspective-converging lines. Detect them by checking whether
`FX_INT(wxL_s)` changed between this row and the previous row
(across `y`), and if so stamp a full-width band:

```c
if (dX != 0) {
    /* existing per-pixel crossings code */
} else if (has_prev && FX_INT(wxL_s) != FX_INT(prev_wxL_s)) {
    for (int col = 0; col < SCREEN_W; col++) row_p[col] = grid_c;
}
```

Now when the player faces north and walks forward, world-Y bands
appear and animate downward across the screen as `py` decreases.

**Insight.** Raycaster ceiling/floor rendering with multiple grid
axes has a degenerate case at exact cardinal facing that's easy to
miss until someone walks around for a while and notices. The fix is
to add a "perpendicular-to-view" code path that operates per-row
rather than per-pixel. Don't assume "facing a cardinal direction"
won't happen in practice — it's the natural rest state after the
player walks into a wall.

**Files.** `sh_src/raycast.c::raycast_draw_ceiling_grid`.

---

## 2026-06 — COLLAB: WIP commits during iteration, squash before push

**Context.** The development loop is "edit code → `make deploy` →
walk around the level on the MiSTer FPGA → notice something →
decide." Most "noticing" leads to "revert the last thing" or "tune a
constant and try again." Without discipline, GitHub history becomes
a wall of "wip: try denser stains" / "wip: didn't work" commits.

**Outcome.** Codified workflow that we follow whenever iterating on
hardware:

1. Every build → `git commit` locally with a `wip:` prefix message
   (sometimes very specific: `wip: bump turn speed 4→8`)
2. `make deploy` ships to the MiSTer
3. Test on real hardware
4. Decide: keep going, revert (`git reset --hard`), or sign off
   (`"works on hw"`)
5. On sign-off, `git reset --soft origin/main` and write one clean
   commit message that describes the **net change** across all the
   squashed WIPs
6. Push that single commit

The reset-soft + single-commit dance keeps all the working tree
state intact, just collapses the history. The signed-off commit
message gets a real prose explanation, not a list of bullet points.

**Insight.** For hardware-loop development specifically, the local
git history needs to be a scratch pad for "what was I trying when
this broke?" — but the public history should only show the net
finished thought. The workflow tax is roughly 30 seconds per squash,
which is trivial compared to the value of a clean upstream log.

**Files.** Memory at
`~/.claude/projects/-Users-mikeholzinger-src-32x-builder/memory/feedback_squash_workflow.md`
keeps this rule alive across sessions.

---

## 2026-06 — COLLAB: Background research agents while foreground builds

**Context.** At several points the project needed deep technical
research — d32xr's optimization techniques, PWM audio implementation
details, etc. — that would take 30+ minutes of focused web reading
and source inspection. Doing it in the foreground meant blocking
implementation work on the answer.

**Outcome.** Spawned background subagents with detailed prompts
(specific files to read, specific questions to answer, specific
output format). Foreground continued with whatever implementation
was unblocked (UI tweaks, simpler optimizations, map iteration).
When the agent's report landed, the foreground absorbed the findings
and made decisions on whether to adopt each item.

Concrete instances:

1. **d32xr optimization mining** — spawned mid-session, came back
   with a ranked adopt-list (DIVU latency-hiding, hand-rolled column
   draw, work-stealing, GBR-TLS, quarter-wave sine, cache-line
   invalidate, PWM mixer). Implemented the safe-to-adopt items in
   the same session.
2. **PWM audio implementation plan** — spawned during a perf
   squash-and-push, came back with a concrete 6-file implementation
   plan including the critical finding that `mars_start.s` is
   missing DMA IRQ dispatch (a piece of infrastructure that has to
   be added before any d32xr-style audio can run).

**Insight.** "Read this thing carefully and report back" is a
distinct task type from "implement this thing." Splitting them
across two agents (foreground implementer + background researcher)
roughly doubles wall-clock throughput, and the resulting report is
a permanent reference even after the research session ends.

**Files.** The reports themselves live in the conversation history;
their *findings* are codified in `ROADMAP.md` and this devlog.

---

## 2026-06 — COLLAB: Header dependency tracking is critical for sanity

**Context.** During the SH-2 split work, walking around the map
suddenly went from 60fps to **1 frame every 5 seconds**, with
partial-frame visual artifacts ("screen splits between rendering
ceiling+floor and occasionally walls pop in"). The code we'd just
added looked fine on inspection. Reverting it didn't help.

**Outcome.** The Makefile didn't track header dependencies. We had
added a new field to `shared_t` in `shared.h`, but `shared.c` —
which allocates the actual `shared_t shared` storage — wasn't
rebuilt because its `.o` file was newer than `shared.c`'s timestamp.
The compiled `shared.o` allocated 4 bytes (the old struct size); the
compiled `raycast.o` wrote 16 bytes into it from the new master-side
snapshot code. The 12 bytes of overflow corrupted whatever happened
to be adjacent in the linker's allocation, which on this build was
the renderer's column z-buffer + parts of the rendering state.

Result: rendering went to garbage in a way that LOOKED like a CPU
bug. We chased the symptom for two cycles (full revert of work-
stealing, slave-side investigation) before realizing it was a build
hygiene issue.

The fix: add `-MMD -MP` to the compile commands so GCC emits a `.d`
file per translation unit listing all its header dependencies, then
`-include` those at the bottom of the Makefile. Two extra flags +
three lines of Makefile.

**Insight.** Without automatic header dep tracking, struct-layout
changes manifest as **memory corruption that exactly mimics
hardware bugs**. The cost of debugging this once is way higher than
the cost of setting up dep tracking in any project that has more
than two source files. Add it on day one.

**Files.** `Makefile` (`-MMD -MP` flags + `-include $(...:.o=.d)`
lines).

---

## 2026-06 — ARCHITECTURE: Throttle the slave's idle loop, not its work

**Context.** Early in the SH-2 split, the slave's polling-dispatcher
pattern was: tight loop reading COMM4, branching on non-zero, doing
the named work, writing 0 back. Per-iteration cost: 1 MMIO read + 1
compare + 1 branch ≈ 8 SH-2 cycles, so ~3M iterations per second.
This was the textbook polling pattern from d32xr.

**Outcome.** The bridge-bandwidth bug forced a refactor. The
realization was that the slave **shouldn't** be polling COMM4 at
3M/sec during idle — the command-response latency we actually care
about is "one frame" = 16ms. Polling at 30K/sec (one check every
33μs) is **500× the latency budget we need**, and dropping that two
orders of magnitude leaves more than enough headroom.

The pattern:

```c
for (;;) {
    uint16_t cmd = MARS_SYS_COMM4;
    if (cmd == MARS_CMD_NONE) {
        for (volatile int i = 0; i < 256; i++);   /* throttle */
        continue;
    }
    /* dispatch */
}
```

Equivalent for the master's slave-ACK wait, using NOPs:

```c
while (MARS_SYS_COMM4 != MARS_CMD_NONE) {
    __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop\n\t"
                         "nop\n\tnop\n\tnop\n\tnop\n\t"
                         "nop\n\tnop\n\tnop\n\tnop\n\t"
                         "nop\n\tnop\n\tnop\n\tnop");
}
```

**Insight.** "Polling tight" isn't free. On a CPU with no idle
state and a shared bus, every iteration of a tight polling loop
costs not just CPU cycles but bus bandwidth. Match the polling rate
to the **actual latency budget** of the response, not the maximum
the CPU can sustain.

**Files.** `sh_src/s_main.c:10-22` (slave throttle);
`sh_src/raycast.c` (master throttles).

---

## 2026-06 — HW-TRUTH: mars_start.s slave IRQ table is missing DMA

**Context.** The PWM audio research agent's plan requires a DMA-
complete interrupt handler on the slave. We hadn't implemented it
yet, but the agent's audit flagged a setup issue we'd otherwise hit
on first test.

**Outcome.** The slave's IRQ dispatcher in `sh_src/mars_start.s`
at `slav_irq:` is a `cmp/eq` chain dispatching on `SR.I3-I0`-derived
level codes — FRT (`0x28`), VBI (`0x38`), HBI (`0x30`), PWM
(`0x20`), CMD (`0x18`), VRES (`0x40`). The "Level 4 & 5" vector slot
(`mars_start.s:203`) is wired to `slav_irq` — but `slav_irq` has no
case for level 4 (DMA), so a DMA-complete interrupt would fall
through the chain and hit the silent `rte` at the end.

This isn't an immediate problem (we don't have any DMA channels
running yet) but it's a trap waiting for the first PWM audio test.
The fix when we do it: add `cmp/eq #0x10, r0; bt slav_dma_irq` to
the chain (DMA's level-4 encoding after the `shlr2; and #0x38` mask
is `0x10`), then write a `slav_dma_irq:` body that clears `CHCR1.TE`
and calls our C handler. ~25 lines of asm, modeled exactly on
d32xr's `sec_dma_irq` in `crt0.s`.

**Insight.** Reading a third-party crt0 for "what's wired" versus
"what's implemented" is worth doing before writing any code that
depends on an IRQ firing. The vector table entry being present is
necessary but not sufficient — the dispatcher logic has to actually
do something with it.

**Files.** `sh_src/mars_start.s::slav_irq` (the chain that needs the
extension); fix pattern in `viciious/d32xr::crt0.s::sec_dma_irq`.

---

## 2026-06 — HW-TRUTH: SH-2 internal-peripheral IRQs use VCR, not auto-vector

**Context.** First attempt at PWM audio on the slave. The implementation
plan from the deep-mining agent said to set IPRA priority and the
DMA-complete interrupt would fire. We wrote the asm dispatch in
`mars_start.s`, plumbed `amb_sound_init` and `amb_dma_handler` through
`sound.c`, set IPRA, and... heard exactly one buffer pass (1.5s) of
audio, then silence. The DMA had clearly run once. The completion
interrupt was never serviced.

**Outcome.** Two interlocking bugs in the IRQ setup, both subtle, both
hidden by the agent's plan being *almost* right:

1. **IPRA bit field for DMA.** SH7095's IPRA layout is `[15:12]=DIVU,
   [11:8]=DMAC, [7:4]=WDT, [3:0]=REF`. The agent's plan said "set bits
   `[15:12]` to 4 for DMA priority," which actually sets *DIVU's*
   priority. DMA priority stays at the reset default of 0 = "this
   interrupt is masked." The correct mask is:

   ```c
   SH2_INT_IPRA = (SH2_INT_IPRA & 0xF0FF) | 0x0400;  /* bits [11:8] = 4 */
   ```

2. **Internal-peripheral IRQs use user-defined vectors via VCR
   registers, not auto-vectors.** The external IRQs on the 32X (VRES,
   VBI, HBI, CMD, PWM) use the standard SH-2 auto-vector path —
   SH-2 looks up the vector number based on the interrupt level. But
   SH-2 *internal* peripherals (DMA, DIVU, WDT, SCI, FRT) use
   user-defined vector numbers via dedicated control registers
   (`VCR0`, `VCR1`, etc.) that have to be set explicitly.

   `SH2_DMA_VCR1` defaults to 0 after reset. Vector 0 in the VBR
   table is the cold-start PC. So when DMA1 completes and tries to
   dispatch its IRQ, the SH-2 looks up `VBR[0]` and jumps to the
   slave's `sstart` — i.e., re-runs slave initialization. The slave
   ends up in a weird re-entered state and the audio never resumes.

   The fix: point VCR1 at a vector slot that holds our IRQ handler
   address. Slot 66 in the slave vector table (the existing "Level
   4 & 5" entry) already contains `slav_irq`, which has a chain that
   dispatches to `slav_dma_irq` when SR.I3-I0 = 4. So:

   ```c
   SH2_DMA_VCR1 = 66;
   ```

After both fixes, audio loops indefinitely with no glitches.

**Insight.** The SH-2 has two distinct interrupt-vector paths: the
external/auto-vector path (where the SH-2 derives the vector number
from the interrupt level it sees on its `IRL` pins) and the
internal/user-vector path (where each on-chip peripheral has a
control register that holds the vector number to use). They look
similar from the C side because they end up dispatching through the
same VBR table, but the *setup* is completely different. If your
peripheral's `VCRx` is unset, the IRQ jumps to vector 0 = the
cold-start handler, which produces extremely confusing symptoms.

Also: when an agent's research plan is *almost* right, the
near-misses are the highest-value findings. The agent correctly
identified that IPRA needed setting and correctly identified the
priority value (4), but got the bit field wrong; the agent never
mentioned VCR1 at all (presumably because d32xr never writes VCR1
either — but d32xr might have had VCR1 pre-set by the cart header,
or might have used a different vector configuration we don't see in
the snippets we read).

**Files.** `sh_src/sound.c::amb_sound_init` (the IPRA + VCR1 writes
with explanatory comments); `sh_src/mars_start.s::slav_dma_irq` (the
asm handler that the user vector points at); `sh_src/mars.h:122-123`
(`SH2_INT_IPRA` and `SH2_DMA_VCR1` declarations).

**Excerpt** — the fix as committed:

```c
/* SH-2 IPRA layout (SH7095): [15:12]=DIVU, [11:8]=DMAC, [7:4]=WDT,
 * [3:0]=REF. Set DMA priority to 4. */
SH2_INT_IPRA = (SH2_INT_IPRA & 0xF0FF) | 0x0400;

/* DMA1 uses a USER-DEFINED interrupt vector. VCR1 holds the vector
 * number; SH-2 reads VBR[VCR1*4] to get the handler. Point at vector
 * slot 66 = "Level 4 & 5" entry, which holds slav_irq. */
SH2_DMA_VCR1 = 66;
```

---

## Template for new entries

```
## YYYY-MM — TAG: Short title

**Context.** What we were trying to do.

**Outcome.** What happened, with concrete numbers where possible.

**Insight.** What this taught us. One sentence, citation-worthy.

**Files.** `src/path/file.ext:line`

**Excerpt.** (Optional — short code snippet.)
```

Tags: `HW-TRUTH` | `OPTIMIZATION` | `BUG` | `ARCHITECTURE` | `COLLAB`
