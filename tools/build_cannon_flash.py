"""Build original, texture-free cannon flash meshes with shared Toolbox codecs.

All geometry is authored here, independently of BZ2/BZCC assets. Metres; the
muzzle is at zero and visible fire extends along render -Z (EXU forward).
"""
import argparse
import json
import math
from pathlib import Path
import struct
import sys


def geometry(length, radius, petals):
    layers = {}

    def layer(name):
        vertices, triangles = [], []
        layers[name] = {"vertices": vertices, "triangles": triangles}
        return vertices, triangles

    def colour(heat, opacity):
        # RGB is also a usable warm fixed-function fallback; B carries heat.
        return (1.0, .20 + .77 * heat, .035 + .93 * heat, opacity)

    def surface(vertices, triangles, rows):
        start = len(vertices)
        width = len(rows[0])
        vertices.extend(v for row in rows for v in row)
        for row in range(len(rows) - 1):
            for col in range(width - 1):
                a = start + row * width + col
                triangles.extend(((a, a + 1, a + width), (a + 1, a + width + 1, a + width)))

    vertices, triangles = layer("Core")
    profile = [(0, .08, .95, .65), (.08, .20, 1, .85), (.22, .27, 1, .85),
               (.40, .23, .94, .70), (.62, .13, .80, .42), (.83, .015, .52, 0)]
    for plane in range(3):
        angle = math.pi * plane / 3
        rows = []
        for z, r, heat, alpha in profile:
            rows.append([([radius*r*(n/4-1)*math.cos(angle), radius*r*(n/4-1)*math.sin(angle), -length*z],
                          colour(heat*(1-.12*abs(n/4-1)), alpha*(1-abs(n/4-1))**1.4), [n/8, z])
                         for n in range(9)])
        surface(vertices, triangles, rows)

    vertices, triangles = layer("Plume")
    # Filled gas sheets pass through the axis rather than outlining a shell.
    # Unequal reaches break rotational symmetry.
    profile = [(0, .10, .95, .0), (.08, .32, .92, .45), (.21, .85, .67, .60),
               (.40, 1.0, .42, .42), (.62, .65, .26, .25), (.82, .26, .12, .10), (1, .015, .03, 0)]
    for petal in range(petals):
        axis = math.pi * petal / petals
        reach = .82 + .18 * (.5 + .5 * math.sin(petal * 2.39 + .4))
        rows = []
        for z, r, heat, alpha in profile:
            row = []
            for n in range(13):
                across = n / 12
                width = 2 * across - 1
                radial = radius * r * width
                row.append(([radial * math.cos(axis), radial * math.sin(axis),
                             -length*z*reach*(1-.12*abs(width))],
                            colour(min(1, heat+.28*(1-abs(width))),
                                   .55*alpha*math.sin(math.pi*across)**1.6), [across, z]))
            rows.append(row)
        surface(vertices, triangles, rows)

    vertices, triangles = layer("Bloom")
    # A small radial veil at the muzzle keeps the effect legible head-on.
    # It is deliberately subordinate to the 3-D plume, not a large halo sprite.
    rows = []
    for r, heat, alpha in ((.015, 1, .8), (.16, .92, .55), (.40, .50, .16), (.66, .12, 0)):
        rows.append([([radius * r * math.cos(2 * math.pi * n / 32),
                       radius * r * math.sin(2 * math.pi * n / 32), -length * .045],
                      colour(heat, alpha), [.5 + .5 * r / .66 * math.cos(2 * math.pi * n / 32),
                                             .5 + .5 * r / .66 * math.sin(2 * math.pi * n / 32)])
                     for n in range(33)])
    surface(vertices, triangles, rows)
    return layers


def build(output, toolbox, preview):
    sys.path.insert(0, str(toolbox.resolve()))
    from battlezone.meshes.ogre import Geometry, OgreMesh, SubMesh, VertexBuffer, VertexElement, read_mesh, write_mesh
    output.mkdir(parents=True, exist_ok=True)
    summary = {}
    for preset, length, radius, petals in (("small", .85, .24, 6), ("heavy", 2.35, .88, 7)):
        layers = geometry(length, radius, petals)
        mesh = OgreMesh("[MeshSerializer_v1.100]", "<")
        for layer_index, (name, data) in enumerate(layers.items()):
            vertices = data["vertices"]
            for pos, rgba, uv in vertices:
                if not all(math.isfinite(x) for x in (*pos, *rgba, *uv)):
                    raise ValueError("Nonfinite vertex")
            for tri in data["triangles"]:
                a, b, c = [vertices[i][0] for i in tri]
                ab, ac = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
                area = sum(x*x for x in (ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]))
                if area < 1e-15:
                    raise ValueError("Degenerate triangle")
            raw = bytearray()
            for pos, rgba, uv in vertices:
                r, g, b, a = [round(max(0, min(1, x)) * 255) for x in rgba]
                raw.extend(struct.pack("<3fI2f", *pos, (a << 24) | (r << 16) | (g << 8) | b,
                                       uv[0] + layer_index * 2, uv[1]))
            geom = Geometry(len(vertices), [VertexElement(0, 2, 1, 0, 0), VertexElement(0, 10, 5, 12, 0),
                                            VertexElement(0, 1, 7, 16, 0)], {0: VertexBuffer(0, 24, bytes(raw), 0)})
            mesh.submeshes.append(SubMesh("EXU/CannonFlash/Amber", False,
                [i for tri in data["triangles"] for i in tri], False, geometry=geom, name=name))
        path = output / ("exu_" + preset + "_flash.mesh")
        encoded = write_mesh(mesh)
        path.write_bytes(encoded)
        checked = read_mesh(encoded)
        if write_mesh(checked) != encoded or len(checked.submeshes) != 3:
            raise ValueError("Mesh round trip failed")
        summary[preset] = {"length_metres": length, "radius_metres": radius,
            "vertices": sum(len(d["vertices"]) for d in layers.values()),
            "triangles": sum(len(d["triangles"]) for d in layers.values()), "bytes": len(encoded)}
        if preview:
            preview.mkdir(parents=True, exist_ok=True)
            (preview / (preset + ".json")).write_text(json.dumps(layers))
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "examples/odf_cannon_flash")
    parser.add_argument("--toolbox", type=Path, default=Path(__file__).resolve().parents[2] / "Battlezone98_Modding_Toolbox")
    parser.add_argument("--preview-data", type=Path)
    args = parser.parse_args()
    build(args.output, args.toolbox, args.preview_data)
