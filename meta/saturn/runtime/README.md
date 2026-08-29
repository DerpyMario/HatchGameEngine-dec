# SEGA Saturn runtime

What a Saturn export is built out of. The exporter copies these next to the
scene's data and generates a `main.c`, an `ip_header.inc` and a `Makefile`
around them.

| File | What it does |
| --- | --- |
| `saturn.h` | the machine: register addresses, colour, fixed point |
| `crt0.s` | the first instruction: a stack, a cleared BSS, a call to `main` |
| `saturn.ld` | where the pieces go -- one blob at 0x06004000, big endian |
| `vdp.c` | VDP2 as a bitmap background, VDP1 as a polygon list |
| `scene3d.c` | transform, cull, sort and draw a 3D scene |
| `pad.c` | the control pad, through the SMPC |
| `string.c` | `memset` and `memcpy`, which GCC emits calls to under `-nostdlib` |
| `ip.s` | the disc header, and the code the console jumps to |
| `ip_header.inc` | a default header; the exporter overwrites it with the scene's |

## No SDK

This is bare metal. There is no SGL and no SBL: every register is written
directly, so a stock `sh-elf-gcc` builds it. The toolchain from
[SaturnSDK](https://github.com/SaturnSDK) works and so does any other -- the
generated `Makefile` takes `SH_PREFIX`.

That is a deliberate trade. SGL would hand the 3D side a matrix stack, a clipper
and a scene graph, and it is what a Saturn game would really use. It also cannot
be redistributed, and an export that only builds for people who already have
SEGA's libraries is not much of an export.

## How a Saturn boots this

The console reads sector zero of the disc, which is outside the filesystem, and
checks it begins `SEGA SEGASATURN`. It takes the addresses out of the header,
copies the header to 0x06002000, loads the first file in the root directory to
0x06004000, and jumps to 0x06002E00 -- offset 0xE00 of the header, where `ip.s`
puts a jump to the program.

`mkisofs -G IP.BIN` is what puts the header in the system area, and the program
is named `0.BIN` so it sorts first in the root directory.

Offsets 0x100 to 0xDFF of the header hold SEGA's security code on a real disc.
Nothing here forges it: it is left as zero, which emulators accept and a retail
console does not.
