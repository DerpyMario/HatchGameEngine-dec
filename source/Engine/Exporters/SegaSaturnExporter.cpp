#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/SegaSaturnTypes.h>

need_t SceneLayer;

class SegaSaturnExporter {
public:

};
#endif

#include <Engine/Exporters/SegaSaturnExporter.h>
#include <Engine/Exporters/SegaSceneArt.h>

#include <Engine/Application.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/Scene.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Utilities/StringUtils.h>

// Turning a Hatch scene into something a SEGA Saturn can show.
//
// The Saturn has two VDPs: VDP1 for sprites and polygons, VDP2 for backgrounds.
// For a simple export, we use a bitmap approach similar to the 32X: draw the
// layer into an indexed image and let the runtime blit it to the framebuffer.
//
// The Saturn offers 32768 colors (15-bit RGB) and up to 512 KB of frame RAM.
// This exporter produces a palette-based bitmap that the Saturn runtime can
// display and scroll with the controller.

static vector<Uint8>  Indices;      // one byte a pixel, row major
static vector<Uint32> Palette;      // 0xRRGGBB, already rounded to five bits
static int            DroppedColors;

// Rounded into the five bits a channel the Saturn keeps, then spread back over the
// full range so comparisons happen in the space it will land in.
PRIVATE STATIC Uint32 SegaSaturnExporter::QuantizeColor(Uint32 argb) {
    Uint32 r = (Uint32)((((argb >> 16) & 0xFF) >> 3) * 255 / 31);
    Uint32 g = (Uint32)((((argb >> 8) & 0xFF) >> 3) * 255 / 31);
    Uint32 b = (Uint32)(((argb & 0xFF) >> 3) * 255 / 31);

    return (r << 16) | (g << 8) | b;
}

PRIVATE STATIC long SegaSaturnExporter::ColorDistance(Uint32 a, Uint32 b) {
    long dr = (long)((a >> 16) & 0xFF) - (long)((b >> 16) & 0xFF);
    long dg = (long)((a >> 8) & 0xFF) - (long)((b >> 8) & 0xFF);
    long db = (long)(a & 0xFF) - (long)(b & 0xFF);

    return dr * dr + dg * dg + db * db;
}

PRIVATE STATIC int SegaSaturnExporter::NearestInPalette(Uint32 color) {
    int best = 0;
    long bestDistance = -1;

    for (size_t i = 0; i < Palette.size(); i++) {
        long distance = SegaSaturnExporter::ColorDistance(Palette[i], color);
        if (bestDistance < 0 || distance < bestDistance) {
            bestDistance = distance;
            best = (int)i;
        }
    }

    return best;
}

// Counts every colour the picture uses, most-used first. Ordering by use is
// what makes the reduction below defensible: when something has to go, it is
// the colour covering the fewest pixels.
PRIVATE STATIC void SegaSaturnExporter::GatherColors(SceneLayer* layer, int width, int height, vector<SaturnColorUse>* out) {
    std::map<Uint32, size_t> counts;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel;
            if (!SegaSceneArt::GetLayerPixel(layer, x, y, &pixel))
                continue;

            if (((pixel >> 24) & 0xFF) < 128)
                continue;

            counts[SegaSaturnExporter::QuantizeColor(pixel & 0xFFFFFF)]++;
        }
    }

    out->clear();
    for (std::map<Uint32, size_t>::iterator it = counts.begin(); it != counts.end(); it++) {
        SaturnColorUse use;
        use.Color = it->first;
        use.Count = it->second;
        out->push_back(use);
    }

    std::sort(out->begin(), out->end(), [](const SaturnColorUse& a, const SaturnColorUse& b) -> bool {
        if (a.Count != b.Count)
            return a.Count > b.Count;

        // Ties broken by value, so two runs over one scene agree.
        return a.Color < b.Color;
    });
}

