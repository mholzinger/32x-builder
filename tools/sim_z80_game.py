#!/usr/bin/env python3
"""Smoke-test the SMS mini-game blob with a subset Z80 interpreter.

Runs the REAL bytes out of sms/game.bin against a fake 68K: loads a
known maze into MAP_BITS/MAP_META, ticks FRAME_MBX like the bridge
does, pokes pad bytes, and reads TILEBUF back. Asserts:

  1. first frame renders (DIRTY set, walls/floor/player/exit tiles)
  2. the player moves on a d-pad press and the frame redraws
  3. walls block
  4. held direction auto-repeats
  5. viewport scrolls when the player walks deep
  6. stepping into the exit cell flips STATE and draws the escape text

The interpreter covers only the opcodes the game uses and raises on
anything else — an unknown opcode IS a test failure.
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GAME = sys.argv[1] if len(sys.argv) > 1 else "maze"
BIN = ROOT / "sms" / "games" / GAME / "game.bin"

TILEBUF, MAP_BITS, MAP_META = 0x1900, 0x1C00, 0x1C80
PAD, DIRTY, STATE, FRAME = 0x1FF4, 0x1FF5, 0x1FF6, 0x1FF7
# the maze frame is 8x6 METATILE CELLS (4x4 art tiles each, v4 32px
# geometry), stamped from the table at $1700 — rows FLOOR WALL EXIT
# PLAYER-STAND-IN + 15 edge-partition combos (kind = 3 + N1+E2+S4+W8)
METATILES = 0x1700
MT_KINDS = 19
MT_FLOOR, MT_WALL, MT_EXIT, MT_PLAYER = 0, 1, 2, 3
PEDGE_N, PEDGE_W = 0x1D00, 0x1D90   # edge-partition bitmaps (33 rows/cols)


PSG_PORT = 0x7F11


class Z80:
    def __init__(self, mem):
        self.m = mem
        self.pc = 0
        self.sp = 0
        self.a = self.b = self.c = self.d = self.e = self.h = self.l = 0
        self.fz = self.fc = False   # only Z and C matter to this program
        self.psg = []               # bytes written to $7F11, in order

    # -- register helpers -------------------------------------------------
    def hl(self): return (self.h << 8) | self.l
    def de(self): return (self.d << 8) | self.e
    def bc(self): return (self.b << 8) | self.c
    def set_hl(self, v): self.h, self.l = (v >> 8) & 0xFF, v & 0xFF
    def set_de(self, v): self.d, self.e = (v >> 8) & 0xFF, v & 0xFF
    def set_bc(self, v): self.b, self.c = (v >> 8) & 0xFF, v & 0xFF

    R = ["b", "c", "d", "e", "h", "l", None, "a"]   # r-field encoding

    def get_r(self, i):
        if i == 6:
            return self.m[self.hl()]
        return getattr(self, self.R[i])

    def set_r(self, i, v):
        if i == 6:
            self.m[self.hl()] = v
        else:
            setattr(self, self.R[i], v)

    def fetch(self):
        v = self.m[self.pc]
        self.pc = (self.pc + 1) & 0xFFFF
        return v

    def fetch16(self):
        lo = self.fetch()
        return lo | (self.fetch() << 8)

    def push(self, v):
        self.sp = (self.sp - 2) & 0xFFFF
        self.m[self.sp] = v & 0xFF
        self.m[self.sp + 1] = (v >> 8) & 0xFF

    def pop(self):
        v = self.m[self.sp] | (self.m[self.sp + 1] << 8)
        self.sp = (self.sp + 2) & 0xFFFF
        return v

    def alu(self, op, v):
        if op == 0:                                   # ADD
            r = self.a + v
            self.fc = r > 0xFF
            self.a = r & 0xFF
            self.fz = self.a == 0
        elif op == 2:                                 # SUB
            r = self.a - v
            self.fc = r < 0
            self.a = r & 0xFF
            self.fz = self.a == 0
        elif op == 4:                                 # AND
            self.a &= v
            self.fc = False
            self.fz = self.a == 0
        elif op == 5:                                 # XOR
            self.a ^= v
            self.fc = False
            self.fz = self.a == 0
        elif op == 6:                                 # OR
            self.a |= v
            self.fc = False
            self.fz = self.a == 0
        elif op == 7:                                 # CP
            r = self.a - v
            self.fc = r < 0
            self.fz = (r & 0xFF) == 0
        else:
            raise NotImplementedError(f"alu op {op}")

    def step(self):
        op = self.fetch()
        if op == 0x00:  return                        # nop
        if op == 0xF3:  return                        # di
        if op == 0x31:  self.sp = self.fetch16(); return
        if op == 0x21:  self.set_hl(self.fetch16()); return
        if op == 0x11:  self.set_de(self.fetch16()); return
        if op == 0x01:  self.set_bc(self.fetch16()); return
        if op == 0x3A:  self.a = self.m[self.fetch16()]; return
        if op == 0x32:
            addr = self.fetch16()
            self.m[addr] = self.a
            if addr == PSG_PORT:
                self.psg.append(self.a)
            return
        if op == 0x2A:                                # ld hl,(nn)
            addr = self.fetch16()
            self.l, self.h = self.m[addr], self.m[addr + 1]
            return
        if op == 0x22:                                # ld (nn),hl
            addr = self.fetch16()
            self.m[addr], self.m[addr + 1] = self.l, self.h
            return
        if op == 0x36:  self.m[self.hl()] = self.fetch(); return
        if op == 0x34:                                # inc (hl)
            v = (self.m[self.hl()] + 1) & 0xFF
            self.m[self.hl()] = v
            self.fz = v == 0
            return
        if op == 0x35:                                # dec (hl)
            v = (self.m[self.hl()] - 1) & 0xFF
            self.m[self.hl()] = v
            self.fz = v == 0
            return
        if op == 0x23:  self.set_hl((self.hl() + 1) & 0xFFFF); return
        if op == 0x13:  self.set_de((self.de() + 1) & 0xFFFF); return
        if op == 0x0B:  self.set_bc((self.bc() - 1) & 0xFFFF); return
        if op == 0x2B:  self.set_hl((self.hl() - 1) & 0xFFFF); return
        if op == 0x19:                                # add hl,de
            r = self.hl() + self.de()
            self.fc = r > 0xFFFF
            self.set_hl(r & 0xFFFF)
            return
        if op == 0x09:                                # add hl,bc
            r = self.hl() + self.bc()
            self.fc = r > 0xFFFF
            self.set_hl(r & 0xFFFF)
            return
        if op == 0x29:                                # add hl,hl
            r = self.hl() * 2
            self.fc = r > 0xFFFF
            self.set_hl(r & 0xFFFF)
            return
        if op == 0x1A:  self.a = self.m[self.de()]; return
        if op == 0x12:                                # ld (de),a
            addr = self.de()
            self.m[addr] = self.a
            if addr == PSG_PORT:
                self.psg.append(self.a)
            return
        if op == 0x2F:  self.a ^= 0xFF; return        # cpl
        if op == 0x0F:                                # rrca
            c = self.a & 1
            self.a = (self.a >> 1) | (c << 7)
            self.fc = bool(c)
            return
        if op == 0x07:                                # rlca
            c = (self.a >> 7) & 1
            self.a = ((self.a << 1) | c) & 0xFF
            self.fc = bool(c)
            return
        if op == 0x1F:                                # rra
            c = self.a & 1
            self.a = (self.a >> 1) | (0x80 if self.fc else 0)
            self.fc = bool(c)
            return
        if op == 0xEB:                                # ex de,hl
            d, e = self.d, self.e
            self.d, self.e, self.h, self.l = self.h, self.l, d, e
            return
        if op == 0xED:                                # ED prefix
            sub = self.fetch()
            if sub == 0xA0:                           # ldi
                addr = self.de()
                self.m[addr] = self.m[self.hl()]
                if addr == PSG_PORT:
                    self.psg.append(self.m[addr])
                self.set_hl((self.hl() + 1) & 0xFFFF)
                self.set_de((self.de() + 1) & 0xFFFF)
                self.set_bc((self.bc() - 1) & 0xFFFF)
                return
            raise NotImplementedError(f"ED {sub:02X}")
        if 0x40 <= op <= 0x7F and op != 0x76:         # ld r,r'
            self.set_r((op >> 3) & 7, self.get_r(op & 7))
            return
        if 0x80 <= op <= 0xBF:                        # alu a,r
            self.alu((op >> 3) & 7, self.get_r(op & 7))
            return
        if op in (0xC6, 0xD6, 0xE6, 0xEE, 0xF6, 0xFE):  # alu a,n
            self.alu({0xC6: 0, 0xD6: 2, 0xE6: 4, 0xEE: 5,
                      0xF6: 6, 0xFE: 7}[op], self.fetch())
            return
        if op in (0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x3E):  # ld r,n
            self.set_r((op >> 3) & 7, self.fetch())
            return
        if op in (0x04, 0x0C, 0x14, 0x1C, 0x24, 0x2C, 0x3C):  # inc r
            i = (op >> 3) & 7
            v = (self.get_r(i) + 1) & 0xFF
            self.set_r(i, v)
            self.fz = v == 0
            return
        if op in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x3D):  # dec r
            i = (op >> 3) & 7
            v = (self.get_r(i) - 1) & 0xFF
            self.set_r(i, v)
            self.fz = v == 0
            return
        if op == 0x18:                                # jr d
            d = self.fetch()
            self.pc = (self.pc + (d - 256 if d > 127 else d)) & 0xFFFF
            return
        if op in (0x20, 0x28, 0x30, 0x38):            # jr cc,d
            d = self.fetch()
            take = {0x20: not self.fz, 0x28: self.fz,
                    0x30: not self.fc, 0x38: self.fc}[op]
            if take:
                self.pc = (self.pc + (d - 256 if d > 127 else d)) & 0xFFFF
            return
        if op == 0x10:                                # djnz
            d = self.fetch()
            self.b = (self.b - 1) & 0xFF
            if self.b:
                self.pc = (self.pc + (d - 256 if d > 127 else d)) & 0xFFFF
            return
        if op == 0xC3:  self.pc = self.fetch16(); return
        if op in (0xC2, 0xCA, 0xD2, 0xDA):            # jp cc,nn
            t = self.fetch16()
            take = {0xC2: not self.fz, 0xCA: self.fz,
                    0xD2: not self.fc, 0xDA: self.fc}[op]
            if take:
                self.pc = t
            return
        if op == 0xCD:                                # call
            t = self.fetch16()
            self.push(self.pc)
            self.pc = t
            return
        if op == 0xC9:  self.pc = self.pop(); return  # ret
        if op in (0xC0, 0xC8, 0xD0, 0xD8):            # ret cc
            take = {0xC0: not self.fz, 0xC8: self.fz,
                    0xD0: not self.fc, 0xD8: self.fc}[op]
            if take:
                self.pc = self.pop()
            return
        if op == 0xC5:  self.push(self.bc()); return
        if op == 0xD5:  self.push(self.de()); return
        if op == 0xE5:  self.push(self.hl()); return
        if op == 0xF5:                                # push af (flags: C+Z only,
            self.push((self.a << 8)                   #  the subset the sim keeps)
                      | (0x40 if self.fz else 0) | (0x01 if self.fc else 0))
            return
        if op == 0xC1:  self.set_bc(self.pop()); return
        if op == 0xD1:  self.set_de(self.pop()); return
        if op == 0xE1:  self.set_hl(self.pop()); return
        if op == 0xF1:                                # pop af
            v = self.pop()
            self.a = (v >> 8) & 0xFF
            self.fz = bool(v & 0x40)
            self.fc = bool(v & 0x01)
            return
        if op == 0xCB:                                # CB prefix: sla/srl
            sub = self.fetch()
            i = sub & 7
            v = self.get_r(i)
            if 0x20 <= sub <= 0x27:                   # sla
                self.fc = bool(v & 0x80)
                v = (v << 1) & 0xFF
            elif 0x38 <= sub <= 0x3F:                 # srl
                self.fc = bool(v & 1)
                v >>= 1
            elif 0x18 <= sub <= 0x1F:                 # rr (through carry)
                c = v & 1
                v = (v >> 1) | (0x80 if self.fc else 0)
                self.fc = bool(c)
            elif 0x08 <= sub <= 0x0F:                 # rrc (bit0 -> bit7 + carry)
                c = v & 1
                v = (v >> 1) | (c << 7)
                self.fc = bool(c)
            else:
                raise NotImplementedError(f"CB {sub:02X}")
            self.set_r(i, v)
            self.fz = v == 0
            return
        raise NotImplementedError(f"opcode {op:02X} at {self.pc - 1:04X}")


# -- harness ---------------------------------------------------------------
mem = bytearray(0x10000)
blob = BIN.read_bytes()
mem[0:len(blob)] = blob

# maze: border walls + a vertical bar at x=5 with a gap at y=16.
# spawn (2,2), exit stamped INTO the right border wall at (31,16).
maze = [[0] * 32 for _ in range(32)]
for i in range(32):
    maze[0][i] = maze[31][i] = maze[i][0] = maze[i][31] = 1
for y in range(1, 31):
    if y != 16:
        maze[y][5] = 1
for y in range(32):
    for x in range(32):
        if maze[y][x]:
            mem[MAP_BITS + y * 4 + (x >> 3)] |= 0x80 >> (x & 7)
mem[MAP_META:MAP_META + 4] = bytes([2, 2, 31, 16])
# edge partitions: a vertical slab on the W edge of (3,1) — blocks the
# LEFT step probed below — and a horizontal slab on the N edge of (6,4),
# visible from spawn as combo cells on both its neighbours
mem[PEDGE_W + 1 * 5 + 0] |= 0x80 >> 3
mem[PEDGE_N + 4 * 4 + 0] |= 0x80 >> 6
# Boot with DIRTY Z80 RAM. TILEBUF lives above the uploaded image, so a
# re-upload never clears it and the program inherits the previous session's
# frame — the title card only paints its banner and text, so anything it
# does not cover was last run's leftovers, compounding every boot until the
# screen was full of them. The program must clear TILEBUF itself.
mem[TILEBUF:TILEBUF + 768] = b"\x55" * 768
# the level name, centered in the 16-tile field ("SIMMAZE")
NAME_TILES = [30, 20, 24, 24, 12, 37, 16]
mem[0x1C84 + 4:0x1C84 + 4 + 7] = bytes(NAME_TILES)

cpu = Z80(mem)


FRAME_COUNT = [0]


def run_frame(pad):
    """One 68K frame: poke pad, tick FRAME, run the Z80 a while."""
    FRAME_COUNT[0] += 1
    mem[PAD] = pad
    mem[FRAME] = (mem[FRAME] + 1) & 0xFF
    for _ in range(200000):
        cpu.step()
        # loop is "settled" when it's back polling FRAME with no change
        if cpu.pc == 0 and False:
            break
    # 200k steps >> one frame of work; the loop just spins on FRAME_MBX


def settle(n=1, pad=0):
    for _ in range(n):
        run_frame(pad)


def tile(row, col):
    return mem[TILEBUF + row * 32 + col]


def metatile(kind):
    return list(mem[METATILES + kind * 16:METATILES + kind * 16 + 16])


def cellkind(cr, cc):
    """Which metatile occupies viewport cell (cr, cc), or None."""
    block = [tile(cr * 4 + r, cc * 4 + t) for r in range(4) for t in range(4)]
    for k in range(MT_KINDS):
        if block == metatile(k):
            return k
    return None


def frame_rows():
    def ch(k):
        if k is None:
            return "?"
        if k > MT_PLAYER:
            return "p"      # an edge-partition combo cell
        return {MT_FLOOR: ".", MT_WALL: "#", MT_PLAYER: "P",
                MT_EXIT: "E"}[k]
    return ["".join(ch(cellkind(r, c)) for c in range(16)) for r in range(12)]


def find_player():
    for r in range(12):
        for c in range(16):
            if cellkind(r, c) == MT_PLAYER:
                return r, c
    return None


fails = 0


def check(name, cond):
    global fails
    print(("PASS  " if cond else "FAIL  ") + name)
    if not cond:
        fails += 1


# boot: lands on the SPECS TERMINAL (clinical operating card), not the
# card and not the game
for _ in range(400000):
    cpu.step()
check("boot: terminal not game (no frame, no player)",
      mem[DIRTY] == 0 and find_player() is None)
settle(2)                            # first tick paints the terminal
check("terminal: MASTER SYSTEM header", tile(2, 9) == 24)      # M
check("terminal: CPU spec line", tile(5, 6) == 14)             # C of CPU:
check("terminal: specimen shows the map name", tile(11, 20) == 30)  # S
      # (MAP_NAME is a centre-padded 16-byte field: SIMMAZE starts 4 in)
check("terminal: cursor on TEST PATTERN",
      tile(15, 6) == 41 and tile(17, 6) == 0)
check("terminal: ENTER: EXIT footer", tile(21, 10) == 16)      # E
mem[DIRTY] = 0
run_frame(0x02)                      # DOWN: cursor to FIELD MAP
check("terminal: cursor moves to FIELD MAP",
      tile(17, 6) == 41 and tile(15, 6) == 0 and mem[DIRTY] == 1)
run_frame(0x00)
run_frame(0x01)                      # UP: cursor back
check("terminal: cursor returns to TEST PATTERN", tile(15, 6) == 41)
run_frame(0x00)
mem[DIRTY] = 0
run_frame(0x40)                      # A opens DIAGNOSTICS (state 3, in-blob)
run_frame(0x00)
check("diag: page painted (DIAGNOSTICS title)", tile(2, 10) == 15)
check("diag: Z80 owns START while the page is up", mem[STATE] == 1)
run_frame(0x40)                      # hold A: controller-test lamp lights
check("diag: A lamp lights", tile(10, 23) == 12)
run_frame(0x00)
check("diag: A lamp clears", tile(10, 23) == 40)
f1 = tile(6, 14)                     # FRAME hex low digit ticks
run_frame(0x00)
check("diag: FRAME counter is live", tile(6, 14) != f1)
run_frame(0x80)                      # START returns to the terminal
run_frame(0x00)
settle(2)
check("diag -> terminal: header back", tile(2, 9) == 24)
check("diag -> terminal: START ownership returned", mem[STATE] == 0)
check("terminal: stale TILEBUF cleared (no leftovers)",
      all(tile(r, c) == 0 for r in (0, 1, 3, 22, 23) for c in range(32)))
mem[DIRTY] = 0
settle(80)                           # chime completes; terminal is static
check("terminal: no redraw while idle", mem[DIRTY] == 0)
run_frame(0x02)                      # DOWN to FIELD MAP
run_frame(0x00)
run_frame(0x40)                      # A begins the exercise
check("button starts game: player cell at spawn (2,2)",
      mem[DIRTY] == 1 and cellkind(2, 2) == MT_PLAYER)
run_frame(0x00)
check("game: border wall cells drawn", cellkind(0, 0) == MT_WALL
      and cellkind(0, 7) == MT_WALL)
check("game: floor cell drawn", cellkind(1, 1) == MT_FLOOR)
check("game: exit off-screen (the 8x6 viewport shows a map corner)",
      all(cellkind(r, c) != MT_EXIT for r in range(12) for c in range(16)))

mem[DIRTY] = 0                       # 68K consumed the frame
settle(2, 0)                         # idle frames: nothing redraws
check("idle: no spurious redraw", mem[DIRTY] == 0)

run_frame(0x08)                      # RIGHT pressed
check("move right: redraw", mem[DIRTY] == 1)
check("move right: player cell (2,3), old cell back to floor",
      cellkind(2, 3) == MT_PLAYER and cellkind(2, 2) == MT_FLOOR)
mem[DIRTY] = 0

settle(8)                            # the move cooldown (the glide) expires
run_frame(0x01)                      # UP into the wall at y=1... (2,3): up = (1,3) floor!
check("move up: (1,3) is open, player moved", cellkind(1, 3) == MT_PLAYER)
mem[DIRTY] = 0
settle(8)                            # cooldown out of the way: the WALL is the gate
run_frame(0x01)                      # UP again into border wall y=0
check("wall blocks: no redraw", mem[DIRTY] == 0)
check("wall blocks: player still at cell (1,3)", cellkind(1, 3) == MT_PLAYER)

# edge partition: (2,1) is open floor, but the W edge of (3,1) carries a
# slab — LEFT must be blocked by the EDGE, not the cell (no cooldown in
# play: the blocked UP above set none)
run_frame(0x00)
run_frame(0x04)                      # LEFT into the partition
check("partition edge blocks the crossing (cell itself is open)",
      mem[DIRTY] == 0 and cellkind(1, 3) == MT_PLAYER)
# and the slab renders: N edge of (6,4) makes combo cells of both
# neighbours — N-combo (kind 4) below the edge, S-combo (kind 7) above
check("partition renders: N-combo cell at (4,6), S-combo at (3,6)",
      cellkind(4, 6) == 3 + 1 and cellkind(3, 6) == 3 + 4)

# held pacing: hold DOWN for 20 frames — the 8-frame move cooldown means
# steps land at frames 1, 10, 19 (steered off the game's own y var)
VAR_VPY = 0x1CA2
p0 = mem[0x1CA1]
run_frame(0x02)
for _ in range(19):
    run_frame(0x02)
    mem[DIRTY] = 0
p1 = mem[0x1CA1]
check(f"held pacing: DOWN for 20 frames moved y {p0} -> {p1} (cooldown 8)",
      p1 - p0 >= 2)

# walk deep down: the viewport must scroll and clamp at the bottom edge.
# The v4 frame is 8x6 cells, so vp_y = clamp(py-3, 0, 26): at map y=30
# it clamps to 26 and the player sits on screen row 30-26 = 4.
for _ in range(150):
    run_frame(0x02)
    mem[DIRTY] = 0
    run_frame(0)
check(f"viewport scrolled: vp_y={mem[VAR_VPY]}, player on screen row 4",
      mem[VAR_VPY] == 26 and cellkind(4, 3) == MT_PLAYER)

# route to the exit door at (31,16): align y=16 (the bar's gap row), then
# walk right through the gap to x=30, then step INTO the door. Steer off
# the game's own player vars so the route can't drift.
VAR_PX, VAR_PY = 0x1CA0, 0x1CA1


def tap(pad):
    run_frame(pad)
    mem[DIRTY] = 0
    settle(8)                        # drain the move cooldown


for _ in range(40):
    if mem[VAR_PY] == 16:
        break
    tap(0x01 if mem[VAR_PY] > 16 else 0x02)
for _ in range(40):
    if mem[VAR_PX] == 30 or mem[STATE]:
        break
    tap(0x08)
# at (30,16) the viewport clamps right (vp_x = clamp(30-4,0,24) = 24,
# vp_y = clamp(16-3,0,26) = 13): the door at map (31,16) sits on screen
# col 31-24 = 7, the player on col 6, both on row 16-13 = 3
check("exit door cell visible beside the player",
      cellkind(3, 7) == MT_EXIT and cellkind(3, 6) == MT_PLAYER)
tap(0x08)                            # the step into the door itself
check("escape: STATE flipped", mem[STATE] == 1)
settle(150, 0)                       # music keeps playing on the escape screen


# ---- music parity: PSG stream must match the reference engine -----------
# Independent reimplementation of games/maze/game.asm's SPACE-A music (and
# of run_engine_space in sms_liminal_gen.py): the whole point is three
# copies of the algorithm agreeing byte for byte. Echoes use the Z80's
# chorus model (ch1 doubles ch2 a divider sharp); section order per
# frame is the parity contract: bass, phrase select, note fire, melody
# decay, drum fire, drum decay.
def ref_psg(seed, frames):
    s = (seed & 0xFFFF) or 1

    def step():
        nonlocal s
        lsb = s & 1
        s >>= 1
        if lsb:
            s ^= 0xB400
        return s

    MOTIFS = [[285, 240, 254, 285], [190, 214, 240, 285],
              [143, 190, 240], [254, 240, 285, 190]]
    BASS = [762, 855, 960, 762]
    BT = [330 + i * 30 for i in range(8)]
    MGAP = [75 + i * 30 for i in range(8)]
    # drum march (ctrl, att, wait): LFSR-free two-bar cadence on ch3,
    # mirrors drum_pat in game.asm byte for byte
    DRUMS = [(0xE6, 4, 45), (0xE6, 6, 39), (0xE6, 8, 3), (0xE6, 8, 3),
             (0xE6, 4, 45), (0xE6, 6, 45), (0xE6, 4, 45), (0xE6, 6, 39),
             (0xE6, 8, 3), (0xE6, 8, 3), (0xE6, 4, 45), (0xE6, 6, 18),
             (0xE6, 8, 27)]

    def tone(base, d):
        return [base | (d & 0xF), (d >> 4) & 0x3F]

    out = [0x9F, 0xBF, 0xDF, 0xFF]               # boot silence
    # chime (frames 0-106): REVERSED SE-GA (sms_putrats) — the chord
    # swells in from silence (0-36), holds (37-81), sweeps DOWN the
    # table (82-105), and cuts (106). Engine ticks from frame 107.
    CH = [(0x80, 1023, 19, 571), (0xA0, 762, 16, 381), (0xC0, 570, 12, 285)]
    b_i, b_att, b_fade, b_t = 0, 15, 0, 0
    motif, mi = [], 0
    m_att, m_fade, note_t, gap = 15, 0, 0, 0
    d_i, d_t, d_att, d_fade = 0, 0, 15, 0
    ch_att = 15
    for f in range(frames):
        if f < 107:
            if f == 0:
                for base, _, _, tgt in CH:
                    out += tone(base, tgt)
            if f <= 36 and f % 3 == 0:
                ch_att -= 1
                out += [0x90 | ch_att, 0xB0 | ch_att, 0xD0 | ch_att]
            elif 82 <= f <= 105:
                i = 105 - f
                for base, start, stp, _ in CH:
                    out += tone(base, start - stp * i)
            elif f == 106:
                out += [0x9F, 0xBF, 0xDF]
            continue
        if b_t == 0:
            d = BASS[b_i & 3]
            b_i += 1 if (step() & 3) else 2
            out += tone(0x80, d)
            out.append(0x90 | 6)
            b_att, b_fade = 6, 30
            b_t = BT[step() & 7]
        b_t -= 1
        if b_att < 15:
            b_fade -= 1
            if b_fade == 0:
                b_fade = 30
                b_att += 1
                out.append(0x90 | b_att)
        if mi >= len(motif):
            if gap == 0:
                motif = MOTIFS[step() & 3]
                mi, note_t = 0, 0
                gap = MGAP[step() & 7]
            else:
                gap -= 1
        if mi < len(motif) and note_t == 0:
            d = motif[mi]
            mi += 1
            out += tone(0xC0, d)
            out.append(0xD0 | 2)
            out += tone(0xA0, d + 1)       # chorus double, one divider sharp
            out.append(0xB0 | 3)
            m_att, m_fade = 2, 6
            note_t = 13 + (step() & 7)
        elif note_t > 0:
            note_t -= 1
        if m_att < 15:
            m_fade -= 1
            if m_fade == 0:
                m_fade = 6
                m_att += 1
                out.append(0xD0 | m_att)
                out.append(0xB0 | min(15, m_att + 1))
        if d_t == 0:
            ctrl, att, wait = DRUMS[d_i]
            d_i = (d_i + 1) % len(DRUMS)
            out.append(ctrl)
            out.append(0xF0 | att)
            d_att, d_fade = att, 2
            d_t = wait
        d_t -= 1
        if d_att < 15:
            d_fade -= 1
            if d_fade == 0:
                d_fade = 2
                d_att += 1
                out.append(0xF0 | d_att)
    return out


m_seed = 0xACE1 ^ 0x0202 ^ 0x101F   # ^ (spawn_y:x) ^ (exit_y:x) of this maze
expected = ref_psg(m_seed, FRAME_COUNT[0])
check(f"music parity: {len(cpu.psg)} PSG writes match reference byte-for-byte",
      cpu.psg == expected)
check("music: melody onsets occurred",
      sum(1 for b in cpu.psg if b & 0xF0 == 0xC0) >= 1)
check("music: drum taps on the noise channel",
      sum(1 for b in cpu.psg if b == 0xE6) >= 2)
# every melody attack ($D2) must be doubled by a chorus attack ($B3) —
# the ch1 detune is the instrument, not a garnish
check("music: chorus doubles every melody onset",
      sum(1 for b in cpu.psg if b == 0xB3) ==
      sum(1 for b in cpu.psg if b == 0xD2) >= 1)
esc = "".join(chr(t - 12 + ord('A')) if 12 <= t <= 37 else ' '
              for t in mem[TILEBUF + 8 * 32:TILEBUF + 8 * 32 + 32])
check(f"debrief: 'EXERCISE COMPLETE' drawn ({esc.strip()!r})",
      "EXERCISECOMPLETE" in esc.replace(" ", ""))
nm = "".join(chr(t - 12 + ord('A')) if 12 <= t <= 37 else ' '
             for t in mem[TILEBUF + 11 * 32:TILEBUF + 11 * 32 + 32])
check(f"debrief: level name stamped ({nm.strip()!r})",
      "SIMMAZE" in nm.replace(" ", ""))
settle(3, 0x08)
check("escaped: further input ignored", mem[STATE] == 1)

# START from the debrief returns to the SPECS TERMINAL (the layered exit:
# this first START belongs to the Z80; the second, seen at the terminal,
# is the 32X's session exit — out of this sim's jurisdiction).
run_frame(0x00)
run_frame(0x80)                      # START edge
run_frame(0x00)
check("debrief: START drops STATE (68K flag falls with it)", mem[STATE] == 0)
settle(2)
check("debrief -> terminal: specs header repainted", tile(2, 9) == 24)
check("debrief -> terminal: cursor home on DIAGNOSTIC", tile(15, 6) == 41)

if fails:
    print("\n".join(frame_rows()))
    sys.exit(f"{fails} FAILURES")
print("all good")
