# SEGA Dreamcast runtime

What a Dreamcast export is built out of. The exporter copies these next to the
scene's data and generates a `main.c` and a `Makefile` around them.

| File | What it does |
| --- | --- |
| `dreamcast.h` | the shapes the scene comes over as, and what the runtime offers |
| `scene2d.c` | a tile scene as PowerVR textures, drawn as quads and scrolled |
| `scene3d.c` | a 3D scene transformed on the SH-4 and drawn as PowerVR polygons |
| `pad.c` | the controller, through the maple bus |

## This one uses a library

The Saturn and 32X exports write hardware registers directly, because on those
machines there is nothing else to write. Here there is:
[KallistiOS](https://github.com/KallistiOS/KallistiOS) is the Dreamcast's
homebrew operating system, with drivers, a C library and a filesystem, and
reimplementing it to avoid the dependency would make a worse export rather than
a purer one.

So what is here is only the part above KOS: turning an exported scene into
polygons. It is much less code than the Saturn runtime for that reason.

## What the hardware does for you

The PowerVR2 is a tile-based deferred renderer. Opaque geometry is resolved per
pixel in hardware, so there is no depth sort here -- faces go out in whatever
order they are stored and come back correct. The Saturn runtime next door has to
sort every face back to front every frame, because its VDP1 has no such thing.

The SH-4 has a floating point unit, so the transform is ordinary floats rather
than the 16.16 fixed point the SH-2 needed.

## Things that cost time to find out

- The PVR culls counter-clockwise faces **by default**. Leave that on while also
  culling in software and every face disappears; the screen comes up black and
  looks like nothing is running.
- Z is 1/w, not a distance, and larger means nearer.
- A face colour with a zero alpha byte is not drawn.
- Textures must be a power of two and at most 1024 on a side, which is why a
  layer is cut into squares.
- The Dreamcast is little endian. Every other SEGA machine this engine exports
  to is big endian, and the exported data is written the other way round for
  this target alone.

## Data

The scene's data goes in a romdisk, which KOS links into the binary and mounts
at `/rd`, so the program opens real files and the disc does not have to carry a
filesystem of its own.
