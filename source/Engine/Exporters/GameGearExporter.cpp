#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/GameGearTypes.h>

need_t SceneLayer;

class GameGearExporter {
public:

};
#endif

#include <Engine/Exporters/GameGearExporter.h>
#include <Engine/Exporters/SegaSceneArt.h>

#include <Engine/Application.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/Scene.h>
#include <Engine/Scene/SceneEnums.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Utilities/StringUtils.h>

// Turning a Hatch scene into something a Game Gear can show.
//
// This has the same shape as the Mega Drive export -- cut the layer into 8x8
// cells, fit them into palettes, deduplicate the patterns, build a nametable --
// but it does not share that code, because the machine underneath is a
// different one. The Game Gear's VDP came from the Master System, not the Mega
// Drive: bitplanes instead of packed nibbles, twelve bits of colour instead of
// nine, two palettes instead of four, and a nametable entry laid out
// differently. What the two exports have in common is the idea, and sharing on
// that basis is how you end up with a converter full of flags that is wrong for
// both machines.
//
// What they do share is SegaSceneArt, which is where the pixels come from.

#define GG_TRANSPARENT 0xFFFFFFFFU

static vector<GameGearCell>    Cells;
static vector<GameGearTile>    Patterns;
static vector<Uint16>          Nametable;
static GameGearPalette         Palettes[GG_PALETTE_COUNT];
static int                     PaletteCount;
static int                     ColorsMapped;

// Rounded into the twelve bits the VDP keeps, then spread back over the full
// range so comparisons happen in the space it will land in.
PRIVATE STATIC Uint32 GameGearExporter::QuantizeColor(Uint32 native) {
    Uint32 pixel = SegaSceneArt::ToARGB(native);

    if (((pixel >> 24) & 0xFF) < 128)
        return GG_TRANSPARENT;

    Uint32 r = (Uint32)((((pixel >> 16) & 0xFF) >> 4) * 255 / 15);
    Uint32 g = (Uint32)((((pixel >> 8) & 0xFF) >> 4) * 255 / 15);
    Uint32 b = (Uint32)(((pixel & 0xFF) >> 4) * 255 / 15);

    return (r << 16) | (g << 8) | b;
}

PRIVATE STATIC long GameGearExporter::ColorDistance(Uint32 a, Uint32 b) {
    long dr = (long)((a >> 16) & 0xFF) - (long)((b >> 16) & 0xFF);
    long dg = (long)((a >> 8) & 0xFF) - (long)((b >> 8) & 0xFF);
    long db = (long)(a & 0xFF) - (long)(b & 0xFF);

    return dr * dr + dg * dg + db * db;
}

// Cuts the layer into cells. Source tiles are usually 16x16, so each becomes
// four; a flipped source tile is read from the far side here rather than being
// left to the nametable, whose flip bits are spent on pattern reuse instead.
PRIVATE STATIC void GameGearExporter::BuildCells(SceneLayer* layer, int mapW, int mapH) {
    int cellsPerTileX = Scene::TileWidth / GG_TILE_SIZE;
    int cellsPerTileY = Scene::TileHeight / GG_TILE_SIZE;

    Cells.assign((size_t)mapW * mapH, GameGearCell());

    for (size_t i = 0; i < Cells.size(); i++) {
        for (int p = 0; p < GG_TILE_SIZE * GG_TILE_SIZE; p++)
            Cells[i].Pixels[p] = GG_TRANSPARENT;

        Cells[i].Palette = 0;
    }

    for (int cy = 0; cy < mapH; cy++) {
        for (int cx = 0; cx < mapW; cx++) {
            int tileX = cx / cellsPerTileX;
            int tileY = cy / cellsPerTileY;
            if (tileX >= layer->Width || tileY >= layer->Height)
                continue;

            // Rows are WidthData apart rather than Width apart: a layer's tile
            // array is allocated at the next power of two above its width.
            Uint32 entry = layer->Tiles[tileX + tileY * layer->WidthData];
            size_t tileID = entry & TILE_IDENT_MASK;
            if (tileID == Scene::EmptyTile)
                continue;

            Uint32* pixels; int stride, tileW, tileH;
            if (!SegaSceneArt::GetTilePixels(tileID, &pixels, &stride, &tileW, &tileH))
                continue;

            bool flipX = (entry & TILE_FLIPX_MASK) != 0;
            bool flipY = (entry & TILE_FLIPY_MASK) != 0;

            int subX = cx % cellsPerTileX;
            int subY = cy % cellsPerTileY;
            if (flipX)
                subX = (cellsPerTileX - 1) - subX;
            if (flipY)
                subY = (cellsPerTileY - 1) - subY;

            GameGearCell* cell = &Cells[(size_t)cx + (size_t)cy * mapW];

            for (int py = 0; py < GG_TILE_SIZE; py++) {
                for (int px = 0; px < GG_TILE_SIZE; px++) {
                    int sx = subX * GG_TILE_SIZE + px;
                    int sy = subY * GG_TILE_SIZE + py;

                    if (flipX)
                        sx = (tileW - 1) - sx;
                    if (flipY)
                        sy = (tileH - 1) - sy;

                    if (sx < 0 || sy < 0 || sx >= tileW || sy >= tileH)
                        continue;

                    cell->Pixels[px + py * GG_TILE_SIZE] =
                        GameGearExporter::QuantizeColor(pixels[sx + sy * stride]);
                }
            }
        }
    }
}

