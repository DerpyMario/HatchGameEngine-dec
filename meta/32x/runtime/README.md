# 32X runtime

The parts of a 32X program that are the same whatever it displays: the boot
header and 68000 stub that hands the cartridge over to the SH-2s, the SH-2
startup and vector table, the linker scripts that put each where it belongs,
and the hardware helpers that bring the 32X VDP up.

These come from the 32X skeleton in [Marsdev](https://github.com/andwn/marsdev)
by Andrew DeRosier, under the MIT licence in `LICENSE.marsdev`. They are kept
here rather than fetched so that an export is a complete project someone can
build without first assembling a toolchain tree by hand.

The exporter copies this directory into every 32X project it writes and adds
the parts that are not the same every time: `sh_src/m_main.c`, which puts the
scene on the screen, and the scene's own palette and bitmap under `res/`.

Nothing in here is generated. Editing a file here changes every export made
afterwards.
