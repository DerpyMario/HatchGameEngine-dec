#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Rendering/3D.h>
#include <Engine/Rendering/Texture.h>
#include <Engine/Rendering/Material.h>

class PolygonRasterizer {
public:
    static bool    DepthTest;
    static size_t  DepthBufferSize;
    static Uint32* DepthBuffer;

    static bool    UseDepthBuffer;

    static bool    UseFog;
    static float   FogDensity;
    static int     FogColor;
};
#endif

#include <Engine/Rendering/Software/SoftwareRenderer.h>
#include <Engine/Rendering/Software/PolygonRasterizer.h>
#include <Engine/Rendering/Software/SoftwareEnums.h>
#include <Engine/Rendering/Software/Scanline.h>
#include <Engine/Rendering/Software/Contour.h>
#include <Engine/Utilities/ColorUtils.h>

bool    PolygonRasterizer::DepthTest = false;
size_t  PolygonRasterizer::DepthBufferSize = 0;
Uint32* PolygonRasterizer::DepthBuffer = NULL;

bool    PolygonRasterizer::UseDepthBuffer = false;

bool    PolygonRasterizer::UseFog = false;
float   PolygonRasterizer::FogDensity = 1.0f;
int     PolygonRasterizer::FogColor = 0x000000;

#define DEPTH_READ_DUMMY(depth) true
#define DEPTH_WRITE_DUMMY(depth)

#define DEPTH_READ_U32(depth)  (depth < PolygonRasterizer::DepthBuffer[dst_x + dst_strideY])
#define DEPTH_WRITE_U32(depth) PolygonRasterizer::DepthBuffer[dst_x + dst_strideY] = depth

#define CLAMP_VAL(v, a, b) if (v < a) v = a; else if (v > b) v = b

int FogEquation(float density, float coord) {
    int result = expf(-density * coord) * 0x100;
    CLAMP_VAL(result, 0, 0x100);
    return 0x100 - result;
}
int DoFogLighting(int color, float fogCoord) {
    int fogValue = FogEquation(PolygonRasterizer::FogDensity, fogCoord / 192.0f);
    if (fogValue != 0)
        return ColorUtils::Blend(color, PolygonRasterizer::FogColor, fogValue);
    return color | 0xFF000000U;
}
Uint32 DoColorTint(Uint32 color, Uint32 colorMult) {
    return ColorUtils::Tint(color, colorMult) | 0xFF000000U;
}

// Cases for PixelNoFiltSet
#define PIXEL_NO_FILT_CASES(drawPolygonMacro, pixelMacro) \
    case BlendFlag_OPAQUE: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetOpaque, pixelMacro); \
        break; \
    case BlendFlag_TRANSPARENT: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetTransparent, pixelMacro); \
        break; \
    case BlendFlag_ADDITIVE: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetAdditive, pixelMacro); \
        break; \
    case BlendFlag_SUBTRACT: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetSubtract, pixelMacro); \
        break; \
    case BlendFlag_MATCH_EQUAL: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetMatchEqual, pixelMacro); \
        break; \
    case BlendFlag_MATCH_NOT_EQUAL: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetMatchNotEqual, pixelMacro); \
        break \

// Cases for PixelTintSet
#define PIXEL_TINT_CASES(drawPolygonMacro, pixelMacro) \
    case BlendFlag_OPAQUE | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetOpaque, pixelMacro); \
        break; \
    case BlendFlag_TRANSPARENT | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetTransparent, pixelMacro); \
        break; \
    case BlendFlag_ADDITIVE | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetAdditive, pixelMacro); \
        break; \
    case BlendFlag_SUBTRACT | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetSubtract, pixelMacro); \
        break; \
    case BlendFlag_MATCH_EQUAL | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetMatchEqual, pixelMacro); \
        break; \
    case BlendFlag_MATCH_NOT_EQUAL | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetMatchNotEqual, pixelMacro); \
        break \

// Cases for PixelNoFiltSet (solid color)
#define PIXEL_NO_FILT_CASES_SOLID(drawPolygonMacro) \
    case BlendFlag_OPAQUE: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetOpaque); \
        break; \
    case BlendFlag_TRANSPARENT: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetTransparent); \
        break; \
    case BlendFlag_ADDITIVE: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetAdditive); \
        break; \
    case BlendFlag_SUBTRACT: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetSubtract); \
        break; \
    case BlendFlag_MATCH_EQUAL: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetMatchEqual); \
        break; \
    case BlendFlag_MATCH_NOT_EQUAL: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetMatchNotEqual); \
        break \

// Cases for PixelNoFiltSet (solid color, without BlendFlag_OPAQUE)
#define PIXEL_NO_FILT_CASES_SOLID_NO_OPAQUE(drawPolygonMacro) \
    case BlendFlag_TRANSPARENT: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetTransparent); \
        break; \
    case BlendFlag_ADDITIVE: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetAdditive); \
        break; \
    case BlendFlag_SUBTRACT: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetSubtract); \
        break; \
    case BlendFlag_MATCH_EQUAL: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetMatchEqual); \
        break; \
    case BlendFlag_MATCH_NOT_EQUAL: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetMatchNotEqual); \
        break \

// Cases for PixelTintSet (solid color)
#define PIXEL_TINT_CASES_SOLID(drawPolygonMacro) \
    case BlendFlag_OPAQUE | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetOpaque); \
        break; \
    case BlendFlag_TRANSPARENT | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetTransparent); \
        break; \
    case BlendFlag_ADDITIVE | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetAdditive); \
        break; \
    case BlendFlag_SUBTRACT | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetSubtract); \
        break; \
    case BlendFlag_MATCH_EQUAL | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetMatchEqual); \
        break; \
    case BlendFlag_MATCH_NOT_EQUAL | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetMatchNotEqual); \
        break \

// Cases for PixelNoFiltSet (with depth, solid color)
#define PIXEL_NO_FILT_CASES_SOLID_DEPTH(drawPolygonMacro, dpR, dpW) \
    case BlendFlag_OPAQUE: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetOpaque, dpR, dpW); \
        break; \
    case BlendFlag_TRANSPARENT: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetTransparent, dpR, dpW); \
        break; \
    case BlendFlag_ADDITIVE: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetAdditive, dpR, dpW); \
        break; \
    case BlendFlag_SUBTRACT: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetSubtract, dpR, dpW); \
        break; \
    case BlendFlag_MATCH_EQUAL: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetMatchEqual, dpR, dpW); \
        break; \
    case BlendFlag_MATCH_NOT_EQUAL: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetMatchNotEqual, dpR, dpW); \
        break \

// Cases for PixelTintSet (with depth, solid color)
#define PIXEL_TINT_CASES_SOLID_DEPTH(drawPolygonMacro, dpR, dpW) \
    case BlendFlag_OPAQUE | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetOpaque, dpR, dpW); \
        break; \
    case BlendFlag_TRANSPARENT | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetTransparent, dpR, dpW); \
        break; \
    case BlendFlag_ADDITIVE | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetAdditive, dpR, dpW); \
        break; \
    case BlendFlag_SUBTRACT | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetSubtract, dpR, dpW); \
        break; \
    case BlendFlag_MATCH_EQUAL | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetMatchEqual, dpR, dpW); \
        break; \
    case BlendFlag_MATCH_NOT_EQUAL | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetMatchNotEqual, dpR, dpW); \
        break \

