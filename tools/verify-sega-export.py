#!/usr/bin/env python3
"""Checks a SEGA export against the art it was made from.

Handles the Mega Drive (and the Mega CD, which shares its VDP) and the Game
Gear, whose VDP came from the Master System instead and agrees with the Mega
Drive's about almost nothing: bitplanes rather than packed nibbles, twelve bits
of colour rather than nine, two palettes rather than four, and a different
nametable layout. Both are decoded here, because the whole point is to arrive at
the picture by a different route than the exporter did.

Reads back what the exporter wrote -- the palette words, the tile patterns, the
nametable's indices and flip bits -- reassembles the picture the VDP would draw
from them, and compares it against the source tileset and map. Nothing in here
uses the engine, so a mistake shared between the exporter and this would have to
be made twice, independently, in two languages.

The comparison is done after rounding the source to the nine bits of colour the
hardware keeps, since that much loss is the machine's and not the exporter's.

This exists because a stride bug shipped once. The exporter walked a scene layer
by its width when a layer's rows are stored as far apart as the next power of
two above it, so every row after the first read the wrong tiles -- and the only
sample it was tested on was 32 columns across, where the two are the same
number. Run it on a layer whose width is not a power of two and it would have
been caught the first time.

Usage:  verify-sega-export.py <megadrive|gamegear|saturn> <export dir> <scenes dir> <map.tmx>
        verify-sega-export.py saturn-3d <export dir> <scenes dir> <scene.scene3d>
"""

import math
import re
import struct
import sys
import zlib


def read_png(path):
    """Enough of a PNG reader for a tileset: 8-bit RGBA, no interlacing."""
    data = open(path, "rb").read()
    pos, width, height, idat = 8, None, None, b""

    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]

        if tag == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", chunk[:10])
            if depth != 8 or colour != 6:
                raise SystemExit("the tileset has to be 8-bit RGBA")
        elif tag == b"IDAT":
            idat += chunk

        pos += 12 + length

    raw = zlib.decompress(idat)
    stride = width * 4
    rows, previous, at = [], bytearray(stride), 0

    for _ in range(height):
        filter_type = raw[at]
        at += 1
        line = bytearray(raw[at:at + stride])
        at += stride

        for x in range(stride):
            a = line[x - 4] if x >= 4 else 0
            b = previous[x]
            c = previous[x - 4] if x >= 4 else 0

            if filter_type == 1:
                line[x] = (line[x] + a) & 0xFF
            elif filter_type == 2:
                line[x] = (line[x] + b) & 0xFF
            elif filter_type == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif filter_type == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                nearest = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + nearest) & 0xFF

        rows.append([tuple(line[x * 4:x * 4 + 4]) for x in range(width)])
        previous = line

    return width, height, rows


def read_map(path):
    text = open(path).read()

    width = int(re.search(r'<layer[^>]*width="(\d+)"', text).group(1))
    height = int(re.search(r'<layer[^>]*height="(\d+)"', text).group(1))

    body = re.search(r'<data encoding="csv">\s*(.*?)\s*</data>', text, re.S).group(1)
    gids = [int(v) for v in body.replace("\n", "").split(",") if v.strip()]

    return width, height, gids


def to_nine_bits(value):
    """What a channel becomes once the Mega Drive's VDP has had it."""
    return (value >> 5) * 255 // 7


def to_twelve_bits(value):
    """The Game Gear keeps four bits a channel rather than three."""
    return (value >> 4) * 255 // 15


def to_fifteen_bits(value):
    """The Saturn keeps five bits a channel."""
    return (value >> 3) * 255 // 31


def read_scene3d(path):
    """The placements out of a 3D scene file. Only what the geometry depends on:
    where each model sits, how it is turned and how big it is."""
    text = open(path).read()
    models = []

    for tag in re.findall(r"<model\b[^>]*/>", text):
        def value(name, fallback):
            found = re.search(r'\b%s="([^"]*)"' % name, tag)
            return float(found.group(1)) if found else fallback

        source = re.search(r'\bsource="([^"]*)"', tag)

        models.append((source.group(1) if source else "",
                       value("x", 0.0), value("y", 0.0), value("z", 0.0),
                       value("rotationX", 0.0), value("rotationY", 0.0), value("rotationZ", 0.0),
                       value("scaleX", 1.0), value("scaleY", 1.0), value("scaleZ", 1.0)))

    return models


