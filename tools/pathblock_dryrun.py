#!/usr/bin/env python3
# Copyright (C) 2026 GrizzlyOne95
#
# This file is part of Extra Utilities.
#
# Extra Utilities is free software: you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
"""Offline mirror of exu.pathing's face footprint (src/Game/PathBlockMath.h).

Loads an object's SDF hierarchy and its LOD0 GEO files from a folder, places
the object on a path grid at a yaw and offset, and prints the ASCII mask the
native code would write: '#' blocked (inside solid at either sample height),
'.' open cell inside the oriented bbox, ' ' outside the bbox. The cell maths
(floor(x / size), centre (i + 0.5) * size) and the nonzero-winding test are
the same as the C++; see Docs/PATH_BLOCK.md.

    python tools/pathblock_dryrun.py <folder> <name> [--yaw 0,30,45] [--offset 0,0]
        [--cell 10] [--h1 1.5] [--h2 3.0]
"""
from __future__ import annotations

import argparse
import math
import os
import struct
import sys

SDF_RECORD = 120


def read_sdf(path):
    """Band-0 records: (name, parent, 3x4 local matrix as right, up, front, posit)."""
    data = open(path, "rb").read()
    tag, _, count = struct.unpack_from("<4sIi", data, 98)
    if tag != b"SGEO":
        raise ValueError(f"{path}: no SGEO chunk at 98")
    recs = []
    for i in range(count):
        o = 110 + i * SDF_RECORD
        name = data[o:o + 8].split(b"\0")[0].decode("ascii", "replace")
        m = struct.unpack_from("<12f", data, o + 8)
        parent = data[o + 56:o + 64].split(b"\0")[0].decode("ascii", "replace")
        recs.append((name, parent, m))
    return recs


def read_geo(path):
    """Vertices and polygon index lists of a BZ1 GEO file."""
    d = open(path, "rb").read()
    _, _, _, nv, nf, _ = struct.unpack_from("<4si16siii", d, 0)
    verts = [struct.unpack_from("<3f", d, 36 + 12 * i) for i in range(nv)]
    o = 36 + 24 * nv
    polys = []
    for _ in range(nf):
        f = struct.unpack_from("<iiBBBffffi3s13sii", d, o)
        o += 55
        n = f[1]
        idx = [struct.unpack_from("<iiff", d, o + 16 * k)[0] for k in range(n)]
        o += 16 * n
        polys.append(idx)
    return verts, polys


def mat_apply(m, v):
    r, u, f, p = m
    return tuple(v[0] * r[i] + v[1] * u[i] + v[2] * f[i] + p[i] for i in range(3))


def mat_rot(m, v):
    r, u, f, _ = m
    return tuple(v[0] * r[i] + v[1] * u[i] + v[2] * f[i] for i in range(3))


def mat_then(a, b):
    """Apply a, then b (child local, then parent)."""
    return (mat_rot(b, a[0]), mat_rot(b, a[1]), mat_rot(b, a[2]), mat_apply(b, a[3]))


IDENTITY = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, 0.0))


def object_triangles(folder, name):
    """Root-local triangles (the root GEO's own matrix is the identity, as in get_obj_bounding_box)."""
    recs = read_sdf(os.path.join(folder, name + ".sdf"))
    by_name = {r[0].lower(): r for r in recs}
    cache = {}

    def world(rec):
        key = rec[0].lower()
        if key in cache:
            return cache[key]
        m = rec[2]
        local = (m[0:3], m[3:6], m[6:9], m[9:12])
        parent = by_name.get(rec[1].lower())
        if parent is None:
            out = IDENTITY
        else:
            out = mat_then(local, world(parent))
        cache[key] = out
        return out

    tris = []
    for rec in recs:
        path = os.path.join(folder, rec[0] + ".geo")
        if not os.path.exists(path):
            continue
        m = world(rec)
        verts, polys = read_geo(path)
        wv = [mat_apply(m, v) for v in verts]
        for p in polys:
            for k in range(1, len(p) - 1):
                tris.append((wv[p[0]], wv[p[k]], wv[p[k + 1]]))
    return tris


def owns_edge(du, dv):
    return dv > 0.0 or (dv == 0.0 and du < 0.0)


