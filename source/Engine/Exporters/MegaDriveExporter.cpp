#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/MegaDriveTypes.h>

need_t SceneLayer;

class MegaDriveExporter {
public:

};
#endif

#include <Engine/Exporters/MegaDriveExporter.h>

#include <Engine/Application.h>
#include <Engine/Graphics.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/Scene.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Sprites/Animation.h>
#include <Engine/Rendering/Texture.h>
#include <Engine/ResourceTypes/ISprite.h>
#include <Engine/Types/Tileset.h>
#include <Engine/Utilities/StringUtils.h>

// Turning a Hatch scene into something a Mega Drive can show.
//
// The engine itself does not run on the console and cannot be made to: its
// memory pools alone ask for more than three hundred times the machine's work
// RAM, and its software rasteriser wants a framebuffer where the VDP offers
// only a grid of tiles. What does cross over is the scene's art. This takes a
// tile layer and produces the three things the hardware actually reads -- a set
// of palettes, a set of 8x8 patterns, and a nametable pointing at them -- then
// writes an SGDK project around them that builds into a ROM.
//
// Three properties of the hardware shape all of it:
//
//   A colour is nine bits, three per channel. Two source colours that differ
//   only below that are one colour here, so everything is rounded into that
//   space before it is counted.
//
//   A nametable entry names one palette for the whole tile. Palettes therefore
//   cannot be handed out by how popular a colour is across the layer -- they
//   have to be fitted tile by tile, because a tile whose colours are spread
//   across two palettes cannot be drawn at all.
//
//   A tile carries a flip bit per axis, so a mirrored tile costs nothing beyond
//   the entry that names it. Every pattern is looked up against those already
//   emitted in all four orientations before another 32 bytes of VRAM is spent.

// A colour as the VDP stores it: three bits a channel, blue high, and the low
// bit of every nibble unused.
#define MD_COLOR_WORD(r, g, b) \
    ((Uint16)(((((Uint32)(b)) >> 5) << 9) | ((((Uint32)(g)) >> 5) << 5) | ((((Uint32)(r)) >> 5) << 1)))

// Distinguishable from any 0x00RRGGBB an opaque pixel reduces to.
#define MD_TRANSPARENT 0xFFFFFFFFU

static vector<MDCell>        Cells;          // one per plane cell, row major
static vector<MegaDriveTile> Patterns;       // the unique 8x8 patterns
static vector<Uint16>        Nametable;
static MDPalette             Palettes[MD_PALETTE_COUNT];
static int                   PaletteCount;
static int                   ColorsMapped;   // colours that had to be approximated

// A texture holds its pixels in whatever order the renderer that created it
// wanted: the GL backend asks for ABGR and the software and Direct3D ones for
// ARGB. Reading them as though they were always ARGB is why the first ROM this
// produced came out with every red and blue exchanged.
//
// The engine has a converter for this, but it forces alpha opaque on the way
// through, and the transparency is needed here to tell a blank pixel from a
// black one. So the swap is done here instead, alpha left alone.
PRIVATE STATIC Uint32 MegaDriveExporter::ToARGB(Uint32 native) {
    if (Graphics::PreferredPixelFormat != SDL_PIXELFORMAT_ABGR8888)
        return native;

    return (native & 0xFF00FF00U) |
           ((native & 0x00FF0000U) >> 16) |
           ((native & 0x000000FFU) << 16);
}

PRIVATE STATIC Uint32 MegaDriveExporter::QuantizeColor(Uint32 native) {
    Uint32 pixel = MegaDriveExporter::ToARGB(native);

    if (((pixel >> 24) & 0xFF) < 128)
        return MD_TRANSPARENT;

    // Rounded into the nine bits the VDP keeps, then spread back over the full
    // range so comparisons and distances are done in the space it will land in.
    Uint32 r = (Uint32)((((pixel >> 16) & 0xFF) >> 5) * 255 / 7);
    Uint32 g = (Uint32)((((pixel >> 8) & 0xFF) >> 5) * 255 / 7);
    Uint32 b = (Uint32)(((pixel & 0xFF) >> 5) * 255 / 7);

    return (r << 16) | (g << 8) | b;
}

