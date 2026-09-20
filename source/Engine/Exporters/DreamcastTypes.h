#ifndef ENGINE_EXPORTERS_DREAMCASTTYPES_H
#define ENGINE_EXPORTERS_DREAMCASTTYPES_H

/* The SEGA Dreamcast, which is a different kind of machine to the rest of the
 * SEGA targets and wants a different kind of export.
 *
 * The Mega Drive, Game Gear, 32X and Saturn all needed the art cut down to fit:
 * palettes of sixteen, five bits a channel, tiles deduplicated to fit a VRAM
 * measured in kilobytes. The Dreamcast has eight megabytes of video memory and
 * a PowerVR2 that draws textured, perspective-correct polygons, so a scene
 * layer goes over as sixteen-bit colour with nothing thrown away.
 *
 * The 3D side is the bigger difference. The Saturn's SH-2 had no floating point
 * and its VDP1 had no depth buffer, so geometry went over as fixed point and
 * had to be sorted back to front every frame. The Dreamcast's SH-4 has an FPU,
 * and the PVR is a deferred renderer that resolves opaque geometry per pixel,
 * so the same scene goes over as floats and needs no sorting at all.
 */

/* What pvr_init_defaults sets up. */
#define DREAMCAST_SCREEN_WIDTH   640
#define DREAMCAST_SCREEN_HEIGHT  480

/* The PVR wants power-of-two textures and will not take one larger than 1024 on
 * a side, so a layer is cut into squares of this. 256 keeps a tile at 128 KB,
 * which is a reasonable unit to spend video memory in. */
#define DREAMCAST_TILE_SIZE      256

/* Eight megabytes of video memory, and the framebuffers and the PVR's own
 * working space come out of it too. This leaves room for those. */
#define DREAMCAST_MAX_TEXTURE_BYTES (5 * 1024 * 1024)

/* The SH-4 transforms every vertex each frame, so this is its budget rather
 * than the PVR's -- the PVR would take far more. */
#define DREAMCAST_MAX_VERTICES   16384
#define DREAMCAST_MAX_FACES      8192

/* A vertex of an exported 3D scene. Floats, because this machine has an FPU --
 * already in world space, with each model's placement baked in. */
struct DreamcastVertex {
    float X, Y, Z;
};

/* A face, as up to four corners. The runtime sends a quad as a strip of four
 * and a triangle as a strip of three. */
struct DreamcastFace {
    Uint16 A, B, C, D;
    Uint32 Color;
    Uint16 Flags;
};

#define DREAMCAST_FACE_TRIANGLE  0x0001

struct DreamcastExportResult {
    bool   Success;
    bool   Is3D;

    int    ImageWidth;          /* what was written, in pixels */
    int    ImageHeight;
    int    LayerWidth;          /* what the layer wanted, same units */
    int    LayerHeight;

    int    TileColumns;
    int    TileRows;
    size_t TextureBytes;

    int    ModelCount;
    int    VertexCount;
    int    FaceCount;
    int    FacesDropped;

    char   Message[512];
};

#endif /* ENGINE_EXPORTERS_DREAMCASTTYPES_H */
