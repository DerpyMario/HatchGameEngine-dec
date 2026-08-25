#ifndef ENGINE_EXPORTERS_MEGACDTYPES_H
#define ENGINE_EXPORTERS_MEGACDTYPES_H

#include <Engine/Exporters/SegaVDPTypes.h>

// Where the scene's art lands in the Mega Drive's VRAM once the disc has been
// read. The Mega CD adds no graphics hardware, so this is the same 64 KB the
// cartridge export works in -- what the disc changes is that the art does not
// have to be inside the program to get there.
//
// These are not chosen: they are what the Mega CD's own BIOS sets up, and the
// generated program leaves them alone. Everything from 0xB800 up belongs to
// the tables the VDP reads every frame, so the patterns get what is below.
#define MCD_VRAM_TILES      0x0000
#define MCD_VRAM_SPRITES    0xB800
#define MCD_VRAM_HSCROLL    0xBC00
#define MCD_VRAM_PLANE      0xC000

#define MCD_TILE_BUDGET     ((MCD_VRAM_SPRITES - MCD_VRAM_TILES) / MD_TILE_BYTES)

// The screen the window onto the plane is, in pixels.
#define MCD_SCREEN_WIDTH    320
#define MCD_SCREEN_HEIGHT   224

// What the export did, in enough detail for the caller to say so. Everything
// here is filled in whether or not it succeeded, so a failure can still report
// how far it got and by how much the scene missed.
struct MegaCDExportResult {
    bool   Success;

    int    TileCount;          // unique tiles written
    int    TilesBeforeDedupe;  // how many there would have been without it
    int    PaletteCount;
    int    ColorsFound;
    int    ColorsDropped;      // reduced away to fit, 0 when the art already fit

    int    PlaneWidth;         // the plane the disc sets up, in 8x8 cells
    int    PlaneHeight;
    int    LayerWidth;         // what the scene layer wanted, same units
    int    LayerHeight;

    size_t TileBytes;
    size_t MapBytes;
    size_t FileBytes;          // the one file the disc carries

    char   Message[512];
};

#endif /* ENGINE_EXPORTERS_MEGACDTYPES_H */