PRIVATE STATIC long MegaDriveExporter::ColorDistance(Uint32 a, Uint32 b) {
    long dr = (long)((a >> 16) & 0xFF) - (long)((b >> 16) & 0xFF);
    long dg = (long)((a >> 8) & 0xFF) - (long)((b >> 8) & 0xFF);
    long db = (long)(a & 0xFF) - (long)(b & 0xFF);

    return dr * dr + dg * dg + db * db;
}

// Reads a source tile's pixels the way the renderer reads them, so a tile that
// draws is a tile that exports.
PRIVATE STATIC bool MegaDriveExporter::GetTilePixels(size_t tileID, Uint32** pixels, int* stride, int* width, int* height) {
    if (tileID >= Scene::TileSpriteInfos.size())
        return false;

    TileSpriteInfo info = Scene::TileSpriteInfos[tileID];
    if (!info.Sprite || info.AnimationIndex < 0)
        return false;

    if ((size_t)info.AnimationIndex >= info.Sprite->Animations.size())
        return false;

    Animation& animation = info.Sprite->Animations[info.AnimationIndex];
    if (info.FrameIndex < 0 || (size_t)info.FrameIndex >= animation.Frames.size())
        return false;

    AnimFrame& frame = animation.Frames[info.FrameIndex];
    if (frame.SheetNumber < 0 || frame.SheetNumber >= 32)
        return false;

    Texture* texture = info.Sprite->Spritesheets[frame.SheetNumber];
    if (!texture || !texture->Pixels)
        return false;

    *stride = (int)texture->Width;
    *pixels = &((Uint32*)texture->Pixels)[frame.X + frame.Y * (int)texture->Width];
    *width = frame.Width;
    *height = frame.Height;

    return true;
}

// Cuts the layer into the 8x8 cells the VDP works in, stopping at the plane's
// edge. Source tiles are usually 16x16, so each becomes four cells.
PRIVATE STATIC void MegaDriveExporter::BuildCells(SceneLayer* layer, int planeW, int planeH) {
    int cellsPerTileX = Scene::TileWidth / MD_TILE_SIZE;
    int cellsPerTileY = Scene::TileHeight / MD_TILE_SIZE;

    Cells.assign((size_t)planeW * planeH, MDCell());

    for (size_t i = 0; i < Cells.size(); i++) {
        for (int p = 0; p < MD_TILE_SIZE * MD_TILE_SIZE; p++)
            Cells[i].Pixels[p] = MD_TRANSPARENT;

        Cells[i].Palette = 0;
    }

    for (int cy = 0; cy < planeH; cy++) {
        for (int cx = 0; cx < planeW; cx++) {
            int tileX = cx / cellsPerTileX;
            int tileY = cy / cellsPerTileY;
            if (tileX >= layer->Width || tileY >= layer->Height)
                continue;

            Uint32 entry = layer->Tiles[tileX + tileY * layer->Width];
            size_t tileID = entry & TILE_IDENT_MASK;
            if (tileID == Scene::EmptyTile)
                continue;

            Uint32* pixels; int stride, tileW, tileH;
            if (!MegaDriveExporter::GetTilePixels(tileID, &pixels, &stride, &tileW, &tileH))
                continue;

            // Which corner of the source tile this cell is. A flipped source
            // tile is read from the far side, so the flip is resolved here
            // rather than being left for the nametable -- the nametable's own
            // flip bits are spent on pattern reuse instead.
            bool flipX = (entry & TILE_FLIPX_MASK) != 0;
            bool flipY = (entry & TILE_FLIPY_MASK) != 0;

            int subX = cx % cellsPerTileX;
            int subY = cy % cellsPerTileY;
            if (flipX)
                subX = (cellsPerTileX - 1) - subX;
            if (flipY)
                subY = (cellsPerTileY - 1) - subY;

            MDCell* cell = &Cells[(size_t)cx + (size_t)cy * planeW];

            for (int py = 0; py < MD_TILE_SIZE; py++) {
                for (int px = 0; px < MD_TILE_SIZE; px++) {
                    int sx = subX * MD_TILE_SIZE + px;
                    int sy = subY * MD_TILE_SIZE + py;

                    if (flipX)
                        sx = (tileW - 1) - sx;
                    if (flipY)
                        sy = (tileH - 1) - sy;

                    if (sx < 0 || sy < 0 || sx >= tileW || sy >= tileH)
                        continue;

                    cell->Pixels[px + py * MD_TILE_SIZE] =
                        MegaDriveExporter::QuantizeColor(pixels[sx + sy * stride]);
                }
            }
        }
    }
}

