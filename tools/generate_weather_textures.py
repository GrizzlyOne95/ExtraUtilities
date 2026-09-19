#!/usr/bin/env python3
"""Generate the placeholder billboard textures for the EXU weather templates.

These are deliberately procedural and deliberately committed. Procedural,
because every pixel is generated here rather than adapted from a third-party
pack, so there is nothing to license and the provenance is this file.
Committed, because the game loads PNGs out of the Workshop folder and a build
step is not part of that pipeline.

The Redux sprite shader (``BZSprite/AlphaBlend`` in stock ``sprites.material``)
multiplies the texture by the incoming vertex colour, and Ogre feeds emitter
``colour_range`` values in as vertex colour. So every texture here is white in
RGB and carries all of its shape in alpha: the ``.particle`` template picks the
colour, the texture picks the silhouette. Tinting a texture here would tint it
twice.

Usage:

    python tools/generate_weather_textures.py            # write Workshop/*.png
    python tools/generate_weather_textures.py --check    # verify they are current

``--check`` is what CI runs: it regenerates into memory and compares, so an
edited generator that was never re-run is caught rather than silently shipping
stale art. The comparison is on decoded pixels, not on file bytes - PNG
encoding is not stable across Pillow and zlib versions, so a byte comparison
would fail on any machine whose Pillow differs from whoever last regenerated.
"""

from __future__ import annotations

import argparse
import io
import os
import sys

try:
    import numpy as np
    from PIL import Image
except ImportError as exc:  # pragma: no cover - environment problem, not logic
    sys.stderr.write(
        "generate_weather_textures.py needs numpy and Pillow: {}\n".format(exc)
    )
    raise SystemExit(2)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT_DIR = os.path.join(ROOT, "Workshop")

# One seed for the whole module keeps regeneration byte-identical, which is what
# lets --check be a meaningful comparison instead of noise.
SEED = 0x42575254


def _radial_distance(size: int) -> np.ndarray:
    """Distance from centre, normalised so 1.0 is the edge of the inscribed circle."""
    axis = (np.arange(size, dtype=np.float64) + 0.5) / size * 2.0 - 1.0
    x, y = np.meshgrid(axis, axis)
    return np.sqrt(x * x + y * y)


def _value_noise(size: int, cells: int, rng: np.random.Generator) -> np.ndarray:
    """Smooth value noise in [0, 1], built by bilinear upscaling a small grid.

    Enough structure to stop a puff reading as a perfect airbrushed circle,
    without pulling in a noise library for four placeholder textures.
    """
    # +1 so the coarse grid wraps cleanly under the interpolation below.
    grid = rng.random((cells + 1, cells + 1))
    coarse = Image.fromarray((grid * 255.0).astype(np.uint8), mode="L")
    smooth = coarse.resize((size, size), Image.BICUBIC)
    return np.asarray(smooth, dtype=np.float64) / 255.0


def _to_rgba(alpha: np.ndarray) -> Image.Image:
    """White RGB, shaped alpha. See the module docstring for why."""
    alpha = np.clip(alpha, 0.0, 1.0)
    height, width = alpha.shape
    rgba = np.empty((height, width, 4), dtype=np.uint8)
    rgba[..., 0:3] = 255
    rgba[..., 3] = np.round(alpha * 255.0).astype(np.uint8)
    return Image.fromarray(rgba, mode="RGBA")


def make_rain_streak(width: int = 32, height: int = 128) -> Image.Image:
    """A soft vertical streak: bright core, fading head and tail.

    Rain is drawn as an ``oriented_common`` billboard aligned to the fall
    direction, so the texture's long axis becomes the direction of travel.
    """
    x = (np.arange(width, dtype=np.float64) + 0.5) / width * 2.0 - 1.0
    y = (np.arange(height, dtype=np.float64) + 0.5) / height
    xx, yy = np.meshgrid(x, y)

    # Across the streak: a narrow gaussian core. Rain reads as rain because the
    # streak is thin; widening this turns drops into falling smears.
    across = np.exp(-(xx * xx) / (2.0 * 0.15 * 0.15))

    # Along the streak: full strength through the middle, easing off at both
    # ends so a drop does not terminate in a hard edge.
    along = np.sin(np.pi * np.clip(yy, 0.0, 1.0)) ** 0.45

    return _to_rgba(across * along * 0.95)


