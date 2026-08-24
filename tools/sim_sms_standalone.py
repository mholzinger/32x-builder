#!/usr/bin/env python3
"""Headless boot test for the STANDALONE maze.sms (make maze-ares).

Reuses sim_z80_game.py's Z80 core, adds the instructions the shim uses
(in/out, otir/ldir, im 1, ei, conditional call, sbc hl, neg, ld de,(nn)),
and models just enough VDP/pad to walk the whole path: boot -> terminal
blit -> cursor input -> A into the maze -> SCROLL MODE (regs 8/9,
seam-streamed name table, the player as 16 hardware sprites).

Run after touching standalone.asm, BEFORE booting Ares — it proved the
ROM innocent the day Ares showed dead input (the answer was Ares' empty
Controller Port 1, not the ROM). No $3F/$3E modeling: ares resets TR/TH
to inputs and never gates the d-pad bits (ares/ms/cpu/memory.cpp,
controller/port.cpp)."""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SIM = str(ROOT / "tools" / "sim_z80_game.py")
src = open(SIM).read()
head = src[:src.index("# -- harness")]
ns = {"__file__": SIM}
exec(compile(head, "sim_head", "exec"), ns)
Z80 = ns["Z80"]


class IO:
    def __init__(self):
        self.vram = bytearray(0x4000)
        self.cram = bytearray(32)
        self.regs = [0] * 16
        self.addr = 0
        self.code = 0
        self.latch = None
        self.vbl = False
        self.pad = 0xFF          # active low: idle
        self.psg = []

    def inp(self, port):
        port &= 0xFF
        if port == 0xBF:
            v = 0x80 if self.vbl else 0
            self.vbl = False
            self.latch = None
            return v
        if port == 0xDC:
            return self.pad
        return 0xFF

    def out(self, port, val):
        port &= 0xFF
        if port == 0xBF:
            if self.latch is None:
                self.latch = val
            else:
                if val >> 6 == 2:
                    self.regs[val & 0x0F] = self.latch
                self.addr = ((val & 0x3F) << 8) | self.latch
                self.code = val >> 6
                self.latch = None
        elif port == 0xBE:
            self.latch = None
            if self.code == 3:
                self.cram[self.addr & 31] = val
            else:
                self.vram[self.addr & 0x3FFF] = val
            self.addr = (self.addr + 1) & 0x3FFF
        elif port == 0x7F:
            self.psg.append(val)


class Z80P(Z80):
    def __init__(self, mem, io):
        super().__init__(mem)
        self.io = io

    def _sbc_hl(self, v):
        r = self.hl() - v - (1 if self.fc else 0)
        self.fc = r < 0
        r &= 0xFFFF
        self.set_hl(r)
        self.fz = r == 0

    def step(self):
        op = self.m[self.pc]
        if op == 0xDB:                       # in a,(n)
            self.a = self.io.inp(self.m[(self.pc + 1) & 0xFFFF])
            self.pc = (self.pc + 2) & 0xFFFF
            return
        if op == 0xD3:                       # out (n),a
            self.io.out(self.m[(self.pc + 1) & 0xFFFF], self.a)
            self.pc = (self.pc + 2) & 0xFFFF
            return
        if op == 0xFB:                       # ei (no IRQs modeled)
            self.pc = (self.pc + 1) & 0xFFFF
            return
        if op in (0xC4, 0xCC, 0xD4, 0xDC):   # call cc,nn
            take = {0xC4: not self.fz, 0xCC: self.fz,
                    0xD4: not self.fc, 0xDC: self.fc}[op]
            t = self.m[(self.pc + 1) & 0xFFFF] \
                | (self.m[(self.pc + 2) & 0xFFFF] << 8)
            self.pc = (self.pc + 3) & 0xFFFF
            if take:
                self.push(self.pc)
                self.pc = t
            return
        if op == 0xED:
            sub = self.m[(self.pc + 1) & 0xFFFF]
            if sub == 0xB0:                  # ldir
                self.pc = (self.pc + 2) & 0xFFFF
                while True:
                    self.m[self.de()] = self.m[self.hl()]
                    self.set_hl((self.hl() + 1) & 0xFFFF)
                    self.set_de((self.de() + 1) & 0xFFFF)
                    self.set_bc((self.bc() - 1) & 0xFFFF)
                    if self.bc() == 0:
                        return
            if sub == 0xB3:                  # otir
                self.pc = (self.pc + 2) & 0xFFFF
                while True:
                    self.io.out(self.c, self.m[self.hl()])
                    self.set_hl((self.hl() + 1) & 0xFFFF)
                    self.b = (self.b - 1) & 0xFF
                    if self.b == 0:
                        return
            if sub == 0x52:                  # sbc hl,de
                self.pc = (self.pc + 2) & 0xFFFF
                self._sbc_hl(self.de())
                return
            if sub == 0x42:                  # sbc hl,bc
                self.pc = (self.pc + 2) & 0xFFFF
                self._sbc_hl(self.bc())
                return
            if sub == 0x5B:                  # ld de,(nn)
                addr = self.m[(self.pc + 2) & 0xFFFF] \
                    | (self.m[(self.pc + 3) & 0xFFFF] << 8)
                self.e = self.m[addr]
                self.d = self.m[(addr + 1) & 0xFFFF]
                self.pc = (self.pc + 4) & 0xFFFF
                return
            if sub == 0x44:                  # neg
                self.pc = (self.pc + 2) & 0xFFFF
                self.fc = self.a != 0
                self.a = (-self.a) & 0xFF
                self.fz = self.a == 0
                return
            if sub == 0x56:                  # im 1
                self.pc = (self.pc + 2) & 0xFFFF
                return
        super().step()


