"""Void-and-cluster (Ulichney) blue-noise threshold matrix.
Ordered Bayer mixes on a regular lattice, which reads as speckle once a
large area sits in a sustained 50/50 mix. Blue noise has the same mean
but no low-frequency structure, so the same mix reads as fine grain."""
import math, random
N = 16
SIG = 1.5
random.seed(7)

def gauss_filter(b):
    out = [[0.0]*N for _ in range(N)]
    R = 4
    for y in range(N):
        for x in range(N):
            if not b[y][x]: continue
            for dy in range(-R, R+1):
                for dx in range(-R, R+1):
                    w = math.exp(-(dx*dx+dy*dy)/(2*SIG*SIG))
                    out[(y+dy) % N][(x+dx) % N] += w
    return out

def tightest(b, f, want):
    best, bv = None, None
    for y in range(N):
        for x in range(N):
            if b[y][x] != want: continue
            v = f[y][x]
            if bv is None or (v > bv if want else v < bv):
                bv, best = v, (x, y)
    return best

b = [[0]*N for _ in range(N)]
ones = 0
while ones < N*N//10:
    x, y = random.randrange(N), random.randrange(N)
    if not b[y][x]: b[y][x] = 1; ones += 1

while True:                                   # break up the initial clusters
    f = gauss_filter(b)
    cx, cy = tightest(b, f, 1); b[cy][cx] = 0
    f = gauss_filter(b)
    vx, vy = tightest(b, f, 0); b[vy][vx] = 1
    if (vx, vy) == (cx, cy): break

proto = [row[:] for row in b]
rank = [[0]*N for _ in range(N)]

work = [row[:] for row in proto]              # phase 1: remove, ranks down
r = ones - 1
while r >= 0:
    f = gauss_filter(work)
    x, y = tightest(work, f, 1)
    work[y][x] = 0; rank[y][x] = r; r -= 1

work = [row[:] for row in proto]              # phase 2+3: add, ranks up
r = ones
while r < N*N:
    f = gauss_filter(work)
    x, y = tightest(work, f, 0)
    work[y][x] = 1; rank[y][x] = r; r += 1

print("static const uint8_t hole_bayer[256] = {")
for y in range(N):
    row = ", ".join("%3d" % (rank[y][x] * 256 // (N*N)) for x in range(N))
    print("    %s," % row)
print("};")
