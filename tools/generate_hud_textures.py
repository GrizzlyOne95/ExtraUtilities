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
"""Generate the textures for EXU's scripted ring gauge (Workshop/exu_hud.material).

``exu_hud_ring_segment.png`` is one annular segment of a 10-per-quarter ring,
the geometry of Battlezone 2's status ring: 9 degrees wide, inner radius 0.6 of
the outer. It is centred on "straight up" in a square texture whose centre is
the ring centre, so rotating the texture about its centre (Ogre's texture
rotation does exactly that) places it at any angle. The segment is white with
shaped alpha and rounded, antialiased corners; the gap between neighbours
comes from the inset, as BZ2's rounded ``gauge.tga`` produced it.

``exu_hud_status_plate.png`` is the backplate of the BZ2-demo status
cluster: a navy panel with a light-blue rounded bezel whose left side follows
the ring, a slanted right edge and a dark-red divider on the centre line. It
is drawn in ring units (vertical ring radius 1, horizontal ``PLATE_ASPECT``)
and its extents are mirrored by ``PLATE`` in Workshop/exu_ring_gauge.lua.

Usage:

    python tools/generate_hud_textures.py            # write Workshop/*.png
    python tools/generate_hud_textures.py --check    # verify they are current
"""

from __future__ import annotations

import argparse
import math
import os
import sys

try:
    import numpy as np
    from PIL import Image
except ImportError as exc:  # pragma: no cover - environment problem, not logic
    sys.stderr.write("generate_hud_textures.py needs numpy and Pillow: {}\n".format(exc))
    raise SystemExit(2)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT_DIR = os.path.join(ROOT, "Workshop")

SEGMENT_SIZE = 512
SEGMENT_DEGREES = 9.0          # 90 degrees / 10 segments
INNER_RATIO = 0.6              # BZ2: inner radius 0.6, outer 1.0
GAP_PIXELS = 3.0               # gap between neighbours, in texture pixels
CORNER_PIXELS = 4.0            # corner rounding radius, in texture pixels
EDGE_MARGIN = 2.0              # keep the outer edge off the texture border

# Status plate, in units of the ring's vertical radius. Keep in step with
# PLATE in Workshop/exu_ring_gauge.lua.
PLATE_SIZE = (1024, 512)
PLATE_ASPECT = 2.2             # horizontal ring radius / vertical
PLATE_PAD = 0.12               # bezel clearance outside the ring
PLATE_LEFT = 2.35              # extents from the ring centre
PLATE_RIGHT = 2.34
PLATE_HALF = 1.15
PLATE_BORDER = 0.06
PLATE_CORNER = 0.15
PLATE_SAMPLES = 4


def segment_alpha(size: int = SEGMENT_SIZE) -> np.ndarray:
    centre = size / 2.0
    outer = centre - EDGE_MARGIN
    inner = outer * INNER_RATIO
    half_gap = GAP_PIXELS / 2.0

    coords = np.arange(size, dtype=np.float64) + 0.5
    x = coords[np.newaxis, :] - centre
    y = centre - coords[:, np.newaxis]          # +y is up in the image
    radius = np.hypot(x, y)
    # Angle from straight up, positive either side.
    angle = np.abs(np.arctan2(x, y))
    half_width = math.radians(SEGMENT_DEGREES / 2.0)

    # Signed distances (pixels, positive outside) to the radial and angular
    # edges, each inset by half the gap, then a rounded-box combination.
    d_radial = np.maximum((inner + half_gap) - radius, radius - (outer - half_gap))
    d_angular = (angle - half_width) * radius + half_gap
    k = CORNER_PIXELS
    qx = d_angular + k
    qy = d_radial + k
    outside = np.hypot(np.maximum(qx, 0.0), np.maximum(qy, 0.0))
    inside = np.minimum(np.maximum(qx, qy), 0.0)
    sdf = outside + inside - k
    return np.clip(0.5 - sdf, 0.0, 1.0)