mem = bytearray(0x10000)
rom = open(ROOT / "sms" / "games" / "maze" / "maze.sms", "rb").read()
mem[0:len(rom)] = rom
for i in range(0xC000, 0x10000):
    mem[i] = 0x55                            # RAM boots dirty
io = IO()
cpu = Z80P(mem, io)


WALK = 20                # frames to walk one 32px cell (16) + margin

def frame(pad=0xFF, steps=9000):
    # A real SMS frame is ~5,400 instructions (3.58 MHz / 60 Hz at ~11
    # cycles each) — NOT the 15-20k this once claimed, which is why a
    # 41,000-instruction rebuild hid here for so long. 9,000 leaves the
    # hardware's own headroom plus margin; the boot and scroll-entry
    # paints ask for their real budget explicitly.
    io.pad = pad
    io.vbl = True
    for _ in range(steps):
        cpu.step()


def frames(n, pad=0xFF):
    for _ in range(n):
        frame(pad)


def nt(row, col):
    return io.vram[0x3800 + (row * 32 + col) * 2]


fails = 0


def check(name, cond):
    global fails
    print(("PASS  " if cond else "FAIL  ") + name)
    if not cond:
        fails += 1


# ---- boot to the terminal (blit mode) -----------------------------------
frame(steps=600000)          # boot shim: 16KB VRAM clear + tile upload
frames(2)
check("boot: terminal blitted to the name table (M of MASTER)",
      nt(2, 9) == 24)
check("boot: BG palette in CRAM (entry 4 = wallpaper yellow $1F)",
      io.cram[4] == 0x1F)
check("boot: sprite palette mirrors BG (CRAM 16+4)",
      io.cram[20] == 0x1F)
check("boot: art tiles in VRAM at tiles 128+ (tile 128 itself is the "
      "blank floor)", any(io.vram[128 * 32:180 * 32]))
check("boot: walk-cycle sprite tiles at 192-255 (figure, not corners)",
      any(io.vram[192 * 32:256 * 32]))
frame(0xFF & ~0x02)                          # DOWN (bit 1, active low)
frame()
check("terminal cursor moved to FIELD MAP", nt(17, 6) == 41 and nt(15, 6) == 0)

# ---- A: into the maze = SCROLL MODE -------------------------------------
frame(0xFF & ~0x10, steps=200000)            # A: the game flips to maze
frame(steps=400000)                          # next vblank: the entry paint
frames(2)
check("maze: scroll mode painted art tiles into the name table",
      any(io.vram[0x3800 + i * 2] >= 128 for i in range(896)))
check("maze: spawn camera is clamped to the corner (regs 8/9 = 0)",
      io.regs[8] == 0 and io.regs[9] == 0)
check("maze: column-0 mask armed (reg 0 = $26)", io.regs[0] == 0x26)
sy, sx, sn = io.vram[0x3F00], io.vram[0x3F80], io.vram[0x3F81]
# spawn (2,2): world (64,64) at 32px cells, camera clamped 0. The 32px
# figure now fills its 32px cell exactly, so there is no straddle
# offset: x = 64, y = 64 (SAT Y stores y-1)
check(f"maze: player sprite block at spawn (Y={sy} X={sx} N={sn}, frame 0)",
      sy == 63 and sx == 64 and sn == 192)
