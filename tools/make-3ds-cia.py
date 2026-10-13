#!/usr/bin/env python3
"""Makes an installable .cia out of a game exported with --export-3ds.

    make-3ds-cia.py <game.3dsx> <runtime.elf> <game.cia>
                    [--makerom PATH] [--bannertool PATH]
                    [--banner PNG] [--sound WAV]
                    [--unique-id 0xF8000-0xFFFFF] [--product-code CTR-H-XXXX]

A .3dsx runs from the Homebrew Launcher. A .cia is installed -- with FBI, say
-- and then sits on the HOME Menu like any other title. The two hold the same
game: this takes the title, the icon and Data.hatch out of the .3dsx the export
wrote, and has makerom put them together with the engine's code.

makerom wants that code as the ELF the runtime .3dsx was made from, not as the
.3dsx, which is why the runtime's .elf is needed too. It has to be the same
build: the engine and the code in the .3dsx are one program.

makerom comes from Project_CTR, and bannertool makes the banner the HOME Menu
shows on the top screen when the title is selected; neither is part of
devkitPro. The banner and its sound default to meta/3ds/banner.png and
meta/3ds/banner.wav. The title's unique ID is worked out from its name, inside
the range homebrew uses, unless one is given.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
META = os.path.join(os.path.dirname(HERE), "meta", "3ds")

SMDH_SIZE = 0x36C0
ROMFS_NONE = 0xFFFFFFFF


def fail(message):
    print("make-3ds-cia: " + message, file=sys.stderr)
    sys.exit(1)


def read_3dsx(data):
    if data[:4] != b"3DSX":
        fail("not a .3dsx")

    header_size = struct.unpack_from("<H", data, 4)[0]
    if header_size < 0x2C:
        fail("the .3dsx has no SMDH or RomFS; export it with --export-3ds")

    smdh_offset, smdh_size, romfs_offset = struct.unpack_from("<III", data, 0x20)
    if smdh_size != SMDH_SIZE or data[smdh_offset:smdh_offset + 4] != b"SMDH":
        fail("the .3dsx has no SMDH")
    if romfs_offset == 0:
        fail("the .3dsx has no RomFS; export it with --export-3ds")

    return data[smdh_offset:smdh_offset + smdh_size], data[romfs_offset:]


# Every file at the root of the RomFS, walked through the root directory's list
# of files. The export only ever puts Data.hatch there.
def romfs_root_files(romfs):
    dir_off, file_off, data_off = struct.unpack_from("<I", romfs, 0xC)[0], \
        struct.unpack_from("<I", romfs, 0x1C)[0], struct.unpack_from("<I", romfs, 0x24)[0]

    files = {}
    offset = struct.unpack_from("<I", romfs, dir_off + 0xC)[0]
    while offset != ROMFS_NONE:
        _, sibling, data_offset, data_size, _, name_len = struct.unpack_from(
            "<IIQQII", romfs, file_off + offset)
        name = romfs[file_off + offset + 0x20:file_off + offset + 0x20 + name_len].decode("utf-16-le")
        start = data_off + data_offset
        files[name] = romfs[start:start + data_size]
        offset = sibling

    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("game_3dsx")
    parser.add_argument("runtime_elf")
    parser.add_argument("output_cia")
    parser.add_argument("--makerom", default="makerom")
    parser.add_argument("--bannertool", default="bannertool")
    parser.add_argument("--banner", default=os.path.join(META, "banner.png"))
    parser.add_argument("--sound", default=os.path.join(META, "banner.wav"))
    parser.add_argument("--rsf", default=os.path.join(META, "cia.rsf"))
    parser.add_argument("--unique-id")
    parser.add_argument("--product-code", default="CTR-H-HTCH")
    args = parser.parse_args()

    smdh, romfs = read_3dsx(open(args.game_3dsx, "rb").read())
    files = romfs_root_files(romfs)
    if "Data.hatch" not in files:
        fail("there is no Data.hatch in the .3dsx's RomFS")

    # English is the second of the sixteen languages.
    title = smdh[8 + 0x200:8 + 0x200 + 0x80].decode("utf-16-le").split("\0")[0] or "Hatch Game"

    if args.unique_id:
        unique_id = int(args.unique_id, 0)
    else:
        unique_id = 0xF8000 + zlib.crc32(title.encode("utf-8")) % 0x8000
    if not 0xF8000 <= unique_id <= 0xFFFFF:
        fail("unique ID %#x is outside the homebrew range 0xF8000-0xFFFFF" % unique_id)

    # makerom reads the title as a word on its command line.
    short_name = re.sub(r"[^A-Za-z0-9]", "", title)[:16] or "HatchGame"

    work = tempfile.mkdtemp(prefix="hatch-cia-")
    try:
        romfs_dir = os.path.join(work, "romfs")
        os.makedirs(romfs_dir)
        with open(os.path.join(romfs_dir, "Data.hatch"), "wb") as f:
            f.write(files["Data.hatch"])

        icon = os.path.join(work, "icon.smdh")
        with open(icon, "wb") as f:
            f.write(smdh)

        banner = os.path.join(work, "banner.bnr")
        subprocess.run([args.bannertool, "makebanner",
                        "-i", args.banner, "-a", args.sound, "-o", banner],
                       check=True, stdout=subprocess.DEVNULL)

        subprocess.run([args.makerom, "-f", "cia", "-o", args.output_cia,
                        "-target", "t", "-exefslogo",
                        "-rsf", args.rsf,
                        "-elf", args.runtime_elf,
                        "-icon", icon,
                        "-banner", banner,
                        "-DAPP_TITLE=" + short_name,
                        "-DAPP_PRODUCT_CODE=" + args.product_code,
                        "-DAPP_UNIQUE_ID=%#x" % unique_id,
                        "-DAPP_ROMFS=" + romfs_dir],
                       check=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print("%s: \"%s\", unique ID %#07x, %d bytes" % (
        args.output_cia, title, unique_id, os.path.getsize(args.output_cia)))


if __name__ == "__main__":
    main()