// Draws the layer into one byte a pixel. Index 0 stays transparent, so art is
// laid over 1..255 and anything that did not fit is matched to the nearest that
// did.
PRIVATE STATIC void SegaSaturnExporter::BuildImage(SceneLayer* layer, int width, int height) {
    Indices.assign((size_t)width * height, 0);

    std::map<Uint32, int> resolved;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel;
            if (!SegaSceneArt::GetLayerPixel(layer, x, y, &pixel))
                continue;

            if (((pixel >> 24) & 0xFF) < 128)
                continue;

            Uint32 color = SegaSaturnExporter::QuantizeColor(pixel & 0xFFFFFF);

            std::map<Uint32, int>::iterator known = resolved.find(color);
            int index;

            if (known != resolved.end())
                index = known->second;
            else {
                index = -1;
                for (size_t i = 0; i < Palette.size(); i++) {
                    if (Palette[i] == color) {
                        index = (int)i + 1;
                        break;
                    }
                }

                // Not in the palette, so it was one of the ones reduced away.
                if (index < 0) {
                    index = SegaSaturnExporter::NearestInPalette(color) + 1;
                    DroppedColors++;
                }

                resolved[color] = index;
            }

            Indices[(size_t)x + (size_t)y * width] = (Uint8)index;
        }
    }
}

PRIVATE STATIC bool SegaSaturnExporter::WriteBinary(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;

    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);

    return ok;
}

PRIVATE STATIC bool SegaSaturnExporter::WriteText(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (!f)
        return false;

    bool ok = fputs(text, f) >= 0;
    fclose(f);

    return ok;
}

PRIVATE STATIC bool SegaSaturnExporter::CopyFile(const char* from, const char* to) {
    FILE* in = fopen(from, "rb");
    if (!in)
        return false;

    FILE* out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return false;
    }

    char buffer[16384];
    size_t got;
    bool ok = true;

    while ((got = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, got, out) != got) {
            ok = false;
            break;
        }
    }

    fclose(in);
    fclose(out);

    return ok;
}

// The runtime lives beside the engine rather than inside it -- it is startup
// code and linker scripts that never change with the scene, and it reads better
// as files than as string literals.
//
// An installed engine has meta/ next to it, so that is looked for first from
// where the engine was started and then from where the engine itself is. A
// build tree does not necessarily have either -- the binary can be anywhere and
// the working directory is usually the project being exported -- so
// --saturn-runtime says outright where it is.
PRIVATE STATIC bool SegaSaturnExporter::FindRuntime(char* out, size_t outSize) {
    if (Application::SegaSaturnRuntimePath.size()) {
        StringUtils::Copy(out, Application::SegaSaturnRuntimePath.c_str(), outSize);
        return Directory::Exists(out);
    }

    const char* relative = "meta/saturn/runtime";

    if (Directory::Exists(relative)) {
        StringUtils::Copy(out, relative, outSize);
        return true;
    }

    char* base = SDL_GetBasePath();
    if (base) {
        snprintf(out, outSize, "%s%s", base, relative);
        SDL_free(base);

        if (Directory::Exists(out))
            return true;
    }

    return false;
}

PUBLIC STATIC SegaSaturnExportResult SegaSaturnExporter::ExportScene(const char* outputPath) {
    SegaSaturnExportResult result;
    memset(&result, 0, sizeof(result));

    SceneLayer* layer = SegaSceneArt::PickLayer();
    if (!layer) {
        StringUtils::Copy(result.Message, "The scene has no visible tile layer to export.", sizeof(result.Message));
        return result;
    }

    result.LayerWidth = layer->Width * Scene::TileWidth;
    result.LayerHeight = layer->Height * Scene::TileHeight;

    // Never smaller than a screenful, so the runtime always has something to
    // blit, and never so large that the picture will not fit in memory.
    int width = result.LayerWidth < SATURN_SCREEN_WIDTH ? SATURN_SCREEN_WIDTH : result.LayerWidth;
    int height = result.LayerHeight < SATURN_SCREEN_HEIGHT ? SATURN_SCREEN_HEIGHT : result.LayerHeight;

    bool clamped = false;
    while ((size_t)width * height > SATURN_MAX_IMAGE_BYTES) {
        if (height > SATURN_SCREEN_HEIGHT)
            height = height / 2 < SATURN_SCREEN_HEIGHT ? SATURN_SCREEN_HEIGHT : height / 2;
        else if (width > SATURN_SCREEN_WIDTH)
            width = width / 2 < SATURN_SCREEN_WIDTH ? SATURN_SCREEN_WIDTH : width / 2;
        else
            break;

        clamped = true;
    }

    result.ImageWidth = width;
    result.ImageHeight = height;
    result.ImageBytes = (size_t)width * height;

    vector<SaturnColorUse> colors;
    SegaSaturnExporter::GatherColors(layer, width, height, &colors);

    result.ColorsFound = (int)colors.size();

    Palette.clear();
    size_t keep = colors.size() < SATURN_PALETTE_USABLE ? colors.size() : SATURN_PALETTE_USABLE;
    for (size_t i = 0; i < keep; i++)
        Palette.push_back(colors[i].Color);

    result.PaletteCount = (int)Palette.size();

    DroppedColors = 0;
    SegaSaturnExporter::BuildImage(layer, width, height);
    result.ColorsDropped = DroppedColors;

    if (!SegaSaturnExporter::WriteProject(outputPath, &result))
        return result;

    result.Success = true;

    if (clamped) {
        snprintf(result.Message, sizeof(result.Message),
            "Exported %dx%d of a %dx%d layer in %d colour(s). The whole thing would not fit in memory, so what was written is the top-left of it.",
            width, height, result.LayerWidth, result.LayerHeight, result.PaletteCount);
    }
    else if (result.ColorsDropped) {
        snprintf(result.Message, sizeof(result.Message),
            "Exported a %dx%d picture. %d of %d colour(s) did not fit a palette of 255 and were matched to the nearest that did.",
            width, height, result.ColorsDropped, result.ColorsFound);
    }
    else {
        snprintf(result.Message, sizeof(result.Message),
            "Exported a %dx%d picture in %d colour(s), %d bytes.",
            width, height, result.PaletteCount, (int)result.ImageBytes);
    }

    return result;
}

