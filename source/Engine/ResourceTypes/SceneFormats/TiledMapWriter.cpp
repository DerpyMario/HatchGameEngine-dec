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
#include <Engine/Types/Tileset.h>

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

// ---------------------------------------------------------- new scenes ---

// Tiled keeps a tileset's image as a path relative to the map that uses it, and
// both paths here are relative to the resources folder, so this walks back out
// of the new scene's folder and down into wherever the image is.
PRIVATE STATIC string TiledMapWriter::MakeRelativePath(const char* fromFolder, const char* toPath) {
    std::string from = fromFolder ? fromFolder : "";
    std::string to = toPath ? toPath : "";

    // Drop the part of the path they have in common, a whole folder at a time.
    size_t common = 0;
    while (true) {
        size_t slash = from.find('/', common);
        if (slash == std::string::npos)
            break;
        if (to.compare(common, slash + 1 - common, from, common, slash + 1 - common) != 0)
            break;

        common = slash + 1;
    }

    std::string relative;
    for (size_t i = common; i < from.size(); i++) {
        if (from[i] == '/')
            relative += "../";
    }

    return relative + to.substr(common);
}

// A whole Tiled map, written from nothing.
//
// Saving works by rewriting the tile data of a file that already exists, which
// leaves no way to start a scene that is not a copy of another one. This writes
// the file a new scene needs: the map, its layers filled with empty tiles, and
// the tilesets it is to be painted from.
//
// The tilesets come from the scene that is loaded, since a new level is nearly
// always another level for the game being worked on, and that is the only way
// to know an image's tile size and how many tiles are in it without going and
// reading the image. With no scene loaded the map is written without any, and
// the layers are still there to be painted once a tileset is added.
PUBLIC STATIC bool TiledMapWriter::WriteNew(const char* outPath, const char* sceneFolder, int width, int height, int tileWidth, int tileHeight, int layerCount, bool withTilesets) {
    if (width < 1 || height < 1 || tileWidth < 1 || tileHeight < 1 || layerCount < 1) {
        Log::Print(Log::LOG_ERROR, "A scene needs a size, a tile size and at least one layer.");
        return false;
    }

    char line[1024];
    std::string text;

    text += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";

    snprintf(line, sizeof(line),
        "<map version=\"1.2\" tiledversion=\"1.2.0\" orientation=\"orthogonal\" "
        "renderorder=\"right-down\" width=\"%d\" height=\"%d\" tilewidth=\"%d\" tileheight=\"%d\" "
        "infinite=\"0\" nextlayerid=\"%d\" nextobjectid=\"1\">\n",
        width, height, tileWidth, tileHeight, layerCount + 1);
    text += line;

    if (withTilesets) {
        for (size_t i = 0; i < Scene::Tilesets.size(); i++) {
            Tileset& tileset = Scene::Tilesets[i];
            if (!tileset.Filename)
                continue;

            std::string source = TiledMapWriter::MakeRelativePath(sceneFolder, tileset.Filename);

            // The name Tiled shows is the image's, without folders or suffix.
            std::string name = source;
            size_t slash = name.find_last_of('/');
            if (slash != std::string::npos)
                name = name.substr(slash + 1);
            size_t dot = name.find_last_of('.');
            if (dot != std::string::npos)
                name = name.substr(0, dot);

            snprintf(line, sizeof(line),
                " <tileset firstgid=\"%d\" name=\"%s\" tilewidth=\"%d\" tileheight=\"%d\" "
                "tilecount=\"%d\" columns=\"%d\">\n",
                (int)tileset.FirstGlobalTileID, name.c_str(),
                tileset.TileWidth, tileset.TileHeight,
                tileset.NumCols * tileset.NumRows, tileset.NumCols);
            text += line;

            snprintf(line, sizeof(line),
                "  <image source=\"%s\" width=\"%d\" height=\"%d\"/>\n",
                source.c_str(),
                tileset.NumCols * tileset.TileWidth,
                tileset.NumRows * tileset.TileHeight);
            text += line;

            text += " </tileset>\n";
        }
    }

    // Tiled's own defaults for a fresh map, so the names mean something to
    // anyone who opens this in Tiled afterwards.
    static const char* layerNames[] = { "Background", "Foreground", "Overlay", "Layer 4" };
    const int namedLayers = (int)(sizeof(layerNames) / sizeof(layerNames[0]));

    for (int i = 0; i < layerCount; i++) {
        char name[64];
        if (i < namedLayers)
            snprintf(name, sizeof(name), "%s", layerNames[i]);
        else
            snprintf(name, sizeof(name), "Layer %d", i + 1);

        snprintf(line, sizeof(line),
            " <layer id=\"%d\" name=\"%s\" width=\"%d\" height=\"%d\">\n",
            i + 1, name, width, height);
        text += line;

        text += "  <data encoding=\"csv\">\n";

        for (int y = 0; y < height; y++) {
            std::string row;
            for (int x = 0; x < width; x++) {
                row += "0";
                if (x + 1 < width || y + 1 < height)
                    row += ",";
            }

            text += row;
            text += "\n";
        }

        text += "  </data>\n";
        text += " </layer>\n";
    }

    text += "</map>\n";

    if (!File::WriteAllBytes(outPath, text.c_str(), text.size())) {
        Log::Print(Log::LOG_ERROR, "Could not write \"%s\"!", outPath);
        return false;
    }

    Log::Print(Log::LOG_IMPORTANT, "Created a %dx%d scene with %d layer(s) at \"%s\".",
        width, height, layerCount, outPath);

    return true;
}
