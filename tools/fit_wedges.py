#!/usr/bin/env python3
"""Fit a GLB's silhouette with a short stack of WEDGES (cbox_t.taper).

bake_boxes.py voxelizes and merges greedy AABBs, which is right for a desk
(slabs and pedestals) and wrong for anything sloped — a shell that leans comes
back as a staircase. This fits the same shape the way the wedge primitive
wants: slice the mesh at N+1 heights, and make each band the wedge whose
bottom rectangle is the lower slice and whose top rectangle is the upper one.

Two bands already reproduce a console that stands vertical to mid-height and
then slopes in: band 0 comes out with near-identical rectangles (a plain box)
and band 1 carries the taper. No voxel grid, no Blender — the cross-sections
come from intersecting triangle edges with a plane, so the fit is exact at
every slice height it is given.

    tools/fit_wedges.py models/sega_master_system.glb [bands] [material]

Prints the boxes normalized to height 1.0 and the fit error. Import
fit_wedges() to place them (tools/compose_desk_pvm.py does).
"""
import json
import os
import struct
import sys

_CT = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2),
       5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
_NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}


def _ident():
    return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def _mul(A, B):
    """Column-major 4x4 multiply, glTF's storage order."""
    R = [0.0] * 16
    for c in range(4):
        for r in range(4):
            R[c * 4 + r] = sum(A[k * 4 + r] * B[c * 4 + k] for k in range(4))
    return R


def _node_matrix(nd):
    """A node is EITHER a `matrix` or a TRS triple. Reading only TRS silently
    drops the matrix form, and this very model hangs its non-uniform scale
    (16.45 x 1 x 8.34) on a matrix node — skipping it imports the console at
    the wrong proportions in both width and depth."""
    if 'matrix' in nd:
        return list(nd['matrix'])
    M = _ident()
    s, r, t = nd.get('scale'), nd.get('rotation'), nd.get('translation')
    if s:
        M = [s[0], 0, 0, 0, 0, s[1], 0, 0, 0, 0, s[2], 0, 0, 0, 0, 1]
    if r:
        x, y, z, w = r
        M = _mul([1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
                  2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
                  2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
                  0, 0, 0, 1], M)
    if t:
        M = _mul([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, t[0], t[1], t[2], 1], M)
    return M


def load_glb(path, material=None):
    """Triangles in glTF world space (Y-up, which is already engine model
    space). `material` keeps only primitives with that material name — the
    console's red top label is a separate material and is not part of the
    silhouette."""
    raw = open(path, 'rb').read()
    total = struct.unpack_from('<I', raw, 8)[0]
    off, chunks = 12, []
    while off < total:
        clen, ctype = struct.unpack_from('<II', raw, off)
        off += 8
        chunks.append((ctype, raw[off:off + clen]))
        off += clen
    g = json.loads(chunks[0][1].decode('utf-8'))
    blob = chunks[1][1]

    def acc(i):
        a = g['accessors'][i]
        bv = g['bufferViews'][a['bufferView']]
        fmt, sz = _CT[a['componentType']]
        n = _NC[a['type']]
        base = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        stride = bv.get('byteStride') or sz * n
        return [struct.unpack_from('<' + fmt * n, blob, base + k * stride)
                for k in range(a['count'])]

    verts, tris = [], []

    def xf(M, p):
        return tuple(M[i] * p[0] + M[4 + i] * p[1] + M[8 + i] * p[2] + M[12 + i]
                     for i in range(3))

    def walk(ni, M):
        nd = g['nodes'][ni]
        M = _mul(M, _node_matrix(nd))
        if 'mesh' in nd:
            for prim in g['meshes'][nd['mesh']]['primitives']:
                if material is not None and 'material' in prim:
                    if g['materials'][prim['material']].get('name') != material:
                        continue
                b = len(verts)
                verts.extend(xf(M, p) for p in acc(prim['attributes']['POSITION']))
                idx = [i[0] for i in acc(prim['indices'])]
                tris.extend((b + idx[k], b + idx[k + 1], b + idx[k + 2])
                            for k in range(0, len(idx) - 2, 3))
        for c in (nd.get('children') or []):
            walk(c, M)

    for sc in g['scenes']:
        for ni in sc['nodes']:
            walk(ni, _ident())
    if not tris:
        raise SystemExit("no triangles%s in %s" %
                         (" for material %r" % material if material else "", path))
    return verts, tris


