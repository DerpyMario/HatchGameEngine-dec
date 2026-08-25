#if INTERFACE
#include <Engine/Includes/Standard.h>

need_t SceneLayer;

class SegaSceneArt {
public:

};
#endif

#include <Engine/Exporters/SegaSceneArt.h>

#include <Engine/Graphics.h>
#include <Engine/Scene.h>
#include <Engine/Scene/SceneEnums.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Sprites/Animation.h>
#include <Engine/Rendering/Texture.h>
#include <Engine/ResourceTypes/ISprite.h>
#include <Engine/Types/Tileset.h>

// Reaching a scene's art the way the renderer reaches it.
//
// Every SEGA export starts here, whatever it does afterwards: the Mega Drive
// and Mega CD cut the layer into 8x8 cells for the VDP, the 32X lays it out as
// a bitmap for its own. What they share is where the pixels come from, and one
// thing about them that is easy to get wrong.

// A texture holds its pixels in whatever order the renderer that created it
// wanted: the GL backend asks for ABGR and the software and Direct3D ones for
// ARGB. Reading them as though they were always ARGB is why the first Mega
// Drive ROM this produced came out with every red and blue exchanged.
//
// The engine has a converter for this, but it forces alpha opaque on the way
// through, and the transparency is needed here to tell a blank pixel from a
// black one. So the swap is done here instead, alpha left alone.
PUBLIC STATIC Uint32 SegaSceneArt::ToARGB(Uint32 native) {
    if (Graphics::PreferredPixelFormat != SDL_PIXELFORMAT_ABGR8888)
        return native;

    return (native & 0xFF00FF00U) |
           ((native & 0x00FF0000U) >> 16) |
           ((native & 0x000000FFU) << 16);
}

// Reads a source tile's pixels the way the renderer reads them, so a tile that
// draws is a tile that exports.
PUBLIC STATIC bool SegaSceneArt::GetTilePixels(size_t tileID, Uint32** pixels, int* stride, int* width, int* height) {
    if (tileID >= Scene::TileSpriteInfos.size())
        return false;

    TileSpriteInfo info = Scene::TileSpriteInfos[tileID];
    if (!info.Sprite || info.AnimationIndex < 0)
        return false;

    if ((size_t)info.AnimationIndex >= info.Sprite->Animations.size())
        return false;

    Animation& animation = info.Sprite->Animations[info.AnimationIndex];
    if (info.FrameIndex < 0 || (size_t)info.FrameIndex >= animation.Frames.size())
        return false;

    AnimFrame& frame = animation.Frames[info.FrameIndex];
    if (frame.SheetNumber < 0 || frame.SheetNumber >= 32)
        return false;

    Texture* texture = info.Sprite->Spritesheets[frame.SheetNumber];
    if (!texture || !texture->Pixels)
        return false;

    *stride = (int)texture->Width;
    *pixels = &((Uint32*)texture->Pixels)[frame.X + frame.Y * (int)texture->Width];
    *width = frame.Width;
    *height = frame.Height;

    return true;
}

PUBLIC STATIC SceneLayer* SegaSceneArt::PickLayer() {
    // The first visible layer that actually has tiles. A scene's later layers
    // are foreground and overlay work that needs a second plane and the sprite
    // engine, neither of which any of these exports writes yet.
    for (size_t i = 0; i < Scene::Layers.size(); i++) {
        SceneLayer* layer = &Scene::Layers[i];
        if (layer->Visible && layer->Tiles && layer->Width > 0 && layer->Height > 0)
            return layer;
    }

    return NULL;
}

// One pixel of a layer, in the layer's own coordinates, as 0xAARRGGBB. Alpha
// comes back as it was stored: a caller decides for itself what counts as
// transparent, because the machines disagree about it.
//
// Returns false where the layer has no tile, which is not the same as a tile
// whose pixel is clear -- but both end up transparent, so callers treat them
// alike.
PUBLIC STATIC bool SegaSceneArt::GetLayerPixel(SceneLayer* layer, int x, int y, Uint32* out) {
    int tileX = x / Scene::TileWidth;
    int tileY = y / Scene::TileHeight;

    if (tileX < 0 || tileY < 0 || tileX >= layer->Width || tileY >= layer->Height)
        return false;

    // A layer's rows are as far apart as the next power of two above its
    // width, not as far apart as its width: the renderer indexes them by
    // shifting. Reading with the width would come out right for a layer 32 or
    // 64 tiles across and skewed for one 40 across.
    Uint32 entry = layer->Tiles[tileX + tileY * layer->WidthData];
    size_t tileID = entry & TILE_IDENT_MASK;
    if (tileID == Scene::EmptyTile)
        return false;

    Uint32* pixels; int stride, tileW, tileH;
    if (!SegaSceneArt::GetTilePixels(tileID, &pixels, &stride, &tileW, &tileH))
        return false;

    int sx = x % Scene::TileWidth;
    int sy = y % Scene::TileHeight;

    if ((entry & TILE_FLIPX_MASK) != 0)
        sx = (tileW - 1) - sx;
    if ((entry & TILE_FLIPY_MASK) != 0)
        sy = (tileH - 1) - sy;

    if (sx < 0 || sy < 0 || sx >= tileW || sy >= tileH)
        return false;

    *out = SegaSceneArt::ToARGB(pixels[sx + sy * stride]);

    return true;
}
