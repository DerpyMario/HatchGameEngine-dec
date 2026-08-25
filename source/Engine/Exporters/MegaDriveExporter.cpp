#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/MegaDriveTypes.h>

need_t SceneLayer;

class MegaDriveExporter {
public:

};
#endif

#include <Engine/Exporters/MegaDriveExporter.h>

#include <Engine/Exporters/SegaSceneArt.h>
#include <Engine/Exporters/SegaVDPConverter.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/Scene.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Utilities/StringUtils.h>

// Turning a Hatch scene into something a Mega Drive can show.
//
// The engine itself does not run on the console and cannot be made to: its
// memory pools alone ask for more than three hundred times the machine's work
// RAM, and its software rasteriser wants a framebuffer where the VDP offers
// only a grid of tiles. What does cross over is the scene's art.
//
// The conversion itself is in SegaVDPConverter, because the Mega CD export
// needs exactly the same one. What is here is the cartridge: an SGDK project
// wrapped around the three things the hardware reads, which builds into a ROM.

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

PUBLIC STATIC MegaDriveExportResult MegaDriveExporter::ExportScene(const char* outputPath) {
    MegaDriveExportResult result;
    memset(&result, 0, sizeof(result));

    SceneLayer* layer = SegaSceneArt::PickLayer();
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
    SegaVDPConverter::ChoosePlaneSize(result.LayerWidth, result.LayerHeight, &planeW, &planeH);

    result.PlaneWidth = planeW;
    result.PlaneHeight = planeH;

    SegaVDPConversion art;
    if (!SegaVDPConverter::Convert(layer, planeW, planeH, &art)) {
        StringUtils::Copy(result.Message, art.Failure, sizeof(result.Message));
        return result;
    }

    result.PaletteCount = art.PaletteCount;
    result.ColorsFound = art.ColorsFound;
    result.ColorsDropped = art.ColorsDropped;
    result.TileCount = art.TileCount;
    result.TilesBeforeDedupe = art.TilesBeforeDedupe;
    result.TileBytes = art.TileBytes;
    result.MapBytes = art.MapBytes;

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

    // --- the three things the VDP reads, as it reads them ---
    vector<Uint8> bytes;

    SegaVDPConverter::PaletteBytes(&bytes);

    snprintf(path, sizeof(path), "%s/res/palette.bin", outputPath);
    if (!MegaDriveExporter::WriteBinary(path, bytes.data(), bytes.size())) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    SegaVDPConverter::TileBytes(&bytes);

    snprintf(path, sizeof(path), "%s/res/tiles.bin", outputPath);
    if (!MegaDriveExporter::WriteBinary(path, bytes.data(), bytes.size())) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    SegaVDPConverter::NametableBytes(&bytes);

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
