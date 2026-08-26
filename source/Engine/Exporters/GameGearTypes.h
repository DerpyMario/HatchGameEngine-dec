#ifndef ENGINE_EXPORTERS_GAMEGEARTYPES_H
#define ENGINE_EXPORTERS_GAMEGEARTYPES_H

// The Game Gear is a Master System that fits in a pocket, and its VDP is the
// Master System's -- which is not the Mega Drive's. Everything about a tile
// differs except how many bytes it takes:
//
//   The Mega Drive stores four bits per pixel packed two to a byte. This VDP
//   stores them as four bitplanes, one bit of every pixel in each, four bytes
//   to a row. Same 32 bytes, entirely different arrangement.
//
//   Colour is twelve bits here, four per channel, against the Mega Drive's
//   nine. That is the Game Gear's own improvement: the Master System it grew
//   out of had six.
//
//   There are two palettes of sixteen rather than four of sixteen, and a
//   nametable entry picks between them with one bit.

#define GG_TILE_SIZE        8
#define GG_TILE_BYTES       32

// Two palettes: the background's and the sprites', and a background tile may
// use either. Entry zero of each is left as the backdrop, so art has fifteen.
#define GG_PALETTE_COUNT    2
#define GG_PALETTE_SIZE     16
#define GG_PALETTE_USABLE   15

// Four bits a channel, red low, packed into two bytes little endian.
#define GG_COLOR_WORD(r, g, b) \
    ((Uint16)((((Uint32)(r)) >> 4) | ((((Uint32)(g)) >> 4) << 4) | ((((Uint32)(b)) >> 4) << 8)))

// The nametable is the Master System's 32x28, and the Game Gear's screen shows
// the middle 160x144 of it -- twenty tiles by eighteen, starting six across and
// three down. Writing the art at that offset puts it on screen with the scroll
// registers left alone.
#define GG_PLANE_WIDTH      32
#define GG_PLANE_HEIGHT     28
#define GG_SCREEN_TILES_X   20
#define GG_SCREEN_TILES_Y   18
#define GG_VIEWPORT_X       6
#define GG_VIEWPORT_Y       3

// VRAM is 16 KB with the nametable at 0x3800 and the sprite table above it, so
// what is left below for patterns is 0x3800 bytes: 448 tiles. The Mega Drive
// allows 2048. This is the limit that actually bites on this machine.
#define GG_VRAM_NAMETABLE   0x3800
#define GG_MAX_TILES        (GG_VRAM_NAMETABLE / GG_TILE_BYTES)

struct GameGearTile {
    Uint8 Data[GG_TILE_BYTES];
};

// One 8x8 cell of a layer before it becomes indices. A pixel is either
// GG_TRANSPARENT or an 0x00RRGGBB already rounded to the twelve bits kept.
struct GameGearCell {
    Uint32 Pixels[GG_TILE_SIZE * GG_TILE_SIZE];
    int    Palette;
};

struct GameGearPalette {
    Uint32 Colors[GG_PALETTE_USABLE];
    int    Count;
};

struct GameGearColorUse {
    Uint32 Color;
    size_t Count;
};

struct GameGearExportResult {
    bool   Success;

    int    TileCount;          // unique tiles written
    int    TilesBeforeDedupe;  // how many there would have been without it
    int    PaletteCount;
    int    ColorsFound;
    int    ColorsDropped;      // reduced away to fit, 0 when the art already fit

    int    MapWidth;           // the map put in ROM, in 8x8 cells
    int    MapHeight;
    int    LayerWidth;         // what the scene layer wanted, same units
    int    LayerHeight;

    size_t TileBytes;
    size_t MapBytes;

    char   Message[512];
};

#endif /* ENGINE_EXPORTERS_GAMEGEARTYPES_H */