def _plate_inside(x: np.ndarray, y: np.ndarray, inset: float) -> np.ndarray:
    """Inside test for the plate outline shrunk by ``inset`` ring units."""
    rx = PLATE_ASPECT + PLATE_PAD - inset
    ry = 1.0 + PLATE_PAD - inset
    half = ry
    # Left: the ring's ellipse grown by the pad. Right: a box whose right edge
    # leans out towards the bottom, with rounded corners.
    left = (x < 0) & ((x / rx) ** 2 + (y / ry) ** 2 <= 1.0)
    t = (y + half) / (2 * half)
    right_edge = PLATE_ASPECT * (0.95 + 0.10 * t) - inset
    k = max(PLATE_CORNER - inset, 0.01)
    qx = x - (right_edge - k)
    qy = np.abs(y) - (half - k)
    corner = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) <= k
    right = (x >= 0) & (x <= right_edge) & (np.abs(y) <= half) & corner
    return left | right


def status_plate() -> np.ndarray:
    w, h = PLATE_SIZE
    n = PLATE_SAMPLES
    xs = (np.arange(w * n) + 0.5) / (w * n) * (PLATE_LEFT + PLATE_RIGHT) - PLATE_LEFT
    ys = (np.arange(h * n) + 0.5) / (h * n) * (2 * PLATE_HALF) - PLATE_HALF
    x, y = np.meshgrid(xs, ys)

    def coverage(mask: np.ndarray) -> np.ndarray:
        return mask.reshape(h, n, w, n).mean(axis=(1, 3))

    outer = coverage(_plate_inside(x, y, 0.0))
    inner = coverage(_plate_inside(x, y, PLATE_BORDER))
    yy = (np.arange(h) + 0.5) / h * (2 * PLATE_HALF) - PLATE_HALF
    divider = np.clip(1.0 - np.abs(yy) / 0.03, 0.0, 1.0)[:, np.newaxis] * inner
    # Bezel shades from light blue at the rim to a deeper blue inside.
    rim = np.clip((outer - inner), 0.0, 1.0)

    fill = np.array([10, 20, 72], dtype=np.float64)
    bezel = np.array([70, 150, 240], dtype=np.float64)
    red = np.array([120, 18, 18], dtype=np.float64)
    rgb = fill[np.newaxis, np.newaxis, :] * inner[..., np.newaxis]
    rgb += bezel * rim[..., np.newaxis]
    rgb = rgb * (1 - divider[..., np.newaxis]) + red * divider[..., np.newaxis]
    alpha = rim * 255.0 + inner * 225.0
    with np.errstate(invalid="ignore", divide="ignore"):
        rgb = np.where(outer[..., np.newaxis] > 0, rgb / np.maximum(outer, 1e-6)[..., np.newaxis], 0)
    out = np.empty((h, w, 4), dtype=np.uint8)
    out[..., :3] = np.clip(rgb + 0.5, 0, 255).astype(np.uint8)
    out[..., 3] = np.clip(alpha + 0.5, 0, 255).astype(np.uint8)
    return out


def build() -> dict[str, Image.Image]:
    alpha = (segment_alpha() * 255.0 + 0.5).astype(np.uint8)
    rgba = np.empty(alpha.shape + (4,), dtype=np.uint8)
    rgba[..., :3] = 255
    rgba[..., 3] = alpha
    return {
        "exu_hud_ring_segment.png": Image.fromarray(rgba, "RGBA"),
        "exu_hud_status_plate.png": Image.fromarray(status_plate(), "RGBA"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="verify committed textures are current")
    args = parser.parse_args()

    stale = []
    for name, image in build().items():
        path = os.path.join(OUTPUT_DIR, name)
        if args.check:
            if not os.path.exists(path):
                stale.append(name)
                continue
            with Image.open(path) as existing:
                current = np.asarray(existing.convert("RGBA"), dtype=np.int16)
            expected = np.asarray(image, dtype=np.int16)
            # A one-step tolerance absorbs rounding differences between
            # numpy builds; a geometry change is far larger than that.
            if current.shape != expected.shape or np.abs(current - expected).max() > 1:
                stale.append(name)
        else:
            image.save(path, optimize=True)
            print("wrote", os.path.relpath(path, ROOT))

    if stale:
        sys.stderr.write("HUD textures out of date: {} (run tools/generate_hud_textures.py)\n".format(", ".join(stale)))
        return 1
    if args.check:
        print("HUD textures are current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
