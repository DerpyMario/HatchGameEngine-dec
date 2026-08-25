#ifndef ENGINE_EXPORTERS_SEGA32XTYPES_H
#define ENGINE_EXPORTERS_SEGA32XTYPES_H

// The 32X does not draw through the Mega Drive's tile grid. It brings two SH-2s
// and a VDP of its own with 256 KB of DRAM held as two framebuffers, and in
// packed-pixel mode a framebuffer is exactly what it sounds like: one byte per
// pixel, indexing a palette of 256. A scene going to a 32X is therefore not cut
// into tiles at all -- it is drawn, the way the engine draws it.

#define X32_SCREEN_WIDTH    320
#define X32_SCREEN_HEIGHT   224

// Colour is fifteen bits, five a channel, red low. Five times the shades per
// channel the Mega Drive's three bits allow.
#define X32_PALETTE_SIZE    256
#define X32_COLOR_WORD(r, g, b) \
    ((Uint16)((((Uint32)(r)) >> 3) | ((((Uint32)(g)) >> 3) << 5) | ((((Uint32)(b)) >> 3) << 10)))

// A framebuffer opens with a table of word offsets, one per line, and the
// pixels follow. Sega's own code starts them at word 0x100 and Marsdev's
// follows it, so a line is 0x100 + n * 160 words in.
#define X32_LINE_TABLE_WORDS 0x100
#define X32_LINE_WORDS       (X32_SCREEN_WIDTH / 2)

// Index 0 is not a colour: it lets the Mega Drive's own output show through.
// The engine's transparent pixels become it, so art starts at 1.
#define X32_PALETTE_USABLE  255

// A bitmap large enough to scroll around is still a bitmap sitting in ROM, and
// a cartridge is not endless. This is the point past which the export says so
// rather than quietly writing something that will not fit.
#define X32_MAX_IMAGE_BYTES (2 * 1024 * 1024)

// GatherColors hands one of these back per distinct colour, and makeheaders
// puts that signature in the generated header -- which can only see what the
// INTERFACE block includes. So it lives here rather than in the .cpp.
struct X32ColorUse {
    Uint32 Color;
    size_t Count;
};

struct Sega32XExportResult {
    bool   Success;

    int    ImageWidth;         // what was written, in pixels
    int    ImageHeight;
    int    LayerWidth;         // what the layer wanted, same units
    int    LayerHeight;

    int    PaletteCount;       // colours actually used, 1..255
    int    ColorsFound;
    int    ColorsDropped;      // reduced away to fit, 0 when the art already fit

    size_t ImageBytes;

    char   Message[512];
};

#endif /* ENGINE_EXPORTERS_SEGA32XTYPES_H */