check("maze: sprite list terminated after 16", io.vram[0x3F10] == 0xD0)

# ---- one step right: sprite slides, camera still clamped ----------------
frame(0xFF & ~0x08)                          # RIGHT
frames(WALK)                                 # a cell walks in 96 frames
check("move right: game cell advanced", mem[0x1CA0 + 0xC000] == 3
      if False else mem[0xDCA0] == 3)        # VAR_PX at RB+$1CA0
check("move right: sprite slid to x=96, camera still 0",
      io.vram[0x3F80] == 96 and io.regs[8] == 0)

# ---- walk down the open column: camera unclamps, seams stream -----------
frames(10 * WALK, 0xFF & ~0x02)              # hold DOWN (mid-map: the
frames(WALK)                                 # camera stays unclamped),
                                             # then let the glide land
py = mem[0xDCA1]                             # VAR_PY at RB+$1CA1
check(f"walk down: game cells advanced (py={py})", 8 <= py <= 25)
camy = min(max(py * 32 - 80, 0), 832)
check(f"walk down: reg 9 = camy % 224 = {camy % 224}",
      io.regs[9] == camy % 224)
check("walk down: player sprite pinned at screen y=80 (Y byte 79)",
      io.vram[0x3F00] == 79)


# ---- the strong check: the whole name table matches the invariant -------
# Name row nr must hold map tile row base+k (nr = (base%28 + k) % 28);
# recompute every expected tile id in Python from the map bits, edge
# bitmaps and metatile table, and diff all 896 entries against VRAM.
def map_wall(x, y):
    return bool(mem[0xDC00 + y * 4 + (x >> 3)] & (0x80 >> (x & 7)))


def pedge_n(x, y):
    return bool(mem[0xDD00 + y * 4 + (x >> 3)] & (0x80 >> (x & 7)))


def pedge_w(x, y):
    return bool(mem[0xDD90 + y * 5 + (x >> 3)] & (0x80 >> (x & 7)))


def cell_kind(cx, cy):
    if (cx, cy) == (31, 16):
        return 2                             # the exit door
    if map_wall(cx, cy):
        return 1
    combo = (pedge_n(cx, cy) * 1 + pedge_w(cx + 1, cy) * 2
             + pedge_n(cx, cy + 1) * 4 + pedge_w(cx, cy) * 8)
    return 3 + combo if combo else 0


def expected_tile(mc, mr):
    if mr >= 128:                            # v4: 32px cells, 128 tile rows
        return 0
    k = cell_kind(mc >> 2, mr >> 2)
    return mem[0x1700 + k * 16 + (mr & 3) * 4 + (mc & 3)]


def check_invariant(label, camx, camy):
    base, colbase = camy >> 3, camx >> 3
    bad = 0
    for nr in range(28):
        mr = base + (nr - base % 28) % 28
        for nc in range(32):
            mc = colbase + ((nc - colbase) & 31)
            if io.vram[0x3800 + (nr * 32 + nc) * 2] != expected_tile(mc, mr):
                bad += 1
    check(f"{label}: name table matches the seam invariant "
          f"({bad} of 896 wrong)", bad == 0)


check_invariant("walk down", 0, camy)

# ---- through the divider gap: horizontal seams + a live reg 8 -----------
for _ in range(30):                          # steer onto row 16: single
    py = mem[0xDCA1]                         # taps (a held pad re-edges
    if py == 16:                             # after every slide),
        break                                # direction-aware
    frames(1, 0xFF & ~(0x01 if py > 16 else 0x02))
    frames(WALK)
frames(WALK)                                 # let the glide land
check("walk to gap row: aligned on row 16", mem[0xDCA1] == 16)
for _ in range(20):
    if mem[0xDCA0] >= 12:
        break
    frames(1, 0xFF & ~0x08)
    frames(WALK)
frames(WALK)                                 # let the glide land
px = mem[0xDCA0]
check(f"walk right: crossed the divider (px={px})", 8 <= px < 30)
camx, camy = px * 32 - 112, 16 * 32 - 80
check(f"walk right: reg 8 = -camx & 255 = {-camx & 255}",
      io.regs[8] == (-camx & 255))
check("walk right: player sprite pinned at screen x=112",
      io.vram[0x3F80] == 112)
check_invariant("walk right", camx, camy)

