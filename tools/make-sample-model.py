#!/usr/bin/env python3
"""Writes the cube the Saturn 3D sample scene is made of.

A Hatch model is a binary, and a binary checked into a repository is a thing
nobody can review. This is the thing that made it, so the cube can be read
rather than trusted -- and regenerated if the format ever moves.

    tools/make-sample-model.py meta/saturn/sample/Resources/Models/cube.hmdl
"""

import struct
import sys

SIZE = 30.0

CORNERS = [(-SIZE, -SIZE, -SIZE), ( SIZE, -SIZE, -SIZE),
           ( SIZE,  SIZE, -SIZE), (-SIZE,  SIZE, -SIZE),
           (-SIZE, -SIZE,  SIZE), ( SIZE, -SIZE,  SIZE),
           ( SIZE,  SIZE,  SIZE), (-SIZE,  SIZE,  SIZE)]

# Two triangles a face, wound so their outward side is the one the camera sees.
TRIANGLES = [(0, 2, 1), (0, 3, 2),   # back
             (4, 5, 6), (4, 6, 7),   # front
             (0, 4, 7), (0, 7, 3),   # left
             (1, 2, 6), (1, 6, 5),   # right
             (0, 1, 5), (0, 5, 4),   # bottom
             (3, 7, 6), (3, 6, 2)]   # top

COLORS = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 0, 255),
          (0, 255, 255, 255), (255, 0, 255, 255), (255, 128, 0, 255), (128, 128, 255, 255)]


def build():
    u8 = lambda v: struct.pack("<B", v)
    u16 = lambda v: struct.pack("<H", v)
    u32 = lambda v: struct.pack("<I", v)
    fixed = lambda v: struct.pack("<q", int(round(v * 65536)))

    out = bytearray()
    out += struct.pack(">I", 0x484D444C)    # "HMDL", the one big-endian field
    out += u8(0)                            # version
    out += u16(1)                           # one mesh

    slots = {}
    for name in ("vertex", "normal", "uv", "colour", "mesh", "material", "anim"):
        slots[name] = len(out)
        out += u32(0)

    def patch(name):
        out[slots[name]:slots[name] + 4] = u32(len(out))

    patch("mesh")
    out += b"cube\x00"
    out += u8(4)                            # vertex colours, no normals or UVs
    out += u32(len(CORNERS))
    out += u32(len(TRIANGLES))
    out += u16(1)                           # one frame
    for i in range(len(CORNERS)):
        out += u32(i)                       # into the vertex store
    for i in range(len(CORNERS)):
        out += u32(i)                       # into the colour store
    for triangle in TRIANGLES:
        for corner in triangle:
            out += u32(corner)

    patch("material")
    out += u8(0)

    # The reader takes this count as a UInt16 while the writer emits a single
    # byte. This file follows the reader, because the reader is what loads it.
    patch("anim")
    out += u16(0)

    patch("vertex")
    out += u32(len(CORNERS))
    for corner in CORNERS:
        out += fixed(corner[0]) + fixed(corner[1]) + fixed(corner[2])

    patch("normal")
    out += u32(0)
    patch("uv")
    out += u32(0)

    patch("colour")
    out += u32(len(COLORS))
    for r, g, b, a in COLORS:
        out += bytes((r, g, b, a))

    return bytes(out)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2

    data = build()
    open(sys.argv[1], "wb").write(data)
    print("wrote %s (%d bytes)" % (sys.argv[1], len(data)))

    return 0


if __name__ == "__main__":
    sys.exit(main())
