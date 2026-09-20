#!/usr/bin/env python3
"""Checks that a Hatch model survives being loaded and saved again.

The engine could read its own model format and write it, and the two halves
disagreed about how the animation count is stored -- a byte going out, two bytes
coming back -- because nothing ever called the writer, so nothing ever put the
two in the same room. A model with no animations at all read its count as the
low byte of something else and went off to load hundreds of animations out of
vertex data.

This reads a model the way the engine's reader does, without using any of the
engine's code, and compares two files field by field. Run it over a model and
the same model after --convert-model and any disagreement between reading and
writing shows up as a difference rather than as a crash six months later.

Usage:  verify-model-roundtrip.py <before.hmdl> <after.hmdl>
"""

import struct
import sys


class Reader:
    def __init__(self, data):
        self.data = data
        self.at = 0

    def seek(self, to):
        self.at = to

    def u8(self):
        value = self.data[self.at]
        self.at += 1
        return value

    def u16(self):
        value = struct.unpack_from("<H", self.data, self.at)[0]
        self.at += 2
        return value

    def u32(self):
        value = struct.unpack_from("<I", self.data, self.at)[0]
        self.at += 4
        return value

    def i64(self):
        value = struct.unpack_from("<q", self.data, self.at)[0]
        self.at += 8
        return value

    def string(self):
        end = self.data.index(b"\x00", self.at)
        value = self.data[self.at:end].decode("latin1")
        self.at = end + 1
        return value


def read_model(path):
    reader = Reader(open(path, "rb").read())

    # The magic is the one big-endian field in the file.
    if struct.unpack_from(">I", reader.data, 0)[0] != 0x484D444C:
        raise SystemExit("%s is not a Hatch model" % path)

    reader.at = 4
    version = reader.u8()
    mesh_count = reader.u16()

    offsets = {}
    for name in ("vertex", "normal", "uv", "colour", "mesh", "material", "anim"):
        offsets[name] = reader.u32()

    reader.seek(offsets["vertex"])
    vertices = [(reader.i64() / 65536.0, reader.i64() / 65536.0, reader.i64() / 65536.0)
                for _ in range(reader.u32())]

    reader.seek(offsets["normal"])
    normals = [(reader.i64() / 65536.0, reader.i64() / 65536.0, reader.i64() / 65536.0)
               for _ in range(reader.u32())]

    reader.seek(offsets["uv"])
    uvs = [(reader.i64() / 65536.0, reader.i64() / 65536.0)
           for _ in range(reader.u32())]

    reader.seek(offsets["colour"])
    colours = [tuple(reader.u8() for _ in range(4)) for _ in range(reader.u32())]

    def stored(table, index):
        return table[index] if index < len(table) else None

    reader.seek(offsets["material"])
    materials = []
    for _ in range(reader.u8()):
        material = {"name": reader.string()}
        flags = reader.u8()
        material["flags"] = flags

        for bit, channel in ((1, "diffuse"), (2, "specular"), (4, "ambient"), (8, "emissive")):
            if flags & bit:
                texture = reader.string()
                material[channel] = (texture, stored(colours, reader.u32()))

        if flags & 16:
            material["shininess"] = (reader.u8(), reader.u8())
        if flags & 32:
            material["opacity"] = reader.u8()

        materials.append(material)

    # A byte, matching what the writer emits.
    reader.seek(offsets["anim"])
    animations = [(reader.string(), reader.u32(), reader.u32())
                  for _ in range(reader.u8())]

    reader.seek(offsets["mesh"])
    meshes = []
    for _ in range(mesh_count):
        mesh = {"name": reader.string()}
        flags = reader.u8()
        mesh["flags"] = flags
        mesh["material"] = reader.u8() if flags & 8 else -1

        count = reader.u32()
        triangles = reader.u32()
        frames = reader.u16()
        mesh["frames"] = frames

        total = count * frames
        mesh["positions"] = [stored(vertices, reader.u32()) for _ in range(total)]
        mesh["normals"] = [stored(normals, reader.u32()) for _ in range(total)] if flags & 1 else []
        mesh["uvs"] = [stored(uvs, reader.u32()) for _ in range(total)] if flags & 2 else []
        mesh["colours"] = [stored(colours, reader.u32()) for _ in range(total)] if flags & 4 else []
        mesh["indices"] = [reader.u32() for _ in range(triangles * 3)]

        meshes.append(mesh)

    return {"version": version, "materials": materials,
            "animations": animations, "meshes": meshes}


def compare(before, after):
    differences = 0

    def check(name, left, right):
        nonlocal differences
        if left == right:
            return
        differences += 1
        print("  %s differs" % name)
        print("    before: %r" % (left,))
        print("    after:  %r" % (right,))

    check("version", before["version"], after["version"])
    check("material count", len(before["materials"]), len(after["materials"]))
    check("animations", before["animations"], after["animations"])
    check("mesh count", len(before["meshes"]), len(after["meshes"]))

    for i in range(min(len(before["materials"]), len(after["materials"]))):
        check("material %d" % i, before["materials"][i], after["materials"][i])

    for i in range(min(len(before["meshes"]), len(after["meshes"]))):
        one, two = before["meshes"][i], after["meshes"][i]
        for key in ("name", "flags", "material", "frames",
                    "positions", "normals", "uvs", "colours", "indices"):
            check("mesh %d %s" % (i, key), one[key], two[key])

    return differences


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    before = read_model(sys.argv[1])
    after = read_model(sys.argv[2])

    print("%s: %d mesh(es), %d material(s), %d animation(s)"
          % (sys.argv[1], len(before["meshes"]), len(before["materials"]),
             len(before["animations"])))

    differences = compare(before, after)

    if differences:
        print("%d difference(s): the model did not survive the round trip" % differences)
        return 1

    print("the model came back unchanged")

    return 0


if __name__ == "__main__":
    sys.exit(main())