PRIVATE STATIC void MegaDriveExporter::CellColors(MDCell* cell, vector<Uint32>* out) {
    out->clear();

    for (int i = 0; i < MD_TILE_SIZE * MD_TILE_SIZE; i++) {
        Uint32 color = cell->Pixels[i];
        if (color == MD_TRANSPARENT)
            continue;

        bool seen = false;
        for (size_t j = 0; j < out->size(); j++) {
            if ((*out)[j] == color) {
                seen = true;
                break;
            }
        }

        if (!seen)
            out->push_back(color);
    }
}

// How many of a cell's colours a palette is missing. -1 when the cell could
// never fit, which is the answer for a cell holding more colours than a palette
// has room for at all.
PRIVATE STATIC int MegaDriveExporter::MissingFrom(MDPalette* palette, vector<Uint32>* colors) {
    int missing = 0;

    for (size_t i = 0; i < colors->size(); i++) {
        bool found = false;
        for (int j = 0; j < palette->Count; j++) {
            if (palette->Colors[j] == (*colors)[i]) {
                found = true;
                break;
            }
        }

        if (!found)
            missing++;
    }

    return missing;
}

// Hands each cell a palette, opening new ones while there are any left and
// otherwise settling for the closest fit.
//
// This is a first-fit: cells are taken in the order they appear and put in the
// palette that is missing the fewest of their colours, which is the one most
// likely to have room for the next cell too. Optimal packing here is a graph
// colouring problem and not worth solving -- art drawn for this machine is
// drawn within these limits already, and art that was not needs to be told so
// rather than shuffled cleverly.
PRIVATE STATIC void MegaDriveExporter::AssignPalettes() {
    for (int i = 0; i < MD_PALETTE_COUNT; i++)
        Palettes[i].Count = 0;

    PaletteCount = 1;
    ColorsMapped = 0;

    vector<Uint32> colors;

    for (size_t c = 0; c < Cells.size(); c++) {
        MDCell* cell = &Cells[c];
        MegaDriveExporter::CellColors(cell, &colors);

        if (colors.size() == 0) {
            cell->Palette = 0;
            continue;
        }

        // The palette that already holds the most of what this cell needs and
        // still has room for the rest.
        int best = -1;
        int bestMissing = 0;

        for (int p = 0; p < PaletteCount; p++) {
            int missing = MegaDriveExporter::MissingFrom(&Palettes[p], &colors);
            if (Palettes[p].Count + missing > MD_PALETTE_USABLE)
                continue;

            if (best < 0 || missing < bestMissing) {
                best = p;
                bestMissing = missing;
            }
        }

        // Nothing had room, so open another palette while the hardware still
        // has one to give.
        if (best < 0 && PaletteCount < MD_PALETTE_COUNT &&
            (int)colors.size() <= MD_PALETTE_USABLE) {
            best = PaletteCount++;
            bestMissing = (int)colors.size();
        }

        if (best >= 0) {
            MDPalette* palette = &Palettes[best];
            for (size_t i = 0; i < colors.size(); i++) {
                if (MegaDriveExporter::MissingFrom(palette, &colors) == 0)
                    break;

                bool found = false;
                for (int j = 0; j < palette->Count; j++) {
                    if (palette->Colors[j] == colors[i]) {
                        found = true;
                        break;
                    }
                }

                if (!found)
                    palette->Colors[palette->Count++] = colors[i];
            }

            cell->Palette = best;
            continue;
        }

        // Out of palettes. The cell goes to whichever one is nearest and its
        // remaining colours get approximated when it is turned into indices.
        int closest = 0;
        long closestCost = -1;

        for (int p = 0; p < PaletteCount; p++) {
            long cost = 0;
            for (size_t i = 0; i < colors.size(); i++) {
                long nearest = -1;
                for (int j = 0; j < Palettes[p].Count; j++) {
                    long distance = MegaDriveExporter::ColorDistance(Palettes[p].Colors[j], colors[i]);
                    if (nearest < 0 || distance < nearest)
                        nearest = distance;
                }
                cost += nearest < 0 ? 0 : nearest;
            }

            if (closestCost < 0 || cost < closestCost) {
                closestCost = cost;
                closest = p;
            }
        }

        cell->Palette = closest;

        for (size_t i = 0; i < colors.size(); i++) {
            bool found = false;
            for (int j = 0; j < Palettes[closest].Count; j++) {
                if (Palettes[closest].Colors[j] == colors[i]) {
                    found = true;
                    break;
                }
            }

            if (!found)
                ColorsMapped++;
        }
    }
}