def engine_transform(rx, ry, rz, sx, sy, sz, px, py, pz):
    """The matrix the engine builds for a placed model, element for element.

    Matrix4x4 is column major -- (row i, column j) is Values[j * 4 + i] -- and
    Translate post-multiplies, so a model's position is applied in its own
    rotated and scaled space rather than in the world's. Both of those are easy
    to get wrong in a way that still looks plausible, so this is written out the
    long way from the engine's own source rather than with a matrix library."""
    sinx, cosx = math.sin(rx), math.cos(rx)
    siny, cosy = math.sin(ry), math.cos(ry)
    sinz, cosz = math.sin(rz), math.cos(rz)
    sinxy = sinx * siny

    rot = [0.0] * 16
    rot[9] = sinx
    rot[0] = (cosy * cosz) + (sinz * sinxy)
    rot[4] = (cosy * sinz) - (cosz * sinxy)
    rot[8] = cosx * siny
    rot[1] = -(cosx * sinz)
    rot[5] = cosx * cosz
    sincosxy = sinx * cosy
    rot[2] = (sinz * sincosxy) - (siny * cosz)
    rot[6] = (-(sinz * siny)) - (cosz * sincosxy)
    rot[10] = cosx * cosy
    rot[15] = 1.0

    scale = [0.0] * 16
    scale[0], scale[5], scale[10], scale[15] = sx, sy, sz, 1.0

    out = [0.0] * 16
    for col in range(4):
        b0, b1, b2, b3 = scale[col * 4:col * 4 + 4]
        for row in range(4):
            out[col * 4 + row] = (b0 * rot[row] + b1 * rot[4 + row] +
                                  b2 * rot[8 + row] + b3 * rot[12 + row])

    out[12] = out[0] * px + out[4] * py + out[8] * pz + out[12]
    out[13] = out[1] * px + out[5] * py + out[9] * pz + out[13]
    out[14] = out[2] * px + out[6] * py + out[10] * pz + out[14]

    return out


def check_saturn_3d(export, scenes, scenename):
    """The Saturn 3D export is a table of world-space vertices and the faces
    over them. This rebuilds every vertex from the scene file and the model it
    came from, and compares."""
    models = read_scene3d("%s/%s" % (scenes, scenename))

    if not models:
        print("%s places no models" % scenename)
        return 1

    blob = open("%s/res/mesh.bin" % export, "rb").read()

    magic, vertex_count, face_count = struct.unpack(">4sII", blob[:12])
    if magic != b"HSM1":
        print("mesh.bin does not start with HSM1 -- got %r" % magic)
        return 1

    expected = 12 + vertex_count * 12 + face_count * 12
    if len(blob) != expected:
        print("mesh.bin is %d bytes; %d vertices and %d faces is %d"
              % (len(blob), vertex_count, face_count, expected))
        return 1

    vertices = []
    at = 12
    for _ in range(vertex_count):
        x, y, z = struct.unpack(">iii", blob[at:at + 12])
        at += 12
        vertices.append((x / 65536.0, y / 65536.0, z / 65536.0))

    faces = []
    for _ in range(face_count):
        faces.append(struct.unpack(">HHHHHH", blob[at:at + 12]))
        at += 12

    # The sample's model is the cube the generator writes, so its corners are
    # known without having to read the binary back.
    corners = [(-30.0, -30.0, -30.0), ( 30.0, -30.0, -30.0),
               ( 30.0,  30.0, -30.0), (-30.0,  30.0, -30.0),
               (-30.0, -30.0,  30.0), ( 30.0, -30.0,  30.0),
               ( 30.0,  30.0,  30.0), (-30.0,  30.0,  30.0)]

    if vertex_count != len(models) * len(corners):
        print("%d vertices for %d model(s) of %d corners"
              % (vertex_count, len(models), len(corners)))
        return 1

    worst = 0.0
    compared = 0

    for index, model in enumerate(models):
        _, px, py, pz, rx, ry, rz, sx, sy, sz = model
        M = engine_transform(rx, ry, rz, sx, sy, sz, px, py, pz)

        for corner in range(len(corners)):
            x, y, z = corners[corner]
            wanted = (M[0] * x + M[4] * y + M[8] * z + M[12],
                      M[1] * x + M[5] * y + M[9] * z + M[13],
                      M[2] * x + M[6] * y + M[10] * z + M[14])
            got = vertices[index * len(corners) + corner]

            for a, b in zip(wanted, got):
                worst = max(worst, abs(a - b))
                compared += 1

    # Every face has to point at vertices that exist, and a triangle has to be
    # the quad the Saturn draws with its last two corners in the same place.
    bad_faces = 0
    for a, b, c, d, colour, flags in faces:
        if max(a, b, c, d) >= vertex_count:
            bad_faces += 1
        elif (flags & 1) and d != c:
            bad_faces += 1

    print("%s: %d vertices and %d faces, %d coordinates compared, worst off by %.5f"
          % (scenename, vertex_count, face_count, compared, worst))

    # A sixteenth of a unit: fixed point rounding, and nothing else.
    if worst > 0.0625:
        print("the geometry does not match the placements it was made from")
        return 1

    if bad_faces:
        print("%d face(s) point outside the vertex table or are malformed" % bad_faces)
        return 1

    return 0


