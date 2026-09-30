# SH-2 cache and timing on the 32X — findings from the sega16-2-32x port

Written 2026-09-24, from the System 16 -> 32X port. Same hardware, very
different workload (tile/sprite compositor, not a raycaster), so treat
the *methods* as portable and the *numbers* as ours unless marked
otherwise.

**Claim discipline, because it matters here:** every item is marked
**RTL** (read from the MiSTer 32X core's Verilog, file:line given),
**MEASURED** (our instrument, our rom, named), or **UNVERIFIED**
(read from source, not confirmed). Nothing below is a datasheet
recollection.

You already distrust ares and already use `.ramtext` + cache purge, so
this skips both. These are four things that were not in your tree as of
2026-09-24.

---

## 1. Two CCR bits your `mars.h` does not define — RTL

Your `sh_src/mars.h:105-107`:

    #define SH2_CCTL_CP   0x10
    #define SH2_CCTL_TW   0x08
    #define SH2_CCTL_CE   0x01

The full register, `srcref/S32X_MiSTer/rtl/SH/SH7604/SH7604_pkg.sv:96-100`:

    bit  CP;   // 0x10  cache purge
    bit  TW;   // 0x08  two-way mode
    bit  OD;   // 0x04  DATA replacement disable      <- missing
    bit  ID;   // 0x02  INSTRUCTION replacement disable <- missing
    bit  CE;   // 0x01  cache enable

And what they do, `CACHE.sv:499`:

    CACHE_UPDATE <= CBUS_ID ? ~CCR.ID : ~CCR.OD;

**`OD` stops DATA fetches from allocating cache lines. `ID` stops
INSTRUCTION fetches from allocating.** Reads still hit if the line is
already resident; what stops is *replacement*.

**Why this is pointed at your engine specifically:** your Root Cause 2
is the uncached framebuffer store binding the wall loop, and your wall
loop competes for cache against streaming texture reads. `OD=1` around
the hot loop stops texture data evicting the loop's own instructions and
working set. Whether that is a net win is an experiment, not a
prediction — but the lever is not in your header, so it has not been
tried.

We had the identical omission and found it the same way.

**Status: RTL for the bits and their effect. UNVERIFIED that setting
them helps any particular workload — we have not run this experiment
either.**

## 2. Cache geometry, and the 1,024-byte rule — RTL

`CACHE.sv:130-143`:

    reg [25:0] WAY0 [64];   // four ways
    reg [25:0] WAY1 [64];
    reg [25:0] WAY2 [64];
    reg [25:0] WAY3 [64];
    ...
    WAY_TAG[0] = WAY0[CBUS_A[9:4]][`TAG] == CBUS_A[28:10];

**64 sets x 4 ways x 16-byte lines = 4 KB. The set index is address bits
9:4, so THE SET PATTERN REPEATS EVERY 1,024 BYTES.**

The consequence for `.ramtext` layout:

    a contiguous hot region of S bytes occupies ceil(S / 1024) ways
    of every set it spans -- out of four.

So 3 KB of contiguous hot code takes 3 of 4 ways across the whole cache
and leaves one way for everything else. 5 KB self-evicts before anything
else gets a look in.

**You already copy hot functions ROM -> cacheable SDRAM. This says how to
ORDER them**: two 900-byte loops placed 1,024 bytes apart collide in
every set; the same two placed adjacently do not. Ordering `.ramtext` by
call-graph locality is free, and measurable with (4) below.

**Status: RTL. The consequence is arithmetic from the RTL, not a
measurement.**

## 3. BlastEm charges for the store your wall loop is bound by

**This is the one we would lead with.**

You iterate on MiSTer because ares does not charge for memory. BlastEm
(`~/src/blastem-0c61d0d95463`, binary in `~/bin/blastem-osx-1.0.0/`)
appears to charge for both of the things ares omits:

    sh2_util.c:80      sh2_generic_burst_read():
                         sh2->cycles += chunk->burst_cycles
                                        * sh2->opts->gen.clock_divider;
                       = per-region CACHE LINE BURST FILL cost

    32x.c:1006-1009    s32x_video_sh2_write() returns wait_cycles, and:
                         sh2->cycles += wait_cycles;
                       = FRAMEBUFFER WRITE BUS WAITS

**Your entire Root Cause 2 is uncached `mov.b` stores to 0x24000000.
That is the second one exactly.**

Practical notes, MEASURED by us:

    blastem -b N -m 32x rom.32x     headless, run N frames, exit.
                                    UNDOCUMENTED in -h; see
                                    blastem.c:429-437.
    speed                           ~385 fps headless on an M-series Mac
                                    (300 frames in 0.78 s user)
    BIOS                            needs 32X_M_BIOS.bin / _S_ / _G_
                                    CWD-relative, plain fopen
                                    (32x.c:1416). MAME's 32x romset has
                                    all three.
    NOT present                     no --dump, no --profile, no input
                                    replay. The debugger is NOT pipeable
                                    (debug.c:2328 uses fgets_timeout;
                                    piping commands returns rc=124, zero
                                    bytes -- we tested twice). gdb remote
                                    (-D) is 68K-ONLY, zero SH-2 refs.
    the patch point                 genesis.c:613, the exit_after site:
                                    frame boundary, `gen` in scope, and
                                    gen->mars gives main/sub sh2_context,
                                    sdram, video, regs (32x.h:82-100).
                                    A --dump is: parse the arg, write
                                    bytes there before exit(0).

**Status: the two fidelity claims are UNVERIFIED — read from source, not
confirmed against hardware. The headless behaviour and speed are
MEASURED by us. Cross-check one figure you already trust from MiSTer
before believing any BlastEm number; we have a hard rule about this
after eight instruments lied to us inside one debugging arc.**

## 4. A union over N frames is not a working set — MEASURED, ours

We profiled our renderer's instruction footprint and reported a "1% cold
tail" of 13,168 bytes that could be moved off the hot path. It did not
exist. Windowed properly:

    window        frames   touched bytes
    1800-1802          2        14,256
    1800-1804          4        14,784
    1800-1810         10        15,408
    1800-2000        200        21,856   <- the number we had been using

**Two frames already touch 14,256 B and ten frames add only 1 KB.** The
long window unions code that never co-resides. **The cache sees ONE
generation.**

Method that works: take two short profile runs N frames apart,
difference them, bucket per-PC counts into 16-byte lines, count distinct
lines. That excludes boot and scene-load code for free and gives the
number the cache actually experiences.

That correction killed our own footprint card before we wrote a line of
it.

## 5. Two ares facts, stated precisely

You already treat MiSTer as truth. These say which ares numbers are
still worth taking:

**MEASURED, ours:** headless ares charges SH-2 **instruction cycles
only** — no SDRAM or uncached waits, no data cache, no instruction
fetch. So ares is exact for *instruction counts and correctness* and
worthless for *anything memory-shaped*. Every size-for-count trade in
our renderer's history measured as a win on ares; some were losses.

**MEASURED, ours:** on ares the wall is `max(echo, mtask)` and the
**slave** reads as critical path; on the FPGA the **master** does.
Removing master work cannot move an ares number by construction. We
shelved a real optimisation for three days on an ares ranking that could
not have shown it.

Your dual-CPU load balancer is exactly the kind of thing that measures
differently on the two, so it is worth knowing which way each leans.

---

## What we would not claim

We have not run (1) or (2) as experiments — the `OD`/`ID` lever and the
`.ramtext` ordering are RTL-derived opportunities, not results. Our own
cache work went a different way: we measured our hot path at ~14 KB
against a 4 KB cache, tried relocation, hot/cold separation and locking,
and **all three measured dead** — relocation because `.text` is already
the cached cart view, hot/cold because there is no cold region, locking
because 2 KB against 14 KB is not a lever.

**Our footprint problem turned out not to be a footprint problem.** Yours
may be different — a raycaster's inner loop is far smaller than a tile
compositor's — which is precisely why the `OD` bit is worth an
afternoon there and was not worth one here.
