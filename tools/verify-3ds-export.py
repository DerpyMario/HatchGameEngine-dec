#!/usr/bin/env python3
"""Checks a .3dsx written by --export-3ds against the game it was made from.

    verify-3ds-export.py <game.3dsx> <project Resources folder> [runtime.3dsx]

This reads the file the way the 3DS does, without any of the engine's code:

  - the 3DSX header, and that the program in it ends where the SMDH starts;
  - the SMDH, and that its titles are the ones in the game's GameConfig.xml;
  - the RomFS, finding Data.hatch the two ways libctru can -- through the root
    directory's list of files, and through the file hash table;
  - Data.hatch, every entry of which is unpacked and compared byte for byte
    with the file in the Resources folder that has the same name, so nothing
    is missing, nothing is extra and nothing changed on the way.

Given the runtime the export was made from, it also checks the program was
copied across untouched.

It knows the format from the files themselves, which is the point: if this and
the exporter disagree, one of them has the format wrong.
"""

import os
import struct
import sys
import xml.etree.ElementTree as ElementTree
import zlib

SMDH_SIZE = 0x36C0
ROMFS_NONE = 0xFFFFFFFF


def fail(message):
    print("FAIL: " + message)
    sys.exit(1)


def program_end(data):
    if data[:4] != b"3DSX":
        fail("not a 3DSX file")

    header_size, reloc_size = struct.unpack_from("<HH", data, 4)
    code, rodata, segment, bss = struct.unpack_from("<IIII", data, 0x10)

    end = header_size + 3 * reloc_size
    for i in range(3):
        absolute, relative = struct.unpack_from("<II", data, header_size + i * reloc_size)
        end += (absolute + relative) * 4

    return header_size, end + code + rodata + (segment - bss)


def read_titles(smdh):
    titles = []
    for language in range(16):
        base = 8 + language * 0x200
        short = smdh[base:base + 0x80].decode("utf-16-le").split("\0")[0]
        long = smdh[base + 0x80:base + 0x180].decode("utf-16-le").split("\0")[0]
        publisher = smdh[base + 0x180:base + 0x200].decode("utf-16-le").split("\0")[0]
        titles.append((short, long, publisher))
    return titles


