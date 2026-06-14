#!/usr/bin/env python3
"""Generate a hand-tuned 32x32 Backrooms-style floor plan.

Designed to sell the "AI-generated dreamlike rooms" feel within
the realistic perf / memory budget of the 32X (1024 bytes of map).

ZONES (all meeting at central spawn so you immediately see variety):

  +---------+----------+
  | Office  |  Nested  |   NW: tight cubicles (Level 0 office feel)
  | (NW)    |  (NE)    |   NE: room-within-room-within-room
  +---------+----------+   (the iconic "doorway into another doorway" shot)
  | Maze    |  Lounge  |   SW: twisty dead-end maze
  | (SW)    |  (SE)    |   SE: open lounge with scattered pillars
  +---------+----------+

Spawn: row 28, col 16, facing north (angle 192). Player looks up the
spine corridor for the classic "infinite corridor" first impression.

Usage:
    python3 tools/gen_backrooms_map.py
        # prints ASCII map + a C array ready to paste into raycast.c
"""

W = H = 32
WALL = 1
FLOOR = 0

g = [[WALL] * W for _ in range(H)]


def carve_rect(x0, y0, x1, y1):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if 0 <= x < W and 0 <= y < H:
                g[y][x] = FLOOR


def wall_h(y, x0, x1):
    for x in range(x0, x1 + 1):
        if 0 <= x < W and 0 <= y < H:
            g[y][x] = WALL


def wall_v(x, y0, y1):
    for y in range(y0, y1 + 1):
        if 0 <= x < W and 0 <= y < H:
            g[y][x] = WALL


def door(x, y):
    g[y][x] = FLOOR


def pillar(x, y):
    g[y][x] = WALL


# ---- Step 1: open the whole interior ---------------------------------
carve_rect(1, 1, 30, 30)


# ---- Step 2: NW office cubicles (rows 1-8, cols 1-14) ----------------
# Three vertical dividers split this zone into four 3-cell-wide cubicles.
# One horizontal divider at row 4 cuts each into a north + south pair.
for x in (4, 8, 12):
    wall_v(x, 1, 8)
wall_h(4, 1, 14)

# Cubicle doorways — irregular positions so each cubicle feels different.
door(2, 4)     # north-west cubicle pair
door(6, 4)
door(11, 4)
door(4, 2)
door(4, 7)
door(8, 6)
door(12, 3)
door(12, 7)
# Opening from office zone into central corridor (south side)
door(2, 8); door(6, 8); door(10, 8); door(14, 5)


# ---- Step 3: NE nested rooms (rows 1-8, cols 17-30) ------------------
# Outer "room" is bounded by the perimeter; we add an inner room and
# an innermost room concentric inside it.
# Inner room (rows 2-7, cols 18-29)
for x in range(18, 30):
    g[2][x] = WALL
    g[7][x] = WALL
for y in range(2, 8):
    g[y][18] = WALL
    g[y][29] = WALL
door(23, 7)    # south doorway out of inner
door(18, 5)    # west doorway

# Innermost room (rows 4-6, cols 21-26)
for x in range(21, 27):
    g[4][x] = WALL
    g[6][x] = WALL
for y in range(4, 7):
    g[y][21] = WALL
    g[y][26] = WALL
door(24, 6)    # south doorway from innermost


# ---- Step 4: divider between N zones and center (row 9) --------------
wall_h(9, 1, 30)
# Strategic openings — one per zone + the spine
door(2, 9)     # west edge into office
door(14, 9)    # central-west passage
door(16, 9)    # spine corridor
door(23, 9)    # central-east passage
door(28, 9)    # east edge into nested zone


# ---- Step 5: central corridor band (rows 10-15) ----------------------
# Mostly open but with a couple of pillar islands to break the sightline
# (Backrooms-style "is that something standing there?" moments).
pillar(8, 12); pillar(9, 12); pillar(8, 13); pillar(9, 13)
pillar(22, 12); pillar(23, 12); pillar(22, 13); pillar(23, 13)

# Vertical stub walls — give the central band a little structure so it
# doesn't read as "one big open box".
wall_v(14, 10, 14)
wall_v(17, 10, 14)
door(14, 12)
door(17, 13)


# ---- Step 6: divider between center and S (row 16) -------------------
wall_h(16, 1, 30)
door(2, 16)
door(8, 16)
door(15, 16); door(16, 16)   # spine corridor (2-wide here)
door(22, 16)
door(28, 16)


# ---- Step 7: SW twisty maze (rows 17-30, cols 1-14) ------------------
# Series of partial walls forming irregular corridors. Designed by hand
# so the player faces real choices at each junction.
maze_segments = [
    # (axis, x0/y0, span_low, span_high) drawn as walls
    # horizontals
    ('h', 18, 2, 6),
    ('h', 18, 9, 13),
    ('h', 20, 4, 11),
    ('h', 22, 2, 4),
    ('h', 22, 7, 12),
    ('h', 24, 5, 9),
    ('h', 26, 2, 6),
    ('h', 26, 10, 13),
    ('h', 28, 4, 7),
    ('h', 28, 11, 13),
    # verticals
    ('v', 4, 23, 28),
    ('v', 6, 19, 21),
    ('v', 9, 25, 28),
    ('v', 11, 18, 21),
    ('v', 13, 22, 25),
]
for seg in maze_segments:
    if seg[0] == 'h':
        wall_h(seg[1], seg[2], seg[3])
    else:
        wall_v(seg[1], seg[2], seg[3])


# ---- Step 8: SE lounge with scattered pillars (rows 17-30, cols 17-30)
# Re-carve to make sure (most of it is already open) and place irregular
# pillars — single wall cells that read as columns / load-bearing posts.
carve_rect(17, 17, 30, 30)
irregular_pillars = [
    (19, 18), (22, 19), (26, 18), (28, 20),
    (19, 22), (23, 21), (27, 23),
    (20, 25), (24, 24), (28, 26),
    (21, 28), (25, 27), (29, 29),
]
for px, py in irregular_pillars:
    if px < 31 and py < 31:
        pillar(px, py)

# A couple of short wall stubs that "don't make sense" — purely for
# that uncanny "why is this here" feel.
wall_h(20, 17, 18)
wall_v(24, 17, 19)
wall_h(27, 19, 21)


# ---- Step 9: spawn corridor (col 16 from row 17 to row 28) -----------
# Tight 1-cell-wide corridor flanked by walls. Long N-S sightline for
# the iconic "infinite hall" first frame.
for y in range(17, 29):
    g[y][15] = WALL
    g[y][17] = WALL
    g[y][16] = FLOOR
# A couple of side doors off the spawn corridor
door(15, 20)   # west door
door(17, 23)   # east door
door(15, 26)   # west door


# ---- Output ----------------------------------------------------------
print("# Backrooms 32x32 hand-tuned floor plan\n")
print("    " + "".join(str(i // 10 % 10) for i in range(W)))
print("    " + "".join(str(i % 10) for i in range(W)))
for j, row in enumerate(g):
    print(f"{j:3d} " + "".join("#" if c == WALL else "." for c in row))

print()
print(f"# C array for raycast world_map:")
print(f"const uint8_t world_map[{H}][{W}] = {{")
for row in g:
    print("    {" + ",".join(str(c) for c in row) + "},")
print("};")
