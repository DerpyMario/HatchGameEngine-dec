#ifndef ENGINE_EXPORTERS_SEGASATURNTYPES_H
#define ENGINE_EXPORTERS_SEGASATURNTYPES_H

/* The SEGA Saturn has two video chips and this exports through both of them.
 *
 * VDP2 draws backgrounds. A tile scene becomes one of its bitmaps: eight bits a
 * pixel, the colours in CRAM, which is why the art below is reduced to a
 * palette rather than kept as true colour.
 *
 * VDP1 draws sprites and polygons into a framebuffer VDP2 then composites. A 3D
 * scene becomes a table of vertices and faces the SH-2 turns into VDP1 polygon
 * commands each frame, which is what the Saturn's own games did.
 */

#define SATURN_SCREEN_WIDTH     352
#define SATURN_SCREEN_HEIGHT    224

/* Work RAM High is a megabyte and the picture has to sit in it alongside the
 * program, so this leaves room for both. */
#define SATURN_MAX_IMAGE_BYTES  (768 * 1024)

/* Index 0 is transparency on a VDP2 background, so the art gets 1..255. */
#define SATURN_PALETTE_USABLE   255

/* What the Saturn keeps of a colour: five bits a channel, red in the low bits.
 * Not a typo for RGB -- the hardware really does put blue at the top. */
#define SATURN_COLOR_WORD(r, g, b) \
    ((Uint16)(((((Uint32)(r)) >> 3) & 0x1F) | \
             (((((Uint32)(g)) >> 3) & 0x1F) << 5) | \
             (((((Uint32)(b)) >> 3) & 0x1F) << 10)))

/* How much of a 3D scene one export will carry. The ceilings are the SH-2's
 * rather than the VDP's: it has to transform every vertex and sort every face
 * inside a frame. */
#define SATURN_MAX_VERTICES     4096
#define SATURN_MAX_FACES        2000

/* GatherColors returns one of these per distinct colour found in the scene. */
struct SaturnColorUse {
    Uint32 Color;
    size_t Count;
};

/* A vertex of an exported 3D scene, in 16.16 fixed point because the SH-2 has
 * no floating point unit. Already in world space: the object's own position,
 * rotation and scale are baked in here rather than left for the console. */
struct SaturnVertex {
    Sint32 X, Y, Z;
};

/* A face, as four corners. VDP1 draws quads, so a triangle is a quad whose last
 * two corners are the same point -- which is how the Saturn always did them. */
struct SaturnFace {
    Uint16 A, B, C, D;
    Uint16 Color;
    Uint16 Flags;
};

#define SATURN_FACE_TRIANGLE    0x0001

/* Result structure returned by the Saturn exporter. */
struct SegaSaturnExportResult {
    bool   Success;
    bool   Is3D;

    int    ImageWidth;         /* what was written, in pixels */
    int    ImageHeight;
    int    LayerWidth;         /* what the layer wanted, same units */
    int    LayerHeight;

    int    PaletteCount;       /* colours actually used, 1..255 */
    int    ColorsFound;
    int    ColorsDropped;      /* reduced away to fit, 0 when art already fit */

    size_t ImageBytes;

    int    ModelCount;         /* 3D: models placed in the scene */
    int    VertexCount;
    int    FaceCount;
    int    FacesDropped;       /* over the ceiling, so not written */

    char   Message[512];
};

#endif /* ENGINE_EXPORTERS_SEGASATURNTYPES_H */