// Cases for PixelNoFiltSet (with depth)
#define PIXEL_NO_FILT_CASES_DEPTH(drawPolygonMacro, placePixelMacro, dpR, dpW) \
    case BlendFlag_OPAQUE: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetOpaque, placePixelMacro, dpR, dpW); \
        break; \
    case BlendFlag_TRANSPARENT: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetTransparent, placePixelMacro, dpR, dpW); \
        break; \
    case BlendFlag_ADDITIVE: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetAdditive, placePixelMacro, dpR, dpW); \
        break; \
    case BlendFlag_SUBTRACT: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetSubtract, placePixelMacro, dpR, dpW); \
        break; \
    case BlendFlag_MATCH_EQUAL: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetMatchEqual, placePixelMacro, dpR, dpW); \
        break; \
    case BlendFlag_MATCH_NOT_EQUAL: \
        drawPolygonMacro(SoftwareRenderer::PixelNoFiltSetMatchNotEqual, placePixelMacro, dpR, dpW); \
        break \

// Cases for PixelTintSet (with depth)
#define PIXEL_TINT_CASES_DEPTH(drawPolygonMacro, placePixelMacro, dpR, dpW) \
    case BlendFlag_OPAQUE | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetOpaque, placePixelMacro, dpR, dpW); \
        break; \
    case BlendFlag_TRANSPARENT | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetTransparent, placePixelMacro, dpR, dpW); \
        break; \
    case BlendFlag_ADDITIVE | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetAdditive, placePixelMacro, dpR, dpW); \
        break; \
    case BlendFlag_SUBTRACT | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetSubtract, placePixelMacro, dpR, dpW); \
        break; \
    case BlendFlag_MATCH_EQUAL | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetMatchEqual, placePixelMacro, dpR, dpW); \
        break; \
    case BlendFlag_MATCH_NOT_EQUAL | BlendFlag_TINT_BIT: \
        drawPolygonMacro(SoftwareRenderer::PixelTintSetMatchNotEqual, placePixelMacro, dpR, dpW); \
        break \

#define POLYGON_BLENDFLAGS_SOLID(drawPolygonMacro, pixelMacro) \
    switch (blendFlag & (BlendFlag_MODE_MASK | BlendFlag_TINT_BIT)) { \
        PIXEL_NO_FILT_CASES(drawPolygonMacro, pixelMacro); \
        PIXEL_TINT_CASES(drawPolygonMacro, pixelMacro); \
    }

#define POLYGON_BLENDFLAGS_SOLID_DEPTH(drawPolygonMacro, dpR, dpW) \
    switch (blendFlag & (BlendFlag_MODE_MASK | BlendFlag_TINT_BIT)) { \
        PIXEL_NO_FILT_CASES_SOLID_DEPTH(drawPolygonMacro, dpR, dpW); \
        PIXEL_TINT_CASES_SOLID_DEPTH(drawPolygonMacro, dpR, dpW); \
    }

#define POLYGON_BLENDFLAGS_DEPTH(drawPolygonMacro, placePixelMacro, dpR, dpW) \
    switch (blendFlag & (BlendFlag_MODE_MASK | BlendFlag_TINT_BIT)) { \
        PIXEL_NO_FILT_CASES_DEPTH(drawPolygonMacro, placePixelMacro, dpR, dpW); \
        PIXEL_TINT_CASES_DEPTH(drawPolygonMacro, placePixelMacro, dpR, dpW); \
    }

#define DRAW_POLYGON_SCANLINE_DEPTH(drawPolygonMacro, placePixel, placePixelPaletted) \
    if (Graphics::UsePalettes && texture->Paletted) { \
        if (UseDepthBuffer) { \
            POLYGON_BLENDFLAGS_DEPTH(drawPolygonMacro, placePixelPaletted, DEPTH_READ_U32, DEPTH_WRITE_U32); \
        } \
        else { \
            POLYGON_BLENDFLAGS_DEPTH(drawPolygonMacro, placePixelPaletted, DEPTH_READ_DUMMY, DEPTH_WRITE_DUMMY); \
        } \
    } else { \
        if (UseDepthBuffer) { \
            POLYGON_BLENDFLAGS_DEPTH(drawPolygonMacro, placePixel, DEPTH_READ_U32, DEPTH_WRITE_U32); \
        } else {  \
            POLYGON_BLENDFLAGS_DEPTH(drawPolygonMacro, placePixel, DEPTH_READ_DUMMY, DEPTH_WRITE_DUMMY); \
        } \
    }

#define POLYGON_SCANLINE_DEPTH(drawPolygonMacro) \
    if (UseFog) { \
        DRAW_POLYGON_SCANLINE_DEPTH(drawPolygonMacro, DRAW_PLACEPIXEL_FOG, DRAW_PLACEPIXEL_PAL_FOG) \
    } \
    else { \
        DRAW_POLYGON_SCANLINE_DEPTH(drawPolygonMacro, DRAW_PLACEPIXEL, DRAW_PLACEPIXEL_PAL) \
    }

#define SCANLINE_INIT_Z() \
    contZ = contour.MinZ; \
    dxZ = (contour.MaxZ - contZ) / contLen
#define SCANLINE_INIT_RGB() \
    contR = contour.MinR; \
    contG = contour.MinG; \
    contB = contour.MinB; \
    dxR = (contour.MaxR - contR) / contLen; \
    dxG = (contour.MaxG - contG) / contLen; \
    dxB = (contour.MaxB - contB) / contLen
#define SCANLINE_INIT_UV() \
    contU = contour.MinU; \
    contV = contour.MinV; \
    dxU = (contour.MaxU - contU) / contLen; \
    dxV = (contour.MaxV - contV) / contLen

#define SCANLINE_STEP_Z() contZ += dxZ
#define SCANLINE_STEP_RGB() \
    contR += dxR; \
    contG += dxG; \
    contB += dxB
#define SCANLINE_STEP_UV() \
    contU += dxU; \
    contV += dxV

#define SCANLINE_STEP_Z_BY(diff) contZ += dxZ * diff
#define SCANLINE_STEP_RGB_BY(diff) \
    contR += dxR * diff; \
    contG += dxG * diff; \
    contB += dxB * diff
#define SCANLINE_STEP_UV_BY(diff) \
    contU += dxU * diff; \
    contV += dxV * diff

#define SCANLINE_GET_MAPZ() mapZ = 1.0f / contZ
#define SCANLINE_GET_INVZ() \
    Uint32 iz = mapZ * 65536; \
    (void)iz /* Make compiler shut up */
#define SCANLINE_GET_RGB() \
    Sint32 colR = contR; \
    Sint32 colG = contG; \
    Sint32 colB = contB
#define SCANLINE_GET_RGB_PERSP() \
    Sint32 colR = contR * mapZ; \
    Sint32 colG = contG * mapZ; \
    Sint32 colB = contB * mapZ
#define SCANLINE_GET_MAPUV() \
    float mapU = contU; \
    float mapV = contV
#define SCANLINE_GET_TEXUV() \
    int texU = ((int)((mapU + 1) * texture->Width) >> 16) % texture->Width; \
    int texV = ((int)((mapV + 1) * texture->Height) >> 16) % texture->Height
#define SCANLINE_GET_COLOR() \
    CLAMP_VAL(colR, 0x00, 0xFF0000); \
    CLAMP_VAL(colG, 0x00, 0xFF0000); \
    CLAMP_VAL(colB, 0x00, 0xFF0000); \
    col = 0xFF000000U | ((colR) & 0xFF0000) | ((colG >> 8) & 0xFF00) | ((colB >> 16) & 0xFF)

#define SCANLINE_WRITE_PIXEL(pixelFunction, px) \
    pixelFunction((Uint32*)&px, &dstPx[dst_x + dst_strideY], blendState, multTableAt, multSubTableAt)

Contour *ContourField = SoftwareRenderer::ContourBuffer;

template <typename T>
static void GetPolygonBounds(T* positions, int count, int& minVal, int& maxVal) {
    minVal = INT_MAX;
    maxVal = INT_MIN;

    while (count--) {
        int tempY = positions->Y >> 16;
        if (minVal > tempY)
            minVal = tempY;
        if (maxVal < tempY)
            maxVal = tempY;
        positions++;
    }
}