// Turns a cell into the 32 bytes the VDP reads: four bytes a row, the high
// nibble of each byte being the left pixel of its pair.
PRIVATE STATIC void MegaDriveExporter::PackCell(MDCell* cell, MegaDriveTile* out) {
    MDPalette* palette = &Palettes[cell->Palette];

    memset(out->Data, 0, MD_TILE_BYTES);

    for (int y = 0; y < MD_TILE_SIZE; y++) {
        for (int x = 0; x < MD_TILE_SIZE; x++) {
            Uint32 color = cell->Pixels[x + y * MD_TILE_SIZE];
            int index = 0;

            if (color != MD_TRANSPARENT) {
                int found = -1;
                for (int j = 0; j < palette->Count; j++) {
                    if (palette->Colors[j] == color) {
                        found = j;
                        break;
                    }
                }

                if (found < 0) {
                    // Approximated, as decided when the palette was assigned.
                    long nearest = -1;
                    for (int j = 0; j < palette->Count; j++) {
                        long distance = MegaDriveExporter::ColorDistance(palette->Colors[j], color);
                        if (nearest < 0 || distance < nearest) {
                            nearest = distance;
                            found = j;
                        }
                    }
                }

                // Slot zero is the backdrop, so art starts at one.
                index = found < 0 ? 0 : found + 1;
            }

            int byte = (x >> 1) + y * 4;
            if (x & 1)
                out->Data[byte] |= (Uint8)(index & 0x0F);
            else
                out->Data[byte] |= (Uint8)((index & 0x0F) << 4);
        }
    }
}

PRIVATE STATIC void MegaDriveExporter::FlipTile(MegaDriveTile* in, bool flipX, bool flipY, MegaDriveTile* out) {
    for (int y = 0; y < MD_TILE_SIZE; y++) {
        for (int x = 0; x < MD_TILE_SIZE; x++) {
            int sx = flipX ? (MD_TILE_SIZE - 1) - x : x;
            int sy = flipY ? (MD_TILE_SIZE - 1) - y : y;

            int sourceByte = (sx >> 1) + sy * 4;
            int index = (sx & 1) ? (in->Data[sourceByte] & 0x0F)
                                 : ((in->Data[sourceByte] >> 4) & 0x0F);

            int byte = (x >> 1) + y * 4;
            if (x & 1)
                out->Data[byte] = (Uint8)((out->Data[byte] & 0xF0) | index);
            else
                out->Data[byte] = (Uint8)((out->Data[byte] & 0x0F) | (index << 4));
        }
    }
}

