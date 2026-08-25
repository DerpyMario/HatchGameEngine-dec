#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/MegaCDTypes.h>

need_t SceneLayer;

class MegaCDExporter {
public:

};
#endif

#include <Engine/Exporters/MegaCDExporter.h>

#include <Engine/Exporters/SegaSceneArt.h>
#include <Engine/Exporters/SegaVDPConverter.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/Scene.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Utilities/StringUtils.h>

// Turning a Hatch scene into something a Mega CD can show.
//
// The Mega CD is a Mega Drive with a disc drive bolted to its side, and its
// graphics hardware is not merely similar -- it is the same VDP, in the same
// machine, with the same 64 KB of VRAM. So the art is converted by exactly the
// code the Mega Drive export uses, and everything different about this export
// is about where the art lives rather than what it looks like.
//
// What the disc changes is that the art no longer has to be inside the
// program. A cartridge holds its graphics as part of the ROM the console
// executes; a disc holds files, and a program reads them. That matters here
// because of a limit the boot format imposes: the Initial Program the BIOS
// loads and runs is at most 3.5 KB, and the Sub Program beside it 28 KB. A
// scene's patterns alone can be fifty. Nothing of any size can travel in the
// program, so the art travels as a file, and the program reads it.
//
// Which is a two-processor errand. The CD drive belongs to the Sub CPU and the
// VDP belongs to the Main CPU, and the only thing they share is Word RAM.  So:
// the Main CPU hands Word RAM over and asks; the Sub CPU reads the file into
// it and hands it back; the Main CPU copies it into CRAM and VRAM. The two
// halves of that are src/ip.s and src/sp.s.

// The art is one file rather than three. It is read in one pass into Word RAM
// and unpacked from there, and its three parts are always in this order.
#define MCD_ART_FILENAME    "hatch.bin"
#define MCD_ART_ISO_NAME    "HATCH.BIN;1"

// A plane side is 32, 64 or 128 cells, and VDP register 16 names each side by
// a code rather than by a number.
PRIVATE STATIC int MegaCDExporter::PlaneSizeCode(int cells) {
    if (cells >= 128)
        return 3;
    if (cells >= 64)
        return 1;

    return 0;
}

PRIVATE STATIC bool MegaCDExporter::WriteBinary(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;

    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);

    return ok;
}

PRIVATE STATIC bool MegaCDExporter::WriteText(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (!f)
        return false;

    bool ok = fputs(text, f) >= 0;
    fclose(f);

    return ok;
}

PUBLIC STATIC MegaCDExportResult MegaCDExporter::ExportScene(const char* outputPath) {
    MegaCDExportResult result;
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
    result.FileBytes = (MD_PALETTE_COUNT * MD_PALETTE_SIZE * 2) + art.TileBytes + art.MapBytes;

    // The disc has room for far more than this, but VRAM does not, and the
    // plane and the two tables the VDP reads every frame have to fit beside
    // the patterns.
    if (result.TileCount > MCD_TILE_BUDGET) {
        snprintf(result.Message, sizeof(result.Message),
            "The layer needs %d unique tiles. VRAM has room for %d of them beside the nametable and the scroll and sprite tables.",
            result.TileCount, MCD_TILE_BUDGET);
        return result;
    }

    if (!MegaCDExporter::WriteProject(outputPath, &result))
        return result;

    result.Success = true;

    if (result.LayerWidth > planeW || result.LayerHeight > planeH) {
        snprintf(result.Message, sizeof(result.Message),
            "Exported %d tiles in %d palette(s). The layer is %dx%d cells and the largest plane the VDP offers is %dx%d, so the disc shows the top-left %dx%d of it.",
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
            "Exported %d tiles in %d palette(s), %d bytes of art on the disc.",
            result.TileCount, result.PaletteCount, (int)result.FileBytes);
    }

    return result;
}

