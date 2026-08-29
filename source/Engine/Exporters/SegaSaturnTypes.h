#ifndef ENGINE_EXPORTERS_SEGASATURNTYPES_H
#define ENGINE_EXPORTERS_SEGASATURNTYPES_H

/* The SEGA Saturn uses the VDP1 for sprites and polygons and the VDP2 for
 * backgrounds. For a simple export, we use bitmap mode with the VDP1 to draw
 * a scene layer as a single large sprite or use VDP2 background planes.
 * 
 * The Saturn has 512 KB of frame RAM and supports 32768 colors (15-bit RGB,
 * 5 bits per channel). A palette-based approach can use up to 255 colors
 * plus transparency.
 */

#define SATURN_SCREEN_WIDTH     352
#define SATURN_SCREEN_HEIGHT    224
#define SATURN_MAX_IMAGE_BYTES  (512 * 1024)   /* 512 KB max for image data */
#define SATURN_PALETTE_USABLE   255            /* Max colors (one reserved for transparency) */

/* Color format: 15-bit RGB (5 bits per channel), stored as 0x0RRRRRGG GGGBBBBB */
#define SATURN_COLOR_WORD(r, g, b) \
    ((Uint16)((((Uint32)(r)) >> 3) | ((((Uint32)(g)) >> 3) << 5) | ((((Uint32)(b)) >> 3) << 10)))

/* GatherColors returns one of these per distinct color found in the scene */
struct SaturnColorUse {
    Uint32 Color;
    size_t Count;
};

/* Result structure returned by the Saturn exporter */
struct SegaSaturnExportResult {
    bool   Success;

    int    ImageWidth;         /* what was written, in pixels */
    int    ImageHeight;
    int    LayerWidth;         /* what the layer wanted, same units */
    int    LayerHeight;

    int    PaletteCount;       /* colors actually used, 1..255 */
    int    ColorsFound;
    int    ColorsDropped;      /* reduced away to fit, 0 when art already fit */

    size_t ImageBytes;

    char   Message[512];
};

#endif /* ENGINE_EXPORTERS_SEGASATURNTYPES_H */