// Finds a pattern among those already emitted, in any of its four
// orientations, and adds it if it is new. Returns the nametable's flip bits
// alongside the index.
PRIVATE STATIC int MegaDriveExporter::AddPattern(MegaDriveTile* tile, bool* flipX, bool* flipY) {
    MegaDriveTile variant;

    for (int orientation = 0; orientation < 4; orientation++) {
        bool fx = (orientation & 1) != 0;
        bool fy = (orientation & 2) != 0;

        MegaDriveExporter::FlipTile(tile, fx, fy, &variant);

        for (size_t i = 0; i < Patterns.size(); i++) {
            if (memcmp(Patterns[i].Data, variant.Data, MD_TILE_BYTES) == 0) {
                *flipX = fx;
                *flipY = fy;
                return (int)i;
            }
        }
    }

    *flipX = false;
    *flipY = false;

    Patterns.push_back(*tile);

    return (int)Patterns.size() - 1;
}

// The largest plane the VDP will accept that still covers the layer. Sides are
// 32, 64 or 128 cells and the two multiplied have to stay within 4096.
PRIVATE STATIC void MegaDriveExporter::ChoosePlaneSize(int wantW, int wantH, int* planeW, int* planeH) {
    static const int sides[3] = { 32, 64, 128 };

    int bestW = 32, bestH = 32;
    long bestCovered = -1;

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            int w = sides[i], h = sides[j];
            if (w * h > MD_MAX_PLANE_CELLS)
                continue;

            // How much of what was asked for this shape actually shows.
            long covered = (long)(w < wantW ? w : wantW) * (h < wantH ? h : wantH);

            // Ties go to the smaller plane, since the nametable is VRAM too.
            if (covered > bestCovered ||
                (covered == bestCovered && w * h < bestW * bestH)) {
                bestCovered = covered;
                bestW = w;
                bestH = h;
            }
        }
    }

    *planeW = bestW;
    *planeH = bestH;
}

PRIVATE STATIC bool MegaDriveExporter::WriteBinary(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;

    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);

    return ok;
}

PRIVATE STATIC bool MegaDriveExporter::WriteText(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (!f)
        return false;

    bool ok = fputs(text, f) >= 0;
    fclose(f);

    return ok;
}

PRIVATE STATIC SceneLayer* MegaDriveExporter::PickLayer() {
    // The first visible layer that actually has tiles. A scene's later layers
    // are foreground and overlay work that needs the second plane and the
    // sprite engine, neither of which this writes yet.
    for (size_t i = 0; i < Scene::Layers.size(); i++) {
        SceneLayer* layer = &Scene::Layers[i];
        if (layer->Visible && layer->Tiles && layer->Width > 0 && layer->Height > 0)
            return layer;
    }

    return NULL;
}

