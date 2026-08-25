#ifndef ENGINE_EXPORTERS_MEGADRIVETYPES_H
#define ENGINE_EXPORTERS_MEGADRIVETYPES_H

#include <Engine/Exporters/SegaVDPTypes.h>

// What the export did, in enough detail for the caller to say so. Everything
// here is filled in whether or not it succeeded, so a failure can still report
// how far it got and by how much the scene missed.
struct MegaDriveExportResult {
    bool   Success;

    int    TileCount;          // unique tiles written
    int    TilesBeforeDedupe;  // how many there would have been without it
    int    PaletteCount;
    int    ColorsFound;
    int    ColorsDropped;      // reduced away to fit, 0 when the art already fit

    int    PlaneWidth;         // the plane the ROM sets up, in 8x8 cells
    int    PlaneHeight;
    int    LayerWidth;         // what the scene layer wanted, same units
    int    LayerHeight;

    size_t TileBytes;
    size_t MapBytes;

    char   Message[512];
};

#endif /* ENGINE_EXPORTERS_MEGADRIVETYPES_H */