#define CLIP_BOUNDS() \
    if (Graphics::CurrentClip.Enabled) { \
        if (dst_y2 > Graphics::CurrentClip.Y + Graphics::CurrentClip.Height) \
            dst_y2 = Graphics::CurrentClip.Y + Graphics::CurrentClip.Height; \
        if (dst_y1 < Graphics::CurrentClip.Y) \
            dst_y1 = Graphics::CurrentClip.Y; \
    } \
    if (dst_y2 > (int)Graphics::CurrentRenderTarget->Height) \
        dst_y2 = (int)Graphics::CurrentRenderTarget->Height; \
    if (dst_y1 < 0) \
        dst_y1 = 0; \
    if (dst_y2 < 0 || dst_y1 >= dst_y2) \
        return

// Draws a polygon
PUBLIC STATIC void PolygonRasterizer::DrawBasic(Vector2* positions, Uint32 color, int count, BlendState blendState) {
    Uint32* dstPx = (Uint32*)Graphics::CurrentRenderTarget->Pixels;
    Uint32  dstStride = Graphics::CurrentRenderTarget->Width;

    int dst_y1, dst_y2;

    if (count == 0)
        return;

    int blendFlag = blendState.Mode;
    int opacity = blendState.Opacity;
    if (opacity == 0 && blendFlag == BlendFlag_TRANSPARENT)
        return;

    GetPolygonBounds<Vector2>(positions, count, dst_y1, dst_y2);
    CLIP_BOUNDS();

    Scanline::Prepare(dst_y1, dst_y2);

    Vector2* lastVector = positions;
    if (count > 1) {
        int countRem = count - 1;
        while (countRem--) {
            Scanline::Process(lastVector[0].X, lastVector[0].Y, lastVector[1].X, lastVector[1].Y);
            lastVector++;
        }
    }
    Scanline::Process(lastVector[0].X, lastVector[0].Y, positions[0].X, positions[0].Y);

    #define DRAW_POLYGON(pixelFunction) for (int dst_y = dst_y1; dst_y < dst_y2; dst_y++) { \
        Contour contour = ContourField[dst_y]; \
        if (contour.MaxX < contour.MinX) { \
            dst_strideY += dstStride; \
            continue; \
        } \
        for (int dst_x = contour.MinX; dst_x < contour.MaxX; dst_x++) { \
            SCANLINE_WRITE_PIXEL(pixelFunction, color); \
        } \
        dst_strideY += dstStride; \
    }

    if (blendFlag & (BlendFlag_TINT_BIT | BlendFlag_FILTER_BIT))
        SoftwareRenderer::SetTintFunction(blendFlag);

    int* multTableAt = &SoftwareRenderer::MultTable[opacity << 8];
    int* multSubTableAt = &SoftwareRenderer::MultSubTable[opacity << 8];
    int dst_strideY = dst_y1 * dstStride;
    switch (blendFlag & (BlendFlag_MODE_MASK | BlendFlag_TINT_BIT)) {
        case BlendFlag_OPAQUE:
            for (int dst_y = dst_y1; dst_y < dst_y2; dst_y++) {
                Contour contour = ContourField[dst_y];
                if (contour.MaxX < contour.MinX) {
                    dst_strideY += dstStride;
                    continue;
                }

                Memory::Memset4(&dstPx[contour.MinX + dst_strideY], color, contour.MaxX - contour.MinX);
                dst_strideY += dstStride;
            }
            break;
        PIXEL_NO_FILT_CASES_SOLID_NO_OPAQUE(DRAW_POLYGON);
        PIXEL_TINT_CASES_SOLID(DRAW_POLYGON);
    }

    #undef DRAW_POLYGON
}
// Draws a blended polygon
PUBLIC STATIC void PolygonRasterizer::DrawBasicBlend(Vector2* positions, int* colors, int count, BlendState blendState) {
    Uint32* dstPx = (Uint32*)Graphics::CurrentRenderTarget->Pixels;
    Uint32  dstStride = Graphics::CurrentRenderTarget->Width;

    int dst_y1, dst_y2;

    if (count == 0)
        return;

    int blendFlag = blendState.Mode;
    int opacity = blendState.Opacity;
    if (opacity == 0 && blendFlag == BlendFlag_TRANSPARENT)
        return;

    GetPolygonBounds<Vector2>(positions, count, dst_y1, dst_y2);
    CLIP_BOUNDS();

    Scanline::Prepare(dst_y1, dst_y2);

    int* lastColor = colors;
    Vector2* lastVector = positions;
    if (count > 1) {
        int countRem = count - 1;
        while (countRem--) {
            Scanline::Process(lastColor[0], lastColor[1], lastVector[0].X, lastVector[0].Y, lastVector[1].X, lastVector[1].Y);
            lastVector++;
            lastColor++;
        }
    }

    Scanline::Process(lastColor[0], colors[0], lastVector[0].X, lastVector[0].Y, positions[0].X, positions[0].Y);

    Sint32 col, contLen;
    float contR, contG, contB, dxR, dxG, dxB;

    #define DRAW_POLYGONBLEND(pixelFunction) for (int dst_y = dst_y1; dst_y < dst_y2; dst_y++) { \
        Contour contour = ContourField[dst_y]; \
        contLen = contour.MaxX - contour.MinX; \
        if (contLen <= 0) { \
            dst_strideY += dstStride; \
            continue; \
        } \
        SCANLINE_INIT_RGB(); \
        if (contour.MapLeft < contour.MinX) { \
            int diff = contour.MinX - contour.MapLeft; \
            SCANLINE_STEP_RGB_BY(diff); \
        } \
        for (int dst_x = contour.MinX; dst_x < contour.MaxX; dst_x++) { \
            SCANLINE_GET_RGB(); \
            SCANLINE_GET_COLOR(); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            SCANLINE_STEP_RGB(); \
        } \
        dst_strideY += dstStride; \
    }

    if (blendFlag & (BlendFlag_TINT_BIT | BlendFlag_FILTER_BIT))
        SoftwareRenderer::SetTintFunction(blendFlag);

    int* multTableAt = &SoftwareRenderer::MultTable[opacity << 8];
    int* multSubTableAt = &SoftwareRenderer::MultSubTable[opacity << 8];
    int dst_strideY = dst_y1 * dstStride;
    switch (blendFlag & (BlendFlag_MODE_MASK | BlendFlag_TINT_BIT)) {
        PIXEL_NO_FILT_CASES_SOLID(DRAW_POLYGONBLEND);
        PIXEL_TINT_CASES_SOLID(DRAW_POLYGONBLEND);
    }

    #undef DRAW_POLYGONBLEND
}
// Draws a polygon with lighting
PUBLIC STATIC void PolygonRasterizer::DrawShaded(Vector3* positions, Uint32 color, int count, BlendState blendState) {
    Uint32* dstPx = (Uint32*)Graphics::CurrentRenderTarget->Pixels;
    Uint32  dstStride = Graphics::CurrentRenderTarget->Width;

    int dst_y1, dst_y2;

    if (count == 0)
        return;

    int blendFlag = blendState.Mode;
    int opacity = blendState.Opacity;
    if (opacity == 0 && blendFlag == BlendFlag_TRANSPARENT)
        return;

    GetPolygonBounds<Vector3>(positions, count, dst_y1, dst_y2);
    CLIP_BOUNDS();

    Scanline::Prepare(dst_y1, dst_y2);

    Vector3* lastVector = positions;
    if (count > 1) {
        int countRem = count - 1;
        while (countRem--) {
            Scanline::ProcessDepth(lastVector[0].X, lastVector[0].Y, lastVector[0].Z, lastVector[1].X, lastVector[1].Y, lastVector[1].Z);
            lastVector++;
        }
    }

    Scanline::ProcessDepth(lastVector[0].X, lastVector[0].Y, lastVector[0].Z, positions[0].X, positions[0].Y, positions[0].Z);

    Sint32 col, contLen;
    float contZ, dxZ;
    float mapZ;

    color |= 0xFF000000U;

    #define PX_GET(pixelFunction) \
        SCANLINE_WRITE_PIXEL(pixelFunction, color)
    #define PX_GET_FOG(pixelFunction) \
        col = DoFogLighting(color, mapZ); \
        SCANLINE_WRITE_PIXEL(pixelFunction, col)

    #define DRAW_POLYGONSHADED(pixelFunction, pixelRead) for (int dst_y = dst_y1; dst_y < dst_y2; dst_y++) { \
        Contour contour = ContourField[dst_y]; \
        contLen = contour.MaxX - contour.MinX; \
        if (contLen <= 0) { \
            dst_strideY += dstStride; \
            continue; \
        } \
        for (int dst_x = contour.MinX; dst_x < contour.MaxX; dst_x++) { \
            pixelRead(pixelFunction); \
        } \
        dst_strideY += dstStride; \
    }
    #define DRAW_POLYGONSHADED_FOG(pixelFunction, pixelRead) for (int dst_y = dst_y1; dst_y < dst_y2; dst_y++) { \
        Contour contour = ContourField[dst_y]; \
        contLen = contour.MaxX - contour.MinX; \
        if (contLen <= 0) { \
            dst_strideY += dstStride; \
            continue; \
        } \
        SCANLINE_INIT_Z(); \
        if (contour.MapLeft < contour.MinX) { \
            SCANLINE_STEP_Z_BY(contour.MinX - contour.MapLeft); \
        } \
        for (int dst_x = contour.MinX; dst_x < contour.MaxX; dst_x++) { \
            SCANLINE_GET_MAPZ(); \
            pixelRead(pixelFunction); \
            SCANLINE_STEP_Z(); \
        } \
        dst_strideY += dstStride; \
    }

    if (blendFlag & (BlendFlag_TINT_BIT | BlendFlag_FILTER_BIT))
        SoftwareRenderer::SetTintFunction(blendFlag);

    int* multTableAt = &SoftwareRenderer::MultTable[opacity << 8];
    int* multSubTableAt = &SoftwareRenderer::MultSubTable[opacity << 8];
    int dst_strideY = dst_y1 * dstStride;

    if (UseFog) {
        POLYGON_BLENDFLAGS_SOLID(DRAW_POLYGONSHADED_FOG, PX_GET_FOG);
    } else {
        POLYGON_BLENDFLAGS_SOLID(DRAW_POLYGONSHADED, PX_GET);
    }

    #undef PX_GET
    #undef PX_GET_FOG

    #undef DRAW_POLYGONSHADED
    #undef DRAW_POLYGONSHADED_FOG
}
// Draws a blended polygon with lighting
PUBLIC STATIC void PolygonRasterizer::DrawBlendShaded(Vector3* positions, int* colors, int count, BlendState blendState) {
    Uint32* dstPx = (Uint32*)Graphics::CurrentRenderTarget->Pixels;
    Uint32  dstStride = Graphics::CurrentRenderTarget->Width;

    int dst_y1, dst_y2;

    if (count == 0)
        return;

    int blendFlag = blendState.Mode;
    int opacity = blendState.Opacity;
    if (opacity == 0 && blendFlag == BlendFlag_TRANSPARENT)
        return;

    GetPolygonBounds<Vector3>(positions, count, dst_y1, dst_y2);
    CLIP_BOUNDS();

    Scanline::Prepare(dst_y1, dst_y2);

    int* lastColor = colors;
    Vector3* lastVector = positions;
    if (count > 1) {
        int countRem = count - 1;
        while (countRem--) {
            Scanline::ProcessDepth(lastColor[0], lastColor[1], lastVector[0].X, lastVector[0].Y, lastVector[0].Z, lastVector[1].X, lastVector[1].Y, lastVector[1].Z);
            lastVector++;
            lastColor++;
        }
    }

    Scanline::ProcessDepth(lastColor[0], colors[0], lastVector[0].X, lastVector[0].Y, lastVector[0].Z, positions[0].X, positions[0].Y, positions[0].Z);

    Sint32 col, contLen;
    float contZ, contR, contG, contB;
    float dxZ, dxR, dxG, dxB;
    float mapZ;

    #define PX_GET(pixelFunction) \
        SCANLINE_GET_COLOR(); \
        SCANLINE_WRITE_PIXEL(pixelFunction, col)
    #define PX_GET_FOG(pixelFunction) \
        SCANLINE_GET_COLOR(); \
        col = DoFogLighting(col, mapZ); \
        SCANLINE_WRITE_PIXEL(pixelFunction, col)

    #define DRAW_POLYGONBLENDSHADED(pixelFunction, pixelRead) for (int dst_y = dst_y1; dst_y < dst_y2; dst_y++) { \
        Contour contour = ContourField[dst_y]; \
        contLen = contour.MaxX - contour.MinX; \
        if (contLen <= 0) { \
            dst_strideY += dstStride; \
            continue; \
        } \
        SCANLINE_INIT_Z(); \
        SCANLINE_INIT_RGB(); \
        if (contour.MapLeft < contour.MinX) { \
            int diff = contour.MinX - contour.MapLeft; \
            SCANLINE_STEP_Z_BY(diff); \
            SCANLINE_STEP_RGB_BY(diff); \
        } \
        for (int dst_x = contour.MinX; dst_x < contour.MaxX; dst_x++) { \
            SCANLINE_GET_MAPZ(); \
            SCANLINE_GET_RGB(); \
            pixelRead(pixelFunction); \
            SCANLINE_STEP_Z(); \
            SCANLINE_STEP_RGB(); \
        } \
        dst_strideY += dstStride; \
    }

    if (blendFlag & (BlendFlag_TINT_BIT | BlendFlag_FILTER_BIT))
        SoftwareRenderer::SetTintFunction(blendFlag);

    int* multTableAt = &SoftwareRenderer::MultTable[opacity << 8];
    int* multSubTableAt = &SoftwareRenderer::MultSubTable[opacity << 8];
    int dst_strideY = dst_y1 * dstStride;

    if (UseFog) {
        POLYGON_BLENDFLAGS_SOLID(DRAW_POLYGONBLENDSHADED, PX_GET_FOG);
    } else {
        POLYGON_BLENDFLAGS_SOLID(DRAW_POLYGONBLENDSHADED, PX_GET);
    }

    #undef PX_GET
    #undef PX_GET_FOG

    #undef DRAW_POLYGONBLENDSHADED
}
// Draws an affine texture mapped polygon
PUBLIC STATIC void PolygonRasterizer::DrawAffine(Texture* texture, Vector3* positions, Vector2* uvs, Uint32 color, int count, BlendState blendState) {
    Uint32* dstPx = (Uint32*)Graphics::CurrentRenderTarget->Pixels;
    Uint32  dstStride = Graphics::CurrentRenderTarget->Width;
    Uint32* srcPx = (Uint32*)texture->Pixels;
    Uint32  srcStride = texture->Width;

    int dst_y1, dst_y2;

    if (count == 0)
        return;

    int blendFlag = blendState.Mode;
    int opacity = blendState.Opacity;
    if (opacity == 0 && blendFlag == BlendFlag_TRANSPARENT)
        return;

    GetPolygonBounds<Vector3>(positions, count, dst_y1, dst_y2);
    CLIP_BOUNDS();

    Scanline::Prepare(dst_y1, dst_y2);

    Vector3* lastVector = positions;
    Vector2* lastUV = uvs;
    if (count > 1) {
        int countRem = count - 1;
        while (countRem--) {
            Scanline::ProcessUVAffine(lastUV[0], lastUV[1], lastVector[0].X, lastVector[0].Y, lastVector[0].Z, lastVector[1].X, lastVector[1].Y, lastVector[1].Z);
            lastVector++;
            lastUV++;
        }
    }

    Scanline::ProcessUVAffine(lastUV[0], uvs[0], lastVector[0].X, lastVector[0].Y, lastVector[0].Z, positions[0].X, positions[0].Y, positions[0].Z);

    Sint32 col, texCol, contLen;
    float contZ, contU, contV;
    float dxZ, dxU, dxV;
    float mapZ;

    int min_x, max_x;
    if (Graphics::CurrentClip.Enabled) {
        min_x = Graphics::CurrentClip.X;
        max_x = Graphics::CurrentClip.X + Graphics::CurrentClip.Width;
    }
    else {
        min_x = 0;
        max_x = (int)Graphics::CurrentRenderTarget->Width;
    }

    #define DRAW_PLACEPIXEL(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU]) & 0xFF000000U) { \
            col = DoColorTint(color, texCol); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        }

    #define DRAW_PLACEPIXEL_PAL(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU])) { \
            col = DoColorTint(color, index[texCol]); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        } \

    #define DRAW_PLACEPIXEL_FOG(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU]) & 0xFF000000U) { \
            col = DoColorTint(color, texCol); \
            col = DoFogLighting(col, mapZ); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        }

    #define DRAW_PLACEPIXEL_PAL_FOG(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU])) { \
            col = DoColorTint(color, index[texCol]); \
            col = DoFogLighting(col, mapZ); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        } \

    #define DRAW_POLYGONAFFINE(pixelFunction, placePixelMacro, dpR, dpW) for (int dst_y = dst_y1; dst_y < dst_y2; dst_y++) { \
        Contour contour = ContourField[dst_y]; \
        contLen = contour.MaxX - contour.MinX; \
        if (contLen <= 0) { \
            dst_strideY += dstStride; \
            continue; \
        } \
        SCANLINE_INIT_Z(); \
        SCANLINE_INIT_UV(); \
        if (contour.MinX < min_x) { \
            int diff = min_x - contour.MinX; \
            SCANLINE_STEP_Z_BY(diff); \
            SCANLINE_STEP_UV_BY(diff); \
            contour.MinX = min_x; \
        } \
        if (contour.MaxX > max_x) \
            contour.MaxX = max_x; \
        index = &SoftwareRenderer::PaletteColors[SoftwareRenderer::PaletteIndexLines[dst_y]][0]; \
        for (int dst_x = contour.MinX; dst_x < contour.MaxX; dst_x++) { \
            SCANLINE_GET_MAPZ(); \
            SCANLINE_GET_INVZ(); \
            if (dpR(iz)) { \
                SCANLINE_GET_MAPUV(); \
                SCANLINE_GET_TEXUV(); \
                placePixelMacro(pixelFunction, dpW); \
            } \
            SCANLINE_STEP_Z(); \
            SCANLINE_STEP_UV(); \
        } \
        dst_strideY += dstStride; \
    }

    if (blendFlag & (BlendFlag_TINT_BIT | BlendFlag_FILTER_BIT))
        SoftwareRenderer::SetTintFunction(blendFlag);

    Uint32* index;
    int* multTableAt = &SoftwareRenderer::MultTable[opacity << 8];
    int* multSubTableAt = &SoftwareRenderer::MultSubTable[opacity << 8];
    int dst_strideY = dst_y1 * dstStride;

    POLYGON_SCANLINE_DEPTH(DRAW_POLYGONAFFINE);

    #undef DRAW_PLACEPIXEL
    #undef DRAW_PLACEPIXEL_PAL
    #undef DRAW_PLACEPIXEL_FOG
    #undef DRAW_PLACEPIXEL_PAL_FOG
    #undef DRAW_POLYGONAFFINE
}
// Draws an affine texture mapped polygon with blending
PUBLIC STATIC void PolygonRasterizer::DrawBlendAffine(Texture* texture, Vector3* positions, Vector2* uvs, int* colors, int count, BlendState blendState) {
    Uint32* dstPx = (Uint32*)Graphics::CurrentRenderTarget->Pixels;
    Uint32  dstStride = Graphics::CurrentRenderTarget->Width;
    Uint32* srcPx = (Uint32*)texture->Pixels;
    Uint32  srcStride = texture->Width;

    int dst_y1, dst_y2;

    if (count == 0)
        return;

    int blendFlag = blendState.Mode;
    int opacity = blendState.Opacity;
    if (opacity == 0 && blendFlag == BlendFlag_TRANSPARENT)
        return;

    GetPolygonBounds<Vector3>(positions, count, dst_y1, dst_y2);
    CLIP_BOUNDS();

    Scanline::Prepare(dst_y1, dst_y2);

    int*     lastColor = colors;
    Vector3* lastPosition = positions;
    Vector2* lastUV = uvs;
    if (count > 1) {
        int countRem = count - 1;
        while (countRem--) {
            Scanline::ProcessUVAffine(lastColor[0], lastColor[1], lastUV[0], lastUV[1], lastPosition[0].X, lastPosition[0].Y, lastPosition[0].Z, lastPosition[1].X, lastPosition[1].Y, lastPosition[1].Z);
            lastPosition++;
            lastColor++;
            lastUV++;
        }
    }

    Scanline::ProcessUVAffine(lastColor[0], colors[0], lastUV[0], uvs[0], lastPosition[0].X, lastPosition[0].Y, lastPosition[0].Z, positions[0].X, positions[0].Y, positions[0].Z);

    Sint32 col, texCol, contLen;
    float contZ, contU, contV, contR, contG, contB;
    float dxZ, dxU, dxV, dxR, dxG, dxB;
    float mapZ;

    int min_x, max_x;
    if (Graphics::CurrentClip.Enabled) {
        min_x = Graphics::CurrentClip.X;
        max_x = Graphics::CurrentClip.X + Graphics::CurrentClip.Width;
    }
    else {
        min_x = 0;
        max_x = (int)Graphics::CurrentRenderTarget->Width;
    }

    #define DRAW_PLACEPIXEL(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU]) & 0xFF000000U) { \
            SCANLINE_GET_COLOR(); \
            col = DoColorTint(col, texCol); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        }

    #define DRAW_PLACEPIXEL_PAL(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU])) { \
            SCANLINE_GET_COLOR(); \
            col = DoColorTint(col, index[texCol]); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        } \

    #define DRAW_PLACEPIXEL_FOG(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU]) & 0xFF000000U) { \
            SCANLINE_GET_COLOR(); \
            col = DoColorTint(col, texCol); \
            col = DoFogLighting(col, mapZ); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        }

    #define DRAW_PLACEPIXEL_PAL_FOG(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU])) { \
            SCANLINE_GET_COLOR(); \
            col = DoColorTint(col, index[texCol]); \
            col = DoFogLighting(col, mapZ); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        } \

    #define DRAW_POLYGONBLENDAFFINE(pixelFunction, placePixelMacro, dpR, dpW) for (int dst_y = dst_y1; dst_y < dst_y2; dst_y++) { \
        Contour contour = ContourField[dst_y]; \
        contLen = contour.MaxX - contour.MinX; \
        if (contLen <= 0) { \
            dst_strideY += dstStride; \
            continue; \
        } \
        SCANLINE_INIT_Z(); \
        SCANLINE_INIT_UV(); \
        SCANLINE_INIT_RGB(); \
        if (contour.MinX < min_x) { \
            int diff = min_x - contour.MinX; \
            SCANLINE_STEP_Z_BY(diff); \
            SCANLINE_STEP_UV_BY(diff); \
            SCANLINE_STEP_RGB_BY(diff); \
            contour.MinX = min_x; \
        } \
        if (contour.MaxX > max_x) \
            contour.MaxX = max_x; \
        index = &SoftwareRenderer::PaletteColors[SoftwareRenderer::PaletteIndexLines[dst_y]][0]; \
        for (int dst_x = contour.MinX; dst_x < contour.MaxX; dst_x++) { \
            SCANLINE_GET_MAPZ(); \
            SCANLINE_GET_INVZ(); \
            if (dpR(iz)) { \
                SCANLINE_GET_RGB(); \
                SCANLINE_GET_MAPUV(); \
                SCANLINE_GET_TEXUV(); \
                placePixelMacro(pixelFunction, dpW); \
            } \
            SCANLINE_STEP_Z(); \
            SCANLINE_STEP_UV(); \
            SCANLINE_STEP_RGB(); \
        } \
        dst_strideY += dstStride; \
    }

    if (blendFlag & (BlendFlag_TINT_BIT | BlendFlag_FILTER_BIT))
        SoftwareRenderer::SetTintFunction(blendFlag);

    Uint32* index;
    int* multTableAt = &SoftwareRenderer::MultTable[opacity << 8];
    int* multSubTableAt = &SoftwareRenderer::MultSubTable[opacity << 8];
    int dst_strideY = dst_y1 * dstStride;

    POLYGON_SCANLINE_DEPTH(DRAW_POLYGONBLENDAFFINE);

    #undef DRAW_PLACEPIXEL
    #undef DRAW_PLACEPIXEL_PAL
    #undef DRAW_PLACEPIXEL_FOG
    #undef DRAW_PLACEPIXEL_PAL_FOG
    #undef DRAW_POLYGONBLENDAFFINE
    #undef BLENDFLAGS
}
// Draws a perspective-correct texture mapped polygon
#ifdef SCANLINE_DIVISION
#define PERSP_MAPPER_SUBDIVIDE(width, placePixelMacro, pixelFunction, dpR, dpW) \
    contZ += dxZ * width; \
    contU += dxU * width; \
    contV += dxV * width; \
    endZ = 1.0f / contZ; \
    endU = contU * endZ; \
    endV = contV * endZ; \
    stepZ = (endZ - startZ) * invContSize; \
    stepU = (endU - startU) * invContSize; \
    stepV = (endV - startV) * invContSize; \
    mapZ = startZ; \
    currU = startU; \
    currV = startV; \
    for (int i = 0; i < width; i++) { \
        Uint32 iz = mapZ * 65536; \
        if (dpR(iz)) { \
            DRAW_PERSP_GET(currU, currV); \
            placePixelMacro(pixelFunction, dpW); \
        } \
        DRAW_PERSP_STEP(); \
        mapZ += stepZ; \
        currU += stepU; \
        currV += stepV; \
        dst_x++; \
    }
