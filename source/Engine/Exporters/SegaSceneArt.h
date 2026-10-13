#ifndef ENGINE_EXPORTERS_SEGASCENEART_H
#define ENGINE_EXPORTERS_SEGASCENEART_H

class TileLayer;

#include <Engine/Includes/Standard.h>

class SegaSceneArt {
public:
    static Uint32 ToARGB(Uint32 native);
    static bool GetTilePixels(size_t tileID, Uint32** pixels, int* stride, int* width, int* height);
    static TileLayer* PickLayer();
    static bool GetLayerPixel(TileLayer* layer, int x, int y, Uint32* out);
};

#endif /* ENGINE_EXPORTERS_SEGASCENEART_H */
