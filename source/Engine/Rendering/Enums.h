#ifndef ENGINE_RENDERING_ENUMS
#define ENGINE_RENDERING_ENUMS

enum {
    BlendMode_NORMAL = 0,
    BlendMode_ADD = 1,
    BlendMode_MAX = 2,
    BlendMode_SUBTRACT = 3,
    BlendMode_MATCH_EQUAL = 4,
    BlendMode_MATCH_NOT_EQUAL = 5,
};

enum {
    BlendFactor_ZERO = 0,
    BlendFactor_ONE = 1,
    BlendFactor_SRC_COLOR = 2,
    BlendFactor_INV_SRC_COLOR = 3,
    BlendFactor_SRC_ALPHA = 4,
    BlendFactor_INV_SRC_ALPHA = 5,
    BlendFactor_DST_COLOR = 6,
    BlendFactor_INV_DST_COLOR = 7,
    BlendFactor_DST_ALPHA = 8,
    BlendFactor_INV_DST_ALPHA = 9,
};

enum {
    TintMode_SRC_NORMAL,
    TintMode_DST_NORMAL,
    TintMode_SRC_BLEND,
    TintMode_DST_BLEND
};

enum {
    Filter_NONE,
    Filter_BLACK_AND_WHITE,
    Filter_INVERT
};

enum {
    DrawBehavior_HorizontalParallax = 0,
    DrawBehavior_VerticalParallax = 1,
    DrawBehavior_CustomTileScanLines = 2,
    DrawBehavior_PGZ1_BG = 3,
};

enum {
    DrawMode_FillTypeMask    = 7,
    DrawMode_LINES           = 0,
    DrawMode_POLYGONS        = 1<<0,
    DrawMode_FLAT_LIGHTING   = 1<<1,
    DrawMode_SMOOTH_LIGHTING = 1<<2,

    DrawMode_LINES_FLAT      = DrawMode_LINES | DrawMode_FLAT_LIGHTING,
    DrawMode_LINES_SMOOTH    = DrawMode_LINES | DrawMode_SMOOTH_LIGHTING,

    DrawMode_POLYGONS_FLAT   = DrawMode_POLYGONS | DrawMode_FLAT_LIGHTING,
    DrawMode_POLYGONS_SMOOTH = DrawMode_POLYGONS | DrawMode_SMOOTH_LIGHTING,

    DrawMode_TEXTURED        = 1<<3,
    DrawMode_AFFINE          = 1<<4,
    DrawMode_DEPTH_TEST      = 1<<5,
    DrawMode_FOG             = 1<<6,
    DrawMode_ORTHOGRAPHIC    = 1<<7
};

#define TILE_FLIPX_MASK 0x80000000U
#define TILE_FLIPY_MASK 0x40000000U
// #define TILE_DIAGO_MASK 0x20000000U
#define TILE_COLLA_MASK 0x30000000U
#define TILE_COLLB_MASK 0x0C000000U
#define TILE_COLLC_MASK 0x03000000U
#define TILE_IDENT_MASK 0x00FFFFFFU

struct TileScanLine {
    Sint64 SrcX;
    Sint64 SrcY;
    Sint64 DeltaX;
    Sint64 DeltaY;
    Uint8 Opacity;
    Uint32 MaxHorzCells;
    Uint32 MaxVertCells;
};
struct Viewport {
    float X;
    float Y;
    float Width;
    float Height;
};
struct ClipArea {
    bool Enabled;
    float X;
    float Y;
    float Width;
    float Height;
};
struct Point {
    float X;
    float Y;
    float Z;
};
struct GraphicsState {
    Viewport           CurrentViewport;
    ClipArea           CurrentClip;
    float              BlendColors[4];
    float              TintColors[4];
    int                BlendMode;
    int                TintMode;
    bool               TextureBlend;
    bool               UseTinting;
    bool               UsePalettes;
};
struct TintState {
    bool   Enabled;
    Uint32 Color;
    Uint16 Amount;
    Uint8  Mode;
};

#endif /* ENGINE_RENDERING_ENUMS */