PUBLIC STATIC MegaDriveExportResult MegaDriveExporter::ExportScene(const char* outputPath) {
    MegaDriveExportResult result;
    memset(&result, 0, sizeof(result));

    SceneLayer* layer = MegaDriveExporter::PickLayer();
    if (!layer) {
        StringUtils::Copy(result.Message, "The scene has no visible tile layer to export.", sizeof(result.Message));
        return result;
    }

    if (Scene::TileWidth < MD_TILE_SIZE || Scene::TileHeight < MD_TILE_SIZE ||
        (Scene::TileWidth % MD_TILE_SIZE) || (Scene::TileHeight % MD_TILE_SIZE)) {
        snprintf(result.Message, sizeof(result.Message),
            "The scene's tiles are %dx%d. The VDP works in 8x8 cells, so tiles have to be a whole number of those on each side.",
            Scene::TileWidth, Scene::TileHeight);
        return result;
    }

    int cellsPerTileX = Scene::TileWidth / MD_TILE_SIZE;
    int cellsPerTileY = Scene::TileHeight / MD_TILE_SIZE;

    result.LayerWidth = layer->Width * cellsPerTileX;
    result.LayerHeight = layer->Height * cellsPerTileY;

    int planeW, planeH;
    MegaDriveExporter::ChoosePlaneSize(result.LayerWidth, result.LayerHeight, &planeW, &planeH);

    result.PlaneWidth = planeW;
    result.PlaneHeight = planeH;

    Patterns.clear();
    Nametable.clear();

    MegaDriveExporter::BuildCells(layer, planeW, planeH);
    MegaDriveExporter::AssignPalettes();

    result.PaletteCount = PaletteCount;
    result.ColorsDropped = ColorsMapped;

    for (int i = 0; i < PaletteCount; i++)
        result.ColorsFound += Palettes[i].Count;
    result.ColorsFound += ColorsMapped;

    // An entirely empty tile is worth having at index zero: every blank cell in
    // the plane then points at the same 32 bytes.
    Nametable.resize(Cells.size());
    result.TilesBeforeDedupe = (int)Cells.size();

    MegaDriveTile packed;
    for (size_t i = 0; i < Cells.size(); i++) {
        MegaDriveExporter::PackCell(&Cells[i], &packed);

        bool flipX, flipY;
        int index = MegaDriveExporter::AddPattern(&packed, &flipX, &flipY);

        if ((int)Patterns.size() > MD_MAX_TILES) {
            snprintf(result.Message, sizeof(result.Message),
                "The layer needs more than %d unique tiles, which is all a nametable entry can address.",
                MD_MAX_TILES);
            return result;
        }

        // Priority stays low: everything here is background.
        Nametable[i] = (Uint16)(((Cells[i].Palette & 3) << 13) |
                                ((flipY ? 1 : 0) << 12) |
                                ((flipX ? 1 : 0) << 11) |
                                (index & 0x07FF));
    }

    result.TileCount = (int)Patterns.size();
    result.TileBytes = Patterns.size() * MD_TILE_BYTES;
    result.MapBytes = Nametable.size() * 2;

    if (!MegaDriveExporter::WriteProject(outputPath, &result))
        return result;

    result.Success = true;

    if (result.LayerWidth > planeW || result.LayerHeight > planeH) {
        snprintf(result.Message, sizeof(result.Message),
            "Exported %d tiles in %d palette(s). The layer is %dx%d cells and the largest plane the VDP offers is %dx%d, so the ROM shows the top-left %dx%d of it.",
            result.TileCount, result.PaletteCount,
            result.LayerWidth, result.LayerHeight, planeW, planeH, planeW, planeH);
    }
    else if (result.ColorsDropped) {
        snprintf(result.Message, sizeof(result.Message),
            "Exported %d tiles in %d palette(s). %d cell colour(s) did not fit the four palettes and were matched to the nearest that did.",
            result.TileCount, result.PaletteCount, result.ColorsDropped);
    }
    else {
        snprintf(result.Message, sizeof(result.Message),
            "Exported %d tiles in %d palette(s), %d bytes of tile data and %d of map.",
            result.TileCount, result.PaletteCount,
            (int)result.TileBytes, (int)result.MapBytes);
    }

    return result;
}