# ---- smoothness: the pan must not stall between cells -------------------
# Every earlier reg-8 check samples the camera at REST, so a per-cell
# hitch passed them all while being plainly visible on hardware. A 16px
# cell glides in 8 frames at 2px, and a held direction must chain
# straight into the next glide: every frame pans, none idle. The two
# ways this breaks are the move cooldown outlasting the glide and the
# shim gating the pad on the landing frame — both cost exactly one dead
# frame per cell (a 12 Hz stutter).
frames(4, 0xFF & ~0x08)                      # absorb the start-from-rest
prev8 = io.regs[8]                           # frames: pressing from a dead
deltas = []                                  # stop costs a beat before the
for _ in range(36):                          # shim sees the new cell
    frames(1, 0xFF & ~0x08)                  # RIGHT held throughout
    deltas.append((prev8 - io.regs[8]) % 256)
    prev8 = io.regs[8]
moved = [i for i, d in enumerate(deltas) if d]
gaps = [b - a for a, b in zip(moved, moved[1:])]
check(f"walk right: pan advances 2px per beat ({sorted(set(d for d in deltas if d))})",
      set(d for d in deltas if d) == {2})
check(f"walk right: beats are evenly spaced, no dropped step "
      f"(gaps {sorted(set(gaps))}, want [1])", set(gaps) == {1})

# ---- walk cycle: legs must keep time while the stride carries farther --
# Travel (WALK_PX) and leg cadence are separate dials, and the cadence is
# the one Mike signed off on: a walk frame every 8 real frames. Chaining
# cells used to flash the idle pose for one frame at each boundary
# (cadence 1,7,8 instead of 8), which read as a hitch in his step.
_pf, _last, _gaps = mem[0xC116], None, []
_x0 = mem[0xC101] | (mem[0xC102] << 8)
for _i in range(80):
    frames(1, 0xFF & ~0x08)                  # RIGHT held
    if mem[0xC116] != _pf:
        if _last is not None:
            _gaps.append(_i - _last)
        _last, _pf = _i, mem[0xC116]
_dist = (mem[0xC101] | (mem[0xC102] << 8)) - _x0
check(f"walk cycle: one leg frame per 8 frames (saw {sorted(set(_gaps))})",
      set(_gaps) == {8})
check(f"walk cycle: stride covers 16px of ground ({_dist}px over "
      f"{len(_gaps) + 1} frames)", _dist // (len(_gaps) + 1) == 16)

# ---- CPU budget: the pan must fit in a real frame -----------------------
# frame() hands the Z80 40,000 instructions, but a real SMS frame is
# ~5,400 (3.58 MHz / 60 Hz at ~11 cycles per instruction). Every check
# above therefore passes on work the hardware has no time for: the
# TILEBUF rebuild cost 41,000 instructions on the frame a step begins —
# eight frames with the camera frozen, which on hardware read as the
# map snapping a whole cell instead of gliding. Measure the instructions
# between 2px pan updates and hold them to something a frame can afford.
_n = [0]
_orig_step = cpu.step
def _counting_step():
    _n[0] += 1
    _orig_step()
cpu.step = _counting_step
io.pad = 0xFF & ~0x08                        # RIGHT held
_last8, _since, _costs = io.regs[8], 0, []
for _ in range(300000):
    io.vbl = True
    cpu.step()
    if io.regs[8] != _last8:
        _costs.append(_n[0] - _since)
        _since = _n[0]
        _last8 = io.regs[8]
    if len(_costs) >= 24:        # wide enough to include seam
        break                    # frames (one per 8px of pan)
cpu.step = _orig_step
_worst = max(_costs[1:]) if len(_costs) > 1 else 0
check(f"walk right: work per pan beat fits its frame (worst "
      f"{_worst:,} instructions, budget ~5,400)", _worst < 5400)

# ---- the final step: escape flips back to blit mode ---------------------
for _ in range(30):                          # RIGHT until the door
    if mem[0xDFF6]:                          # STATE_MBX
        break
    frames(1, 0xFF & ~0x08)
    frames(WALK)
frames(3)
check("escape: game state flipped", mem[0xDFF6] == 1)
check("escape: scroll regs home, sprites off",
      io.regs[8] == 0 and io.regs[9] == 0 and io.vram[0x3F00] == 0xD0)
esc = "".join(chr(t - 12 + ord("A")) if 12 <= t <= 37 else " "
              for t in [nt(8, c) for c in range(32)])
check(f"escape: EXERCISE COMPLETE blitted ({esc.strip()!r})",
      "EXERCISECOMPLETE" in esc.replace(" ", ""))

if fails:
    sys.exit(f"{fails} FAILURES")
print("standalone scroll mode works in the model")
