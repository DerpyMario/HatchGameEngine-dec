#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/Sega32XTypes.h>

need_t SceneLayer;

class Sega32XExporter {
public:

};
#endif

#include <Engine/Exporters/Sega32XExporter.h>
#include <Engine/Exporters/SegaSceneArt.h>

#include <Engine/Application.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/Scene.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Utilities/StringUtils.h>

// Turning a Hatch scene into something a 32X can show.
//
// This is the one SEGA machine in the family that does not want tiles. Its own
// VDP holds two framebuffers and, in packed-pixel mode, reads one byte per
// pixel straight out of them. So where the Mega Drive export takes a layer
// apart into patterns and a nametable, this one simply draws the layer and
// hands over the picture -- which is much closer to what the engine does
// anyway, and keeps far more of the art: fifteen bits of colour against the
// Mega Drive's nine, and 255 of them on screen at once against 61.
//
// The picture goes into ROM whole and the SH-2 blits the part of it the camera
// is over. That is what a 32X game does; there is nowhere else for a bitmap to
// live on a cartridge.

static vector<Uint8>  Indices;      // one byte a pixel, row major
static vector<Uint32> Palette;      // 0xRRGGBB, already rounded to five bits
static int            DroppedColors;

// Rounded into the five bits a channel the 32X keeps, then spread back over the
// full range so comparisons happen in the space it will land in.
PRIVATE STATIC Uint32 Sega32XExporter::QuantizeColor(Uint32 argb) {
    Uint32 r = (Uint32)((((argb >> 16) & 0xFF) >> 3) * 255 / 31);
    Uint32 g = (Uint32)((((argb >> 8) & 0xFF) >> 3) * 255 / 31);
    Uint32 b = (Uint32)(((argb & 0xFF) >> 3) * 255 / 31);

    return (r << 16) | (g << 8) | b;
}

PRIVATE STATIC long Sega32XExporter::ColorDistance(Uint32 a, Uint32 b) {
    long dr = (long)((a >> 16) & 0xFF) - (long)((b >> 16) & 0xFF);
    long dg = (long)((a >> 8) & 0xFF) - (long)((b >> 8) & 0xFF);
    long db = (long)(a & 0xFF) - (long)(b & 0xFF);

    return dr * dr + dg * dg + db * db;
}

PRIVATE STATIC int Sega32XExporter::NearestInPalette(Uint32 color) {
    int best = 0;
    long bestDistance = -1;

    for (size_t i = 0; i < Palette.size(); i++) {
        long distance = Sega32XExporter::ColorDistance(Palette[i], color);
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
PRIVATE STATIC void Sega32XExporter::GatherColors(SceneLayer* layer, int width, int height, vector<X32ColorUse>* out) {
    std::map<Uint32, size_t> counts;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel;
            if (!SegaSceneArt::GetLayerPixel(layer, x, y, &pixel))
                continue;

            if (((pixel >> 24) & 0xFF) < 128)
                continue;

            counts[Sega32XExporter::QuantizeColor(pixel & 0xFFFFFF)]++;
        }
    }

    out->clear();
    for (std::map<Uint32, size_t>::iterator it = counts.begin(); it != counts.end(); it++) {
        X32ColorUse use;
        use.Color = it->first;
        use.Count = it->second;
        out->push_back(use);
    }

    std::sort(out->begin(), out->end(), [](const X32ColorUse& a, const X32ColorUse& b) -> bool {
        if (a.Count != b.Count)
            return a.Count > b.Count;

        // Ties broken by value, so two runs over one scene agree.
        return a.Color < b.Color;
    });
}

// Draws the layer into one byte a pixel. Index 0 stays transparent, so art is
// laid over 1..255 and anything that did not fit is matched to the nearest that
// did.
PRIVATE STATIC void Sega32XExporter::BuildImage(SceneLayer* layer, int width, int height) {
    Indices.assign((size_t)width * height, 0);

    std::map<Uint32, int> resolved;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel;
            if (!SegaSceneArt::GetLayerPixel(layer, x, y, &pixel))
                continue;

            if (((pixel >> 24) & 0xFF) < 128)
                continue;

            Uint32 color = Sega32XExporter::QuantizeColor(pixel & 0xFFFFFF);

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
                    index = Sega32XExporter::NearestInPalette(color) + 1;
                    DroppedColors++;
                }

                resolved[color] = index;
            }

            Indices[(size_t)x + (size_t)y * width] = (Uint8)index;
        }
    }
}