PRIVATE STATIC bool MegaDriveExporter::WriteProject(const char* outputPath, MegaDriveExportResult* result) {
    char path[1024];

    if (!Directory::Exists(outputPath) && !Directory::CreatePath(outputPath)) {
        snprintf(result->Message, sizeof(result->Message), "Could not create \"%s\".", outputPath);
        return false;
    }

    snprintf(path, sizeof(path), "%s/res", outputPath);
    if (!Directory::Exists(path) && !Directory::CreatePath(path)) {
        snprintf(result->Message, sizeof(result->Message), "Could not create \"%s\".", path);
        return false;
    }

    snprintf(path, sizeof(path), "%s/src", outputPath);
    if (!Directory::Exists(path) && !Directory::CreatePath(path)) {
        snprintf(result->Message, sizeof(result->Message), "Could not create \"%s\".", path);
        return false;
    }

    // --- the palettes, as the VDP's own colour words ---
    Uint16 paletteData[MD_PALETTE_COUNT * MD_PALETTE_SIZE];
    memset(paletteData, 0, sizeof(paletteData));

    for (int p = 0; p < MD_PALETTE_COUNT; p++) {
        for (int i = 0; i < Palettes[p].Count; i++) {
            Uint32 color = Palettes[p].Colors[i];
            paletteData[p * MD_PALETTE_SIZE + i + 1] = MD_COLOR_WORD(
                (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        }
    }

    // The 68000 is big endian and so is everything the VDP reads, so the words
    // go out high byte first regardless of what wrote them.
    vector<Uint8> bytes;

    bytes.clear();
    for (size_t i = 0; i < MD_PALETTE_COUNT * MD_PALETTE_SIZE; i++) {
        bytes.push_back((Uint8)(paletteData[i] >> 8));
        bytes.push_back((Uint8)(paletteData[i] & 0xFF));
    }

    snprintf(path, sizeof(path), "%s/res/palette.bin", outputPath);
    if (!MegaDriveExporter::WriteBinary(path, bytes.data(), bytes.size())) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // --- the tile patterns ---
    bytes.clear();
    for (size_t i = 0; i < Patterns.size(); i++) {
        for (int j = 0; j < MD_TILE_BYTES; j++)
            bytes.push_back(Patterns[i].Data[j]);
    }

    snprintf(path, sizeof(path), "%s/res/tiles.bin", outputPath);
    if (!MegaDriveExporter::WriteBinary(path, bytes.data(), bytes.size())) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // --- the nametable ---
    bytes.clear();
    for (size_t i = 0; i < Nametable.size(); i++) {
        bytes.push_back((Uint8)(Nametable[i] >> 8));
        bytes.push_back((Uint8)(Nametable[i] & 0xFF));
    }

    snprintf(path, sizeof(path), "%s/res/map.bin", outputPath);
    if (!MegaDriveExporter::WriteBinary(path, bytes.data(), bytes.size())) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // --- the resource script rescomp turns into arrays ---
    // far is FALSE so the data stays where a plain read can reach it rather
    // than being pushed past the first bank at the end of the ROM.
    const char* resText =
        "BIN hatch_palette \"palette.bin\" 2 2 0 NONE FALSE\n"
        "BIN hatch_tiles   \"tiles.bin\"   2 2 0 NONE FALSE\n"
        "BIN hatch_map     \"map.bin\"     2 2 0 NONE FALSE\n";

    snprintf(path, sizeof(path), "%s/res/resources.res", outputPath);
    if (!MegaDriveExporter::WriteText(path, resText)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return MegaDriveExporter::WritePlayer(outputPath, result);
}

PRIVATE STATIC bool MegaDriveExporter::WritePlayer(const char* outputPath, MegaDriveExportResult* result) {
    char path[1024];
    char text[8192];

    // The program the ROM runs. It puts the palettes in CRAM, the patterns in
    // VRAM and the nametable in the plane, then lets the pad move the window
    // over it. Anything past that is a game, and a game is what someone writes
    // here next.
    snprintf(text, sizeof(text),
        "// Generated by the Hatch Game Engine's Mega Drive exporter.\n"
        "//\n"
        "// The scene's art is in res/, already in the shapes the VDP reads. This\n"
        "// shows it and lets the pad scroll around; the game goes here.\n"
        "\n"
        "#include <genesis.h>\n"
        "#include \"resources.h\"\n"
        "\n"
        "#define PLANE_W %d\n"
        "#define PLANE_H %d\n"
        "#define TILE_COUNT %d\n"
        "\n"
        "// Where the scene's tiles start in VRAM. Everything below this belongs\n"
        "// to SGDK -- its font lives there, among other things.\n"
        "#define FIRST_TILE TILE_USER_INDEX\n"
        "\n"
        "int main(bool hard)\n"
        "{\n"
        "    u16 cameraX = 0;\n"
        "    u16 cameraY = 0;\n"
        "\n"
        "    if (!hard)\n"
        "        SYS_hardReset();\n"
        "\n"
        "    VDP_setScreenWidth320();\n"
        "    VDP_setPlaneSize(PLANE_W, PLANE_H, TRUE);\n"
        "\n"
        "    // Four palettes of sixteen, straight into CRAM.\n"
        "    PAL_setColors(0, (u16*)hatch_palette, 64, CPU);\n"
        "\n"
        "    VDP_loadTileData((const u32*)hatch_tiles, FIRST_TILE, TILE_COUNT, DMA);\n"
        "\n"
        "    // The map was written with tile indices counted from zero, so the\n"
        "    // base index is added as it goes in.\n"
        "    VDP_setTileMapDataRectEx(BG_A, (const u16*)hatch_map,\n"
        "        TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, FIRST_TILE),\n"
        "        0, 0, PLANE_W, PLANE_H, PLANE_W, DMA);\n"
        "\n"
        "    while (TRUE)\n"
        "    {\n"
        "        u16 pad = JOY_readJoypad(JOY_1);\n"
        "\n"
        "        if (pad & BUTTON_RIGHT) cameraX += 2;\n"
        "        if (pad & BUTTON_LEFT)  cameraX -= 2;\n"
        "        if (pad & BUTTON_DOWN)  cameraY += 2;\n"
        "        if (pad & BUTTON_UP)    cameraY -= 2;\n"
        "\n"
        "        VDP_setHorizontalScroll(BG_A, -cameraX);\n"
        "        VDP_setVerticalScroll(BG_A, cameraY);\n"
        "\n"
        "        SYS_doVBlankProcess();\n"
        "    }\n"
        "\n"
        "    return 0;\n"
        "}\n",
        result->PlaneWidth, result->PlaneHeight, result->TileCount);

    snprintf(path, sizeof(path), "%s/src/main.c", outputPath);
    if (!MegaDriveExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // SGDK's own makefile does the work; this only points at it.
    const char* makefileText =
        "# Built with SGDK: https://github.com/Stephane-D/SGDK\n"
        "#\n"
        "#   export GDK=/path/to/SGDK\n"
        "#   make\n"
        "#\n"
        "# The ROM comes out as out/rom.bin.\n"
        "\n"
        "GDK ?= /opt/sgdk\n"
        "\n"
        "all:\n"
        "\t$(MAKE) -f $(GDK)/makefile.gen\n"
        "\n"
        "clean:\n"
        "\t$(MAKE) -f $(GDK)/makefile.gen clean\n"
        "\n"
        ".PHONY: all clean\n";

    snprintf(path, sizeof(path), "%s/Makefile", outputPath);
    if (!MegaDriveExporter::WriteText(path, makefileText)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    snprintf(text, sizeof(text),
        "# %s, for the Mega Drive\n"
        "\n"
        "Exported from the Hatch Game Engine. The scene's art has been converted\n"
        "into the forms the VDP reads directly:\n"
        "\n"
        "| File | What it holds |\n"
        "| --- | --- |\n"
        "| `res/palette.bin` | %d palette(s) of 15 colours, as 9-bit VDP colour words |\n"
        "| `res/tiles.bin` | %d unique 8x8 tiles, 4 bits per pixel, %d bytes |\n"
        "| `res/map.bin` | a %dx%d nametable, %d bytes |\n"
        "\n"
        "## Building\n"
        "\n"
        "```sh\n"
        "export GDK=/path/to/SGDK\n"
        "make\n"
        "```\n"
        "\n"
        "`out/rom.bin` is the result, and runs on hardware or in any emulator.\n"
        "\n"
        "## What this is and is not\n"
        "\n"
        "`src/main.c` shows the scene and scrolls it with the pad. It is a\n"
        "starting point, not a game: Hatch's own game logic is bytecode for a VM\n"
        "that does not exist on this machine, so none of it came across. What did\n"
        "come across is the art, in a form the hardware can use, which is the part\n"
        "that is tedious to redo by hand.\n"
        "\n"
        "The tiles were deduplicated across all four orientations, since a\n"
        "nametable entry carries a flip bit per axis: %d cells became %d tiles.\n",
        Scene::CurrentScene[0] ? Scene::CurrentScene : "Scene",
        result->PaletteCount,
        result->TileCount, (int)result->TileBytes,
        result->PlaneWidth, result->PlaneHeight, (int)result->MapBytes,
        result->TilesBeforeDedupe, result->TileCount);

    snprintf(path, sizeof(path), "%s/README.md", outputPath);
    if (!MegaDriveExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return true;
}