PRIVATE STATIC void GameGearExporter::CellColors(GameGearCell* cell, vector<Uint32>* out) {
    out->clear();

    for (int i = 0; i < GG_TILE_SIZE * GG_TILE_SIZE; i++) {
        Uint32 color = cell->Pixels[i];
        if (color == GG_TRANSPARENT)
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

PRIVATE STATIC int GameGearExporter::MissingFrom(GameGearPalette* palette, vector<Uint32>* colors) {
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

// Hands each cell a palette, first fit. Two palettes of fifteen is thirty
// colours for a whole screen, which is tight -- the Mega Drive has sixty-one --
// so a scene drawn for a bigger machine will spend some of its colours here.
PRIVATE STATIC void GameGearExporter::AssignPalettes() {
    for (int i = 0; i < GG_PALETTE_COUNT; i++)
        Palettes[i].Count = 0;

    PaletteCount = 1;
    ColorsMapped = 0;

    vector<Uint32> colors;

    for (size_t c = 0; c < Cells.size(); c++) {
        GameGearCell* cell = &Cells[c];
        GameGearExporter::CellColors(cell, &colors);

        if (colors.size() == 0) {
            cell->Palette = 0;
            continue;
        }

        int best = -1;
        int bestMissing = 0;

        for (int p = 0; p < PaletteCount; p++) {
            int missing = GameGearExporter::MissingFrom(&Palettes[p], &colors);
            if (Palettes[p].Count + missing > GG_PALETTE_USABLE)
                continue;

            if (best < 0 || missing < bestMissing) {
                best = p;
                bestMissing = missing;
            }
        }

        if (best < 0 && PaletteCount < GG_PALETTE_COUNT &&
            (int)colors.size() <= GG_PALETTE_USABLE) {
            best = PaletteCount++;
            bestMissing = (int)colors.size();
        }

        if (best >= 0) {
            GameGearPalette* palette = &Palettes[best];
            for (size_t i = 0; i < colors.size(); i++) {
                bool found = false;
                for (int j = 0; j < palette->Count; j++) {
                    if (palette->Colors[j] == colors[i]) {
                        found = true;
                        break;
                    }
                }

                if (!found && palette->Count < GG_PALETTE_USABLE)
                    palette->Colors[palette->Count++] = colors[i];
            }

            cell->Palette = best;
            continue;
        }

        // Out of palettes. The cell goes to whichever is nearest and its
        // remaining colours are approximated when it becomes indices.
        int closest = 0;
        long closestCost = -1;

        for (int p = 0; p < PaletteCount; p++) {
            long cost = 0;
            for (size_t i = 0; i < colors.size(); i++) {
                long nearest = -1;
                for (int j = 0; j < Palettes[p].Count; j++) {
                    long distance = GameGearExporter::ColorDistance(Palettes[p].Colors[j], colors[i]);
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

// Into bitplanes. Every row is four bytes, one per plane, and plane b of a row
// holds bit b of each of its eight pixels -- leftmost pixel in the high bit.
// This is where the Mega Drive's packed nibbles and this machine's bitplanes
// part company.
PRIVATE STATIC void GameGearExporter::PackCell(GameGearCell* cell, GameGearTile* out) {
    GameGearPalette* palette = &Palettes[cell->Palette];

    memset(out->Data, 0, GG_TILE_BYTES);

    for (int y = 0; y < GG_TILE_SIZE; y++) {
        for (int x = 0; x < GG_TILE_SIZE; x++) {
            Uint32 color = cell->Pixels[x + y * GG_TILE_SIZE];
            int index = 0;

            if (color != GG_TRANSPARENT) {
                int found = -1;
                for (int j = 0; j < palette->Count; j++) {
                    if (palette->Colors[j] == color) {
                        found = j;
                        break;
                    }
                }

                if (found < 0) {
                    long nearest = -1;
                    for (int j = 0; j < palette->Count; j++) {
                        long distance = GameGearExporter::ColorDistance(palette->Colors[j], color);
                        if (nearest < 0 || distance < nearest) {
                            nearest = distance;
                            found = j;
                        }
                    }
                }

                // Entry zero is the backdrop, so art starts at one.
                index = found < 0 ? 0 : found + 1;
            }

            for (int plane = 0; plane < 4; plane++) {
                if (index & (1 << plane))
                    out->Data[y * 4 + plane] |= (Uint8)(0x80 >> x);
            }
        }
    }
}

PRIVATE STATIC void GameGearExporter::FlipTile(GameGearTile* in, bool flipX, bool flipY, GameGearTile* out) {
    memset(out->Data, 0, GG_TILE_BYTES);

    for (int y = 0; y < GG_TILE_SIZE; y++) {
        int sy = flipY ? (GG_TILE_SIZE - 1) - y : y;

        for (int x = 0; x < GG_TILE_SIZE; x++) {
            int sx = flipX ? (GG_TILE_SIZE - 1) - x : x;

            for (int plane = 0; plane < 4; plane++) {
                if (in->Data[sy * 4 + plane] & (0x80 >> sx))
                    out->Data[y * 4 + plane] |= (Uint8)(0x80 >> x);
            }
        }
    }
}

// A nametable entry carries a flip bit per axis, so a mirrored pattern costs
// only the entry naming it.
PRIVATE STATIC int GameGearExporter::AddPattern(GameGearTile* tile, bool* flipX, bool* flipY) {
    GameGearTile variant;

    for (int orientation = 0; orientation < 4; orientation++) {
        bool fx = (orientation & 1) != 0;
        bool fy = (orientation & 2) != 0;

        GameGearExporter::FlipTile(tile, fx, fy, &variant);

        for (size_t i = 0; i < Patterns.size(); i++) {
            if (memcmp(Patterns[i].Data, variant.Data, GG_TILE_BYTES) == 0) {
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

PRIVATE STATIC bool GameGearExporter::WriteBinary(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;

    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);

    return ok;
}

PRIVATE STATIC bool GameGearExporter::WriteText(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (!f)
        return false;

    bool ok = fputs(text, f) >= 0;
    fclose(f);

    return ok;
}

PUBLIC STATIC GameGearExportResult GameGearExporter::ExportScene(const char* outputPath) {
    GameGearExportResult result;
    memset(&result, 0, sizeof(result));

    SceneLayer* layer = SegaSceneArt::PickLayer();
    if (!layer) {
        StringUtils::Copy(result.Message, "The scene has no visible tile layer to export.", sizeof(result.Message));
        return result;
    }

    if (Scene::TileWidth < GG_TILE_SIZE || Scene::TileHeight < GG_TILE_SIZE ||
        (Scene::TileWidth % GG_TILE_SIZE) || (Scene::TileHeight % GG_TILE_SIZE)) {
        snprintf(result.Message, sizeof(result.Message),
            "The scene's tiles are %dx%d. This VDP works in 8x8 cells, so tiles have to be a whole number of those on each side.",
            Scene::TileWidth, Scene::TileHeight);
        return result;
    }

    int cellsPerTileX = Scene::TileWidth / GG_TILE_SIZE;
    int cellsPerTileY = Scene::TileHeight / GG_TILE_SIZE;

    result.LayerWidth = layer->Width * cellsPerTileX;
    result.LayerHeight = layer->Height * cellsPerTileY;

    // The whole layer goes into ROM as a map and the Z80 copies the visible
    // window of it into the nametable, so this is not bounded by the plane the
    // way the Mega Drive export is. It is bounded by the cartridge: a map entry
    // is two bytes and a 32 KB ROM has to hold the patterns too.
    int mapW = result.LayerWidth < GG_SCREEN_TILES_X ? GG_SCREEN_TILES_X : result.LayerWidth;
    int mapH = result.LayerHeight < GG_SCREEN_TILES_Y ? GG_SCREEN_TILES_Y : result.LayerHeight;

    result.MapWidth = mapW;
    result.MapHeight = mapH;

    Patterns.clear();
    Nametable.clear();

    GameGearExporter::BuildCells(layer, mapW, mapH);
    GameGearExporter::AssignPalettes();

    result.PaletteCount = PaletteCount;
    result.ColorsDropped = ColorsMapped;

    for (int i = 0; i < PaletteCount; i++)
        result.ColorsFound += Palettes[i].Count;
    result.ColorsFound += ColorsMapped;

    Nametable.resize(Cells.size());
    result.TilesBeforeDedupe = (int)Cells.size();

    GameGearTile packed;
    for (size_t i = 0; i < Cells.size(); i++) {
        GameGearExporter::PackCell(&Cells[i], &packed);

        bool flipX, flipY;
        int index = GameGearExporter::AddPattern(&packed, &flipX, &flipY);

        if ((int)Patterns.size() > GG_MAX_TILES) {
            snprintf(result.Message, sizeof(result.Message),
                "The layer needs more than %d unique tiles. That is everything VRAM has room for below the nametable, and the Game Gear has 16 KB of it.",
                GG_MAX_TILES);
            return result;
        }

        // Bits: index 0-8, horizontal flip 9, vertical flip 10, palette 11.
        Nametable[i] = (Uint16)((index & 0x01FF) |
                                ((flipX ? 1 : 0) << 9) |
                                ((flipY ? 1 : 0) << 10) |
                                ((Cells[i].Palette & 1) << 11));
    }

    result.TileCount = (int)Patterns.size();
    result.TileBytes = Patterns.size() * GG_TILE_BYTES;
    result.MapBytes = Nametable.size() * 2;

    if (!GameGearExporter::WriteProject(outputPath, &result))
        return result;

    result.Success = true;

    if (result.ColorsDropped) {
        snprintf(result.Message, sizeof(result.Message),
            "Exported %d tiles in %d palette(s). %d cell colour(s) did not fit two palettes of fifteen and were matched to the nearest that did.",
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

PRIVATE STATIC bool GameGearExporter::WriteProject(const char* outputPath, GameGearExportResult* result) {
    char path[1024];

    const char* dirs[2] = { "", "/res" };
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof(path), "%s%s", outputPath, dirs[i]);
        if (!Directory::Exists(path) && !Directory::CreatePath(path)) {
            snprintf(result->Message, sizeof(result->Message), "Could not create \"%s\".", path);
            return false;
        }
    }

    vector<Uint8> bytes;

    // --- the palettes: two of sixteen, little endian, entry zero the backdrop ---
    bytes.clear();
    for (int p = 0; p < GG_PALETTE_COUNT; p++) {
        for (int i = 0; i < GG_PALETTE_SIZE; i++) {
            Uint16 word = 0;

            if (i > 0 && (i - 1) < Palettes[p].Count) {
                Uint32 color = Palettes[p].Colors[i - 1];
                word = GG_COLOR_WORD((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
            }

            bytes.push_back((Uint8)(word & 0xFF));
            bytes.push_back((Uint8)(word >> 8));
        }
    }

    snprintf(path, sizeof(path), "%s/res/palette.bin", outputPath);
    if (!GameGearExporter::WriteBinary(path, bytes.data(), bytes.size())) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // --- the patterns ---
    bytes.clear();
    for (size_t i = 0; i < Patterns.size(); i++) {
        for (int j = 0; j < GG_TILE_BYTES; j++)
            bytes.push_back(Patterns[i].Data[j]);
    }

    snprintf(path, sizeof(path), "%s/res/tiles.bin", outputPath);
    if (!GameGearExporter::WriteBinary(path, bytes.data(), bytes.size())) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // --- the map, little endian like everything this Z80 reads ---
    bytes.clear();
    for (size_t i = 0; i < Nametable.size(); i++) {
        bytes.push_back((Uint8)(Nametable[i] & 0xFF));
        bytes.push_back((Uint8)(Nametable[i] >> 8));
    }

    snprintf(path, sizeof(path), "%s/res/map.bin", outputPath);
    if (!GameGearExporter::WriteBinary(path, bytes.data(), bytes.size())) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return GameGearExporter::WriteSources(outputPath, result);
}

// SDCC has no .incbin, so the data becomes C arrays. They are streamed rather
// than built in memory first: a map is two bytes a cell and a large scene runs
// to hundreds of thousands of them.
PRIVATE STATIC bool GameGearExporter::WriteDataArrays(const char* outputPath, GameGearExportResult* result) {
    char path[1024];

    snprintf(path, sizeof(path), "%s/res/data.c", outputPath);
    FILE* f = fopen(path, "w");
    if (!f) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    fprintf(f, "// Generated by the Hatch Game Engine's Game Gear exporter.\n");
    fprintf(f, "//\n");
    fprintf(f, "// const at file scope puts these in ROM, which is where they have to be:\n");
    fprintf(f, "// the Game Gear has 8 KB of work RAM and the map alone is larger.\n");
    fprintf(f, "\n#include \"data.h\"\n\n");

    // Two palettes of sixteen, as the VDP's own colour words.
    fprintf(f, "const unsigned char scene_palette[%d] = {", GG_PALETTE_COUNT * GG_PALETTE_SIZE * 2);
    for (int p = 0; p < GG_PALETTE_COUNT; p++) {
        for (int i = 0; i < GG_PALETTE_SIZE; i++) {
            Uint16 word = 0;

            if (i > 0 && (i - 1) < Palettes[p].Count) {
                Uint32 color = Palettes[p].Colors[i - 1];
                word = GG_COLOR_WORD((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
            }

            if (((p * GG_PALETTE_SIZE + i) % 8) == 0)
                fprintf(f, "\n    ");

            fprintf(f, "0x%02X,0x%02X,", word & 0xFF, word >> 8);
        }
    }
    fprintf(f, "\n};\n\n");

    fprintf(f, "const unsigned char scene_tiles[%d] = {", (int)(Patterns.size() * GG_TILE_BYTES));
    for (size_t i = 0; i < Patterns.size(); i++) {
        for (int j = 0; j < GG_TILE_BYTES; j++) {
            if ((j % 16) == 0)
                fprintf(f, "\n    ");

            fprintf(f, "0x%02X,", Patterns[i].Data[j]);
        }
    }
    fprintf(f, "\n};\n\n");

    fprintf(f, "const unsigned int scene_map[%d] = {", (int)Nametable.size());
    for (size_t i = 0; i < Nametable.size(); i++) {
        if ((i % 12) == 0)
            fprintf(f, "\n    ");

        fprintf(f, "0x%04X,", Nametable[i]);
    }
    fprintf(f, "\n};\n");

    fclose(f);

    char text[2048];
    snprintf(text, sizeof(text),
        "// Generated by the Hatch Game Engine's Game Gear exporter.\n"
        "\n"
        "#ifndef SCENE_DATA_H\n"
        "#define SCENE_DATA_H\n"
        "\n"
        "#define SCENE_MAP_W    %d\n"
        "#define SCENE_MAP_H    %d\n"
        "#define SCENE_TILES    %d\n"
        "\n"
        "extern const unsigned char scene_palette[%d];\n"
        "extern const unsigned char scene_tiles[%d];\n"
        "extern const unsigned int  scene_map[%d];\n"
        "\n"
        "#endif\n",
        result->MapWidth, result->MapHeight, result->TileCount,
        GG_PALETTE_COUNT * GG_PALETTE_SIZE * 2,
        (int)(Patterns.size() * GG_TILE_BYTES),
        (int)Nametable.size());

    snprintf(path, sizeof(path), "%s/res/data.h", outputPath);
    if (!GameGearExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return true;
}

PRIVATE STATIC bool GameGearExporter::WriteSources(const char* outputPath, GameGearExportResult* result) {
    char path[1024];
    char text[6144];

    if (!GameGearExporter::WriteDataArrays(outputPath, result))
        return false;

    snprintf(text, sizeof(text),
        "// Generated by the Hatch Game Engine's Game Gear exporter.\n"
        "//\n"
        "// The scene's tiles, palettes and map are in res/. This puts the part of\n"
        "// it the camera is over on the screen; the game goes here.\n"
        "\n"
        "#include \"SMSlib.h\"\n"
        "#include \"data.h\"\n"
        "\n"
        "// The Game Gear shows the middle 160x144 of the Master System's frame,\n"
        "// which is twenty tiles by eighteen starting six across and three down.\n"
        "// Writing there puts the art on screen with the scroll left alone.\n"
        "#define VIEW_X   %d\n"
        "#define VIEW_Y   %d\n"
        "#define VIEW_W   %d\n"
        "#define VIEW_H   %d\n"
        "\n"
        "static unsigned int cameraX = 0;\n"
        "static unsigned int cameraY = 0;\n"
        "\n"
        "// One screenful out of the map and into the nametable. A row at a time,\n"
        "// because the map in ROM is wider than the window and its rows are not\n"
        "// next to each other.\n"
        "static void drawView(void)\n"
        "{\n"
        "    unsigned char y;\n"
        "\n"
        "    for (y = 0; y < VIEW_H; y++) {\n"
        "        SMS_loadTileMapArea(VIEW_X, VIEW_Y + y,\n"
        "            &scene_map[(cameraY + y) * SCENE_MAP_W + cameraX], VIEW_W, 1);\n"
        "    }\n"
        "}\n"
        "\n"
        "void main(void)\n"
        "{\n"
        "    unsigned int keys;\n"
        "    unsigned char moved = 1;\n"
        "\n"
        "    SMS_VRAMmemsetW(0, 0x0000, 16384);\n"
        "\n"
        "    SMS_loadTiles(scene_tiles, 0, SCENE_TILES * 32);\n"
        "\n"
        "    // Twelve-bit colour, which is the Game Gear's own improvement on the\n"
        "    // Master System's six. A tile picks between these two with one bit.\n"
        "    GG_loadBGPalette(scene_palette);\n"
        "    GG_loadSpritePalette(scene_palette + 32);\n"
        "\n"
        "    drawView();\n"
        "    SMS_displayOn();\n"
        "\n"
        "    for (;;) {\n"
        "        SMS_waitForVBlank();\n"
        "\n"
        "        if (moved) {\n"
        "            drawView();\n"
        "            moved = 0;\n"
        "        }\n"
        "\n"
        "        keys = SMS_getKeysStatus();\n"
        "\n"
        "        if ((keys & PORT_A_KEY_RIGHT) && cameraX + VIEW_W < SCENE_MAP_W) { cameraX++; moved = 1; }\n"
        "        if ((keys & PORT_A_KEY_LEFT)  && cameraX > 0)                    { cameraX--; moved = 1; }\n"
        "        if ((keys & PORT_A_KEY_DOWN)  && cameraY + VIEW_H < SCENE_MAP_H) { cameraY++; moved = 1; }\n"
        "        if ((keys & PORT_A_KEY_UP)    && cameraY > 0)                    { cameraY--; moved = 1; }\n"
        "    }\n"
        "}\n"
        "\n"
        "SMS_EMBED_SEGA_ROM_HEADER(9999, 0);\n"
        "SMS_EMBED_SDSC_HEADER_AUTO_DATE(1, 0, \"Hatch Game Engine\", \"%s\", \"Exported scene\");\n",
        GG_VIEWPORT_X, GG_VIEWPORT_Y, GG_SCREEN_TILES_X, GG_SCREEN_TILES_Y,
        Scene::CurrentScene[0] ? Scene::CurrentScene : "Scene");

    snprintf(path, sizeof(path), "%s/main.c", outputPath);
    if (!GameGearExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // devkitSMS supplies the startup, the library and the tool that turns
    // SDCC's output into a cartridge image. TARGET_GG is what switches SMSlib's
    // twelve-bit palette calls on; without it the Master System's six-bit ones
    // are all that is declared.
    const char* makefileText =
        "# Built with SDCC and devkitSMS:\n"
        "#\n"
        "#   export DEVKITSMS=/path/to/devkitSMS\n"
        "#   make\n"
        "#\n"
        "# The cartridge comes out as out/rom.gg.\n"
        "\n"
        "DEVKITSMS  ?= /opt/devkitSMS\n"
        "SMSLIB     ?= $(DEVKITSMS)/SMSlib\n"
        "\n"
        "# All three are overridable because devkitSMS ships them prebuilt and\n"
        "# those binaries do not always match the SDCC in front of them. Building\n"
        "# them from the sources beside them fixes that:\n"
        "#\n"
        "#   make -C $(DEVKITSMS)/crt0/src\n"
        "#   make -C $(SMSLIB)/src\n"
        "#   cc -O2 -o ihx2sms $(DEVKITSMS)/ihx2sms/src/ihx2sms.c\n"
        "#\n"
        "#   make SMSLIB_LIB=$(SMSLIB)/src/SMSlib_GG.lib \\\n"
        "#        CRT0=$(DEVKITSMS)/crt0/src/crt0_sms.rel IHX2SMS=./ihx2sms\n"
        "SMSLIB_LIB ?= $(SMSLIB)/SMSlib_GG.lib\n"
        "CRT0       ?= $(DEVKITSMS)/crt0/crt0_sms.rel\n"
        "IHX2SMS    ?= $(DEVKITSMS)/ihx2sms/Linux/ihx2sms\n"
        "\n"
        "CC = sdcc\n"
        "\n"
        "# TARGET_GG picks the Game Gear out of the Master System family SMSlib\n"
        "# covers: the screen is smaller and the palette is twelve bits, not six.\n"
        "CFLAGS  = -mz80 -DTARGET_GG -I$(SMSLIB)/src -Ires --peep-file $(SMSLIB)/src/peep-rules.txt\n"
        "LDFLAGS = -mz80 --no-std-crt0 --data-loc 0xC000\n"
        "\n"
        "OBJS = main.rel res/data.rel\n"
        "\n"
        "all: out/rom.gg\n"
        "\n"
        "out:\n"
        "\tmkdir -p out\n"
        "\n"
        "%.rel: %.c\n"
        "\t$(CC) $(CFLAGS) -c $< -o $@\n"
        "\n"
        "out/rom.ihx: $(OBJS) | out\n"
        "\t$(CC) -o $@ $(LDFLAGS) $(CRT0) $(SMSLIB_LIB) $(OBJS)\n"
        "\n"
        "out/rom.gg: out/rom.ihx\n"
        "\t$(IHX2SMS) $< $@\n"
        "\n"
        "clean:\n"
        "\trm -rf out *.rel *.lst *.sym *.asm res/*.rel res/*.lst res/*.sym res/*.asm\n"
        "\n"
        "# sdcc leaves a .ihx behind even when the link fails, which would make the\n"
        "# next build think there was nothing to do.\n"
        ".PHONY: all clean out/rom.ihx\n";

    snprintf(path, sizeof(path), "%s/Makefile", outputPath);
    if (!GameGearExporter::WriteText(path, makefileText)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    snprintf(text, sizeof(text),
        "# %s, for the Game Gear\n"
        "\n"
        "Exported from the Hatch Game Engine. The Game Gear's VDP came from the\n"
        "Master System, not the Mega Drive, so the art is in that machine's forms:\n"
        "\n"
        "| File | What it holds |\n"
        "| --- | --- |\n"
        "| `res/data.c` | the palettes, the tile patterns and the map, as arrays in ROM |\n"
        "| `main.c` | the Z80 program that shows them |\n"
        "\n"
        "%d unique tiles in %d palette(s), from %d cells. A tile is four bitplanes\n"
        "rather than the Mega Drive's packed nibbles, colour is twelve bits rather\n"
        "than nine, and there are two palettes of sixteen rather than four.\n"
        "\n"
        "## Building\n"
        "\n"
        "```sh\n"
        "export DEVKITSMS=/path/to/devkitSMS\n"
        "make\n"
        "```\n"
        "\n"
        "Needs SDCC for the Z80 and [devkitSMS](https://github.com/sverx/devkitSMS)\n"
        "for the startup code, SMSlib and `ihx2sms`. `out/rom.gg` is the result.\n"
        "\n"
        "## What this is and is not\n"
        "\n"
        "The pad scrolls around the map. That is all: Hatch's game logic is bytecode\n"
        "for a VM that will not fit on a Z80 with 8 KB of work RAM, so none of it\n"
        "came across.\n"
        "\n"
        "The tightest limit here is VRAM. There are 16 KB of it, the nametable takes\n"
        "the top, and what is left below holds %d tiles at most -- against the Mega\n"
        "Drive's 2048. Colour is the other one: thirty on screen where the Mega\n"
        "Drive has sixty-one.\n",
        Scene::CurrentScene[0] ? Scene::CurrentScene : "Scene",
        result->TileCount, result->PaletteCount, result->TilesBeforeDedupe,
        GG_MAX_TILES);

    snprintf(path, sizeof(path), "%s/README.md", outputPath);
    if (!GameGearExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return true;
}