PRIVATE STATIC bool Sega32XExporter::WriteBinary(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;

    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);

    return ok;
}

PRIVATE STATIC bool Sega32XExporter::WriteText(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (!f)
        return false;

    bool ok = fputs(text, f) >= 0;
    fclose(f);

    return ok;
}

PRIVATE STATIC bool Sega32XExporter::CopyFile(const char* from, const char* to) {
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

// The runtime lives beside the engine rather than inside it -- it is 60 KB of
// startup code and linker scripts that never change with the scene, and it
// reads better as files than as string literals.
//
// An installed engine has meta/ next to it, so that is looked for first from
// where the engine was started and then from where the engine itself is. A
// build tree does not necessarily have either -- the binary can be anywhere and
// the working directory is usually the project being exported -- so
// --32x-runtime says outright where it is.
PRIVATE STATIC bool Sega32XExporter::FindRuntime(char* out, size_t outSize) {
    if (Application::Sega32XRuntimePath.size()) {
        StringUtils::Copy(out, Application::Sega32XRuntimePath.c_str(), outSize);
        return Directory::Exists(out);
    }

    const char* relative = "meta/32x/runtime";

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

PUBLIC STATIC Sega32XExportResult Sega32XExporter::ExportScene(const char* outputPath) {
    Sega32XExportResult result;
    memset(&result, 0, sizeof(result));

    SceneLayer* layer = SegaSceneArt::PickLayer();
    if (!layer) {
        StringUtils::Copy(result.Message, "The scene has no visible tile layer to export.", sizeof(result.Message));
        return result;
    }

    result.LayerWidth = layer->Width * Scene::TileWidth;
    result.LayerHeight = layer->Height * Scene::TileHeight;

    // Never smaller than a screenful, so the runtime always has something to
    // blit, and never so large that the picture will not fit on a cartridge.
    int width = result.LayerWidth < X32_SCREEN_WIDTH ? X32_SCREEN_WIDTH : result.LayerWidth;
    int height = result.LayerHeight < X32_SCREEN_HEIGHT ? X32_SCREEN_HEIGHT : result.LayerHeight;

    bool clamped = false;
    while ((size_t)width * height > X32_MAX_IMAGE_BYTES) {
        if (height > X32_SCREEN_HEIGHT)
            height = height / 2 < X32_SCREEN_HEIGHT ? X32_SCREEN_HEIGHT : height / 2;
        else if (width > X32_SCREEN_WIDTH)
            width = width / 2 < X32_SCREEN_WIDTH ? X32_SCREEN_WIDTH : width / 2;
        else
            break;

        clamped = true;
    }

    result.ImageWidth = width;
    result.ImageHeight = height;
    result.ImageBytes = (size_t)width * height;

    vector<X32ColorUse> colors;
    Sega32XExporter::GatherColors(layer, width, height, &colors);

    result.ColorsFound = (int)colors.size();

    Palette.clear();
    size_t keep = colors.size() < X32_PALETTE_USABLE ? colors.size() : X32_PALETTE_USABLE;
    for (size_t i = 0; i < keep; i++)
        Palette.push_back(colors[i].Color);

    result.PaletteCount = (int)Palette.size();

    DroppedColors = 0;
    Sega32XExporter::BuildImage(layer, width, height);
    result.ColorsDropped = DroppedColors;

    if (!Sega32XExporter::WriteProject(outputPath, &result))
        return result;

    result.Success = true;

    if (clamped) {
        snprintf(result.Message, sizeof(result.Message),
            "Exported %dx%d of a %dx%d layer in %d colour(s). The whole thing would not fit in a cartridge, so what was written is the top-left of it.",
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

PRIVATE STATIC bool Sega32XExporter::WriteProject(const char* outputPath, Sega32XExportResult* result) {
    char path[1024];
    char runtime[1024];

    if (!Sega32XExporter::FindRuntime(runtime, sizeof(runtime))) {
        StringUtils::Copy(result->Message,
            "Could not find the 32X runtime. It ships as meta/32x/runtime beside the engine; point --32x-runtime at it if it is somewhere else.",
            sizeof(result->Message));
        return false;
    }

    const char* dirs[4] = { "", "/res", "/md_src", "/sh_src" };
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof(path), "%s%s", outputPath, dirs[i]);
        if (!Directory::Exists(path) && !Directory::CreatePath(path)) {
            snprintf(result->Message, sizeof(result->Message), "Could not create \"%s\".", path);
            return false;
        }
    }

    // --- the runtime, copied in as it is ---
    static const char* mdFiles[5] = { "common.h", "font.s", "md.ld", "md_main.c", "md_start.s" };
    static const char* shFiles[7] = { "mars.c", "mars.h", "mars.ld", "mars_start.s", "s_main.c", "string.c", "string.h" };

    char from[1024];
    for (int i = 0; i < 5; i++) {
        snprintf(from, sizeof(from), "%s/md_src/%s", runtime, mdFiles[i]);
        snprintf(path, sizeof(path), "%s/md_src/%s", outputPath, mdFiles[i]);
        if (!Sega32XExporter::CopyFile(from, path)) {
            snprintf(result->Message, sizeof(result->Message), "Could not copy \"%s\".", from);
            return false;
        }
    }
    for (int i = 0; i < 7; i++) {
        snprintf(from, sizeof(from), "%s/sh_src/%s", runtime, shFiles[i]);
        snprintf(path, sizeof(path), "%s/sh_src/%s", outputPath, shFiles[i]);
        if (!Sega32XExporter::CopyFile(from, path)) {
            snprintf(result->Message, sizeof(result->Message), "Could not copy \"%s\".", from);
            return false;
        }
    }

    snprintf(from, sizeof(from), "%s/LICENSE.marsdev", runtime);
    snprintf(path, sizeof(path), "%s/LICENSE.marsdev", outputPath);
    Sega32XExporter::CopyFile(from, path);

    // --- the palette, as the 32X's own colour words, big endian ---
    vector<Uint8> bytes;

    // Entry zero is left at black: nothing indexes it, since it is the value
    // that lets the Mega Drive's output through.
    bytes.push_back(0);
    bytes.push_back(0);

    for (size_t i = 0; i < X32_PALETTE_USABLE; i++) {
        Uint16 word = 0;
        if (i < Palette.size()) {
            Uint32 color = Palette[i];
            word = X32_COLOR_WORD((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        }

        bytes.push_back((Uint8)(word >> 8));
        bytes.push_back((Uint8)(word & 0xFF));
    }

    snprintf(path, sizeof(path), "%s/res/palette.bin", outputPath);
    if (!Sega32XExporter::WriteBinary(path, bytes.data(), bytes.size())) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // --- the picture, one byte a pixel ---
    snprintf(path, sizeof(path), "%s/res/image.bin", outputPath);
    if (!Sega32XExporter::WriteBinary(path, Indices.data(), Indices.size())) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return Sega32XExporter::WriteSources(outputPath, result);
}

PRIVATE STATIC bool Sega32XExporter::WriteSources(const char* outputPath, Sega32XExportResult* result) {
    char path[1024];
    char text[8192];

    // The SH-2 program. It brings the 32X VDP up in packed-pixel mode, loads
    // the palette into CRAM, and blits the part of the picture the camera is
    // over into the back framebuffer. Blitting only when something moved is
    // what keeps it quick: 320x224 is seventy-odd kilobytes, and copying that
    // every frame is most of what a 23 MHz SH-2 can do.
    snprintf(text, sizeof(text),
        "// Generated by the Hatch Game Engine's 32X exporter.\n"
        "//\n"
        "// The scene is in res/ as a palette and a bitmap. This puts it on the\n"
        "// screen and lets the pad move over it; the game goes here.\n"
        "\n"
        "#include \"mars.h\"\n"
        "\n"
        "#define IMAGE_W %d\n"
        "#define IMAGE_H %d\n"
        "\n"
        "extern const unsigned char scene_image[];\n"
        "extern const unsigned short scene_palette[];\n"
        "\n"
        "static int cameraX = 0;\n"
        "static int cameraY = 0;\n"
        "static unsigned short currentFB = 0;\n"
        "\n"
        "static void loadPalette(void)\n"
        "{\n"
        "    volatile unsigned short *cram = &MARS_CRAM;\n"
        "    int i;\n"
        "\n"
        "    for (i = 0; i < 256; i++)\n"
        "        cram[i] = scene_palette[i];\n"
        "}\n"
        "\n"
        "// One screenful out of the picture and into the framebuffer. The line\n"
        "// table the VDP reads sits in the first 0x100 words, so pixels start\n"
        "// there and every line is 160 words further on.\n"
        "static void blit(void)\n"
        "{\n"
        "    volatile unsigned short *fb = &MARS_FRAMEBUFFER;\n"
        "    int y;\n"
        "\n"
        "    for (y = 0; y < %d; y++) {\n"
        "        const unsigned char *src = &scene_image[(cameraY + y) * IMAGE_W + cameraX];\n"
        "        volatile unsigned short *dst = &fb[0x100 + y * %d];\n"
        "        int x;\n"
        "\n"
        "        // Two pixels to a word, and the VDP wants the left one high.\n"
        "        for (x = 0; x < %d; x += 2)\n"
        "            *dst++ = (unsigned short)((src[x] << 8) | src[x + 1]);\n"
        "    }\n"
        "}\n"
        "\n"
        "static void swapBuffers(void)\n"
        "{\n"
        "    MARS_VDP_FBCTL = currentFB ^ 1;\n"
        "    while ((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB) ;\n"
        "    currentFB ^= 1;\n"
        "}\n"
        "\n"
        "int m_main(void)\n"
        "{\n"
        "    int moved = 1;\n"
        "\n"
        "    Hw32xInit(MARS_VDP_MODE_256, 0);\n"
        "    loadPalette();\n"
        "\n"
        "    for (;;) {\n"
        "        unsigned short pad;\n"
        "\n"
        "        HwMdReadPad(0);\n"
        "        pad = MARS_SYS_COMM8;\n"
        "\n"
        "        if ((pad & SEGA_CTRL_RIGHT) && cameraX < IMAGE_W - %d) { cameraX += 2; moved = 1; }\n"
        "        if ((pad & SEGA_CTRL_LEFT)  && cameraX > 0)            { cameraX -= 2; moved = 1; }\n"
        "        if ((pad & SEGA_CTRL_DOWN)  && cameraY < IMAGE_H - %d) { cameraY += 2; moved = 1; }\n"
        "        if ((pad & SEGA_CTRL_UP)    && cameraY > 0)            { cameraY -= 2; moved = 1; }\n"
        "\n"
        "        // Both framebuffers have to be caught up before the blit can\n"
        "        // stop, or the flip shows the one that was missed.\n"
        "        if (moved) {\n"
        "            blit();\n"
        "            swapBuffers();\n"
        "            blit();\n"
        "            swapBuffers();\n"
        "            moved = 0;\n"
        "        }\n"
        "\n"
        "        Hw32xDelay(1);\n"
        "    }\n"
        "\n"
        "    return 0;\n"
        "}\n",
        result->ImageWidth, result->ImageHeight,
        X32_SCREEN_HEIGHT, X32_LINE_WORDS, X32_SCREEN_WIDTH,
        X32_SCREEN_WIDTH, X32_SCREEN_HEIGHT);

    snprintf(path, sizeof(path), "%s/sh_src/m_main.c", outputPath);
    if (!Sega32XExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // The data, brought in as it sits on disk. .incbin keeps it out of the C
    // compiler, which would otherwise be asked to parse a couple of hundred
    // thousand initialisers.
    const char* dataText =
        "! Generated by the Hatch Game Engine's 32X exporter.\n"
        "!\n"
        "! The scene's palette and picture, included as they are rather than\n"
        "! turned into C arrays -- a few hundred thousand initialisers is a lot\n"
        "! to ask a compiler to read for no gain.\n"
        "\n"
        "    .section .data\n"
        "\n"
        "    .global _scene_palette\n"
        "    .align 2\n"
        "_scene_palette:\n"
        "    .incbin \"res/palette.bin\"\n"
        "\n"
        "    .global _scene_image\n"
        "    .align 2\n"
        "_scene_image:\n"
        "    .incbin \"res/image.bin\"\n";

    snprintf(path, sizeof(path), "%s/sh_src/scene_data.s", outputPath);
    if (!Sega32XExporter::WriteText(path, dataText)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return Sega32XExporter::WriteBuildFiles(outputPath, result);
}

PRIVATE STATIC bool Sega32XExporter::WriteBuildFiles(const char* outputPath, Sega32XExportResult* result) {
    char path[1024];
    char text[6144];

    // A 32X cartridge holds code for two different processors, so the build has
    // two of everything. The prefixes are overridable because the toolchain is
    // named one way when Marsdev builds it and another when a distribution
    // packages it, and the instructions are the same either way.
    const char* makefileText =
        "# Built with an SH-2 and an m68k toolchain. Marsdev supplies both:\n"
        "#\n"
        "#   export MARSDEV=/path/to/mars\n"
        "#   make\n"
        "#\n"
        "# A distribution's own cross compilers work too, if they are named:\n"
        "#\n"
        "#   make SHBIN= MDBIN= SHPREFIX=sh-elf- MDPREFIX=m68k-linux-gnu-\n"
        "#\n"
        "# The ROM comes out as out/rom.32x.\n"
        "\n"
        "MARSDEV  ?= $(HOME)/mars\n"
        "SHBIN    ?= $(MARSDEV)/sh-elf/bin/\n"
        "MDBIN    ?= $(MARSDEV)/m68k-elf/bin/\n"
        "SHPREFIX ?= sh-elf-\n"
        "MDPREFIX ?= m68k-elf-\n"
        "\n"
        "SHCC   = $(SHBIN)$(SHPREFIX)gcc\n"
        "SHAS   = $(SHBIN)$(SHPREFIX)as\n"
        "SHOBJC = $(SHBIN)$(SHPREFIX)objcopy\n"
        "MDCC   = $(MDBIN)$(MDPREFIX)gcc\n"
        "MDOBJC = $(MDBIN)$(MDPREFIX)objcopy\n"
        "\n"
        "# -m2 -mb: an SH-2, big endian. Both matter -- the part is an SH-2 and\n"
        "# the whole machine is big endian.\n"
        "SHCFLAGS  = -m2 -mb -O2 -Wall -std=c99 -ffreestanding -fomit-frame-pointer -Ish_src\n"
        "SHASFLAGS = -Ish_src --small\n"
        "MDCFLAGS  = -m68000 -mshort -O2 -Wall -std=c99 -ffreestanding -Imd_src\n"
        "MDASFLAGS = -x assembler-with-cpp -m68000 -Imd_src -Wa,--register-prefix-optional\n"
        "\n"
        "# The 68000 objects. This side is only the cartridge header and the code\n"
        "# that hands the machine to the SH-2s.\n"
        "MDOBJS = md_src/md_start.o md_src/font.o md_src/md_main.o\n"
        "\n"
        "# mars_start.o carries the 32X header and has to lead the link, and it\n"
        "# is also what pulls the 68000 binary in, so that has to exist first.\n"
        "SHOBJS = sh_src/mars_start.o sh_src/scene_data.o sh_src/mars.o \\\n"
        "         sh_src/m_main.o sh_src/s_main.o sh_src/string.o\n"
        "\n"
        "all: out/rom.32x\n"
        "\n"
        "out:\n"
        "\tmkdir -p out\n"
        "\n"
        "md_src/%.o: md_src/%.s\n"
        "\t$(MDCC) $(MDASFLAGS) -c $< -o $@\n"
        "\n"
        "md_src/%.o: md_src/%.c\n"
        "\t$(MDCC) $(MDCFLAGS) -c $< -o $@\n"
        "\n"
        "# The 68000 program, flattened. mars_start.s includes this whole.\n"
        "md_start.bin: $(MDOBJS)\n"
        "\t$(MDCC) -T md_src/md.ld -nostdlib $(MDOBJS) -o md_start.elf \\\n"
        "\t  -Wl,--build-id=none -lgcc\n"
        "\t$(MDOBJC) -O binary md_start.elf $@\n"
        "\n"
        "sh_src/mars_start.o: sh_src/mars_start.s md_start.bin\n"
        "\t$(SHAS) $(SHASFLAGS) $< -o $@\n"
        "\n"
        "sh_src/scene_data.o: sh_src/scene_data.s res/palette.bin res/image.bin\n"
        "\t$(SHAS) $(SHASFLAGS) $< -o $@\n"
        "\n"
        "sh_src/%.o: sh_src/%.c\n"
        "\t$(SHCC) $(SHCFLAGS) -c $< -o $@\n"
        "\n"
        "# A cartridge is read in whole blocks, so the image is padded out to one.\n"
        "out/rom.32x: $(SHOBJS) | out\n"
        "\t$(SHCC) -m2 -mb -T sh_src/mars.ld -nostdlib $(SHOBJS) -o out/rom.elf \\\n"
        "\t  -Wl,--build-id=none -lgcc\n"
        "\t$(SHOBJC) -O binary out/rom.elf out/rom.tmp\n"
        "\tdd if=out/rom.tmp of=$@ bs=8192 conv=sync status=none\n"
        "\trm -f out/rom.tmp\n"
        "\n"
        "clean:\n"
        "\trm -rf out $(MDOBJS) $(SHOBJS) md_start.bin md_start.elf\n"
        "\n"
        ".PHONY: all clean\n";

    snprintf(path, sizeof(path), "%s/Makefile", outputPath);
    if (!Sega32XExporter::WriteText(path, makefileText)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    snprintf(text, sizeof(text),
        "# %s, for the 32X\n"
        "\n"
        "Exported from the Hatch Game Engine. The 32X has a framebuffer rather\n"
        "than a tile grid, so the scene was drawn rather than taken apart:\n"
        "\n"
        "| File | What it holds |\n"
        "| --- | --- |\n"
        "| `res/palette.bin` | 256 colour words, fifteen bits each, five per channel |\n"
        "| `res/image.bin` | a %dx%d picture, one byte a pixel, %d bytes |\n"
        "| `sh_src/m_main.c` | the SH-2 program that shows it |\n"
        "| `md_src/`, `sh_src/mars*` | the startup every 32X program needs, from Marsdev (MIT, see LICENSE.marsdev) |\n"
        "\n"
        "%d of the %d colour(s) the scene uses are in the palette. Index 0 is not\n"
        "one of them: on this machine it is the value that lets the Mega Drive's\n"
        "own output show through, so the engine's transparent pixels become it.\n"
        "\n"
        "## Building\n"
        "\n"
        "```sh\n"
        "export MARSDEV=/path/to/mars\n"
        "make\n"
        "```\n"
        "\n"
        "`out/rom.32x` is the result. It needs an emulator that does 32X --\n"
        "PicoDrive, Ares and Kega Fusion all do; Genesis Plus GX does not.\n"
        "\n"
        "## What this is and is not\n"
        "\n"
        "The pad scrolls around the picture. That is all it does: Hatch's game\n"
        "logic is bytecode for a VM that does not exist on an SH-2, so none of it\n"
        "came across. The art did, and at fifteen bits of colour rather than the\n"
        "Mega Drive's nine it came across nearly intact.\n"
        "\n"
        "The picture sits in ROM whole and the SH-2 blits the part the camera is\n"
        "over, only when it has moved. Blitting a screenful is seventy thousand\n"
        "bytes, which is most of what this processor can do in a frame.\n",
        Scene::CurrentScene[0] ? Scene::CurrentScene : "Scene",
        result->ImageWidth, result->ImageHeight, (int)result->ImageBytes,
        result->PaletteCount, result->ColorsFound);

    snprintf(path, sizeof(path), "%s/README.md", outputPath);
    if (!Sega32XExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return true;
}