def section(verts, tris, y):
    """Exact cross-section extents at height y: every triangle edge that
    straddles the plane contributes its crossing point. Vertex-binning was the
    obvious alternative and it lies near a flat top, where a whole face sits at
    one height and the band above it holds no vertices at all."""
    xs, zs = [], []
    for a, b, c in tris:
        for p, q in ((verts[a], verts[b]), (verts[b], verts[c]), (verts[c], verts[a])):
            if p[1] == q[1] or (p[1] - y) * (q[1] - y) > 0:
                continue
            t = (y - p[1]) / (q[1] - p[1])
            if 0.0 <= t <= 1.0:
                xs.append(p[0] + t * (q[0] - p[0]))
                zs.append(p[2] + t * (q[2] - p[2]))
    if not xs:
        return None
    return (min(xs), max(xs), min(zs), max(zs))


def fit_wedges(path, bands=2, material=None, eps=1e-3):
    """Wedge stack normalized to height 1.0 and centred on the model's own
    x/z footprint centre. Returns (boxes, width, depth) — boxes as
    (x0, y0, z0, x1, y1, z1, tx0, tz0, tx1, tz1, taper), the cbox_t order.

    Slices sit just inside each band edge: exactly at a boundary the plane can
    graze a horizontal face and pick up that whole face's extent instead of the
    solid's.
    """
    verts, tris = load_glb(path, material)
    ys = [p[1] for p in verts]
    y_lo, y_hi = min(ys), max(ys)
    H = y_hi - y_lo
    if H <= 0:
        raise SystemExit("degenerate model height in " + path)
    s = 1.0 / H
    inset = H * eps

    cuts = []
    for i in range(bands + 1):
        y = y_lo + H * i / bands
        y = min(max(y, y_lo + inset), y_hi - inset)
        sec = section(verts, tris, y)
        if sec is None:
            raise SystemExit("empty cross-section at y=%.4f" % y)
        cuts.append(sec)

    all_x = [v for c in cuts for v in c[0:2]]
    all_z = [v for c in cuts for v in c[2:4]]
    cx = (min(all_x) + max(all_x)) * 0.5
    cz = (min(all_z) + max(all_z)) * 0.5

    def nx(v):
        return (v - cx) * s

    def nz(v):
        return (v - cz) * s

    boxes = []
    for i in range(bands):
        lo, hi = cuts[i], cuts[i + 1]
        y0 = (y_lo + H * i / bands - y_lo) * s
        y1 = (y_lo + H * (i + 1) / bands - y_lo) * s
        b = [nx(lo[0]), y0, nz(lo[2]), nx(lo[1]), y1, nz(lo[3]),
             nx(hi[0]), nz(hi[2]), nx(hi[1]), nz(hi[3])]
        # taper only when the top rectangle actually differs — a band that
        # comes back square is a plain box, and the flag says so.
        flat = all(abs(b[6 + k] - b[(0, 2, 3, 5)[k]]) < 1e-4 for k in range(4))
        b.append(0 if flat else 1)
        boxes.append(tuple(b))
    return boxes, (max(all_x) - min(all_x)) * s, (max(all_z) - min(all_z)) * s


def _main():
    path = sys.argv[1] if len(sys.argv) > 1 else "models/sega_master_system.glb"
    bands = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    material = sys.argv[3] if len(sys.argv) > 3 else None
    if not os.path.isabs(path):
        path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), path)
    boxes, w, d = fit_wedges(path, bands, material)
    print("normalized to height 1.0:  W %.4f  D %.4f   (W:H:D = %.3f : 1 : %.3f)"
          % (w, d, w, d))
    for i, b in enumerate(boxes):
        print("  band %d taper=%d" % (i, b[10]))
        print("    base x %+.4f..%+.4f  z %+.4f..%+.4f   y %.4f..%.4f"
              % (b[0], b[3], b[2], b[5], b[1], b[4]))
        if b[10]:
            print("    top  x %+.4f..%+.4f  z %+.4f..%+.4f"
                  % (b[6], b[8], b[7], b[9]))


if __name__ == "__main__":
    _main()