def winding(tris, x, y, z):
    """Nonzero-winding count along local +X from (x, y, z); mirrors PathBlockMath::WindingAt."""
    w = 0
    for A, B, C in tris:
        ay, az, by, bz, cy, cz = A[1], A[2], B[1], B[2], C[1], C[2]
        area = (by - ay) * (cz - az) - (bz - az) * (cy - ay)
        if area == 0.0:
            continue
        P, Q, R = (A, B, C) if area > 0.0 else (A, C, B)
        py, pz, qy, qz, ry, rz = P[1], P[2], Q[1], Q[2], R[1], R[2]
        ok = True
        ws = []
        for (sy, sz, ey, ez) in ((qy, qz, ry, rz), (ry, rz, py, pz), (py, pz, qy, qz)):
            e = (ey - sy) * (z - sz) - (ez - sz) * (y - sy)
            if e < 0.0 or (e == 0.0 and not owns_edge(ey - sy, ez - sz)):
                ok = False
                break
            ws.append(e)
        if not ok:
            continue
        hx = (ws[0] * P[0] + ws[1] * Q[0] + ws[2] * R[0]) / abs(area)
        if hx > x:
            w += 1 if area < 0.0 else -1
    return w


def bbox(tris):
    lo = [min(t[k][i] for t in tris for k in range(3)) for i in range(3)]
    hi = [max(t[k][i] for t in tris for k in range(3)) for i in range(3)]
    return lo, hi


def mask(tris, yaw_deg, ox, oz, cell, h1, h2):
    lo, hi = bbox(tris)
    c, s = math.cos(math.radians(yaw_deg)), math.sin(math.radians(yaw_deg))
    # BZ yaw: right = (c, 0, -s), front = (s, 0, c); world = right * lx + front * lz + pos
    rx, rz, fx, fz = c, -s, s, c
    corners = [(lo[0], lo[2]), (hi[0], lo[2]), (hi[0], hi[2]), (lo[0], hi[2])]
    wc = [(rx * a + fx * b + ox, rz * a + fz * b + oz) for a, b in corners]
    x0, x1 = min(p[0] for p in wc), max(p[0] for p in wc)
    z0, z1 = min(p[1] for p in wc), max(p[1] for p in wc)
    i0, i1 = math.floor(x0 / cell), math.floor(x1 / cell)
    j0, j1 = math.floor(z0 / cell), math.floor(z1 / cell)
    det = rx * fz - fx * rz
    rows, inbox, blocked = [], 0, 0
    for j in range(j1, j0 - 1, -1):          # +Z up the page
        row = ""
        for i in range(i0, i1 + 1):
            wx, wz = (i + 0.5) * cell - ox, (j + 0.5) * cell - oz
            lx = (fz * wx - fx * wz) / det
            lz = (-rz * wx + rx * wz) / det
            if not (lo[0] < lx < hi[0] and lo[2] < lz < hi[2]):
                row += " "
                continue
            inbox += 1
            solid = winding(tris, lx, h1, lz) != 0 or winding(tris, lx, h2, lz) != 0
            blocked += solid
            row += "#" if solid else "."
        rows.append(row)
    return rows, inbox, blocked, (lo, hi)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder")
    ap.add_argument("name")
    ap.add_argument("--yaw", default="0,30,45,90")
    ap.add_argument("--offset", action="append", default=None, help="x,z (repeatable)")
    ap.add_argument("--cell", type=float, default=10.0)
    ap.add_argument("--h1", type=float, default=1.5)
    ap.add_argument("--h2", type=float, default=3.0)
    a = ap.parse_args(argv)
    tris = object_triangles(a.folder, a.name)
    lo, hi = bbox(tris)
    print(f"{a.name}: {len(tris)} triangles, root-local bbox "
          f"x {lo[0]:.2f}..{hi[0]:.2f}  y {lo[1]:.2f}..{hi[1]:.2f}  z {lo[2]:.2f}..{hi[2]:.2f}")
    offsets = a.offset or ["0,0", "5,5"]
    for yaw in [float(v) for v in a.yaw.split(",")]:
        for off in offsets:
            ox, oz = (float(v) for v in off.split(","))
            rows, inbox, blocked, _ = mask(tris, yaw, ox, oz, a.cell, a.h1, a.h2)
            print(f"\nyaw {yaw:g} deg, origin ({ox:g}, {oz:g}), cell {a.cell:g} m: "
                  f"{inbox} cells in bbox, {blocked} blocked")
            for r in rows:
                print("  |" + r + "|")
    return 0


if __name__ == "__main__":
    sys.exit(main())