def make_dust_mote(size: int = 64) -> Image.Image:
    """An irregular soft mote with heavy falloff."""
    rng = np.random.default_rng(SEED)
    radius = _radial_distance(size)

    # Perturb the radius with noise so the silhouette is lumpy rather than a
    # circle. Too few cells and the lumps become one big lobe that reads as a
    # corner; these stay fine enough to keep the mote broadly round.
    noise = _value_noise(size, 6, rng)
    radius = radius * (0.84 + 0.30 * noise)

    alpha = np.clip(1.0 - radius, 0.0, 1.0) ** 1.9

    # A second, finer noise layer breaks up the interior so overlapping motes
    # do not all read as the same quad.
    alpha *= 0.70 + 0.30 * _value_noise(size, 12, rng)
    return _to_rgba(alpha * 0.85)


def make_haboob_cloud(size: int = 128) -> Image.Image:
    """A wide, very soft veil for the storm wall.

    Haboob billboards are enormous and heavily overlapped, so peak alpha has to
    stay low: density comes from stacking many cards, not from any one card
    being opaque.
    """
    rng = np.random.default_rng(SEED + 1)
    radius = _radial_distance(size)
    radius = radius * (0.86 + 0.26 * _value_noise(size, 5, rng))

    alpha = np.clip(1.0 - radius, 0.0, 1.0) ** 2.4
    alpha *= 0.68 + 0.32 * _value_noise(size, 10, rng)
    return _to_rgba(alpha * 0.42)


def make_mist_card(size: int = 128) -> Image.Image:
    """The softest texture in the set: almost pure falloff, no visible structure.

    Mist must read as depth rather than as particles. Any edge the eye can
    resolve turns the whole layer back into a pile of quads, which is exactly
    what the depth-fade work in OpenShim would later be fixing.
    """
    rng = np.random.default_rng(SEED + 2)
    radius = _radial_distance(size)
    radius = radius * (0.85 + 0.26 * _value_noise(size, 2, rng))

    alpha = np.clip(1.0 - radius, 0.0, 1.0) ** 3.2
    return _to_rgba(alpha * 0.30)


TEXTURES = {
    "exu_rain_streak.png": make_rain_streak,
    "exu_dust_mote.png": make_dust_mote,
    "exu_haboob_cloud.png": make_haboob_cloud,
    "exu_mist_card.png": make_mist_card,
}


def encode(image: Image.Image) -> bytes:
    buffer = io.BytesIO()
    # optimize keeps the committed files small.
    image.save(buffer, format="PNG", optimize=True)
    return buffer.getvalue()


def pixels_match(path: str, expected: Image.Image) -> bool:
    """Compare decoded RGBA pixels rather than encoded bytes.

    The generator is deterministic in what it draws, but PNG *encoding* is not
    stable across Pillow and zlib versions, so this is the only comparison that
    means "the committed art is what the generator produces" on every machine.
    """
    with Image.open(path) as stored:
        stored = stored.convert("RGBA")
        if stored.size != expected.size:
            return False
        return np.array_equal(np.asarray(stored), np.asarray(expected.convert("RGBA")))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the committed textures match this generator instead of writing them",
    )
    args = parser.parse_args()

    stale = []
    for name, build in sorted(TEXTURES.items()):
        path = os.path.join(OUTPUT_DIR, name)
        image = build()

        if args.check:
            if not os.path.exists(path):
                stale.append("{}: missing".format(name))
            elif not pixels_match(path, image):
                stale.append("{}: out of date".format(name))
            continue

        data = encode(image)
        with open(path, "wb") as handle:
            handle.write(data)
        print("wrote {} ({} bytes)".format(name, len(data)))

    if args.check:
        if stale:
            sys.stderr.write(
                "weather textures are stale; run tools/generate_weather_textures.py:\n"
            )
            for entry in stale:
                sys.stderr.write("  {}\n".format(entry))
            return 1
        print("weather textures are up to date.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