def check_saturn(export, scenes, mapname):
    """The Saturn export is a flat 8-bit picture and a 256-colour palette, so
    this walks it pixel by pixel rather than tile by tile: every pixel of the
    written image has to be the tileset pixel the map put there, in the colour
    the Saturn can hold."""
    _, _, tiles_png = read_png("%s/tileset.png" % scenes)
    map_w, map_h, gids = read_map("%s/%s" % (scenes, mapname))

    palette_bytes = open("%s/res/palette.bin" % export, "rb").read()
    palette = struct.unpack(">%dH" % (len(palette_bytes) // 2), palette_bytes)

    image = open("%s/res/image.bin" % export, "rb").read()

    tile_w = tile_h = 16
    width = map_w * tile_w
    height = map_h * tile_h

    if len(image) != width * height:
        print("image.bin is %d bytes; a %dx%d picture is %d"
              % (len(image), width, height, width * height))
        return 1

    def palette_rgb(word):
        return ((word & 0x1F) * 255 // 31,
                ((word >> 5) & 0x1F) * 255 // 31,
                ((word >> 10) & 0x1F) * 255 // 31)

    compared = differing = 0
    first = None

    for y in range(height):
        for x in range(width):
            compared += 1

            gid = gids[(x // tile_w) + (y // tile_h) * map_w]

            if gid == 0:
                wanted = None
            else:
                source = gid - 1
                r, g, b, a = tiles_png[y % tile_h][source * tile_w + (x % tile_w)]
                wanted = None if a < 128 else (to_fifteen_bits(r),
                                               to_fifteen_bits(g),
                                               to_fifteen_bits(b))

            index = image[x + y * width]

            # Index 0 is the one a VDP2 background shows nothing for.
            got = None if index == 0 else palette_rgb(palette[index])

            if wanted != got:
                differing += 1
                if first is None:
                    first = (x, y, gid, wanted, index, got)

    print("%s: %dx%d picture, %d pixels compared, %d differing"
          % (mapname, width, height, compared, differing))

    if first:
        x, y, gid, wanted, index, got = first
        print("  first at (%d, %d): tile gid %d wanted %s, image index %d gave %s"
              % (x, y, gid, wanted, index, got))
        return 1

    return 0


def plane_size(cells):
    for w, h in ((32, 32), (64, 32), (32, 64), (64, 64), (128, 32), (32, 128)):
        if w * h == cells:
            return w, h

    raise SystemExit("a nametable of %d cells is not a plane the VDP offers" % cells)


def check_gamegear(export, scenes, mapname):
    _, _, tiles_png = read_png("%s/tileset.png" % scenes)
    map_w, map_h, gids = read_map("%s/%s" % (scenes, mapname))

    palette_bytes = open("%s/res/palette.bin" % export, "rb").read()
    patterns = open("%s/res/tiles.bin" % export, "rb").read()
    nametable_bytes = open("%s/res/map.bin" % export, "rb").read()

    # Little endian here, unlike the Mega Drive: this is a Z80 machine.
    palette = struct.unpack("<32H", palette_bytes)
    nametable = struct.unpack("<%dH" % (len(nametable_bytes) // 2), nametable_bytes)

    def palette_rgb(word):
        return ((word & 0x0F) * 255 // 15,
                ((word >> 4) & 0x0F) * 255 // 15,
                ((word >> 8) & 0x0F) * 255 // 15)

    # The map covers the whole layer here rather than a fixed plane.
    plane_w = map_w * 2
    plane_h = len(nametable) // plane_w

    compared = differing = 0

    for cy in range(plane_h):
        for cx in range(plane_w):
            entry = nametable[cx + cy * plane_w]
            index = entry & 0x01FF
            flip_x = (entry >> 9) & 1
            flip_y = (entry >> 10) & 1
            which = (entry >> 11) & 1

            tile_x, tile_y = cx // 2, cy // 2

            for py in range(8):
                for px in range(8):
                    compared += 1

                    if tile_x >= map_w or tile_y >= map_h:
                        wanted = (0, 0, 0)
                    else:
                        gid = gids[tile_x + tile_y * map_w]
                        if gid == 0:
                            wanted = (0, 0, 0)
                        else:
                            source = gid - 1
                            r, g, b, a = tiles_png[(cy % 2) * 8 + py][source * 16 + (cx % 2) * 8 + px]
                            wanted = (0, 0, 0) if a < 128 else (
                                to_twelve_bits(r), to_twelve_bits(g), to_twelve_bits(b))

                    sx = 7 - px if flip_x else px
                    sy = 7 - py if flip_y else py

                    # Four bitplanes, one byte each per row, leftmost pixel in
                    # the high bit.
                    colour = 0
                    for plane in range(4):
                        if patterns[index * 32 + sy * 4 + plane] & (0x80 >> sx):
                            colour |= 1 << plane

                    got = (0, 0, 0) if colour == 0 else palette_rgb(palette[which * 16 + colour])

                    if wanted != got:
                        differing += 1

    print("%s: map %dx%d cells, %d pixels compared, %d differing"
          % (mapname, plane_w, plane_h, compared, differing))

    return 1 if differing else 0


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2

    machine, export, scenes, mapname = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

    if machine == "gamegear":
        result = check_gamegear(export, scenes, mapname)
        if result:
            print("the export does not match the art it was made from")
        return result

    if machine == "saturn-3d":
        result = check_saturn_3d(export, scenes, mapname)
        if result:
            print("the export does not match the scene it was made from")
        return result

    if machine == "saturn":
        result = check_saturn(export, scenes, mapname)
        if result:
            print("the export does not match the art it was made from")
        return result

    if machine != "megadrive":
        print("unknown machine %r -- expected megadrive, gamegear, saturn or saturn-3d" % machine)
        return 2

    _, _, tiles_png = read_png("%s/tileset.png" % scenes)
    map_w, map_h, gids = read_map("%s/%s" % (scenes, mapname))

    palette = struct.unpack(">64H", open("%s/res/palette.bin" % export, "rb").read())
    patterns = open("%s/res/tiles.bin" % export, "rb").read()
    nametable_bytes = open("%s/res/map.bin" % export, "rb").read()

    cells = len(nametable_bytes) // 2
    plane_w, plane_h = plane_size(cells)
    nametable = struct.unpack(">%dH" % cells, nametable_bytes)

    def palette_rgb(word):
        return (((word >> 1) & 7) * 255 // 7,
                ((word >> 5) & 7) * 255 // 7,
                ((word >> 9) & 7) * 255 // 7)

    compared = differing = 0

    for cy in range(plane_h):
        for cx in range(plane_w):
            entry = nametable[cx + cy * plane_w]
            index = entry & 0x07FF
            flip_x = (entry >> 11) & 1
            flip_y = (entry >> 12) & 1
            which = (entry >> 13) & 3

            tile_x, tile_y = cx // 2, cy // 2

            for py in range(8):
                for px in range(8):
                    compared += 1

                    if tile_x >= map_w or tile_y >= map_h:
                        wanted = (0, 0, 0)
                    else:
                        gid = gids[tile_x + tile_y * map_w]
                        if gid == 0:
                            wanted = (0, 0, 0)
                        else:
                            source = gid - 1
                            r, g, b, a = tiles_png[(cy % 2) * 8 + py][source * 16 + (cx % 2) * 8 + px]
                            wanted = (0, 0, 0) if a < 128 else (
                                to_nine_bits(r), to_nine_bits(g), to_nine_bits(b))

                    sx = 7 - px if flip_x else px
                    sy = 7 - py if flip_y else py

                    byte = patterns[index * 32 + (sx >> 1) + sy * 4]
                    colour = (byte & 0x0F) if (sx & 1) else ((byte >> 4) & 0x0F)

                    got = (0, 0, 0) if colour == 0 else palette_rgb(palette[which * 16 + colour])

                    if wanted != got:
                        differing += 1

    print("%s: plane %dx%d, %d pixels compared, %d differing"
          % (mapname, plane_w, plane_h, compared, differing))

    if differing:
        print("the export does not match the art it was made from")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