PRIVATE STATIC bool MegaCDExporter::WriteProject(const char* outputPath, MegaCDExportResult* result) {
    char path[1024];

    // res and build hold nothing the export writes, but Megadev's makefile
    // insists on being told about both and puts its own work in them.
    const char* folders[5] = { "", "/src", "/res", "/disc", "/build" };

    for (int i = 0; i < 5; i++) {
        snprintf(path, sizeof(path), "%s%s", outputPath, folders[i]);

        if (!Directory::Exists(path) && !Directory::CreatePath(path)) {
            snprintf(result->Message, sizeof(result->Message), "Could not create \"%s\".", path);
            return false;
        }
    }

    // --- the art, as one file, in the order the program unpacks it ---
    vector<Uint8> file;
    vector<Uint8> part;

    SegaVDPConverter::PaletteBytes(&part);
    file.insert(file.end(), part.begin(), part.end());

    SegaVDPConverter::TileBytes(&part);
    file.insert(file.end(), part.begin(), part.end());

    SegaVDPConverter::NametableBytes(&part);
    file.insert(file.end(), part.begin(), part.end());

    snprintf(path, sizeof(path), "%s/disc/%s", outputPath, MCD_ART_FILENAME);
    if (!MegaCDExporter::WriteBinary(path, file.data(), file.size())) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return MegaCDExporter::WritePlayer(outputPath, result);
}