#define DO_PERSP_MAPPING(placePixelMacro, pixelFunction, dpR, dpW) { \
    int dst_x = contour.MinX; \
    int contWidth = contour.MaxX - contour.MinX; \
    int contSize = 16; \
    float invContSize = 1.0f / contSize; \
    float startZ = 1.0f / contZ; \
    float startU = contU * startZ; \
    float startV = contV * startZ; \
    float endZ, endU, endV; \
    float stepZ, stepU, stepV; \
    float mapZ, currU, currV; \
    while (contWidth >= contSize) { \
        PERSP_MAPPER_SUBDIVIDE(contSize, placePixelMacro, pixelFunction, dpR, dpW); \
        startU = endU; \
        startV = endV; \
        startZ = endZ; \
        contWidth -= contSize; \
    } \
    if (contWidth > 0) { \
        if (contWidth > 1) { \
            PERSP_MAPPER_SUBDIVIDE(contWidth, placePixelMacro, pixelFunction, dpR, dpW); \
        } \
        else { \
            Uint32 iz = startZ * 65536; \
            if (dpR(iz)) { \
                DRAW_PERSP_GET(startU, startV); \
                placePixelMacro(pixelFunction, dpW); \
            } \
        } \
    } \
}
#else
#define DO_PERSP_MAPPING(placePixelMacro, pixelFunction, dpR, dpW) \
    for (int dst_x = contour.MinX; dst_x < contour.MaxX; dst_x++) { \
        SCANLINE_GET_MAPZ(); \
        SCANLINE_GET_INVZ(); \
        if (dpR(iz)) { \
            DRAW_PERSP_GET(contU * mapZ, contV * mapZ); \
            placePixelMacro(pixelFunction, dpW); \
        } \
        SCANLINE_STEP_Z(); \
        SCANLINE_STEP_UV(); \
        DRAW_PERSP_STEP(); \
    }
