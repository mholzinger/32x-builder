#!/usr/bin/env python3
# Offline sim of procgen carve_open_field — replicates the C exactly to
# eyeball the organic footprint shapes and prove spawn-connectivity before
# any ROM build. Mirrors sh_src/procgen.c.
from collections import deque
MAP_W = MAP_H = 32
SPAWN_CX, SPAWN_CY = 16, 28

_state = 1
def xs32():
    global _state
    x = _state & 0xFFFFFFFF
    x ^= (x << 13) & 0xFFFFFFFF
    x ^= (x >> 17)
    x ^= (x << 5) & 0xFFFFFFFF
    _state = x & 0xFFFFFFFF
    return _state
def xs32_range(lo, hi):
    if hi <= lo: return lo
    return lo + xs32() % (hi - lo + 1)

def carve_room(m, x, y, w, h):
    for j in range(h):
        for i in range(w):
            cx, cy = x+i, y+j
            if 0 <= cx < MAP_W and 0 <= cy < MAP_H:
                m[cy][cx] = 0

def overlaps_open(m, x, y, w, h):
    n = 0
    for j in range(h):
        for i in range(w):
            cx, cy = x+i, y+j
            if 0 <= cx < MAP_W and 0 <= cy < MAP_H and m[cy][cx] == 0:
                n += 1
    return n

def carve_open_field(m):
    sw, sh = xs32_range(8,12), xs32_range(8,11)
    sx = SPAWN_CX - sw//2
    sy = SPAWN_CY - sh + 1
    if sx < 2: sx = 2
    if sx + sw > MAP_W - 2: sx = MAP_W - 2 - sw
    if sy < 2: sy = 2
    carve_room(m, sx, sy, sw, sh)
    target = xs32_range(8,13)
    placed, attempts = 0, target*28
    while attempts > 0 and placed < target:
        attempts -= 1
        roll = xs32() % 100
        if roll < 35:
            if xs32() & 1: bw, bh = xs32_range(9,16), xs32_range(2,3)
            else:          bw, bh = xs32_range(2,3),  xs32_range(9,16)
        elif roll < 55:
            bw, bh = xs32_range(3,5), xs32_range(3,5)
        else:
            bw, bh = xs32_range(5,12), xs32_range(5,12)
        bx = xs32_range(2, MAP_W-3-bw)
        by = xs32_range(2, MAP_H-3-bh)
        ov = overlaps_open(m, bx, by, bw, bh)
        if ov < 2 or ov > bw*bh - 2: continue
        carve_room(m, bx, by, bw, bh)
        placed += 1

def connectivity(m):
    total = sum(r.count(0) for r in m)
    seen = [[False]*MAP_W for _ in range(MAP_H)]
    q = deque([(SPAWN_CX, SPAWN_CY)]); seen[SPAWN_CY][SPAWN_CX] = True
    reach = 0
    while q:
        x, y = q.popleft()
        if m[y][x] != 0: continue
        reach += 1
        for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
            nx, ny = x+dx, y+dy
            if 0<=nx<MAP_W and 0<=ny<MAP_H and not seen[ny][nx] and m[ny][nx]==0:
                seen[ny][nx] = True; q.append((nx,ny))
    return reach, total

def show(seed):
    global _state
    _state = seed if seed else 1
    for _ in range(8): xs32()          # warmup, matches procgen_run
    m = [[1]*MAP_W for _ in range(MAP_H)]
    carve_open_field(m)
    reach, total = connectivity(m)
    print(f"--- seed {seed}: open={total} reachable-from-spawn={reach} "
          f"{'OK' if reach==total else '*** DISCONNECTED ***'}")
    for y in range(MAP_H):
        row = ""
        for x in range(MAP_W):
            if (x,y) == (SPAWN_CX,SPAWN_CY): row += "S"
            else: row += "." if m[y][x]==0 else "#"
        print(row)
    print()

for s in (1, 7, 12345, 0xCAFE, 99, 424242):
    show(s)
