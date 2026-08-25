#!/usr/bin/env python3
"""Checks a Mega Drive export against the art it was made from.

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

Usage:  verify-megadrive-export.py <export dir> <scenes dir> <map.tmx>
"""

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
    """What a channel becomes once the VDP has had it."""
    return (value >> 5) * 255 // 7


def plane_size(cells):
    for w, h in ((32, 32), (64, 32), (32, 64), (64, 64), (128, 32), (32, 128)):
        if w * h == cells:
            return w, h

    raise SystemExit("a nametable of %d cells is not a plane the VDP offers" % cells)


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2

    export, scenes, mapname = sys.argv[1], sys.argv[2], sys.argv[3]

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