#endif
PUBLIC STATIC void PolygonRasterizer::DrawPerspective(Texture* texture, Vector3* positions, Vector2* uvs, Uint32 color, int count, BlendState blendState) {
    Uint32* dstPx = (Uint32*)Graphics::CurrentRenderTarget->Pixels;
    Uint32  dstStride = Graphics::CurrentRenderTarget->Width;
    Uint32* srcPx = (Uint32*)texture->Pixels;
    Uint32  srcStride = texture->Width;

    int dst_y1, dst_y2;

    if (count == 0)
        return;

    int blendFlag = blendState.Mode;
    int opacity = blendState.Opacity;
    if (opacity == 0 && blendFlag == BlendFlag_TRANSPARENT)
        return;

    GetPolygonBounds<Vector3>(positions, count, dst_y1, dst_y2);
    CLIP_BOUNDS();

    Scanline::Prepare(dst_y1, dst_y2);

    Vector3* lastVector = positions;
    Vector2* lastUV = uvs;
    if (count > 1) {
        int countRem = count - 1;
        while (countRem--) {
            Scanline::ProcessUV(lastUV[0], lastUV[1], lastVector[0].X, lastVector[0].Y, lastVector[0].Z, lastVector[1].X, lastVector[1].Y, lastVector[1].Z);
            lastVector++;
            lastUV++;
        }
    }

    Scanline::ProcessUV(lastUV[0], uvs[0], lastVector[0].X, lastVector[0].Y, lastVector[0].Z, positions[0].X, positions[0].Y, positions[0].Z);

    Sint32 col, texCol, contLen;
    float contZ, contU, contV;
    float dxZ, dxU, dxV;
    float mapZ;

    #define DRAW_PLACEPIXEL(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU]) & 0xFF000000U) { \
            col = DoColorTint(color, texCol); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        }

    #define DRAW_PLACEPIXEL_PAL(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU])) { \
            col = DoColorTint(color, index[texCol]); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        } \

    #define DRAW_PLACEPIXEL_FOG(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU]) & 0xFF000000U) { \
            col = DoColorTint(color, texCol); \
            col = DoFogLighting(col, mapZ); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        }

    #define DRAW_PLACEPIXEL_PAL_FOG(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU])) { \
            col = DoColorTint(color, index[texCol]); \
            col = DoFogLighting(col, mapZ); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        } \

    #define DRAW_PERSP_GET(u, v) \
        float mapU = u; \
        float mapV = v; \
        SCANLINE_GET_TEXUV()

    #define DRAW_PERSP_STEP()

    #define DRAW_POLYGONPERSP(pixelFunction, placePixelMacro, dpR, dpW) for (int dst_y = dst_y1; dst_y < dst_y2; dst_y++) { \
        Contour contour = ContourField[dst_y]; \
        contLen = contour.MapRight - contour.MapLeft; \
        if (contLen <= 0) { \
            dst_strideY += dstStride; \
            continue; \
        } \
        SCANLINE_INIT_Z(); \
        SCANLINE_INIT_UV(); \
        if (contour.MapLeft < contour.MinX) { \
            int diff = contour.MinX - contour.MapLeft; \
            SCANLINE_STEP_Z_BY(diff); \
            SCANLINE_STEP_UV_BY(diff); \
        } \
        index = &SoftwareRenderer::PaletteColors[SoftwareRenderer::PaletteIndexLines[dst_y]][0]; \
        DO_PERSP_MAPPING(placePixelMacro, pixelFunction, dpR, dpW); \
        dst_strideY += dstStride; \
    }

    if (blendFlag & (BlendFlag_TINT_BIT | BlendFlag_FILTER_BIT))
        SoftwareRenderer::SetTintFunction(blendFlag);

    Uint32* index;
    int* multTableAt = &SoftwareRenderer::MultTable[opacity << 8];
    int* multSubTableAt = &SoftwareRenderer::MultSubTable[opacity << 8];
    int dst_strideY = dst_y1 * dstStride;

    POLYGON_SCANLINE_DEPTH(DRAW_POLYGONPERSP);

    #undef DRAW_PLACEPIXEL
    #undef DRAW_PLACEPIXEL_PAL
    #undef DRAW_PLACEPIXEL_FOG
    #undef DRAW_PLACEPIXEL_PAL_FOG
    #undef DRAW_PERSP_GET
    #undef DRAW_PERSP_STEP
    #undef DRAW_POLYGONPERSP
}
// Draws a perspective-correct texture mapped polygon with blending
PUBLIC STATIC void PolygonRasterizer::DrawBlendPerspective(Texture* texture, Vector3* positions, Vector2* uvs, int* colors, int count, BlendState blendState) {
    Uint32* dstPx = (Uint32*)Graphics::CurrentRenderTarget->Pixels;
    Uint32  dstStride = Graphics::CurrentRenderTarget->Width;
    Uint32* srcPx = (Uint32*)texture->Pixels;
    Uint32  srcStride = texture->Width;

    int dst_y1, dst_y2;

    if (count == 0)
        return;

    int blendFlag = blendState.Mode;
    int opacity = blendState.Opacity;
    if (opacity == 0 && blendFlag == BlendFlag_TRANSPARENT)
        return;

    GetPolygonBounds<Vector3>(positions, count, dst_y1, dst_y2);
    CLIP_BOUNDS();

    Scanline::Prepare(dst_y1, dst_y2);

    int*     lastColor = colors;
    Vector3* lastPosition = positions;
    Vector2* lastUV = uvs;
    if (count > 1) {
        int countRem = count - 1;
        while (countRem--) {
            Scanline::ProcessUV(lastColor[0], lastColor[1], lastUV[0], lastUV[1], lastPosition[0].X, lastPosition[0].Y, lastPosition[0].Z, lastPosition[1].X, lastPosition[1].Y, lastPosition[1].Z);
            lastPosition++;
            lastColor++;
            lastUV++;
        }
    }

    Scanline::ProcessUV(lastColor[0], colors[0], lastUV[0], uvs[0], lastPosition[0].X, lastPosition[0].Y, lastPosition[0].Z, positions[0].X, positions[0].Y, positions[0].Z);

    Sint32 col, texCol, contLen;
    float contZ, contU, contV, contR, contG, contB;
    float dxZ, dxU, dxV, dxR, dxG, dxB;
    float mapZ;

    #define DRAW_PLACEPIXEL(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU]) & 0xFF000000U) { \
            SCANLINE_GET_COLOR(); \
            col = DoColorTint(col, texCol); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        }

    #define DRAW_PLACEPIXEL_PAL(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU])) { \
            SCANLINE_GET_COLOR(); \
            col = DoColorTint(col, index[texCol]); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        }

    #define DRAW_PLACEPIXEL_FOG(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU]) & 0xFF000000U) { \
            SCANLINE_GET_COLOR(); \
            col = DoColorTint(col, texCol); \
            col = DoFogLighting(col, mapZ); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        }

    #define DRAW_PLACEPIXEL_PAL_FOG(pixelFunction, dpW) \
        if ((texCol = srcPx[(texV * srcStride) + texU])) { \
            SCANLINE_GET_COLOR(); \
            col = DoColorTint(col, index[texCol]); \
            col = DoFogLighting(col, mapZ); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            dpW(iz); \
        }

    #define DRAW_PERSP_GET(u, v) \
        float mapU = u; \
        float mapV = v; \
        SCANLINE_GET_RGB_PERSP(); \
        SCANLINE_GET_TEXUV()

    #define DRAW_PERSP_STEP() SCANLINE_STEP_RGB()

    #define DRAW_POLYGONBLENDPERSP(pixelFunction, placePixelMacro, dpR, dpW) for (int dst_y = dst_y1; dst_y < dst_y2; dst_y++) { \
        Contour contour = ContourField[dst_y]; \
        contLen = contour.MapRight - contour.MapLeft; \
        if (contLen <= 0) { \
            dst_strideY += dstStride; \
            continue; \
        } \
        SCANLINE_INIT_Z(); \
        SCANLINE_INIT_RGB(); \
        SCANLINE_INIT_UV(); \
        if (contour.MapLeft < contour.MinX) { \
            int diff = contour.MinX - contour.MapLeft; \
            SCANLINE_STEP_Z_BY(diff); \
            SCANLINE_STEP_RGB_BY(diff); \
            SCANLINE_STEP_UV_BY(diff); \
        } \
        index = &SoftwareRenderer::PaletteColors[SoftwareRenderer::PaletteIndexLines[dst_y]][0]; \
        DO_PERSP_MAPPING(placePixelMacro, pixelFunction, dpR, dpW); \
        dst_strideY += dstStride; \
    }

    if (blendFlag & (BlendFlag_TINT_BIT | BlendFlag_FILTER_BIT))
        SoftwareRenderer::SetTintFunction(blendFlag);

    Uint32* index;
    int* multTableAt = &SoftwareRenderer::MultTable[opacity << 8];
    int* multSubTableAt = &SoftwareRenderer::MultSubTable[opacity << 8];
    int dst_strideY = dst_y1 * dstStride;

    POLYGON_SCANLINE_DEPTH(DRAW_POLYGONBLENDPERSP);

    #undef DRAW_PLACEPIXEL
    #undef DRAW_PLACEPIXEL_PAL
    #undef DRAW_PLACEPIXEL_FOG
    #undef DRAW_PLACEPIXEL_PAL_FOG
    #undef DRAW_PERSP_GET
    #undef DRAW_PERSP_STEP
    #undef DRAW_POLYGONBLENDPERSP
}
// Draws a polygon with depth testing
PUBLIC STATIC void PolygonRasterizer::DrawDepth(Vector3* positions, Uint32 color, int count, BlendState blendState) {
    Uint32* dstPx = (Uint32*)Graphics::CurrentRenderTarget->Pixels;
    Uint32  dstStride = Graphics::CurrentRenderTarget->Width;

    int dst_y1, dst_y2;

    if (count == 0)
        return;

    int blendFlag = blendState.Mode;
    int opacity = blendState.Opacity;
    if (opacity == 0 && blendFlag == BlendFlag_TRANSPARENT)
        return;

    GetPolygonBounds<Vector3>(positions, count, dst_y1, dst_y2);
    CLIP_BOUNDS();

    Scanline::Prepare(dst_y1, dst_y2);

    Vector3* lastVector = positions;
    if (count > 1) {
        int countRem = count - 1;
        while (countRem--) {
            Scanline::ProcessDepth(lastVector[0].X, lastVector[0].Y, lastVector[0].Z, lastVector[1].X, lastVector[1].Y, lastVector[1].Z);
            lastVector++;
        }
    }

    Scanline::ProcessDepth(lastVector[0].X, lastVector[0].Y, lastVector[0].Z, positions[0].X, positions[0].Y, positions[0].Z);

    Sint32 col, contLen;
    float contZ, dxZ;
    float mapZ;

    color |= 0xFF000000U;

    #define PX_GET(pixelFunction) \
        SCANLINE_GET_MAPZ(); \
        SCANLINE_GET_INVZ(); \
        if (DEPTH_READ_U32(iz)) { \
            SCANLINE_WRITE_PIXEL(pixelFunction, color); \
            DEPTH_WRITE_U32(iz); \
        }
    #define PX_GET_FOG(pixelFunction) \
        SCANLINE_GET_MAPZ(); \
        SCANLINE_GET_INVZ(); \
        if (DEPTH_READ_U32(iz)) { \
            col = DoFogLighting(color, mapZ); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            DEPTH_WRITE_U32(iz); \
        }

    #define DRAW_POLYGONDEPTH(pixelFunction, pixelRead) for (int dst_y = dst_y1; dst_y < dst_y2; dst_y++) { \
        Contour contour = ContourField[dst_y]; \
        contLen = contour.MaxX - contour.MinX; \
        if (contLen <= 0) { \
            dst_strideY += dstStride; \
            continue; \
        } \
        SCANLINE_INIT_Z(); \
        if (contour.MapLeft < contour.MinX) { \
            SCANLINE_STEP_Z_BY(contour.MinX - contour.MapLeft); \
        } \
        for (int dst_x = contour.MinX; dst_x < contour.MaxX; dst_x++) { \
            pixelRead(pixelFunction); \
            SCANLINE_STEP_Z(); \
        } \
        dst_strideY += dstStride; \
    }

    if (blendFlag & (BlendFlag_TINT_BIT | BlendFlag_FILTER_BIT))
        SoftwareRenderer::SetTintFunction(blendFlag);

    int* multTableAt = &SoftwareRenderer::MultTable[opacity << 8];
    int* multSubTableAt = &SoftwareRenderer::MultSubTable[opacity << 8];
    int dst_strideY = dst_y1 * dstStride;

    if (UseFog) {
        POLYGON_BLENDFLAGS_SOLID(DRAW_POLYGONDEPTH, PX_GET_FOG);
    } else {
        POLYGON_BLENDFLAGS_SOLID(DRAW_POLYGONDEPTH, PX_GET);
    }

    #undef PX_GET
    #undef PX_GET_FOG

    #undef DRAW_POLYGONDEPTH
}
// Draws a blended polygon with depth testing
PUBLIC STATIC void PolygonRasterizer::DrawBlendDepth(Vector3* positions, int* colors, int count, BlendState blendState) {
    Uint32* dstPx = (Uint32*)Graphics::CurrentRenderTarget->Pixels;
    Uint32  dstStride = Graphics::CurrentRenderTarget->Width;

    int dst_y1, dst_y2;

    if (count == 0)
        return;

    int blendFlag = blendState.Mode;
    int opacity = blendState.Opacity;
    if (opacity == 0 && blendFlag == BlendFlag_TRANSPARENT)
        return;

    GetPolygonBounds<Vector3>(positions, count, dst_y1, dst_y2);
    CLIP_BOUNDS();

    Scanline::Prepare(dst_y1, dst_y2);

    int* lastColor = colors;
    Vector3* lastVector = positions;
    if (count > 1) {
        int countRem = count - 1;
        while (countRem--) {
            Scanline::ProcessDepth(lastColor[0], lastColor[1], lastVector[0].X, lastVector[0].Y, lastVector[0].Z, lastVector[1].X, lastVector[1].Y, lastVector[1].Z);
            lastVector++;
            lastColor++;
        }
    }

    Scanline::ProcessDepth(lastColor[0], colors[0], lastVector[0].X, lastVector[0].Y, lastVector[0].Z, positions[0].X, positions[0].Y, positions[0].Z);

    Sint32 col, contLen;
    float contZ, contR, contG, contB;
    float dxZ, dxR, dxG, dxB;
    float mapZ;

    #define PX_GET(pixelFunction) \
        SCANLINE_GET_MAPZ(); \
        SCANLINE_GET_INVZ(); \
        if (DEPTH_READ_U32(iz)) { \
            SCANLINE_GET_RGB(); \
            SCANLINE_GET_COLOR(); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            DEPTH_WRITE_U32(iz); \
        }
    #define PX_GET_FOG(pixelFunction) \
        SCANLINE_GET_MAPZ(); \
        SCANLINE_GET_INVZ(); \
        if (DEPTH_READ_U32(iz)) { \
            SCANLINE_GET_RGB(); \
            SCANLINE_GET_COLOR(); \
            col = DoFogLighting(col, mapZ); \
            SCANLINE_WRITE_PIXEL(pixelFunction, col); \
            DEPTH_WRITE_U32(iz); \
        }

    #define DRAW_POLYGONBLENDDEPTH(pixelFunction, pixelRead) for (int dst_y = dst_y1; dst_y < dst_y2; dst_y++) { \
        Contour contour = ContourField[dst_y]; \
        contLen = contour.MaxX - contour.MinX; \
        if (contLen <= 0) { \
            dst_strideY += dstStride; \
            continue; \
        } \
        SCANLINE_INIT_Z(); \
        SCANLINE_INIT_RGB(); \
        if (contour.MapLeft < contour.MinX) { \
            int diff = contour.MinX - contour.MapLeft; \
            SCANLINE_STEP_Z_BY(diff); \
            SCANLINE_STEP_RGB_BY(diff); \
        } \
        for (int dst_x = contour.MinX; dst_x < contour.MaxX; dst_x++) { \
            pixelRead(pixelFunction); \
            SCANLINE_STEP_Z(); \
            SCANLINE_STEP_RGB(); \
        } \
        dst_strideY += dstStride; \
    }

    if (blendFlag & (BlendFlag_TINT_BIT | BlendFlag_FILTER_BIT))
        SoftwareRenderer::SetTintFunction(blendFlag);

    int* multTableAt = &SoftwareRenderer::MultTable[opacity << 8];
    int* multSubTableAt = &SoftwareRenderer::MultSubTable[opacity << 8];
    int dst_strideY = dst_y1 * dstStride;

    if (UseFog) {
        POLYGON_BLENDFLAGS_SOLID(DRAW_POLYGONBLENDDEPTH, PX_GET_FOG);
    } else {
        POLYGON_BLENDFLAGS_SOLID(DRAW_POLYGONBLENDDEPTH, PX_GET);
    }

    #undef PX_GET
    #undef PX_GET_FOG

    #undef DRAW_POLYGONBLENDDEPTH
}