PRIVATE STATIC bool MegaCDExporter::WritePlayer(const char* outputPath, MegaCDExportResult* result) {
    char path[1024];
    char text[16384];

    // How far the window can travel before it runs off the art. The plane
    // wraps rather than stopping, so this is what keeps the scene from
    // repeating along its own edge.
    int visibleW = result->PlaneWidth < result->LayerWidth ? result->PlaneWidth : result->LayerWidth;
    int visibleH = result->PlaneHeight < result->LayerHeight ? result->PlaneHeight : result->LayerHeight;

    int cameraMaxX = visibleW * MD_TILE_SIZE - MCD_SCREEN_WIDTH;
    int cameraMaxY = visibleH * MD_TILE_SIZE - MCD_SCREEN_HEIGHT;

    if (cameraMaxX < 0)
        cameraMaxX = 0;
    if (cameraMaxY < 0)
        cameraMaxY = 0;

    // The Main CPU's half. Almost all of the VDP's setup is the Mega CD BIOS's
    // own default, which is why so little of it is here.
    snprintf(text, sizeof(text),
        "/*\n"
        " * Generated by the Hatch Game Engine's Mega CD exporter.\n"
        " *\n"
        " * The Main CPU's half: it asks the Sub CPU for the scene's art, puts it in\n"
        " * CRAM and VRAM, and then moves a window over it with the pad. The game\n"
        " * goes here.\n"
        " */\n"
        "\n"
        ".section .text\n"
        "\n"
        "#include <macros.s>\n"
        "#include <system.macros.s>\n"
        "#include <main/memmap.def.h>\n"
        "#include <main/bios.def.h>\n"
        "#include <main/gate_arr.def.h>\n"
        "#include <main/gate_arr.macros.s>\n"
        "#include <main/vdp.def.h>\n"
        "#include <main/io.def.h>\n"
        "\n"
        "/* The three parts of the file, in the order they are in it. */\n"
        ".equ ART_PALETTE_BYTES, %d\n"
        ".equ ART_TILE_BYTES,    %d\n"
        ".equ ART_MAP_BYTES,     %d\n"
        "\n"
        "/* Where they go. Both of these are the BIOS's own defaults, which is why\n"
        "   nothing here has to set them. */\n"
        ".equ VRAM_TILES,         0x%04X\n"
        ".equ VRAM_PLANE,         0x%04X\n"
        ".equ VRAM_HSCROLL_TABLE, 0x%04X\n"
        "\n"
        "/* The plane this scene wants: %dx%d cells, as VDP register 16 spells it.\n"
        "   The BIOS leaves it at 64x64; one word sets it either way. */\n"
        ".equ PLANE_SIZE,   0x%02X\n"
        "\n"
        "/* How far the window may travel. The plane wraps rather than ending, so\n"
        "   without this the scene would repeat along its own edge. */\n"
        ".equ CAMERA_MAX_X, %d\n"
        ".equ CAMERA_MAX_Y, %d\n"
        "\n"
        "/* The one thing the Sub CPU program knows how to do. */\n"
        ".equ CMD_LOAD_ART, 1\n"
        "\n"
        "/* An address as the VDP wants to be told it: fourteen bits in the first\n"
        "   word, the top two in the second, and the access bits around them. */\n"
        "#define VDP_ADDR(mode, addr) ((mode) | (((addr) & 0x3FFF) << 16) | (((addr) >> 14) & 3))\n"
        "\n"
        "  DISABLE_INTERRUPTS\n"
        "\n"
        "  /* The BIOS has a whole VDP setup ready: a 64x64 plane A at 0xC000, plane\n"
        "     B at 0xE000, the sprite and scroll tables under them, 40 cells across\n"
        "     and the display off. This changes the plane's size, and sends the two\n"
        "     nametables it is not using to the one it is: plane B pointed at plane\n"
        "     A means the same picture is behind itself, so nothing shows through\n"
        "     where the art is transparent, and the window -- which is switched off\n"
        "     but whose address is not -- stops sitting where the patterns go. */\n"
        "  jbsr     BIOS_LOAD_DEFAULT_VDP_REGS\n"
        "  jbsr     BIOS_CLEAR_VRAM\n"
        "  jbsr     BIOS_CLEAR_COMM\n"
        "\n"
        "  move.w   #(VDP_REG_PL_SIZE | PLANE_SIZE), (VDP_CTRL)\n"
        "  move.w   #(VDP_REG_PLB_ADDR | (VRAM_PLANE >> 13)), (VDP_CTRL)\n"
        "  move.w   #(VDP_REG_WIN_ADDR | ((VRAM_PLANE >> 10) & 0x3E)), (VDP_CTRL)\n"
        "\n"
        "  clr.w    (camera_x)\n"
        "  clr.w    (camera_y)\n"
        "\n"
        "  /* The Sub CPU is kept in step by the vertical blank interrupt, and the\n"
        "     drive will not deliver anything without it, so this has to be up\n"
        "     before the art is asked for. */\n"
        "  move     #0, (BIOS_VBLANK_HANDLER_FLAGS)\n"
        "  move.l   #BIOS_VBLANK_HANDLER, (EXVEC_LEVEL6)\n"
        "\n"
        "  ENABLE_INTERRUPTS\n"
        "\n"
        "  /* Hand Word RAM over, ask for the file, wait to be told it is there,\n"
        "     and take Word RAM back. Only one side of the machine can hold it at a\n"
        "     time, which is what all of this careful handing back and forth is. */\n"
        "  GRANT_2M\n"
        "  move.w   #CMD_LOAD_ART, GA_REG_COMCMD0\n"
        "0:tst.w    GA_REG_COMSTAT0\n"
        "  beq      0b\n"
        "  move.w   #0, GA_REG_COMCMD0\n"
        "1:tst.w    GA_REG_COMSTAT0\n"
        "  bne      1b\n"
        "  WAIT_2M\n"
        "\n"
        "  /* Word RAM is ours and the art is in it. */\n"
        "  movea.l  #WORD_RAM, a0\n"
        "\n"
        "  move.l   #VDP_ADDR(CRAM_W, 0), (VDP_CTRL)\n"
        "  move.w   #((ART_PALETTE_BYTES / 2) - 1), d0\n"
        "0:move.w   (a0)+, (VDP_DATA)\n"
        "  dbra     d0, 0b\n"
        "\n"
        "  move.l   #VDP_ADDR(VRAM_W, VRAM_TILES), (VDP_CTRL)\n"
        "  move.w   #((ART_TILE_BYTES / 2) - 1), d0\n"
        "0:move.w   (a0)+, (VDP_DATA)\n"
        "  dbra     d0, 0b\n"
        "\n"
        "  move.l   #VDP_ADDR(VRAM_W, VRAM_PLANE), (VDP_CTRL)\n"
        "  move.w   #((ART_MAP_BYTES / 2) - 1), d0\n"
        "0:move.w   (a0)+, (VDP_DATA)\n"
        "  dbra     d0, 0b\n"
        "\n"
        "  jbsr     BIOS_VDP_DISP_ENABLE\n"
        "\n"
        "scroll_loop:\n"
        "  /* The wait also reads the pads, and sends the Sub CPU its interrupt. */\n"
        "  jbsr     BIOS_VBLANK_WAIT_DEFAULT\n"
        "\n"
        "  move.b   (BIOS_JOY1_HOLD), d0\n"
        "  move.w   (camera_x), d1\n"
        "  move.w   (camera_y), d2\n"
        "\n"
        "  btst     #3, d0            /* right */\n"
        "  beq      0f\n"
        "  addq.w   #2, d1\n"
        "0:btst     #2, d0            /* left */\n"
        "  beq      1f\n"
        "  subq.w   #2, d1\n"
        "1:btst     #1, d0            /* down */\n"
        "  beq      2f\n"
        "  addq.w   #2, d2\n"
        "2:btst     #0, d0            /* up */\n"
        "  beq      3f\n"
        "  subq.w   #2, d2\n"
        "3:\n"
        "  tst.w    d1\n"
        "  bpl      4f\n"
        "  moveq    #0, d1\n"
        "4:cmpi.w   #CAMERA_MAX_X, d1\n"
        "  ble      5f\n"
        "  move.w   #CAMERA_MAX_X, d1\n"
        "5:tst.w    d2\n"
        "  bpl      6f\n"
        "  moveq    #0, d2\n"
        "6:cmpi.w   #CAMERA_MAX_Y, d2\n"
        "  ble      7f\n"
        "  move.w   #CAMERA_MAX_Y, d2\n"
        "7:\n"
        "  move.w   d1, (camera_x)\n"
        "  move.w   d2, (camera_y)\n"
        "\n"
        "  /* Horizontal scroll is how far the plane is pushed right, so a camera\n"
        "     moving right is a plane moving left. Vertical is the other way round\n"
        "     and needs no such thing. */\n"
        "  neg.w    d1\n"
        "\n"
        "  move.l   #VDP_ADDR(VRAM_W, VRAM_HSCROLL_TABLE), (VDP_CTRL)\n"
        "  move.w   d1, (VDP_DATA)\n"
        "  move.w   d1, (VDP_DATA)\n"
        "\n"
        "  move.l   #VDP_ADDR(VSRAM_W, 0), (VDP_CTRL)\n"
        "  move.w   d2, (VDP_DATA)\n"
        "  move.w   d2, (VDP_DATA)\n"
        "\n"
        "  bra      scroll_loop\n"
        "\n"
        ".section .bss\n"
        "\n"
        "camera_x: .space 2\n"
        "camera_y: .space 2\n",
        MD_PALETTE_COUNT * MD_PALETTE_SIZE * 2,
        (int)result->TileBytes,
        (int)result->MapBytes,
        MCD_VRAM_TILES, MCD_VRAM_PLANE, MCD_VRAM_HSCROLL,
        result->PlaneWidth, result->PlaneHeight,
        MegaCDExporter::PlaneSizeCode(result->PlaneWidth) |
            (MegaCDExporter::PlaneSizeCode(result->PlaneHeight) << 4),
        cameraMaxX, cameraMaxY);

    snprintf(path, sizeof(path), "%s/src/ip.s", outputPath);
    if (!MegaCDExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // The Sub CPU's half. It owns the drive, so the file has to be read here
    // and handed across, and this is the smallest thing that does that:
    // Megadev's CD-ROM access loop and a command table with one command in it.
    snprintf(text, sizeof(text),
        "/*\n"
        " * Generated by the Hatch Game Engine's Mega CD exporter.\n"
        " *\n"
        " * The Sub CPU's half. The drive is on this side of the machine and the VDP\n"
        " * is on the other, so all this does is read the file the Main CPU asks for\n"
        " * into Word RAM and hand Word RAM back.\n"
        " */\n"
        "\n"
        "#include <macros.s>\n"
        "#include <sub/bios.def.h>\n"
        "#include <sub/memmap.def.h>\n"
        "#include <sub/gate_arr.def.h>\n"
        "#include <sub/sub.macro.s>\n"
        "#include <sub/cdrom.def.h>\n"
        "#include <sub/cdrom.macro.s>\n"
        "\n"
        ".equ CMD_LOAD_ART, 1\n"
        "\n"
        ".section .text\n"
        "\n"
        "/* The CD-ROM access code itself, which becomes part of this program. */\n"
        "#include \"sub/cdrom.s\"\n"
        "\n"
        ".section .text\n"
        "\n"
        "/*\n"
        " * The access code runs as a loop that has to be turned over once per\n"
        " * interrupt. The Main CPU sends one every vertical blank.\n"
        " */\n"
        "GLABEL sp_int2\n"
        "  PROCESS_ACC_LOOP\n"
        "  rts\n"
        "\n"
        "GLABEL sp_init\n"
        "  /* Worth doing again even if the BIOS already did it: without it the\n"
        "     drive can misbehave later. */\n"
        "  lea      drv_init_tracklist, a0\n"
        "  BIOSCALL #BIOS_DRV_INIT\n"
        "0:BIOSCALL #BIOS_CDB_STAT\n"
        "  andi.b   #0xF0, (CDSTAT).w\n"
        "  bne      0b\n"
        "\n"
        "  CLEAR_COMM_REGS\n"
        "\n"
        "  /* Word RAM as one 256 KB piece rather than two banks, and this side\n"
        "     holding it to begin with. */\n"
        "  andi.w   #~(GA_MASK_RETURN_2M | GA_MASK_WORDRAM_LAYOUT), GA_REG_MEMMODE\n"
        "\n"
        "  INIT_ACC_LOOP\n"
        "  rts\n"
        "\n"
        "drv_init_tracklist:\n"
        "  .byte 1, 0xFF\n"
        "  .align 2\n"
        "\n"
        "GLABEL sp_main\n"
        "  /* Read the disc's table of contents once, so files can be asked for by\n"
        "     name afterwards rather than by where they sit on the disc. */\n"
        "  move.w   #CDROM_LOAD_FILE_LIST, access_op\n"
        "  WAIT_FOR_ACC_OP\n"
        "  cmpi.w   #CDROM_RESULT_OK, d0\n"
        "  bne      sp_fatal\n"
        "\n"
        "command_loop:\n"
        "  /* Read the command twice: the Main CPU may be writing it as we look. */\n"
        "  move.w   GA_REG_COMCMD0, d0\n"
        "  beq      command_loop\n"
        "  cmp.w    GA_REG_COMCMD0, d0\n"
        "  bne      command_loop\n"
        "\n"
        "  cmpi.w   #CMD_LOAD_ART, d0\n"
        "  bne      command_done\n"
        "\n"
        "  lea      art_filename, a0\n"
        "  WAIT_2M\n"
        "  lea      WORD_RAM_2M, a1\n"
        "  jbsr     load_file_sub\n"
        "  cmpi.w   #CDROM_RESULT_OK, d0\n"
        "  bne      sp_fatal\n"
        "  GRANT_2M\n"
        "\n"
        "command_done:\n"
        "  /* Answer, then wait for the question to be taken back, so that neither\n"
        "     side reads the other mid-change. */\n"
        "  move.w   GA_REG_COMCMD0, GA_REG_COMSTAT0\n"
        "0:move.w   GA_REG_COMCMD0, d0\n"
        "  bne      0b\n"
        "  move.w   GA_REG_COMCMD0, d0\n"
        "  bne      0b\n"
        "  move.w   #0, GA_REG_COMSTAT0\n"
        "  jra      command_loop\n"
        "\n"
        "GLABEL sp_user\n"
        "  rts\n"
        "\n"
        "/* Nothing sensible is left to do, so say so where it can be seen. */\n"
        "sp_fatal:\n"
        "  moveq    #BIOS_LED_ERROR, d1\n"
        "  BIOSCALL #BIOS_LEDSET\n"
        "0:nop\n"
        "  bra 0b\n"
        "\n"
        ".section .rodata\n"
        "\n"
        "/* ISO 9660 level 1, which is eight characters, three of extension, and a\n"
        "   version nobody ever looks at. */\n"
        "art_filename:\n"
        "  .asciz \"%s\"\n"
        "  .align 2\n",
        MCD_ART_ISO_NAME);

    snprintf(path, sizeof(path), "%s/src/sp.s", outputPath);
    if (!MegaCDExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // Megadev's own makefile does the work -- assembling both programs, laying
    // out the boot sector and building the image -- and this only tells it
    // what the project is.
    const char* makefileText =
        "# Built with Megadev: https://github.com/drojaazu/megadev\n"
        "#\n"
        "#   export MEGADEV_PATH=/path/to/megadev\n"
        "#   make\n"
        "#\n"
        "# The disc image comes out as hatch.iso. Megadev builds it with the m68k\n"
        "# cross compiler and mkisofs, both of which most distributions package.\n"
        "\n"
        "PROJECT_ID:=hatch\n"
        "TARGET:=MEGACD\n"
        "PROJECT_NAME:=\"HATCH SCENE\"\n"
        "HEADER_SOFTWARE_ID=\"HATCH EXPORT\"\n"
        "\n"
        "# Which security code goes in the boot sector, and what the display is\n"
        "# timed for. Both can be changed on the command line: make REGION=EU\n"
        "# VIDEO=PAL\n"
        "REGION?=US\n"
        "VIDEO?=NTSC\n"
        "\n"
        "SRC_PATH:=src\n"
        "RES_PATH:=res\n"
        "BUILD_PATH:=build\n"
        "DISC_PATH:=disc\n"
        "\n"
        "MEGADEV_PATH?=/opt/megadev\n"
        "\n"
        "include $(MEGADEV_PATH)/megadev.make\n"
        "\n"
        ".PHONY: all clean\n"
        "\n"
        "all: $(PROJECT_ID).iso\n"
        "\n"
        "clean:\n"
        "\t@rm -f $(PROJECT_ID).iso $(BUILD_PATH)/* > /dev/null 2>&1\n";

    snprintf(path, sizeof(path), "%s/makefile", outputPath);
    if (!MegaCDExporter::WriteText(path, makefileText)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // An image on its own is a track without a table of contents, and most
    // emulators will not mount one. The sheet is written now rather than by
    // the build because it never changes.
    const char* cueText =
        "FILE \"hatch.iso\" BINARY\n"
        "  TRACK 01 MODE1/2048\n"
        "    INDEX 01 00:00:00\n";

    snprintf(path, sizeof(path), "%s/hatch.cue", outputPath);
    if (!MegaCDExporter::WriteText(path, cueText)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    snprintf(text, sizeof(text),
        "# %s, for the Mega CD\n"
        "\n"
        "Exported from the Hatch Game Engine. The Mega CD shows its pictures with\n"
        "the Mega Drive's own VDP, so the art is in exactly the forms that chip\n"
        "reads -- what the disc changes is how it gets there.\n"
        "\n"
        "| File | What it holds |\n"
        "| --- | --- |\n"
        "| `disc/hatch.bin` | the art, %d bytes, in three parts one after another |\n"
        "| `src/ip.s` | the Main CPU's program: asks for the file, shows it, scrolls it |\n"
        "| `src/sp.s` | the Sub CPU's: reads the file off the disc into Word RAM |\n"
        "| `makefile` | Megadev's build, which lays out the boot sector and the image |\n"
        "| `hatch.cue` | the sheet an emulator wants beside the image |\n"
        "\n"
        "`disc/hatch.bin` is, in order:\n"
        "\n"
        "| Bytes | What |\n"
        "| --- | --- |\n"
        "| 0 to %d | four palettes of sixteen 9-bit colour words |\n"
        "| %d to %d | %d unique 8x8 patterns, four bits per pixel |\n"
        "| %d to %d | a %dx%d nametable: tile index, palette, and a flip bit per axis |\n"
        "\n"
        "## Building\n"
        "\n"
        "```sh\n"
        "export MEGADEV_PATH=/path/to/megadev\n"
        "make\n"
        "```\n"
        "\n"
        "`hatch.iso` is the result, and `hatch.cue` is already beside it: most\n"
        "emulators will not mount an image without a sheet saying what track it\n"
        "is.\n"
        "\n"
        "A Mega CD needs its own BIOS to boot anything, this included; that is the\n"
        "machine's, not the disc's, and emulators ask for it separately.\n"
        "\n"
        "## Why the art is a file and not part of the program\n"
        "\n"
        "The Mega CD's boot format gives the Initial Program 3.5 KB and the Sub\n"
        "Program 28 KB, because both live in the sixteen sectors at the start of\n"
        "the disc that the BIOS reads before anything else runs. This scene's art\n"
        "is %d bytes, and a scene with a full plane of its own patterns would be\n"
        "nearer sixty thousand. So the art travels as a file, which is the\n"
        "arrangement the machine was built for: the disc holds far more than the\n"
        "console can, and a program reads what it needs.\n"
        "\n"
        "Reading it takes both processors. The drive belongs to the Sub CPU and the\n"
        "VDP to the Main CPU, and the two share only Word RAM. So `ip.s` hands Word\n"
        "RAM over and asks; `sp.s` reads the file into it and hands it back; `ip.s`\n"
        "copies it into CRAM and VRAM.\n"
        "\n"
        "## What this is and is not\n"
        "\n"
        "`src/ip.s` shows the scene and scrolls it with the pad. It is a starting\n"
        "point, not a game: Hatch's own game logic is bytecode for a VM that does\n"
        "not exist on this machine, so none of it came across. What did come across\n"
        "is the art.\n"
        "\n"
        "The tiles were deduplicated across all four orientations, since a\n"
        "nametable entry carries a flip bit per axis: %d cells became %d tiles.\n"
        "\n"
        "Nothing here uses the disc for what the disc is really for. There is room\n"
        "on it for hundreds of scenes and an hour of music, and the machine has a\n"
        "second processor sitting idle between loads.\n",
        Scene::CurrentScene[0] ? Scene::CurrentScene : "Scene",
        (int)result->FileBytes,
        MD_PALETTE_COUNT * MD_PALETTE_SIZE * 2,
        MD_PALETTE_COUNT * MD_PALETTE_SIZE * 2,
        (int)(MD_PALETTE_COUNT * MD_PALETTE_SIZE * 2 + result->TileBytes),
        result->TileCount,
        (int)(MD_PALETTE_COUNT * MD_PALETTE_SIZE * 2 + result->TileBytes),
        (int)result->FileBytes,
        result->PlaneWidth, result->PlaneHeight,
        (int)result->FileBytes,
        result->TilesBeforeDedupe, result->TileCount);

    snprintf(path, sizeof(path), "%s/README.md", outputPath);
    if (!MegaCDExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return true;
}
