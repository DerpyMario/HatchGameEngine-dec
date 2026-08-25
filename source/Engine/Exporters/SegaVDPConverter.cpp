#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/SegaVDPTypes.h>

need_t SceneLayer;

class SegaVDPConverter {
public:

};
#endif

#include <Engine/Exporters/SegaVDPConverter.h>

#include <Engine/Exporters/SegaSceneArt.h>
#include <Engine/Scene.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Types/Tileset.h>

// Turning a scene layer into what a Mega Drive's VDP reads.
//
// The Mega CD is a Mega Drive with a disc drive bolted to its side. Its
// graphics hardware is not merely similar to the Mega Drive's -- it is the
// same chip, in the same machine, with the same 64 KB of VRAM. So both exports
// convert their art here, and differ only in what they wrap around it: one
// builds a cartridge, the other a disc.
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

static vector<SegaVDPCell>        Cells;          // one per plane cell, row major
static vector<SegaVDPTile> Patterns;       // the unique 8x8 patterns
static vector<Uint16>        Nametable;
static SegaVDPPalette             Palettes[MD_PALETTE_COUNT];
static int                   PaletteCount;
static int                   ColorsMapped;   // colours that had to be approximated
PRIVATE STATIC Uint32 SegaVDPConverter::QuantizeColor(Uint32 native) {
    Uint32 pixel = SegaSceneArt::ToARGB(native);

    if (((pixel >> 24) & 0xFF) < 128)
        return MD_TRANSPARENT;

    // Rounded into the nine bits the VDP keeps, then spread back over the full
    // range so comparisons and distances are done in the space it will land in.
    Uint32 r = (Uint32)((((pixel >> 16) & 0xFF) >> 5) * 255 / 7);
    Uint32 g = (Uint32)((((pixel >> 8) & 0xFF) >> 5) * 255 / 7);
    Uint32 b = (Uint32)(((pixel & 0xFF) >> 5) * 255 / 7);

    return (r << 16) | (g << 8) | b;
}

PRIVATE STATIC long SegaVDPConverter::ColorDistance(Uint32 a, Uint32 b) {
    long dr = (long)((a >> 16) & 0xFF) - (long)((b >> 16) & 0xFF);
    long dg = (long)((a >> 8) & 0xFF) - (long)((b >> 8) & 0xFF);
    long db = (long)(a & 0xFF) - (long)(b & 0xFF);

    return dr * dr + dg * dg + db * db;
}

// Cuts the layer into the 8x8 cells the VDP works in, stopping at the plane's
// edge. Source tiles are usually 16x16, so each becomes four cells.
PRIVATE STATIC void SegaVDPConverter::BuildCells(SceneLayer* layer, int planeW, int planeH) {
    int cellsPerTileX = Scene::TileWidth / MD_TILE_SIZE;
    int cellsPerTileY = Scene::TileHeight / MD_TILE_SIZE;

    Cells.assign((size_t)planeW * planeH, SegaVDPCell());

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

            // Rows are WidthData apart rather than Width apart: the layer
            // pads itself out to a power of two so the renderer can index it
            // by shifting.
            Uint32 entry = layer->Tiles[tileX + tileY * layer->WidthData];
            size_t tileID = entry & TILE_IDENT_MASK;
            if (tileID == Scene::EmptyTile)
                continue;

            Uint32* pixels; int stride, tileW, tileH;
            if (!SegaSceneArt::GetTilePixels(tileID, &pixels, &stride, &tileW, &tileH))
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

            SegaVDPCell* cell = &Cells[(size_t)cx + (size_t)cy * planeW];

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
                        SegaVDPConverter::QuantizeColor(pixels[sx + sy * stride]);
                }
            }
        }
    }
}

