#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Scene/SceneLayer.h>

need_t SceneLayer;

class TiledMapWriter {
};
#endif

#include <Engine/ResourceTypes/SceneFormats/TiledMapWriter.h>

#include <Engine/Diagnostics/Log.h>
#include <Engine/Filesystem/File.h>
#include <Engine/Scene.h>
#include <Engine/Scene/SceneEnums.h>
#include <Engine/Scene/SceneLayer.h>

// Writes tile layers back into a Tiled map.
//
// The engine could only ever read scenes, so the editor had nothing to save
// through. Rather than generate a whole new file and risk dropping everything
// the engine does not model -- tilesets, object groups, custom properties,
// editor settings -- this rewrites only the <data> element of each tile layer
// in the original file and leaves every other byte where it was. Whatever Tiled
// or a hand edit put in there survives a save untouched.
//
// Tile data goes out as CSV. The reader accepts it, it needs no compression,
// and it can be read by a person, which matters for something that overwrites
// a file people care about.

// Finds the offset of the n-th occurrence of an element's opening tag.
PRIVATE STATIC size_t TiledMapWriter::FindElement(const string& text, const char* element, size_t from) {
    std::string openTag = std::string("<") + element;

    while (true) {
        size_t at = text.find(openTag, from);
        if (at == std::string::npos)
            return std::string::npos;

        // "<layer" must not match "<layers" or similar; the character after the
        // name has to end it.
        char next = text[at + openTag.size()];
        if (next == ' ' || next == '\t' || next == '\n' || next == '\r' || next == '>' || next == '/')
            return at;

        from = at + openTag.size();
    }
}

// Turns one of the engine's live layers into the comma separated list of tile
// IDs that Tiled stores, one row per line.
PRIVATE STATIC string TiledMapWriter::MakeTileCSV(SceneLayer& layer) {
    std::string csv = "\n";
    char number[24];

    for (int y = 0; y < layer.Height; y++) {
        for (int x = 0; x < layer.Width; x++) {
            Uint32 tile = layer.Tiles[x + (y << layer.WidthInBits)];

            // Tiled stores the tile's global ID with its flip flags in the top
            // bits, which is what the reader keeps in the low bits plus the
            // same two flags. The collision bits the reader adds are the
            // engine's own and have no place in the file.
            Uint32 gid = tile & TILE_IDENT_MASK;
            if (tile & TILE_FLIPX_MASK)
                gid |= 0x80000000U;
            if (tile & TILE_FLIPY_MASK)
                gid |= 0x40000000U;

            snprintf(number, sizeof(number), "%u", gid);
            csv += number;

            if (x + 1 < layer.Width || y + 1 < layer.Height)
                csv += ",";
        }

        csv += "\n";
    }

    return csv;
}

// Replaces the contents of one <data> element, and makes sure its attributes
// say the data is now plain CSV.
PRIVATE STATIC bool TiledMapWriter::ReplaceLayerData(string& text, size_t layerAt, SceneLayer& layer) {
    size_t dataAt = TiledMapWriter::FindElement(text, "data", layerAt);
    if (dataAt == std::string::npos)
        return false;

    size_t openEnd = text.find('>', dataAt);
    if (openEnd == std::string::npos)
        return false;

    size_t closeAt = text.find("</data>", openEnd);
    if (closeAt == std::string::npos)
        return false;

    std::string replacement = "<data encoding=\"csv\">";
    replacement += TiledMapWriter::MakeTileCSV(layer);
    replacement += "  ";

    text.replace(dataAt, closeAt - dataAt, replacement);

    return true;
}

// Rewrites originalPath's tile data from the live scene and saves it to
// outPath. The two may be the same file.
PUBLIC STATIC bool TiledMapWriter::Write(const char* originalPath, const char* outPath) {
    char* original = NULL;
    size_t originalSize = File::ReadAllBytes(originalPath, &original);
    if (!originalSize || !original) {
        Log::Print(Log::LOG_ERROR, "Could not read \"%s\" to save over it!", originalPath);
        return false;
    }

    std::string text(original, originalSize);
    free(original);

    if (text.find("<map") == std::string::npos) {
        Log::Print(Log::LOG_ERROR, "\"%s\" is not a Tiled map.", originalPath);
        return false;
    }

    size_t written = 0;
    size_t searchFrom = 0;

    for (size_t i = 0; i < Scene::Layers.size(); i++) {
        size_t layerAt = TiledMapWriter::FindElement(text, "layer", searchFrom);
        if (layerAt == std::string::npos) {
            Log::Print(Log::LOG_WARN,
                "\"%s\" has fewer tile layers than the scene; layer %d was not saved.",
                originalPath, (int)i);
            break;
        }

        if (!TiledMapWriter::ReplaceLayerData(text, layerAt, Scene::Layers[i])) {
            Log::Print(Log::LOG_ERROR, "Layer %d in \"%s\" has no tile data to replace.",
                (int)i, originalPath);
            return false;
        }

        // Carry on past the layer that was just rewritten.
        searchFrom = TiledMapWriter::FindElement(text, "layer", layerAt + 1);
        if (searchFrom == std::string::npos)
            searchFrom = text.size();

        written++;
    }

    if (!File::WriteAllBytes(outPath, text.c_str(), text.size())) {
        Log::Print(Log::LOG_ERROR, "Could not write \"%s\"!", outPath);
        return false;
    }

    Log::Print(Log::LOG_IMPORTANT, "Saved %d tile layer(s) to \"%s\".", (int)written, outPath);

    return true;
}