PUBLIC STATIC void     PolygonRasterizer::SetDepthTest(bool enabled) {
    DepthTest = enabled;
    if (!DepthTest)
        return;

    size_t dpSize = Graphics::CurrentRenderTarget->Width * Graphics::CurrentRenderTarget->Height;

    if (DepthBuffer == NULL || dpSize != DepthBufferSize) {
        DepthBufferSize = dpSize;
        DepthBuffer = (Uint32*)Memory::Realloc(DepthBuffer, DepthBufferSize * sizeof(*DepthBuffer));
    }

    memset(DepthBuffer, 0xFF, dpSize * sizeof(*DepthBuffer));
}
PUBLIC STATIC void     PolygonRasterizer::FreeDepthBuffer(void) {
    Memory::Free(DepthBuffer);
    DepthBuffer = NULL;
}

PUBLIC STATIC void     PolygonRasterizer::SetUseDepthBuffer(bool enabled) {
    UseDepthBuffer = enabled;
}

PUBLIC STATIC void     PolygonRasterizer::SetUseFog(bool enabled) {
    UseFog = enabled;
}
PUBLIC STATIC void     PolygonRasterizer::SetFogColor(float r, float g, float b) {
    Uint8 colorR = (Uint32)(r * 0xFF);
    Uint8 colorG = (Uint32)(g * 0xFF);
    Uint8 colorB = (Uint32)(b * 0xFF);
    FogColor = colorR << 16 | colorG << 8 | colorB;
}
PUBLIC STATIC void     PolygonRasterizer::SetFogDensity(float density) {
    FogDensity = density;
}
