#ifndef ENGINE_EXPORTERS_SEGAVDPTYPES_H
#define ENGINE_EXPORTERS_SEGAVDPTYPES_H

// The shapes the Mega Drive's VDP works in, which are the Mega CD's too: the
// disc drive adds storage, processors and RAM to the machine, and not one
// pixel of new graphics hardware. Both exports convert their art into these.
//
// Tiles are 8x8 at four bits per pixel: 32 bytes each, one row every four
// bytes, the high nibble being the leftmost pixel of its pair.
#define MD_TILE_SIZE        8
#define MD_TILE_BYTES       32

// Four palettes of sixteen entries. Entry zero of each is not a colour of its
// own -- it is the backdrop showing through -- so fifteen are available to art.
#define MD_PALETTE_COUNT    4
#define MD_PALETTE_SIZE     16
#define MD_PALETTE_USABLE   15

// A nametable entry addresses a tile with eleven bits, and the plane it lives
// in can be 32, 64 or 128 tiles on a side as long as the two multiplied stay
// within 4096.
#define MD_MAX_TILES        2048
#define MD_MAX_PLANE_CELLS  4096

struct SegaVDPTile {
    Uint8 Data[MD_TILE_BYTES];
};

// One 8x8 cell lifted out of a scene layer, before it has been turned into
// indices. A pixel is either MD_TRANSPARENT or an 0x00RRGGBB already rounded
// into the nine bits the VDP keeps.
struct SegaVDPCell {
    Uint32 Pixels[MD_TILE_SIZE * MD_TILE_SIZE];
    int    Palette;
};

struct SegaVDPPalette {
    Uint32 Colors[MD_PALETTE_USABLE];
    int    Count;
};

// What one conversion produced, in numbers. Both exports copy what they need
// of this into their own result, since what they say afterwards differs: one
// is talking about a cartridge and the other about a disc.
struct SegaVDPConversion {
    int    TileCount;          // unique tiles produced
    int    TilesBeforeDedupe;  // how many there would have been without it
    int    PaletteCount;
    int    ColorsFound;
    int    ColorsDropped;      // reduced away to fit, 0 when the art already fit

    int    PlaneWidth;         // the plane the conversion filled, in 8x8 cells
    int    PlaneHeight;

    size_t TileBytes;
    size_t MapBytes;

    char   Failure[256];       // why it stopped, when it did
};

#endif /* ENGINE_EXPORTERS_SEGAVDPTYPES_H */
