#ifndef ENGINE_RESOURCETYPES_SCENEFORMATS_TILEDMAPWRITER_H
#define ENGINE_RESOURCETYPES_SCENEFORMATS_TILEDMAPWRITER_H

class TileLayer;

#include <Engine/Includes/Standard.h>
#include <Engine/Scene/SceneLayer.h>

class TiledMapWriter {
private:
    static size_t FindElement(const string& text, const char* element, size_t from);
    static string MakeTileCSV(TileLayer& layer);
    static bool ReplaceLayerData(string& text, size_t layerAt, TileLayer& layer);
    static string MakeRelativePath(const char* fromFolder, const char* toPath);

public:
    static bool Write(const char* originalPath, const char* outPath);
    static bool WriteNew(const char* outPath, const char* sceneFolder, int width, int height, int tileWidth, int tileHeight, int layerCount, bool withTilesets);
};

#endif /* ENGINE_RESOURCETYPES_SCENEFORMATS_TILEDMAPWRITER_H */