PRIVATE STATIC void SegaVDPConverter::CellColors(SegaVDPCell* cell, vector<Uint32>* out) {
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
PRIVATE STATIC int SegaVDPConverter::MissingFrom(SegaVDPPalette* palette, vector<Uint32>* colors) {
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
PRIVATE STATIC void SegaVDPConverter::AssignPalettes() {
    for (int i = 0; i < MD_PALETTE_COUNT; i++)
        Palettes[i].Count = 0;

    PaletteCount = 1;
    ColorsMapped = 0;

    vector<Uint32> colors;

    for (size_t c = 0; c < Cells.size(); c++) {
        SegaVDPCell* cell = &Cells[c];
        SegaVDPConverter::CellColors(cell, &colors);

        if (colors.size() == 0) {
            cell->Palette = 0;
            continue;
        }

        // The palette that already holds the most of what this cell needs and
        // still has room for the rest.
        int best = -1;
        int bestMissing = 0;

        for (int p = 0; p < PaletteCount; p++) {
            int missing = SegaVDPConverter::MissingFrom(&Palettes[p], &colors);
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
            SegaVDPPalette* palette = &Palettes[best];
            for (size_t i = 0; i < colors.size(); i++) {
                if (SegaVDPConverter::MissingFrom(palette, &colors) == 0)
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
                    long distance = SegaVDPConverter::ColorDistance(Palettes[p].Colors[j], colors[i]);
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
PRIVATE STATIC void SegaVDPConverter::PackCell(SegaVDPCell* cell, SegaVDPTile* out) {
    SegaVDPPalette* palette = &Palettes[cell->Palette];

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
                        long distance = SegaVDPConverter::ColorDistance(palette->Colors[j], color);
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

PRIVATE STATIC void SegaVDPConverter::FlipTile(SegaVDPTile* in, bool flipX, bool flipY, SegaVDPTile* out) {
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
PRIVATE STATIC int SegaVDPConverter::AddPattern(SegaVDPTile* tile, bool* flipX, bool* flipY) {
    SegaVDPTile variant;

    for (int orientation = 0; orientation < 4; orientation++) {
        bool fx = (orientation & 1) != 0;
        bool fy = (orientation & 2) != 0;

        SegaVDPConverter::FlipTile(tile, fx, fy, &variant);

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
PUBLIC STATIC void SegaVDPConverter::ChoosePlaneSize(int wantW, int wantH, int* planeW, int* planeH) {
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

// Runs the whole conversion over one layer, into a plane of the given size.
// Everything the callers need afterwards is read back through the accessors
// below rather than handed over, since there is only ever one conversion in
// flight and the largest part of it is 64 KB of patterns.
PUBLIC STATIC bool SegaVDPConverter::Convert(SceneLayer* layer, int planeW, int planeH, SegaVDPConversion* out) {
    memset(out, 0, sizeof(SegaVDPConversion));

    out->PlaneWidth = planeW;
    out->PlaneHeight = planeH;

    Patterns.clear();
    Nametable.clear();

    SegaVDPConverter::BuildCells(layer, planeW, planeH);
    SegaVDPConverter::AssignPalettes();

    out->PaletteCount = PaletteCount;
    out->ColorsDropped = ColorsMapped;

    for (int i = 0; i < PaletteCount; i++)
        out->ColorsFound += Palettes[i].Count;
    out->ColorsFound += ColorsMapped;

    // An entirely empty tile is worth having at index zero: every blank cell in
    // the plane then points at the same 32 bytes.
    Nametable.resize(Cells.size());
    out->TilesBeforeDedupe = (int)Cells.size();

    SegaVDPTile packed;
    for (size_t i = 0; i < Cells.size(); i++) {
        SegaVDPConverter::PackCell(&Cells[i], &packed);

        bool flipX, flipY;
        int index = SegaVDPConverter::AddPattern(&packed, &flipX, &flipY);

        if ((int)Patterns.size() > MD_MAX_TILES) {
            snprintf(out->Failure, sizeof(out->Failure),
                "The layer needs more than %d unique tiles, which is all a nametable entry can address.",
                MD_MAX_TILES);
            return false;
        }

        // Priority stays low: everything here is background.
        Nametable[i] = (Uint16)(((Cells[i].Palette & 3) << 13) |
                                ((flipY ? 1 : 0) << 12) |
                                ((flipX ? 1 : 0) << 11) |
                                (index & 0x07FF));
    }

    out->TileCount = (int)Patterns.size();
    out->TileBytes = Patterns.size() * MD_TILE_BYTES;
    out->MapBytes = Nametable.size() * 2;

    return true;
}

// The three blobs the hardware reads, in the byte order it reads them. The
// 68000 is big endian and so is everything on the far side of it, so the words
// go out high byte first regardless of what wrote them.
//
// Both exports want exactly these. What differs is where they end up: baked
// into a cartridge, or written to a disc as a file.

// Four palettes of sixteen colour words. Entry zero of each is the backdrop
// rather than a colour, and stays black; entries the layer did not need stay
// black too.
PUBLIC STATIC void SegaVDPConverter::PaletteBytes(vector<Uint8>* out) {
    Uint16 words[MD_PALETTE_COUNT * MD_PALETTE_SIZE];
    memset(words, 0, sizeof(words));

    for (int p = 0; p < MD_PALETTE_COUNT; p++) {
        for (int i = 0; i < Palettes[p].Count; i++) {
            Uint32 color = Palettes[p].Colors[i];
            words[p * MD_PALETTE_SIZE + i + 1] = MD_COLOR_WORD(
                (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        }
    }

    out->clear();
    for (size_t i = 0; i < MD_PALETTE_COUNT * MD_PALETTE_SIZE; i++) {
        out->push_back((Uint8)(words[i] >> 8));
        out->push_back((Uint8)(words[i] & 0xFF));
    }
}

PUBLIC STATIC void SegaVDPConverter::TileBytes(vector<Uint8>* out) {
    out->clear();

    for (size_t i = 0; i < Patterns.size(); i++) {
        for (int j = 0; j < MD_TILE_BYTES; j++)
            out->push_back(Patterns[i].Data[j]);
    }
}

PUBLIC STATIC void SegaVDPConverter::NametableBytes(vector<Uint8>* out) {
    out->clear();

    for (size_t i = 0; i < Nametable.size(); i++) {
        out->push_back((Uint8)(Nametable[i] >> 8));
        out->push_back((Uint8)(Nametable[i] & 0xFF));
    }
}