def romfs_hash(parent, name, buckets):
    value = parent ^ 123456789
    for unit in struct.unpack("<%dH" % (len(name) // 2), name):
        value = ((value >> 5) | (value << 27)) & 0xFFFFFFFF
        value ^= unit
    return value % buckets


def find_in_romfs(romfs, wanted):
    (header_size, dir_hash_off, dir_hash_size, dir_off, dir_size, file_hash_off,
     file_hash_size, file_off, file_size, data_off) = struct.unpack_from("<10I", romfs, 0)

    if header_size != 0x28:
        fail("RomFS header is %#x bytes, not 0x28" % header_size)

    name = wanted.encode("utf-16-le")

    def file_entry(offset):
        parent, sibling, data_offset, data_size, next_hash, name_len = struct.unpack_from(
            "<IIQQII", romfs, file_off + offset)
        entry_name = romfs[file_off + offset + 0x20:file_off + offset + 0x20 + name_len]
        return parent, sibling, data_offset, data_size, next_hash, entry_name

    # Through the root directory: its first file, then each one's sibling.
    _, _, _, child_file, _, root_name_len = struct.unpack_from("<6I", romfs, dir_off)
    if root_name_len != 0:
        fail("the root directory has a name")

    by_listing = None
    offset = child_file
    while offset != ROMFS_NONE:
        _, sibling, data_offset, data_size, _, entry_name = file_entry(offset)
        if entry_name == name:
            by_listing = (data_offset, data_size)
            break
        offset = sibling

    # Through the hash table, which is how libctru opens a file by path.
    buckets = file_hash_size // 4
    offset = struct.unpack_from("<I", romfs, file_hash_off + 4 * romfs_hash(0, name, buckets))[0]
    by_hash = None
    while offset != ROMFS_NONE:
        parent, _, data_offset, data_size, next_hash, entry_name = file_entry(offset)
        if parent == 0 and entry_name == name:
            by_hash = (data_offset, data_size)
            break
        offset = next_hash

    if by_listing is None:
        fail("%s is not in the RomFS root's list of files" % wanted)
    if by_hash is None:
        fail("%s cannot be found through the RomFS hash table" % wanted)
    if by_listing != by_hash:
        fail("the two ways of finding %s disagree" % wanted)

    data_offset, data_size = by_hash
    start = data_off + data_offset
    if start + data_size > len(romfs):
        fail("%s runs past the end of the RomFS" % wanted)

    return romfs[start:start + data_size]


def read_hatch(data):
    if data[:5] != b"HATCH":
        fail("Data.hatch does not start with HATCH")

    count = struct.unpack_from("<H", data, 8)[0]
    entries = {}
    for i in range(count):
        name_hash, offset, size, flags, stored = struct.unpack_from("<IQQIQ", data, 10 + i * 32)
        if flags != 0:
            fail("entry %08x is encrypted, which an export never does" % name_hash)

        body = data[offset:offset + stored]
        if len(body) != stored:
            fail("entry %08x runs past the end of Data.hatch" % name_hash)

        if stored != size:
            body = zlib.decompress(body)
            if len(body) != size:
                fail("entry %08x unpacks to %d bytes, not %d" % (name_hash, len(body), size))

        if name_hash in entries:
            fail("entry %08x is in Data.hatch twice" % name_hash)
        entries[name_hash] = body

    return entries


def game_titles(resources):
    config = os.path.join(resources, "GameConfig.xml")
    if not os.path.exists(config):
        return None

    root = ElementTree.parse(config).getroot()

    def text(tag):
        node = root.find(tag)
        return node.text.strip() if node is not None and node.text else ""

    title = text("gameTitle") or text("name")
    short = text("shortTitle") or title
    description = text("description") or title
    developer = text("developer") or "Unknown"
    return short, description, developer


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)

    exported = open(sys.argv[1], "rb").read()
    resources = sys.argv[2]

    header_size, end = program_end(exported)
    if header_size < 0x2C:
        fail("the header has no room for an SMDH or a RomFS")

    smdh_offset, smdh_size, romfs_offset = struct.unpack_from("<III", exported, 0x20)
    if smdh_offset != end:
        fail("the SMDH starts at %#x, but the program ends at %#x" % (smdh_offset, end))
    if smdh_size != SMDH_SIZE:
        fail("the SMDH is %#x bytes, not %#x" % (smdh_size, SMDH_SIZE))
    if romfs_offset != smdh_offset + smdh_size:
        fail("the RomFS does not follow the SMDH")

    if len(sys.argv) > 3:
        runtime = open(sys.argv[3], "rb").read()
        runtime_header, runtime_end = program_end(runtime)
        if exported[header_size:end] != runtime[runtime_header:runtime_end]:
            fail("the program is not the runtime's")
        if exported[8:0x20] != runtime[8:0x20] or exported[:4] != runtime[:4]:
            fail("the header's segment sizes are not the runtime's")
        print("program: %d bytes, the same as the runtime's" % (end - header_size))

    smdh = exported[smdh_offset:smdh_offset + smdh_size]
    if smdh[:4] != b"SMDH":
        fail("the SMDH has no SMDH magic")

    titles = read_titles(smdh)
    if len(set(titles)) != 1:
        fail("the titles differ between languages")

    expected = game_titles(resources)
    if expected is not None:
        # Fields are cut to fit: 63, 127 and 63 characters.
        expected = (expected[0][:63], expected[1][:127], expected[2][:63])
        if titles[0] != expected:
            fail("the SMDH says %r, but GameConfig.xml says %r" % (titles[0], expected))
    print("SMDH: %r / %r / %r" % titles[0])

    if not any(smdh[0x24C0:0x36C0]):
        fail("the large icon is blank")

    hatch = find_in_romfs(exported[romfs_offset:], "Data.hatch")
    entries = read_hatch(hatch)
    print("RomFS: Data.hatch, %d bytes, %d entries" % (len(hatch), len(entries)))

    checked = 0
    for directory, _, files in os.walk(resources):
        for filename in files:
            if filename.startswith("."):
                continue

            path = os.path.join(directory, filename)
            name = os.path.relpath(path, resources).replace(os.sep, "/")
            name_hash = zlib.crc32(name.encode("utf-8"))

            if name_hash not in entries:
                fail("%s is not in Data.hatch" % name)
            if entries.pop(name_hash) != open(path, "rb").read():
                fail("%s is not the same in Data.hatch" % name)

            checked += 1

    if entries:
        fail("Data.hatch has %d entries no file in the Resources folder matches" % len(entries))

    print("OK: %d file(s) match the Resources folder byte for byte" % checked)


if __name__ == "__main__":
    main()