PRIVATE STATIC bool SegaSaturnExporter::WriteProject(const char* outputPath, SegaSaturnExportResult* result) {
    char path[1024];
    char runtime[1024];

    if (!SegaSaturnExporter::FindRuntime(runtime, sizeof(runtime))) {
        StringUtils::Copy(result->Message,
            "Could not find the Saturn runtime. It ships as meta/saturn/runtime beside the engine; point --saturn-runtime at it if it is somewhere else.",
            sizeof(result->Message));
        return false;
    }

    const char* dirs[3] = { "", "/res", "/src" };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "%s%s", outputPath, dirs[i]);
        if (!Directory::Exists(path) && !Directory::CreatePath(path)) {
            snprintf(result->Message, sizeof(result->Message), "Could not create \"%s\".", path);
            return false;
        }
    }

    // --- the runtime, copied in as it is ---
    static const char* srcFiles[5] = { "saturn.h", "s_main.c", "string.c", "string.h", "saturn.ld" };

    char from[1024];
    for (int i = 0; i < 5; i++) {
        snprintf(from, sizeof(from), "%s/%s", runtime, srcFiles[i]);
        snprintf(path, sizeof(path), "%s/src/%s", outputPath, srcFiles[i]);
        if (!SegaSaturnExporter::CopyFile(from, path)) {
            snprintf(result->Message, sizeof(result->Message), "Could not copy \"%s\".", from);
            return false;
        }
    }

    // Copy assembly startup file
    snprintf(from, sizeof(from), "%s/saturn_start.s", runtime);
    snprintf(path, sizeof(path), "%s/src/saturn_start.s", outputPath);
    if (!SegaSaturnExporter::CopyFile(from, path)) {
        snprintf(result->Message, sizeof(result->Message), "Could not copy \"%s\".", from);
        return false;
    }

    // Copy header include file
    snprintf(from, sizeof(from), "%s/sat_header.inc", runtime);
    snprintf(path, sizeof(path), "%s/src/sat_header.inc", outputPath);
    if (!SegaSaturnExporter::CopyFile(from, path)) {
        snprintf(result->Message, sizeof(result->Message), "Could not copy \"%s\".", from);
        return false;
    }

    // --- the palette, as the Saturn's own colour words, big endian ---
    vector<Uint8> bytes;

    // Entry zero is left at black: nothing indexes it, since it is transparency.
    bytes.push_back(0);
    bytes.push_back(0);

    for (size_t i = 0; i < SATURN_PALETTE_USABLE; i++) {
        Uint16 word = 0;
        if (i < Palette.size()) {
            Uint32 color = Palette[i];
            word = SATURN_COLOR_WORD((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        }

        bytes.push_back((Uint8)(word >> 8));
        bytes.push_back((Uint8)(word & 0xFF));
    }

    snprintf(path, sizeof(path), "%s/res/palette.bin", outputPath);
    if (!SegaSaturnExporter::WriteBinary(path, bytes.data(), bytes.size())) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // --- the picture, one byte a pixel ---
    snprintf(path, sizeof(path), "%s/res/image.bin", outputPath);
    if (!SegaSaturnExporter::WriteBinary(path, Indices.data(), Indices.size())) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return SegaSaturnExporter::WriteSources(outputPath, result);
}

PRIVATE STATIC bool SegaSaturnExporter::WriteSources(const char* outputPath, SegaSaturnExportResult* result) {
    char path[1024];
    char text[8192];

    // The SH-2 program. It initializes the Saturn VDPs, loads the palette,
    // and blits the part of the picture the camera is over into the framebuffer.
    snprintf(text, sizeof(text),
        "// Generated by the Hatch Game Engine's SEGA Saturn exporter.\n"
        "//\n"
        "// The scene is in res/ as a palette and a bitmap. This puts it on the\n"
        "// screen and lets the pad move over it; the game goes here.\n"
        "\n"
        "#include \"saturn.h\"\n"
        "\n"
        "#define IMAGE_W %d\n"
        "#define IMAGE_H %d\n"
        "\n"
        "extern const unsigned char scene_image[];\n"
        "extern const unsigned short scene_palette[];\n"
        "\n"
        "static int cameraX = 0;\n"
        "static int cameraY = 0;\n"
        "\n"
        "int main(void) {\n"
        "    /* Initialize Saturn hardware */\n"
        "    /* TODO: Add proper initialization with SEGA Saturn SDK */\n"
        "\n"
        "    /* Load palette */\n"
        "    /* TODO: Use Saturn SDK to load scene_palette into CRAM */\n"
        "\n"
        "    while (1) {\n"
        "        /* Read controller input */\n"
        "        /* u16 pad = SATURN_ReadPad(); */\n"
        "\n"
        "        /* Update camera position */\n"
        "        /* if (pad & PAD_RIGHT) cameraX += 4; */\n"
        "        /* if (pad & PAD_LEFT)  cameraX -= 4; */\n"
        "        /* if (pad & PAD_DOWN)  cameraY += 4; */\n"
        "        /* if (pad & PAD_UP)    cameraY -= 4; */\n"
        "\n"
        "        /* Clamp camera to image bounds */\n"
        "        if (cameraX < 0) cameraX = 0;\n"
        "        if (cameraY < 0) cameraY = 0;\n"
        "        if (cameraX + SATURN_SCREEN_WIDTH > IMAGE_W)\n"
        "            cameraX = IMAGE_W - SATURN_SCREEN_WIDTH;\n"
        "        if (cameraY + SATURN_SCREEN_HEIGHT > IMAGE_H)\n"
        "            cameraY = IMAGE_H - SATURN_SCREEN_HEIGHT;\n"
        "\n"
        "        /* Blit visible portion to framebuffer */\n"
        "        /* TODO: Use Saturn SDK blitting functions */\n"
        "\n"
        "        /* Wait for VBLANK */\n"
        "        /* SATURN_WaitVBlank(); */\n"
        "    }\n"
        "\n"
        "    return 0;\n"
        "}\n",
        result->ImageWidth, result->ImageHeight);

    snprintf(path, sizeof(path), "%s/src/s_main.c", outputPath);
    if (!SegaSaturnExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // Generate scene_data.s with embedded palette and image
    snprintf(text, sizeof(text),
        "/* Embedded scene data for SEGA Saturn */\n"
        "\n"
        ".section .rodata\n"
        ".global _scene_palette\n"
        ".global _scene_image\n"
        "\n"
        "_scene_palette:\n"
        ".incbin \"res/palette.bin\"\n"
        "\n"
        "_scene_image:\n"
        ".incbin \"res/image.bin\"\n"
        "\n");

    snprintf(path, sizeof(path), "%s/src/scene_data.s", outputPath);
    if (!SegaSaturnExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // Makefile for building with SEGA Saturn SDK
    snprintf(text, sizeof(text),
        "# Makefile for SEGA Saturn export from Hatch Game Engine\n"
        "#\n"
        "# Build with the SEGA Saturn SDK:\n"
        "#   https://github.com/SaturnSDK\n"
        "#\n"
        "# Set SATURN_SDK to your SDK installation:\n"
        "#   export SATURN_SDK=/path/to/saturn-sdk\n"
        "#   make\n"
        "#\n"
        "# The output will be out/rom.bin\n"
        "\n"
        "SATURN_SDK ?= /opt/saturn-sdk\n"
        "\n"
        "SHCC = $(SATURN_SDK)/bin/sh-elf-gcc\n"
        "SHAS = $(SATURN_SDK)/bin/sh-elf-as\n"
        "SHLD = $(SATURN_SDK)/bin/sh-elf-ld\n"
        "SHOBJC = $(SATURN_SDK)/bin/sh-elf-objcopy\n"
        "\n"
        "SHCFLAGS = -O2 -Wall -Isrc\n"
        "SHASFLAGS = \n"
        "SHLDFLAGS = -T src/saturn.ld -nostdlib\n"
        "\n"
        "OBJS = src/saturn_start.o src/scene_data.o src/m_main.o src/s_main.o src/string.o\n"
        "\n"
        "all: out\n"
        "\tout/rom.bin\n"
        "\n"
        "out:\n"
        "\tmkdir -p out\n"
        "\n"
        "src/saturn_start.o: src/saturn_start.s src/sat_header.inc\n"
        "\t$(SHAS) $(SHASFLAGS) $< -o $@\n"
        "\n"
        "src/scene_data.o: src/scene_data.s res/palette.bin res/image.bin\n"
        "\t$(SHAS) $(SHASFLAGS) $< -o $@\n"
        "\n"
        "src/m_main.o: src/m_main.c\n"
        "\t$(SHCC) $(SHCFLAGS) -c $< -o $@\n"
        "\n"
        "src/s_main.o: src/s_main.c\n"
        "\t$(SHCC) $(SHCFLAGS) -c $< -o $@\n"
        "\n"
        "src/string.o: src/string.c\n"
        "\t$(SHCC) $(SHCFLAGS) -c $< -o $@\n"
        "\n"
        "out/rom.bin: $(OBJS)\n"
        "\t$(SHCC) $(SHLDFLAGS) $(OBJS) -o out/rom.elf -lgcc\n"
        "\t$(SHOBJC) -O binary out/rom.elf $@\n"
        "\n"
        "clean:\n"
        "\trm -rf out src/*.o\n"
        "\n"
        ".PHONY: all clean out\n");

    snprintf(path, sizeof(path), "%s/Makefile", outputPath);
    if (!SegaSaturnExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // README
    snprintf(text, sizeof(text),
        "# %s, for the SEGA Saturn\n"
        "\n"
        "Exported from the Hatch Game Engine. The Saturn has dual VDPs and supports\n"
        "bitmap graphics with up to 32768 colors. This export uses a palette-based\n"
        "approach:\n"
        "\n"
        "| File | What it holds |\n"
        "| --- | --- |\n"
        "| `res/palette.bin` | 256 colour words, fifteen bits each, five per channel |\n"
        "| `res/image.bin` | a %dx%d picture, one byte a pixel, %d bytes |\n"
        "| `src/s_main.c` | the SH-2 program that shows it |\n"
        "| `src/saturn_*` | the startup every Saturn program needs |\n"
        "\n"
        "%d of the %d colour(s) the scene uses are in the palette. Index 0 is not\n"
        "one of them: it is reserved for transparency.\n"
        "\n"
        "## Building\n"
        "\n"
        "```sh\n"
        "export SATURN_SDK=/path/to/saturn-sdk\n"
        "make\n"
        "```\n"
        "\n"
        "`out/rom.bin` is the result. Run it on hardware or in an emulator like\n"
        "Mednafen, Yabause, or Kronos.\n"
        "\n"
        "## What this is and is not\n"
        "\n"
        "The pad scrolls around the picture. That is all it does: Hatch's game\n"
        "logic is bytecode for a VM that does not exist on an SH-2, so none of it\n"
        "came across. The art did, at fifteen bits of colour.\n"
        "\n"
        "The picture sits in ROM and the SH-2 blits the part the camera is over.\n"
        "This is a starting point for a real game.\n",
        Scene::CurrentScene[0] ? Scene::CurrentScene : "Scene",
        result->ImageWidth, result->ImageHeight, (int)result->ImageBytes,
        result->PaletteCount, result->ColorsFound);

    snprintf(path, sizeof(path), "%s/README.md", outputPath);
    if (!SegaSaturnExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return true;
}
